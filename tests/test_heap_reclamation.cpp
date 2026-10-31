#include "test_helpers.h"

#include "builtin_types/dict.h"
#include "builtin_types/list.h"
#include "builtin_types/module_object.h"
#include "builtin_types/string_iterator.h"
#include "builtin_types/tuple.h"
#include "memory/global_heap.h"
#include "memory/heap_reclamation.h"
#include "memory/native_layout_descriptor.h"
#include "object_model/overflow_slots.h"
#include "object_model/refcount.h"
#include "object_model/shape.h"
#include "object_model/validity_cell.h"
#include "object_model/vm_array_backing.h"
#include "runtime/expanded_call.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <new>

namespace cl
{
    namespace
    {
        HeapObject *allocate_until_slab_changes(ThreadState *thread,
                                                GlobalHeap &heap,
                                                SlabAllocator *slab)
        {
            for(size_t idx = 0; idx < 10000; ++idx)
            {
                HeapObject *obj = thread->make_internal_raw<ValidityCell>(
                    ValidityCellDependencyMutability::Mutable);
                if(heap.slab_for_object_unlocked(obj) != slab)
                {
                    return obj;
                }
            }
            return nullptr;
        }

        bool slab_has_valid_object(SlabAllocator *slab, HeapObject *target)
        {
            bool found = false;
            slab->for_each_valid_object([&](HeapObject *obj) {
                if(obj == target)
                {
                    found = true;
                }
            });
            return found;
        }

    }  // namespace

    TEST(HeapReclamation, CachedGeneralDictShapeSurvivesWithoutInstances)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        GlobalHeap &heap = context.vm().get_refcounted_global_heap();
        Shape *general_shape = context.vm().exact_dict_general_shape();
        SlabAllocator *shape_slab =
            heap.slab_for_object_unlocked(general_shape);

        {
            Owned<TValue<Dict>> dict(thread->make_object_value<Dict>());
            ASSERT_FALSE(
                dict.value()
                    .extract()
                    ->set_item(thread, Value::from_smi(1), Value::None())
                    .has_exception());
            ASSERT_EQ(general_shape, dict.value().extract()->get_shape());
        }
        context.vm().run_heap_reclamation();

        ASSERT_TRUE(slab_has_valid_object(shape_slab, general_shape));
        EXPECT_NE(HeapLifecycleState::Dead, general_shape->lifecycle_state());
        EXPECT_GT(general_shape->refcount(), 0);
        EXPECT_EQ(general_shape, thread->get_exact_dict_general_shape());
        EXPECT_EQ(context.vm().dict_class(), general_shape->get_class());
        EXPECT_GE(
            general_shape->lookup_descriptor_index(
                context.vm().get_or_create_interned_string_value(L"__class__")),
            0);

        Owned<TValue<Dict>> dict(thread->make_object_value<Dict>());
        ASSERT_FALSE(dict.value()
                         .extract()
                         ->set_item(thread, Value::from_smi(2), Value::None())
                         .has_exception());
        EXPECT_EQ(general_shape, dict.value().extract()->get_shape());
        EXPECT_NE(HeapLifecycleState::Dead, general_shape->lifecycle_state());
    }

    TEST(HeapReclamation, RootCollectionIncludesFrameSlotRoot)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        String *string = thread->make_object_raw<String>(L"root");
        Value *slot = thread->clover_frame_sentinel() - 1;
        *slot = Value::from_oop(string);
        thread->publish_safepoint_scan_record(SafepointScanRecord::idle(slot));

        ReclamationRootSet roots;
        collect_reclamation_roots_from_thread(roots, *thread);

        EXPECT_TRUE(roots.contains(string));
    }

    TEST(HeapReclamation, RootCollectionIncludesAccumulatorRoot)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        String *string = thread->make_object_raw<String>(L"accumulator");
        SafepointScanRecord record =
            SafepointScanRecord::idle(thread->clover_frame_sentinel());
        record.accumulator_or_not_present = Value::from_oop(string);
        thread->publish_safepoint_scan_record(record);

        ReclamationRootSet roots;
        collect_reclamation_roots_from_thread(roots, *thread);

        EXPECT_TRUE(roots.contains(string));
    }

    TEST(HeapReclamation, RootCollectionIncludesActiveCodeObjectRoot)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        TValue<String> name =
            context.vm().get_or_create_interned_string_value(L"<active-code>");
        ModuleObject *module = context.make_test_module_object(
            name, context.vm().global_builtins_module().raw_value());
        CodeObject *code = thread->make_object_raw<CodeObject>(
            nullptr, TValue<ModuleObject>::from_oop(module), nullptr, name);
        SafepointScanRecord record =
            SafepointScanRecord::idle(thread->clover_frame_sentinel());
        record.active_code_object_or_not_present = Value::from_oop(code);
        thread->publish_safepoint_scan_record(record);

        ReclamationRootSet roots;
        collect_reclamation_roots_from_thread(roots, *thread);

        EXPECT_TRUE(roots.contains(code));
    }

    TEST(HeapReclamation, RootCollectionDoesNotDereferenceJunk)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        Value junk;
        junk.as.integer = 0x1010;
        Value *slot = thread->clover_frame_sentinel() - 1;
        *slot = junk;
        thread->publish_safepoint_scan_record(SafepointScanRecord::idle(slot));

        ReclamationRootSet roots;
        collect_reclamation_roots_from_thread(roots, *thread);

        EXPECT_TRUE(roots.contains(reinterpret_cast<HeapObject *>(0x1010)));
    }

    TEST(HeapReclamation, ZctProcessingRestoresPositiveRefcountEntry)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        size_t zct_size_before = thread->zero_count_table_size();
        String *string = thread->make_object_raw<String>(L"retained");
        thread->add_to_zero_count_table_if_needed(string);
        ASSERT_EQ(zct_size_before + 1, thread->zero_count_table_size());
        ASSERT_EQ(HeapLifecycleState::InZct, string->lifecycle_state());

        incref_heap_ptr(string);
        context.vm().run_heap_reclamation();

        EXPECT_EQ(1, string->refcount());
        EXPECT_EQ(HeapLifecycleState::Normal, string->lifecycle_state());
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(string));
        decref_heap_ptr(string);
    }

#ifndef NDEBUG
    TEST(HeapReclamation, ZctProcessingRejectsDuplicateZctEntry)
    {
        test::VmTestContext context;
        ThreadState *thread = context.vm().make_new_thread();
        ThreadState::ActivationScope active_thread(thread);
        ValidityCell *object = thread->make_internal_raw<ValidityCell>(
            ValidityCellDependencyMutability::Mutable);
        thread->add_to_zero_count_table_if_needed(object);
        ASSERT_TRUE(thread->zero_count_table_contains_for_testing(object));

        object->set_lifecycle_state(HeapLifecycleState::Normal);
        thread->add_to_zero_count_table_if_needed(object);
        ASSERT_EQ(2u, thread->zero_count_table_size());

        EXPECT_DEATH(
            { context.vm().run_heap_reclamation(); },
            "duplicate heap object in zero count table");
    }

    TEST(HeapReclamation, ReclamationRejectsDuplicateEntryAcrossThreadZcts)
    {
        EXPECT_DEATH(
            {
                VirtualMachine vm;
                ThreadStateList threads;
                threads.push_back(std::make_unique<ThreadState>(&vm));
                threads.push_back(std::make_unique<ThreadState>(&vm));

                ThreadState *first_thread = threads[0].get();
                ThreadState *second_thread = threads[1].get();
                ThreadState::ActivationScope active_thread(first_thread);
                ValidityCell *object =
                    first_thread->make_internal_raw<ValidityCell>(
                        ValidityCellDependencyMutability::Mutable);
                first_thread->add_to_zero_count_table_if_needed(object);
                object->set_lifecycle_state(HeapLifecycleState::Normal);
                second_thread->add_to_zero_count_table_if_needed(object);

                validate_zero_count_tables_for_reclamation(threads);
            },
            "duplicate heap object in zero count table");
    }
#endif

    TEST(HeapReclamation, ZctProcessingKeepsStackRootedZeroEntry)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        size_t zct_size_before = thread->zero_count_table_size();
        String *string = thread->make_object_raw<String>(L"stack-rooted");
        thread->add_to_zero_count_table_if_needed(string);
        ASSERT_EQ(zct_size_before + 1, thread->zero_count_table_size());
        Value *slot = thread->clover_frame_sentinel() - 1;
        *slot = Value::from_oop(string);
        thread->publish_safepoint_scan_record(SafepointScanRecord::idle(slot));
        context.vm().run_heap_reclamation();

        EXPECT_EQ(0, string->refcount());
        EXPECT_EQ(HeapLifecycleState::InZct, string->lifecycle_state());
        EXPECT_TRUE(thread->zero_count_table_contains_for_testing(string));
        *slot = Value::not_present();
    }

    TEST(HeapReclamation, FullReclamationReclaimsUnrootedZctEntry)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        GlobalHeap &heap = context.vm().get_refcounted_global_heap();
        context.vm().run_heap_reclamation();
        uint64_t valid_objects_before_alloc = heap.count_valid_objects_slow();
        ValidityCell *object = thread->make_internal_raw<ValidityCell>(
            ValidityCellDependencyMutability::Mutable);
        SlabAllocator *slab = heap.slab_for_object_unlocked(object);
        thread->add_to_zero_count_table_if_needed(object);
        ASSERT_TRUE(thread->zero_count_table_contains_for_testing(object));
        uint64_t valid_objects_after_alloc = heap.count_valid_objects_slow();
        ASSERT_EQ(valid_objects_before_alloc + 1, valid_objects_after_alloc);
        ASSERT_TRUE(slab_has_valid_object(slab, object));

        context.vm().run_heap_reclamation();

        EXPECT_EQ(valid_objects_after_alloc - 1,
                  heap.count_valid_objects_slow());
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(object));
    }

    TEST(HeapReclamation, ZctProcessingReclaimsCascadedChild)
    {
        test::VmTestContext context;
        ThreadState *thread = context.vm().make_new_thread();
        ThreadState::ActivationScope active_thread(thread);
        GlobalHeap &heap = context.vm().get_refcounted_global_heap();
        context.vm().run_heap_reclamation();
        uint64_t valid_objects_before_alloc = heap.count_valid_objects_slow();
        String *child = thread->make_object_raw<String>(L"zct-child");
        Tuple *owner = thread->make_object_raw<Tuple>(1);
        owner->initialize_item_unchecked(0, Value::from_oop(child));
        ASSERT_EQ(1, child->refcount());
        thread->add_to_zero_count_table_if_needed(owner);
        ASSERT_TRUE(thread->zero_count_table_contains_for_testing(owner));
        ASSERT_FALSE(thread->zero_count_table_contains_for_testing(child));
        SlabAllocator *owner_slab = heap.slab_for_object_unlocked(owner);
        SlabAllocator *child_slab = heap.slab_for_object_unlocked(child);
        ASSERT_TRUE(slab_has_valid_object(owner_slab, owner));
        ASSERT_TRUE(slab_has_valid_object(child_slab, child));
        uint64_t valid_objects_after_alloc = heap.count_valid_objects_slow();
        ASSERT_EQ(valid_objects_before_alloc + 2, valid_objects_after_alloc);

        {
            ThreadState::NoActiveThreadScope no_active_thread;
            context.vm().run_heap_reclamation();
        }

        EXPECT_EQ(valid_objects_after_alloc - 2,
                  heap.count_valid_objects_slow());
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(owner));
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(child));
    }

    TEST(HeapReclamation, StringIteratorRetainsSourceAndReclaimsItWithOwner)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        GlobalHeap &heap = context.vm().get_refcounted_global_heap();
        context.vm().run_heap_reclamation();
        TValue<String> source = thread->make_object_value<String>(L"source");
        String *source_ptr = source.extract();
        StringIterator *iterator =
            thread->make_object_raw<StringIterator>(source);
        auto object_is_valid = [&heap](HeapObject *object) {
            return heap.has_slab_for_address_for_testing(object) &&
                   slab_has_valid_object(heap.slab_for_object_unlocked(object),
                                         object);
        };
        ASSERT_EQ(1, source_ptr->refcount());
        EXPECT_EQ(sizeof(StringIterator), object_size_in_bytes(iterator));

        // Only the iterator is rooted; the source must survive reclamation.
        Value *slot = thread->clover_frame_sentinel() - 1;
        *slot = Value::from_oop(iterator);
        thread->publish_safepoint_scan_record(SafepointScanRecord::idle(slot));
        thread->add_to_zero_count_table_if_needed(iterator);
        context.vm().run_heap_reclamation();
        ASSERT_TRUE(object_is_valid(iterator));
        ASSERT_TRUE(object_is_valid(source_ptr));
        EXPECT_EQ(1, source_ptr->refcount());
        EXPECT_EQ(std::wstring_view(L"source"), string_view(iterator->string));

        // Dropping the iterator reclaims both objects via its Member edge.
        *slot = Value::not_present();
        context.vm().run_heap_reclamation();
        EXPECT_FALSE(object_is_valid(iterator));
        EXPECT_FALSE(object_is_valid(source_ptr));
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(iterator));
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(source_ptr));
    }

    TEST(HeapReclamation, NativeObjectOverflowStorageReleasesChild)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        GlobalHeap &heap = context.vm().get_refcounted_global_heap();
        context.vm().run_heap_reclamation();
        List *owner = thread->make_object_raw<List>();
        String *child = thread->make_object_raw<String>(L"overflow-child");
        OverflowSlots *storage = thread->make_internal_raw<OverflowSlots>(1, 4);
        storage->set(0, Value::from_oop(child));
        owner->overflow_storage = storage;
        ASSERT_EQ(1, storage->refcount());
        ASSERT_EQ(1, child->refcount());
        auto *owner_slab = heap.slab_for_object_unlocked(owner);
        auto *storage_slab = heap.slab_for_object_unlocked(storage);
        auto *child_slab = heap.slab_for_object_unlocked(child);
        thread->add_to_zero_count_table_if_needed(owner);
        context.vm().run_heap_reclamation();
        EXPECT_FALSE(slab_has_valid_object(owner_slab, owner));
        EXPECT_FALSE(slab_has_valid_object(storage_slab, storage));
        EXPECT_FALSE(slab_has_valid_object(child_slab, child));
    }

    TEST(HeapReclamation, CodeObjectReleasesOwnershipBeforeNativeDestruction)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope activation(thread);
        Owned<TValue<String>> child(
            thread->make_object_value<String>(L"child"));
        TValue<String> module_name =
            context.vm().get_or_create_interned_string_value(L"<module>");
        ModuleObject *module = context.make_test_module_object(
            module_name, context.vm().global_builtins_module().raw_value());
        int32_t module_refs = module->refcount();

        // Not heap-registered: exercise the two phases separately without
        // scheduling this object for reclamation again after destruction.
        alignas(CodeObject) std::byte storage[sizeof(CodeObject)];
        CodeObject *code = new(storage) CodeObject(
            context.vm().code_class(), nullptr,
            TValue<ModuleObject>::from_oop(module), nullptr, child.value());
        code->constant_table.emplace_back(child.value().raw_value());
        code->inline_caches.expanded_call_caches.emplace_back();
        auto &cache = code->inline_caches.expanded_call_caches.back();
        cache.binding_plan = std::make_unique<ExpandedCallCachedPlan>();
        cache.binding_plan->keyword_names.emplace_back(child.value());
        ASSERT_EQ(4, child.extract()->refcount());
        ASSERT_EQ(module_refs + 1, module->refcount());

        const ReleaseDescriptor &descriptor =
            native_layout_descriptor_for(CodeObject::native_layout).release;
        descriptor.custom_release(code);
        EXPECT_EQ(1, child.extract()->refcount());
        EXPECT_EQ(module_refs, module->refcount());
        EXPECT_TRUE(code->constant_table.empty());
        EXPECT_EQ(nullptr, cache.binding_plan);
        size_t zct_size = thread->zero_count_table_size();

        descriptor.destroy(code);
        EXPECT_EQ(1, child.extract()->refcount());
        EXPECT_EQ(module_refs, module->refcount());
        EXPECT_EQ(zct_size, thread->zero_count_table_size());
    }

    TEST(HeapReclamation, CodeObjectCustomReleaseReclaimsConstantTableChild)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        GlobalHeap &heap = context.vm().get_refcounted_global_heap();
        context.vm().run_heap_reclamation();

        String *child = thread->make_object_raw<String>(L"code-constant");
        TValue<String> code_name =
            context.vm().get_or_create_interned_string_value(L"<test>");
        ModuleObject *module = context.make_test_module_object(
            code_name, context.vm().global_builtins_module().raw_value());
        CodeObject *code_object = thread->make_object_raw<CodeObject>(
            nullptr, TValue<ModuleObject>::from_oop(module), nullptr,
            code_name);
        String *overflow_child =
            thread->make_object_raw<String>(L"code-overflow");
        OverflowSlots *storage = thread->make_internal_raw<OverflowSlots>(1, 4);
        storage->set(0, Value::from_oop(overflow_child));
        code_object->overflow_storage = storage;
        ASSERT_EQ(1, storage->refcount());
        ASSERT_EQ(1, overflow_child->refcount());
        auto *storage_slab = heap.slab_for_object_unlocked(storage);
        auto *overflow_child_slab =
            heap.slab_for_object_unlocked(overflow_child);
        code_object->constant_table.emplace_back(Value::from_oop(child));
        code_object->inline_caches.expanded_call_caches.emplace_back();
        auto &expanded = code_object->inline_caches.expanded_call_caches.back();
        expanded.binding_plan = std::make_unique<ExpandedCallCachedPlan>();
        expanded.binding_plan->keyword_names.emplace_back(
            TValue<String>::from_oop(child));
        code_object->inline_caches.function_call_caches.push_back(
            FunctionCallInlineCache{});
        code_object->inline_caches.function_call_caches.back().guard_value =
            Value::from_oop(child);
        ASSERT_EQ(2, child->refcount());
        SlabAllocator *child_slab = heap.slab_for_object_unlocked(child);
        SlabAllocator *module_slab = heap.slab_for_object_unlocked(module);
        SlabAllocator *code_object_slab =
            heap.slab_for_object_unlocked(code_object);
        ASSERT_TRUE(slab_has_valid_object(child_slab, child));
        ASSERT_TRUE(slab_has_valid_object(module_slab, module));
        ASSERT_TRUE(slab_has_valid_object(code_object_slab, code_object));

        thread->add_to_zero_count_table_if_needed(code_object);
        ASSERT_TRUE(thread->zero_count_table_contains_for_testing(code_object));
        ASSERT_FALSE(thread->zero_count_table_contains_for_testing(child));
        ASSERT_FALSE(thread->zero_count_table_contains_for_testing(module));

        context.vm().run_heap_reclamation();

        EXPECT_FALSE(slab_has_valid_object(child_slab, child));
        EXPECT_FALSE(slab_has_valid_object(module_slab, module));
        EXPECT_FALSE(slab_has_valid_object(code_object_slab, code_object));
        EXPECT_FALSE(slab_has_valid_object(storage_slab, storage));
        EXPECT_FALSE(
            slab_has_valid_object(overflow_child_slab, overflow_child));
        EXPECT_FALSE(
            thread->zero_count_table_contains_for_testing(code_object));
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(child));
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(module));
    }

    TEST(HeapReclamation, ShapeReleasesOwnershipBeforeNativeDestruction)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope activation(thread);
        Owned<TValue<String>> name(thread->make_object_value<String>(L"a"));
        ClassObject *cls = context.vm().object_class();
        alignas(Shape) std::byte storage[sizeof(Shape)];
        Shape *shape =
            new(storage) Shape(TValue<ClassObject>::from_oop(cls), nullptr, 0,
                               0, 0, shape_flag(ShapeFlag::None), 0);
        OwnedHeapPtr<Shape> next(
            shape->derive_transition(name.value(), ShapeTransitionVerb::Add));
        int32_t class_refs = cls->refcount();
        // Builtin classes can be immortal; only refcounted storage contributes
        // a reference-count decrement during the release phase.
        int32_t class_release_count = heap_ptr_is_refcounted(cls) ? 1 : 0;
        int32_t name_refs = name.extract()->refcount();
        ASSERT_EQ(2, next.get()->refcount());

        const ReleaseDescriptor &descriptor =
            native_layout_descriptor_for(Shape::native_layout).release;
        descriptor.custom_release(shape);
        EXPECT_EQ(0u, shape->transition_count());
        EXPECT_EQ(class_refs - class_release_count, cls->refcount());
        EXPECT_EQ(name_refs - 1, name.extract()->refcount());
        EXPECT_EQ(1, next.get()->refcount());
        size_t zct_size = thread->zero_count_table_size();

        descriptor.destroy(shape);
        EXPECT_EQ(class_refs - class_release_count, cls->refcount());
        EXPECT_EQ(name_refs - 1, name.extract()->refcount());
        EXPECT_EQ(1, next.get()->refcount());
        EXPECT_EQ(zct_size, thread->zero_count_table_size());
    }

    TEST(HeapReclamation, ShapeCustomReleaseReclaimsTransitionTarget)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        GlobalHeap &heap = context.vm().get_refcounted_global_heap();
        context.vm().run_heap_reclamation();
        uint64_t valid_objects_before_alloc = heap.count_valid_objects_slow();

        TValue<String> a_name(
            context.vm().get_or_create_interned_string_value(L"a"));
        Shape *root_shape = thread->make_internal_raw<Shape>(
            TValue<ClassObject>::from_oop(context.vm().object_class()), nullptr,
            0, 0, 0, shape_flag(ShapeFlag::None), 0);
        Shape *child_shape =
            root_shape->derive_transition(a_name, ShapeTransitionVerb::Add);
        ASSERT_EQ(1, child_shape->refcount());

        thread->add_to_zero_count_table_if_needed(root_shape);
        ASSERT_TRUE(thread->zero_count_table_contains_for_testing(root_shape));
        ASSERT_FALSE(
            thread->zero_count_table_contains_for_testing(child_shape));
        uint64_t valid_objects_after_alloc = heap.count_valid_objects_slow();
        ASSERT_EQ(valid_objects_before_alloc + 2, valid_objects_after_alloc);

        context.vm().run_heap_reclamation();

        EXPECT_EQ(valid_objects_after_alloc - 2,
                  heap.count_valid_objects_slow());
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(root_shape));
        EXPECT_FALSE(
            thread->zero_count_table_contains_for_testing(child_shape));
    }

    TEST(HeapReclamation, HeapPtrArrayBackingReclaimsFakeValuePointerCell)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        GlobalHeap &heap = context.vm().get_refcounted_global_heap();
        context.vm().run_heap_reclamation();
        uint64_t valid_objects_before_alloc = heap.count_valid_objects_slow();

        ValidityCell *child = thread->make_internal_raw<ValidityCell>(
            ValidityCellDependencyMutability::Mutable);
        HeapPtrArrayBacking *backing =
            thread->make_internal_raw<HeapPtrArrayBacking>(1);
        backing->elements[0] = incref_heap_ptr(child);
        ASSERT_EQ(1, child->refcount());

        thread->add_to_zero_count_table_if_needed(backing);
        ASSERT_TRUE(thread->zero_count_table_contains_for_testing(backing));
        ASSERT_FALSE(thread->zero_count_table_contains_for_testing(child));
        uint64_t valid_objects_after_alloc = heap.count_valid_objects_slow();
        ASSERT_EQ(valid_objects_before_alloc + 2, valid_objects_after_alloc);

        context.vm().run_heap_reclamation();

        EXPECT_EQ(valid_objects_after_alloc - 2,
                  heap.count_valid_objects_slow());
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(backing));
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(child));
    }

    TEST(HeapReclamation, FullReclamationReclaimsCompactDynamicObject)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        GlobalHeap &heap = context.vm().get_refcounted_global_heap();
        context.vm().run_heap_reclamation();
        uint64_t valid_objects_before_alloc = heap.count_valid_objects_slow();
        String *child = thread->make_object_raw<String>(L"tuple-child");
        Tuple *owner = thread->make_object_raw<Tuple>(1);
        owner->initialize_item_unchecked(0, Value::from_oop(child));
        ASSERT_EQ(1, child->refcount());
        thread->add_to_zero_count_table_if_needed(owner);
        ASSERT_TRUE(thread->zero_count_table_contains_for_testing(owner));
        ASSERT_FALSE(thread->zero_count_table_contains_for_testing(child));
        SlabAllocator *owner_slab = heap.slab_for_object_unlocked(owner);
        SlabAllocator *child_slab = heap.slab_for_object_unlocked(child);
        ASSERT_TRUE(slab_has_valid_object(owner_slab, owner));
        ASSERT_TRUE(slab_has_valid_object(child_slab, child));
        uint64_t valid_objects_after_alloc = heap.count_valid_objects_slow();
        ASSERT_EQ(valid_objects_before_alloc + 2, valid_objects_after_alloc);

        context.vm().run_heap_reclamation();

        EXPECT_EQ(valid_objects_after_alloc - 2,
                  heap.count_valid_objects_slow());
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(owner));
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(child));
    }

    TEST(HeapReclamation, FullReclamationReleasesInactiveSlab)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        GlobalHeap &heap = context.vm().get_refcounted_global_heap();

        ValidityCell *current_object = thread->make_internal_raw<ValidityCell>(
            ValidityCellDependencyMutability::Mutable);
        SlabAllocator *initial_slab =
            heap.slab_for_object_unlocked(current_object);
        HeapObject *fresh_slab_object =
            allocate_until_slab_changes(thread, heap, initial_slab);
        ASSERT_NE(nullptr, fresh_slab_object);

        ValidityCell *target = thread->make_internal_raw<ValidityCell>(
            ValidityCellDependencyMutability::Mutable);
        void *target_address = target;
        SlabAllocator *target_slab = heap.slab_for_object_unlocked(target);
        ASSERT_EQ(heap.slab_for_object_unlocked(fresh_slab_object),
                  target_slab);
        ASSERT_TRUE(heap.has_slab_for_address_for_testing(target_address));

        HeapObject *next_slab_object =
            allocate_until_slab_changes(thread, heap, target_slab);
        ASSERT_NE(nullptr, next_slab_object);
        void *next_slab_address = next_slab_object;
        ASSERT_NE(target_slab, heap.slab_for_object_unlocked(next_slab_object));

        context.vm().run_heap_reclamation();

        EXPECT_FALSE(heap.has_slab_for_address_for_testing(target_address));
        EXPECT_TRUE(heap.has_slab_for_address_for_testing(next_slab_address));
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(target));
    }

    TEST(HeapReclamation, FullReclamationReleasesDedicatedLargeSlab)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        GlobalHeap &heap = context.vm().get_refcounted_global_heap();
        context.vm().run_heap_reclamation();
        size_t tuple_size = LargeAllocationSize / sizeof(Value);
        Tuple *tuple = thread->make_object_raw<Tuple>(tuple_size);
        void *tuple_address = tuple;
        thread->add_to_zero_count_table_if_needed(tuple);
        ASSERT_TRUE(thread->zero_count_table_contains_for_testing(tuple));
        ASSERT_TRUE(heap.has_slab_for_address_for_testing(tuple_address));
        SlabAllocator *slab = heap.slab_for_object_unlocked(tuple);
        EXPECT_EQ(1u, slab->slab_pin_count());
        ASSERT_EQ(1u, slab->count_valid_objects_slow());
        uint64_t valid_objects_after_alloc = heap.count_valid_objects_slow();

        context.vm().run_heap_reclamation();

        EXPECT_EQ(valid_objects_after_alloc - 1,
                  heap.count_valid_objects_slow());
        EXPECT_FALSE(heap.has_slab_for_address_for_testing(tuple_address));
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(tuple));
    }

    TEST(HeapReclamation, EpochScanReclaimsUnrootedYoungZeroRefcountObject)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        GlobalHeap &heap = context.vm().get_refcounted_global_heap();

        size_t tuple_size = LargeAllocationSize / sizeof(Value);
        Tuple *tuple = thread->make_object_raw<Tuple>(tuple_size);
        void *tuple_address = tuple;
        SlabAllocator *slab = heap.slab_for_object_unlocked(tuple);
        ASSERT_FALSE(thread->zero_count_table_contains_for_testing(tuple));
        ASSERT_EQ(1u, slab->slab_pin_count());
        ASSERT_EQ(HeapLifecycleState::Normal, tuple->lifecycle_state());

        context.vm().run_heap_reclamation();

        EXPECT_FALSE(heap.has_slab_for_address_for_testing(tuple_address));
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(tuple));
    }

    TEST(HeapReclamation, EpochScanMovesRootedYoungZeroRefcountObjectToZct)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        GlobalHeap &heap = context.vm().get_refcounted_global_heap();

        size_t tuple_size = LargeAllocationSize / sizeof(Value);
        Tuple *tuple = thread->make_object_raw<Tuple>(tuple_size);
        void *tuple_address = tuple;
        SlabAllocator *slab = heap.slab_for_object_unlocked(tuple);
        ASSERT_FALSE(thread->zero_count_table_contains_for_testing(tuple));
        ASSERT_EQ(1u, slab->slab_pin_count());
        ASSERT_EQ(HeapLifecycleState::Normal, tuple->lifecycle_state());

        Value *slot = thread->clover_frame_sentinel() - 1;
        *slot = Value::from_oop(tuple);
        thread->publish_safepoint_scan_record(SafepointScanRecord::idle(slot));

        context.vm().run_heap_reclamation();

        EXPECT_TRUE(heap.has_slab_for_address_for_testing(tuple_address));
        EXPECT_EQ(0u, slab->slab_pin_count());
        EXPECT_EQ(1u, slab->count_valid_objects_slow());
        EXPECT_EQ(HeapLifecycleState::InZct, tuple->lifecycle_state());
        EXPECT_TRUE(thread->zero_count_table_contains_for_testing(tuple));
    }

    TEST(HeapReclamation, EpochScanIgnoresPositiveRefcountObject)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        GlobalHeap &heap = context.vm().get_refcounted_global_heap();
        context.vm().run_heap_reclamation();
        uint64_t valid_objects_before_alloc = heap.count_valid_objects_slow();

        ValidityCell *object = thread->make_internal_raw<ValidityCell>(
            ValidityCellDependencyMutability::Mutable);
        incref_heap_ptr(object);
        ASSERT_FALSE(thread->zero_count_table_contains_for_testing(object));
        ASSERT_EQ(1, object->refcount());
        ASSERT_EQ(HeapLifecycleState::Normal, object->lifecycle_state());
        ASSERT_EQ(valid_objects_before_alloc + 1,
                  heap.count_valid_objects_slow());

        context.vm().run_heap_reclamation();

        EXPECT_EQ(1, object->refcount());
        EXPECT_EQ(HeapLifecycleState::Normal, object->lifecycle_state());
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(object));
        EXPECT_EQ(valid_objects_before_alloc + 1,
                  heap.count_valid_objects_slow());

        decref_heap_ptr(object);
        context.vm().run_heap_reclamation();
    }

    TEST(HeapReclamation, EpochScanIgnoresNonNormalYoungObjects)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        GlobalHeap &heap = context.vm().get_refcounted_global_heap();
        context.vm().run_heap_reclamation();
        uint64_t valid_objects_before_alloc = heap.count_valid_objects_slow();

        ValidityCell *in_zct = thread->make_internal_raw<ValidityCell>(
            ValidityCellDependencyMutability::Mutable);
        ValidityCell *reclaiming = thread->make_internal_raw<ValidityCell>(
            ValidityCellDependencyMutability::Mutable);
        ValidityCell *dead = thread->make_internal_raw<ValidityCell>(
            ValidityCellDependencyMutability::Mutable);
        in_zct->set_lifecycle_state(HeapLifecycleState::InZct);
        reclaiming->set_lifecycle_state(HeapLifecycleState::Reclaiming);
        dead->set_lifecycle_state(HeapLifecycleState::Dead);
        ASSERT_EQ(valid_objects_before_alloc + 3,
                  heap.count_valid_objects_slow());

        context.vm().run_heap_reclamation();

        EXPECT_EQ(0, in_zct->refcount());
        EXPECT_EQ(0, reclaiming->refcount());
        EXPECT_EQ(0, dead->refcount());
        EXPECT_EQ(HeapLifecycleState::InZct, in_zct->lifecycle_state());
        EXPECT_EQ(HeapLifecycleState::Reclaiming,
                  reclaiming->lifecycle_state());
        EXPECT_EQ(HeapLifecycleState::Dead, dead->lifecycle_state());
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(in_zct));
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(reclaiming));
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(dead));
        EXPECT_EQ(valid_objects_before_alloc + 3,
                  heap.count_valid_objects_slow());
    }
}  // namespace cl

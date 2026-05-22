#include "test_helpers.h"

#include "global_heap.h"
#include "heap_reclamation.h"
#include "module_object.h"
#include "refcount.h"
#include "shape.h"
#include "tuple.h"
#include "validity_cell.h"
#include "vm_array_backing.h"

#include <gtest/gtest.h>

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
                HeapObject *obj = thread->make_internal_raw<ValidityCell>();
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

    TEST(HeapReclamation, RootCollectionIncludesFrameSlotRoot)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        String *string = thread->make_object_raw<String>(L"root");
        Value *slot = thread->clover_frame_sentinel() - 1;
        *slot = Value::from_oop(string);
        thread->publish_safepoint_scan_record(slot, Value::not_present());

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
        thread->publish_safepoint_scan_record(thread->clover_frame_sentinel(),
                                              Value::from_oop(string));

        ReclamationRootSet roots;
        collect_reclamation_roots_from_thread(roots, *thread);

        EXPECT_TRUE(roots.contains(string));
    }

    TEST(HeapReclamation, RootCollectionDoesNotDereferenceJunk)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        Value junk;
        junk.as.integer = 0x1010;
        Value *slot = thread->clover_frame_sentinel() - 1;
        *slot = junk;
        thread->publish_safepoint_scan_record(slot, Value::not_present());

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
        ASSERT_EQ(HeapLifecycleState::InZct, string->lifecycle_state);

        incref_heap_ptr(string);
        context.vm().run_heap_reclamation();

        EXPECT_EQ(1, string->refcount);
        EXPECT_EQ(HeapLifecycleState::Normal, string->lifecycle_state);
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(string));
        decref_heap_ptr(string);
    }

#ifndef NDEBUG
    TEST(HeapReclamation, ZctProcessingRejectsDuplicateZctEntry)
    {
        test::VmTestContext context;
        ThreadState *thread = context.vm().make_new_thread();
        ThreadState::ActivationScope active_thread(thread);
        ValidityCell *object = thread->make_internal_raw<ValidityCell>();
        thread->add_to_zero_count_table_if_needed(object);
        ASSERT_TRUE(thread->zero_count_table_contains_for_testing(object));

        object->lifecycle_state = HeapLifecycleState::Normal;
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
                    first_thread->make_internal_raw<ValidityCell>();
                first_thread->add_to_zero_count_table_if_needed(object);
                object->lifecycle_state = HeapLifecycleState::Normal;
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
        thread->publish_safepoint_scan_record(slot, Value::not_present());
        context.vm().run_heap_reclamation();

        EXPECT_EQ(0, string->refcount);
        EXPECT_EQ(HeapLifecycleState::InZct, string->lifecycle_state);
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
        ValidityCell *object = thread->make_internal_raw<ValidityCell>();
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
        ASSERT_EQ(1, child->refcount);
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

    TEST(HeapReclamation, CodeObjectCustomDeallocReclaimsConstantTableChild)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        GlobalHeap &heap = context.vm().get_refcounted_global_heap();
        context.vm().run_heap_reclamation();
        uint64_t valid_objects_before_alloc = heap.count_valid_objects_slow();

        String *child = thread->make_object_raw<String>(L"code-constant");
        TValue<String> code_name =
            context.vm().get_or_create_interned_string_value(L"<test>");
        ModuleObject *module = thread->make_module_object(code_name);
        CodeObject *code_object = thread->make_object_raw<CodeObject>(
            nullptr, TValue<ModuleObject>::from_oop(module), nullptr,
            code_name);
        code_object->constant_table.emplace_back(Value::from_oop(child));
        code_object->function_call_caches.push_back(FunctionCallInlineCache{});
        code_object->function_call_caches.back().guard_value =
            Value::from_oop(child);
        ASSERT_EQ(1, child->refcount);

        thread->add_to_zero_count_table_if_needed(code_object);
        ASSERT_TRUE(thread->zero_count_table_contains_for_testing(code_object));
        ASSERT_FALSE(thread->zero_count_table_contains_for_testing(child));
        uint64_t valid_objects_after_alloc = heap.count_valid_objects_slow();
        ASSERT_EQ(valid_objects_before_alloc + 3, valid_objects_after_alloc);

        context.vm().run_heap_reclamation();

        EXPECT_EQ(valid_objects_after_alloc - 3,
                  heap.count_valid_objects_slow());
        EXPECT_FALSE(
            thread->zero_count_table_contains_for_testing(code_object));
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(child));
    }

    TEST(HeapReclamation, ShapeCustomDeallocReclaimsTransitionTarget)
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
        ASSERT_EQ(1, child_shape->refcount);

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

        ValidityCell *child = thread->make_internal_raw<ValidityCell>();
        HeapPtrArrayBacking *backing =
            thread->make_internal_raw<HeapPtrArrayBacking>(1);
        backing->elements[0] = incref_heap_ptr(child);
        ASSERT_EQ(1, child->refcount);

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
        ASSERT_EQ(1, child->refcount);
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

        ValidityCell *current_object =
            thread->make_internal_raw<ValidityCell>();
        SlabAllocator *initial_slab =
            heap.slab_for_object_unlocked(current_object);
        HeapObject *fresh_slab_object =
            allocate_until_slab_changes(thread, heap, initial_slab);
        ASSERT_NE(nullptr, fresh_slab_object);

        ValidityCell *target = thread->make_internal_raw<ValidityCell>();
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
        ASSERT_EQ(HeapLifecycleState::Normal, tuple->lifecycle_state);

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
        ASSERT_EQ(HeapLifecycleState::Normal, tuple->lifecycle_state);

        Value *slot = thread->clover_frame_sentinel() - 1;
        *slot = Value::from_oop(tuple);
        thread->publish_safepoint_scan_record(slot, Value::not_present());

        context.vm().run_heap_reclamation();

        EXPECT_TRUE(heap.has_slab_for_address_for_testing(tuple_address));
        EXPECT_EQ(0u, slab->slab_pin_count());
        EXPECT_EQ(1u, slab->count_valid_objects_slow());
        EXPECT_EQ(HeapLifecycleState::InZct, tuple->lifecycle_state);
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

        ValidityCell *object = thread->make_internal_raw<ValidityCell>();
        incref_heap_ptr(object);
        ASSERT_FALSE(thread->zero_count_table_contains_for_testing(object));
        ASSERT_EQ(1, object->refcount);
        ASSERT_EQ(HeapLifecycleState::Normal, object->lifecycle_state);
        ASSERT_EQ(valid_objects_before_alloc + 1,
                  heap.count_valid_objects_slow());

        context.vm().run_heap_reclamation();

        EXPECT_EQ(1, object->refcount);
        EXPECT_EQ(HeapLifecycleState::Normal, object->lifecycle_state);
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

        ValidityCell *in_zct = thread->make_internal_raw<ValidityCell>();
        ValidityCell *reclaiming = thread->make_internal_raw<ValidityCell>();
        ValidityCell *dead = thread->make_internal_raw<ValidityCell>();
        in_zct->lifecycle_state = HeapLifecycleState::InZct;
        reclaiming->lifecycle_state = HeapLifecycleState::Reclaiming;
        dead->lifecycle_state = HeapLifecycleState::Dead;
        ASSERT_EQ(valid_objects_before_alloc + 3,
                  heap.count_valid_objects_slow());

        context.vm().run_heap_reclamation();

        EXPECT_EQ(0, in_zct->refcount);
        EXPECT_EQ(0, reclaiming->refcount);
        EXPECT_EQ(0, dead->refcount);
        EXPECT_EQ(HeapLifecycleState::InZct, in_zct->lifecycle_state);
        EXPECT_EQ(HeapLifecycleState::Reclaiming, reclaiming->lifecycle_state);
        EXPECT_EQ(HeapLifecycleState::Dead, dead->lifecycle_state);
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(in_zct));
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(reclaiming));
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(dead));
        EXPECT_EQ(valid_objects_before_alloc + 3,
                  heap.count_valid_objects_slow());
    }
}  // namespace cl

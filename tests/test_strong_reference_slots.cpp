#include "bytecode/code_object.h"
#include "memory/native_layout_descriptor.h"
#include "object_model/overflow_slots.h"
#include "object_model/shape.h"
#include "object_model/validity_cell.h"
#include "runtime/expanded_call.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <vector>

using namespace cl;

namespace
{
    void expect_references(HeapObject *object,
                           std::initializer_list<HeapObject *> expected)
    {
        std::vector<HeapObject *> actual;
        visit_strong_reference_slots(object, [&](HeapReferenceSlot slot) {
            actual.push_back(slot.load_pointer());
        });
        EXPECT_EQ(expected.size(), actual.size());
        for(HeapObject *reference: expected)
        {
            EXPECT_EQ(std::count(expected.begin(), expected.end(), reference),
                      std::count(actual.begin(), actual.end(), reference));
        }
    }

    template <typename Array> HeapObject *array_backing(Array &array)
    {
        HeapObject *backing = nullptr;
        size_t cells = 0;
        array.visit_strong_reference_slots([&](HeapReferenceSlot slot) {
            ++cells;
            backing = slot.load_pointer();
        });
        EXPECT_EQ(1u, cells);
        return backing;
    }
}  // namespace

TEST(StrongReferenceSlots, CodeObjectVisitsOwnedCellsButNotWeakCaches)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    Owned<TValue<String>> name(thread->make_object_value<String>(L"name"));
    Owned<TValue<String>> replacement(
        thread->make_object_value<String>(L"replacement"));
    OwnedHeapPtr<ModuleObject> module(context.make_test_module_object(
        name.value(), context.vm().global_builtins_module().raw_value()));
    OwnedHeapPtr<Scope> scope(thread->make_internal_raw<Scope>(nullptr));
    OwnedHeapPtr<OverflowSlots> overflow(
        thread->make_internal_raw<OverflowSlots>(0, 1));
    OwnedHeapPtr<CodeObject> code(thread->make_object_raw<CodeObject>(
        nullptr, TValue<ModuleObject>::from_oop(module.get()), scope.get(),
        name.value()));
    code.get()->overflow_storage = overflow.get();
    code.get()->docstring = Optional<TValue<String>>(name.value());
    code.get()->function_keyword_remap.add(name.value(), 7);
    code.get()->constant_table.reserve(12);
    code.get()->constant_table.emplace_back(name.raw_value());
    code.get()->constant_table.emplace_back(name.raw_value());
    code.get()->constant_table.emplace_back(Value::from_smi(42));

    auto &caches = code.get()->inline_caches;
    caches.function_call_caches.emplace_back();
    caches.function_call_caches.back().guard_value = name.raw_value();
    caches.operator_caches.emplace_back();
    caches.operator_caches.back().operand_shape_keys[0] =
        ShapeKey::from_shape(code.get()->get_shape());
    // Include both an absent plan and an owned plan in external native storage.
    caches.expanded_call_caches.resize(2);
    auto &expanded = caches.expanded_call_caches.back();
    expanded.guard_value = name.raw_value();
    OwnedHeapPtr<ValidityCell> weak_cell(
        thread->make_internal_raw<ValidityCell>(
            ValidityCellDependencyMutability::Mutable));
    expanded.validity_cell = weak_cell.get();
    expanded.binding_plan = std::make_unique<ExpandedCallCachedPlan>();
    expanded.binding_plan->keyword_names.emplace_back(name.value());
    expanded.binding_plan->keyword_names.emplace_back(name.value());

    HeapObject *names_backing = array_backing(
        code.get()->function_keyword_remap.keyword_bindable_names);
    HeapObject *indexes_backing = array_backing(
        code.get()->function_keyword_remap.keyword_bindable_parameter_indexes);
    int32_t name_refs = name.extract()->refcount();
    int32_t replacement_refs = replacement.extract()->refcount();
    size_t zct_size = thread->zero_count_table_size();
    expect_references(
        code.get(), {code.get()->get_shape(), overflow.get(), module.get(),
                     scope.get(), name.extract(), name.extract(), nullptr,
                     names_backing, indexes_backing, name.extract(),
                     name.extract(), nullptr, name.extract(), name.extract()});

    size_t rewrites = 0;
    visit_strong_reference_slots(code.get(), [&](HeapReferenceSlot slot) {
        if(slot.load_pointer() == name.extract())
        {
            slot.rewrite_pointer(replacement.extract());
            ++rewrites;
        }
    });
    EXPECT_EQ(6u, rewrites);
    EXPECT_EQ(replacement.value(), code.get()->name.value());
    EXPECT_EQ(replacement.raw_value(), code.get()->docstring.raw_value());
    EXPECT_EQ(replacement.raw_value(), code.get()->constant_table[0].value());
    EXPECT_EQ(replacement.raw_value(), code.get()->constant_table[1].value());
    EXPECT_EQ(Value::from_smi(42), code.get()->constant_table[2].value());
    EXPECT_EQ(replacement.value(),
              expanded.binding_plan->keyword_names[0].value());
    EXPECT_EQ(replacement.value(),
              expanded.binding_plan->keyword_names[1].value());
    EXPECT_EQ(name.raw_value(), caches.function_call_caches[0].guard_value);
    EXPECT_EQ(name.raw_value(), expanded.guard_value);
    EXPECT_EQ(weak_cell.get(), expanded.validity_cell);
    // Neither the module nor the VM-array backing is recursively scanned.
    EXPECT_EQ(name.raw_value(), code.get()->function_keyword_remap.name_at(0));
    EXPECT_EQ(name_refs, name.extract()->refcount());
    EXPECT_EQ(replacement_refs, replacement.extract()->refcount());
    EXPECT_EQ(zct_size, thread->zero_count_table_size());

    // Simulate relocation, then restore the ownership graph before RC teardown.
    visit_strong_reference_slots(code.get(), [&](HeapReferenceSlot slot) {
        if(slot.load_pointer() == replacement.extract())
        {
            slot.rewrite_pointer(name.extract());
        }
    });
    expect_references(names_backing, {name.extract()});
    expect_references(indexes_backing, {});
}

TEST(StrongReferenceSlots, RewritesArrayBackingHandlesWithoutVisitingElements)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    Owned<TValue<String>> name(thread->make_object_value<String>(L"first"));
    Owned<TValue<String>> other(thread->make_object_value<String>(L"second"));
    OwnedHeapPtr<CodeObject> code(context.compile_file(L"pass"));
    OwnedHeapPtr<CodeObject> replacement(context.compile_file(L"pass"));
    auto &original_remap = code.get()->function_keyword_remap;
    auto &new_remap = replacement.get()->function_keyword_remap;
    original_remap.add(name.value(), 7);
    new_remap.add(other.value(), 9);
    HeapObject *old_names =
        array_backing(original_remap.keyword_bindable_names);
    HeapObject *old_indexes =
        array_backing(original_remap.keyword_bindable_parameter_indexes);
    HeapObject *new_names = array_backing(new_remap.keyword_bindable_names);
    HeapObject *new_indexes =
        array_backing(new_remap.keyword_bindable_parameter_indexes);
    int32_t old_name_refs = old_names->refcount();
    int32_t old_index_refs = old_indexes->refcount();
    int32_t new_name_refs = new_names->refcount();
    int32_t new_index_refs = new_indexes->refcount();
    size_t zct_size = thread->zero_count_table_size();

    size_t rewrites = 0;
    visit_strong_reference_slots(code.get(), [&](HeapReferenceSlot slot) {
        if(slot.load_pointer() == old_names)
        {
            slot.rewrite_pointer(new_names);
            ++rewrites;
        }
        else if(slot.load_pointer() == old_indexes)
        {
            slot.rewrite_pointer(new_indexes);
            ++rewrites;
        }
    });
    EXPECT_EQ(2u, rewrites);
    EXPECT_EQ(other.raw_value(), original_remap.name_at(0));
    EXPECT_EQ(9, original_remap.parameter_index_at(0));
    EXPECT_EQ(old_name_refs, old_names->refcount());
    EXPECT_EQ(old_index_refs, old_indexes->refcount());
    EXPECT_EQ(new_name_refs, new_names->refcount());
    EXPECT_EQ(new_index_refs, new_indexes->refcount());
    EXPECT_EQ(zct_size, thread->zero_count_table_size());

    visit_strong_reference_slots(code.get(), [&](HeapReferenceSlot slot) {
        if(slot.load_pointer() == new_names)
        {
            slot.rewrite_pointer(old_names);
        }
        else if(slot.load_pointer() == new_indexes)
        {
            slot.rewrite_pointer(old_indexes);
        }
    });
    EXPECT_EQ(name.raw_value(), original_remap.name_at(0));
    EXPECT_EQ(7, original_remap.parameter_index_at(0));
}

TEST(StrongReferenceSlots,
     ShapeIncludesLatentNamesAndTransitionsNotPreviousShape)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    Owned<TValue<String>> name(thread->make_object_value<String>(L"a"));
    Owned<TValue<String>> replacement(thread->make_object_value<String>(L"b"));
    auto cls_name = context.vm().get_or_create_interned_string_value(L"Slots");
    OwnedHeapPtr<ClassObject> cls(thread->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class(), NativeLayoutId::Instance));
    OwnedHeapPtr<Shape> root(Shape::make_root_with_single_descriptor(
        TValue<ClassObject>::from_oop(cls.get()), name.value(),
        DescriptorInfo::make({0, StorageKind::Inline},
                             descriptor_flag(DescriptorFlag::StableSlot)),
        1, 2, shape_flag(ShapeFlag::None)));
    Shape *latent = root.get()->derive_transition(name.value(),
                                                  ShapeTransitionVerb::Delete);
    Shape *successor =
        latent->derive_transition(name.value(), ShapeTransitionVerb::Add);
    OwnedHeapPtr<Shape> new_successor(
        successor->clone_with_flags(successor->flags()));
    ASSERT_EQ(1u, latent->latent_count());
    ASSERT_EQ(root.get(), latent->get_previous_shape());
    expect_references(latent,
                      {cls.get(), name.extract(), name.extract(), successor});
    int32_t name_refs = name.extract()->refcount();
    int32_t replacement_refs = replacement.extract()->refcount();
    int32_t successor_refs = successor->refcount();
    int32_t new_successor_refs = new_successor.get()->refcount();
    size_t zct_size = thread->zero_count_table_size();

    size_t rewrites = 0;
    visit_strong_reference_slots(latent, [&](HeapReferenceSlot slot) {
        if(slot.load_pointer() == name.extract())
        {
            slot.rewrite_pointer(replacement.extract());
            ++rewrites;
        }
        else if(slot.load_pointer() == successor)
        {
            slot.rewrite_pointer(new_successor.get());
            ++rewrites;
        }
    });
    EXPECT_EQ(3u, rewrites);
    EXPECT_EQ(replacement.value(), latent->get_property_name(0));
    EXPECT_EQ(new_successor.get(),
              latent->lookup_transition(replacement.value(),
                                        ShapeTransitionVerb::Add,
                                        descriptor_flag(DescriptorFlag::None)));
    EXPECT_EQ(root.get(), latent->get_previous_shape());
    EXPECT_EQ(name.value(), root.get()->get_property_name(0));
    EXPECT_EQ(name.value(), successor->get_property_name(0));
    EXPECT_EQ(name_refs, name.extract()->refcount());
    EXPECT_EQ(replacement_refs, replacement.extract()->refcount());
    EXPECT_EQ(successor_refs, successor->refcount());
    EXPECT_EQ(new_successor_refs, new_successor.get()->refcount());
    EXPECT_EQ(zct_size, thread->zero_count_table_size());

    visit_strong_reference_slots(latent, [&](HeapReferenceSlot slot) {
        if(slot.load_pointer() == replacement.extract())
        {
            slot.rewrite_pointer(name.extract());
        }
        else if(slot.load_pointer() == new_successor.get())
        {
            slot.rewrite_pointer(successor);
        }
    });
}

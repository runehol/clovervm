#include "builtin_types/dict.h"
#include "memory/scoped_root.h"
#include "object_model/function.h"
#include "object_model/shape.h"
#include "object_model/validity_cell.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

using namespace cl;

namespace
{
    constexpr size_t table_count =
        static_cast<size_t>(OperatorDispatchTableId::Count);

    bool vm_has_root(VirtualMachine &vm, HeapObject *target)
    {
        bool found = false;
        vm.visit_persistent_root_slots([&](HeapReferenceSlot slot) {
            found |= slot.load_pointer() == target;
        });
        return found;
    }

    using SavedRoots = std::vector<std::pair<HeapReferenceSlot, HeapObject *>>;

    void restore_vm_roots(const SavedRoots &originals)
    {
        // These VM field/backing-array cells stay address-stable throughout
        // each test. Save only changed cells and restore them directly, without
        // depending on enumeration order. Restore before managed execution/RC.
        for(const auto &[slot, original]: originals)
            slot.rewrite_pointer(original);
    }

    std::vector<Function *> helper_functions(VirtualMachine &vm)
    {
        return {vm.hash_value_helper_function().extract(),
                vm.test_equal_helper_function().extract(),
                vm.ordering_helper_function(OrderingOp::Less).extract(),
                vm.ordering_helper_function(OrderingOp::LessEqual).extract(),
                vm.ordering_helper_function(OrderingOp::Greater).extract(),
                vm.ordering_helper_function(OrderingOp::GreaterEqual).extract(),
                vm.enumerate_call_function().extract(),
                vm.iterable_to_exact_tuple_function().extract(),
                vm.iterable_to_exact_list_function().extract(),
                vm.bytearray_snapshot_function().extract(),
                vm.dict_insert_if_absent_known_hash_function().extract(),
                vm.call_keyword_error_function().extract(),
                vm.call_keyword_merge_function().extract(),
                vm.call_keyword_mapping_function().extract(),
                vm.call_keyword_add_function().extract(),
                vm.list_extend_function().extract(),
                vm.membership_iter_fallback_function().extract(),
                vm.membership_sequence_fallback_function().extract()};
    }
}  // namespace

TEST(VMPersistentRoots, PopulatedCachesAreRootedAndReused)
{
    test::VmTestContext context;
    VirtualMachine &vm = context.vm();
    ThreadState::ActivationScope activation(context.thread());
    auto adapter =
        vm.clover_function_entry_adapter(MaxCloverFunctionEntryAdapterArgs);
    ASSERT_FALSE(adapter.has_exception());
    auto errno_map = vm.os_error_errno_map();
    ASSERT_FALSE(errno_map.has_exception());
    EXPECT_TRUE(vm_has_root(vm, adapter.value()));
    EXPECT_TRUE(vm_has_root(vm, errno_map.value().extract()));

    auto cached_adapter =
        vm.clover_function_entry_adapter(MaxCloverFunctionEntryAdapterArgs);
    ASSERT_FALSE(cached_adapter.has_exception());
    auto cached_errno_map = vm.os_error_errno_map();
    ASSERT_FALSE(cached_errno_map.has_exception());
    EXPECT_EQ(adapter.value(), cached_adapter.value());
    EXPECT_EQ(errno_map.value(), cached_errno_map.value());
}

TEST(VMPersistentRoots, CachedHelperRewritesAreGetterVisibleAndRCNeutral)
{
    test::VmTestContext context;
    VirtualMachine &vm = context.vm();
    ThreadState &thread = *context.thread();
    ThreadState::ActivationScope activation(&thread);
    auto helpers = helper_functions(vm);
    for(Function *source: helpers)
    {
        auto candidate =
            std::find_if(helpers.begin(), helpers.end(), [&](Function *other) {
                return other != source && heap_ptr_is_interned(other) ==
                                              heap_ptr_is_interned(source);
            });
        ASSERT_NE(helpers.end(), candidate);
        Function *replacement = *candidate;
        int32_t source_refs = source->refcount;
        int32_t replacement_refs = replacement->refcount;
        size_t zct_size = thread.zero_count_table_size();
        SavedRoots originals;
        // A single source/replacement pair makes the rewrite idempotent even
        // if a cell is visited again through a different view.
        vm.visit_persistent_root_slots([&](HeapReferenceSlot slot) {
            if(slot.load_pointer() == source)
            {
                originals.emplace_back(slot, source);
                slot.rewrite_pointer(replacement);
            }
        });
        auto observed = helper_functions(vm);
        for(size_t index = 0; index < helpers.size(); ++index)
            EXPECT_EQ(helpers[index] == source ? replacement : helpers[index],
                      observed[index]);
        EXPECT_EQ(source_refs, source->refcount);
        EXPECT_EQ(replacement_refs, replacement->refcount);
        EXPECT_EQ(zct_size, thread.zero_count_table_size());
        restore_vm_roots(originals);
        EXPECT_EQ(helpers, helper_functions(vm));
    }
}

TEST(VMPersistentRoots, RewritesVMAliasesWithoutTouchingThreadCachesOrMaps)
{
    test::VmTestContext context;
    VirtualMachine &vm = context.vm();
    ThreadState &thread = *context.thread();
    ThreadState::ActivationScope activation(&thread);
    ClassObject *original_class = vm.int_class();
    ClassObject *replacement_class = vm.float_class();
    auto name = original_class->get_name();
    Shape *original_shape = vm.exact_dict_general_shape();
    OwnedHeapPtr<Shape> replacement_shape(
        original_shape->clone_with_flags(original_shape->flags()));
    SavedRoots originals;
    int32_t class_refs = original_class->refcount;
    int32_t replacement_class_refs = replacement_class->refcount;
    int32_t shape_refs = original_shape->refcount;
    int32_t replacement_shape_refs = replacement_shape.get()->refcount;
    size_t zct_size = thread.zero_count_table_size();
    vm.visit_persistent_root_slots([&](HeapReferenceSlot slot) {
        if(slot.load_pointer() == original_class)
        {
            originals.emplace_back(slot, original_class);
            slot.rewrite_pointer(replacement_class);
        }
        else if(slot.load_pointer() == original_shape)
        {
            originals.emplace_back(slot, original_shape);
            slot.rewrite_pointer(replacement_shape.get());
        }
    });
    EXPECT_EQ(replacement_class, vm.int_class());
    EXPECT_EQ(replacement_class,
              vm.class_for_native_layout(NativeLayoutId::BigInt));
    EXPECT_EQ(replacement_shape.get(), vm.exact_dict_general_shape());
    EXPECT_EQ(original_class,
              thread.class_for_native_layout(NativeLayoutId::BigInt));
    EXPECT_EQ(original_shape, thread.get_exact_dict_general_shape());
    EXPECT_EQ(original_class, vm.builtin_class_by_name(name));
    EXPECT_EQ(class_refs, original_class->refcount);
    EXPECT_EQ(replacement_class_refs, replacement_class->refcount);
    EXPECT_EQ(shape_refs, original_shape->refcount);
    EXPECT_EQ(replacement_shape_refs, replacement_shape.get()->refcount);
    EXPECT_EQ(zct_size, thread.zero_count_table_size());
    restore_vm_roots(originals);
    EXPECT_EQ(original_class, vm.int_class());
    EXPECT_EQ(original_class,
              vm.class_for_native_layout(NativeLayoutId::BigInt));
    EXPECT_EQ(original_shape, vm.exact_dict_general_shape());
}

TEST(VMPersistentRoots, DispatchNameRewritesPreserveNativePointersAndScalars)
{
    test::VmTestContext context;
    VirtualMachine &vm = context.vm();
    auto original_name = vm.get_or_create_interned_string_value(L"__add__");
    auto replacement_name =
        vm.get_or_create_interned_string_value(L"__relocated_add__");
    SavedRoots originals;
    std::array<OperatorDispatchTable, table_count> tables;
    std::array<std::vector<OperatorStep>, table_count> rows;
    bool has_source = false;
    for(size_t index = 0; index < table_count; ++index)
    {
        tables[index] =
            vm.operator_dispatch_table_by_id(static_cast<uint32_t>(index));
        for(size_t row = 0; row < tables[index].n_steps; ++row)
        {
            rows[index].push_back(
                tables[index].step(static_cast<uint8_t>(row)));
            has_source |=
                rows[index].back().dunder_name == original_name.extract();
        }
    }
    ASSERT_TRUE(has_source);
    vm.visit_persistent_root_slots([&](HeapReferenceSlot slot) {
        if(slot.load_pointer() == original_name.extract())
        {
            originals.emplace_back(slot, original_name.extract());
            slot.rewrite_pointer(replacement_name.extract());
        }
    });
    for(size_t index = 0; index < table_count; ++index)
    {
        const auto &table =
            vm.operator_dispatch_table_by_id(static_cast<uint32_t>(index));
        EXPECT_EQ(tables[index].steps, table.steps);
        EXPECT_EQ(tables[index].n_steps, table.n_steps);
        for(size_t row = 0; row < rows[index].size(); ++row)
        {
            const OperatorStep &before = rows[index][row];
            const OperatorStep &after = table.steps[row];
            EXPECT_EQ(before.dunder_name == original_name.extract()
                          ? replacement_name.extract()
                          : before.dunder_name,
                      after.dunder_name);
            EXPECT_EQ(before.action, after.action);
            EXPECT_EQ(before.applicability, after.applicability);
            EXPECT_EQ(before.else_skip, after.else_skip);
            EXPECT_EQ(before.truth_result_kind, after.truth_result_kind);
        }
    }
    EXPECT_EQ(original_name,
              vm.get_or_create_interned_string_value(L"__add__"));
    restore_vm_roots(originals);
}

TEST(VMPersistentRoots, ReclamationCombinesVMAndThreadRoots)
{
    test::VmTestContext context;
    VirtualMachine &vm = context.vm();
    ThreadState &thread = *context.thread();
    ThreadState::ActivationScope activation(&thread);
    ModuleObject *original = vm.global_builtins_module().extract();
    auto name = vm.get_or_create_interned_string_value(L"rooted-builtins");
    ModuleObject *module =
        context.make_test_module_object(name, Value::from_oop(original));
    ValidityCell *native = thread.make_internal_raw<ValidityCell>(
        ValidityCellDependencyMutability::Mutable);
    thread.add_to_zero_count_table_if_needed(module);
    thread.add_to_zero_count_table_if_needed(native);
    ASSERT_EQ(0, module->refcount);
    ASSERT_EQ(0, native->refcount);
    vm.set_global_builtins_module(module);
    {
        ScopedRoot root(thread, HeapReferenceSlot(&native));
        vm.run_heap_reclamation();
        EXPECT_EQ(0, module->refcount);
        EXPECT_EQ(0, native->refcount);
        EXPECT_TRUE(thread.zero_count_table_contains_for_testing(module));
        EXPECT_TRUE(thread.zero_count_table_contains_for_testing(native));
        EXPECT_EQ(module, vm.global_builtins_module().extract());
    }
    vm.set_global_builtins_module(original);
    vm.run_heap_reclamation();
    EXPECT_FALSE(thread.zero_count_table_contains_for_testing(module));
    EXPECT_FALSE(thread.zero_count_table_contains_for_testing(native));
}

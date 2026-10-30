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
#include <vector>

using namespace cl;

namespace
{
    constexpr size_t layout_count = static_cast<size_t>(NativeLayoutId::Count);
    constexpr size_t table_count =
        static_cast<size_t>(OperatorDispatchTableId::Count);

    std::vector<HeapObject *> vm_roots(VirtualMachine &vm)
    {
        std::vector<HeapObject *> roots;
        vm.visit_persistent_root_slots([&](HeapReferenceSlot slot) {
            roots.push_back(slot.load_pointer());
        });
        return roots;
    }

    void restore_vm_roots(VirtualMachine &vm,
                          const std::vector<HeapObject *> &originals)
    {
        // The physical member set and traversal order are unchanged during
        // these tests. Restore simulated substitutions before ordinary RC or
        // managed execution can observe unrelated replacement objects.
        size_t index = 0;
        vm.visit_persistent_root_slots([&](HeapReferenceSlot slot) {
            HeapObject *original = originals[index++];
            if(original != nullptr)
                slot.rewrite_pointer(original);
            else
                EXPECT_EQ(nullptr, slot.load_pointer());
        });
        EXPECT_EQ(originals.size(), index);
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

TEST(VMPersistentRoots, CoversEveryFixedCellIncludingNullsAndAliases)
{
    test::VmTestContext context;
    VirtualMachine &vm = context.vm();
    ThreadState::ActivationScope activation(context.thread());
    std::vector<HeapObject *> expected{
        vm.type_class(),          vm.str_class(),
        vm.int_class(),           vm.bool_class(),
        vm.none_type_class(),     vm.not_implemented_type_class(),
        vm.ellipsis_type_class(), vm.dunder_class_name().extract()};
    for(size_t table_index = 0; table_index < table_count; ++table_index)
    {
        const OperatorDispatchTable &table = vm.operator_dispatch_table_by_id(
            static_cast<uint32_t>(table_index));
        // The backing array has six initialized rows even for shorter tables.
        for(size_t row = 0; row < 6; ++row)
            expected.push_back(table.steps == nullptr
                                   ? nullptr
                                   : table.steps[row].dunder_name);
    }
    for(Shape *shape:
        {vm.smi_shape(), vm.bool_shape(), vm.none_shape(),
         vm.not_implemented_shape(), vm.ellipsis_shape(),
         vm.str_instance_root_shape(), vm.exact_dict_string_key_shape(),
         vm.exact_dict_general_shape(), vm.slice_step_none_shape(),
         vm.slice_general_shape()})
        expected.push_back(shape);
    for(size_t index = 0; index < layout_count; ++index)
        expected.push_back(
            vm.class_for_native_layout(static_cast<NativeLayoutId>(index)));
    for(uint32_t arity = 0; arity <= MaxCloverFunctionEntryAdapterArgs; ++arity)
    {
        auto adapter = vm.clover_function_entry_adapter(arity);
        ASSERT_FALSE(adapter.has_exception());
        expected.push_back(adapter.value());
    }
    auto errno_map = vm.os_error_errno_map();
    ASSERT_FALSE(errno_map.has_exception());
    expected.push_back(vm.get_range_builtin().as.ptr);
    expected.push_back(errno_map.value().extract());
    auto helpers = helper_functions(vm);
    EXPECT_EQ(18u, helpers.size());
    expected.insert(expected.end(), helpers.begin(), helpers.end());
    expected.push_back(vm.global_builtins_module().extract());
    expected.push_back(vm.bytearray_snapshot_function()
                           .extract()
                           ->code_object.extract()
                           ->get_defining_module()
                           .extract());
    expected.push_back(vm.sys_module().extract());
    expected.push_back(vm.imported_modules().extract());

    EXPECT_EQ(8 + table_count * 6 + 10 + layout_count +
                  MaxCloverFunctionEntryAdapterArgs + 1 + 20 + 4,
              expected.size());
    EXPECT_EQ(expected, vm_roots(vm));
    EXPECT_GT(std::count(expected.begin(), expected.end(), vm.str_class()), 1);
    EXPECT_GT(std::count(expected.begin(), expected.end(), nullptr), 0);
}

TEST(VMPersistentRoots, LazyCachesReplaceTheirPreviouslyEmptyCells)
{
    test::VmTestContext context;
    VirtualMachine &vm = context.vm();
    ThreadState::ActivationScope activation(context.thread());
    auto before = vm_roots(vm);
    auto adapter =
        vm.clover_function_entry_adapter(MaxCloverFunctionEntryAdapterArgs);
    ASSERT_FALSE(adapter.has_exception());
    auto with_adapter = vm_roots(vm);
    EXPECT_EQ(0, std::count(before.begin(), before.end(), adapter.value()));
    EXPECT_EQ(1, std::count(with_adapter.begin(), with_adapter.end(),
                            adapter.value()));
    size_t adapter_changes = 0;
    for(size_t index = 0; index < before.size(); ++index)
    {
        if(before[index] != with_adapter[index])
        {
            ++adapter_changes;
            EXPECT_EQ(nullptr, before[index]);
            EXPECT_EQ(adapter.value(), with_adapter[index]);
        }
    }
    EXPECT_EQ(1u, adapter_changes);
    auto errno_map = vm.os_error_errno_map();
    ASSERT_FALSE(errno_map.has_exception());
    EXPECT_EQ(0, std::count(before.begin(), before.end(),
                            errno_map.value().extract()));
    auto after = vm_roots(vm);
    ASSERT_EQ(before.size(), after.size());
    size_t changed = 0;
    for(size_t index = 0; index < before.size(); ++index)
    {
        if(with_adapter[index] == after[index])
            continue;
        ++changed;
        EXPECT_EQ(nullptr, with_adapter[index]);
        if(after[index] != errno_map.value().extract())
        {
            // Building the errno dictionary can call managed helpers and
            // populate additional entry adapters. Only those cells may change.
            constexpr size_t adapter_begin =
                8 + table_count * 6 + 10 + layout_count;
            ASSERT_GE(index, adapter_begin);
            ASSERT_LE(index - adapter_begin, MaxCloverFunctionEntryAdapterArgs);
            auto warmed = vm.clover_function_entry_adapter(
                static_cast<uint32_t>(index - adapter_begin));
            ASSERT_FALSE(warmed.has_exception());
            EXPECT_EQ(warmed.value(), after[index]);
        }
    }
    EXPECT_GE(changed, 1u);
    EXPECT_EQ(1, std::count(after.begin(), after.end(), adapter.value()));
    EXPECT_EQ(
        1, std::count(after.begin(), after.end(), errno_map.value().extract()));
    EXPECT_EQ(after, vm_roots(vm));
}

TEST(VMPersistentRoots, CachedHelperRewritesAreGetterVisibleAndRCNeutral)
{
    test::VmTestContext context;
    VirtualMachine &vm = context.vm();
    ThreadState &thread = *context.thread();
    ThreadState::ActivationScope activation(&thread);
    auto helpers = helper_functions(vm);
    auto originals = vm_roots(vm);
    std::vector<Function *> replacements;
    std::vector<int32_t> refcounts;
    for(Function *source: helpers)
    {
        auto replacement = std::find_if(
            helpers.begin(), helpers.end(), [&](Function *candidate) {
                return candidate != source && heap_ptr_is_interned(candidate) ==
                                                  heap_ptr_is_interned(source);
            });
        ASSERT_NE(helpers.end(), replacement);
        replacements.push_back(*replacement);
        refcounts.push_back(source->refcount);
    }
    size_t zct_size = thread.zero_count_table_size();
    size_t rewrites = 0;
    vm.visit_persistent_root_slots([&](HeapReferenceSlot slot) {
        auto found =
            std::find(helpers.begin(), helpers.end(), slot.load_pointer());
        if(found != helpers.end())
        {
            slot.rewrite_pointer(replacements[found - helpers.begin()]);
            ++rewrites;
        }
    });
    EXPECT_EQ(helpers.size(), rewrites);
    EXPECT_EQ(replacements, helper_functions(vm));
    for(size_t index = 0; index < helpers.size(); ++index)
        EXPECT_EQ(refcounts[index], helpers[index]->refcount);
    EXPECT_EQ(zct_size, thread.zero_count_table_size());
    restore_vm_roots(vm, originals);
    EXPECT_EQ(helpers, helper_functions(vm));
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
    auto originals = vm_roots(vm);
    int32_t class_refs = original_class->refcount;
    int32_t replacement_class_refs = replacement_class->refcount;
    int32_t shape_refs = original_shape->refcount;
    int32_t replacement_shape_refs = replacement_shape.get()->refcount;
    size_t zct_size = thread.zero_count_table_size();
    size_t rewrites = 0;
    vm.visit_persistent_root_slots([&](HeapReferenceSlot slot) {
        if(slot.load_pointer() == original_class)
        {
            slot.rewrite_pointer(replacement_class);
            ++rewrites;
        }
        else if(slot.load_pointer() == original_shape)
        {
            slot.rewrite_pointer(replacement_shape.get());
            ++rewrites;
        }
    });
    EXPECT_EQ(
        std::count(originals.begin(), originals.end(), original_class) +
            std::count(originals.begin(), originals.end(), original_shape),
        rewrites);
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
    restore_vm_roots(vm, originals);
    EXPECT_EQ(originals, vm_roots(vm));
}

TEST(VMPersistentRoots, DispatchNameRewritesPreserveNativePointersAndScalars)
{
    test::VmTestContext context;
    VirtualMachine &vm = context.vm();
    auto original_name = vm.get_or_create_interned_string_value(L"__add__");
    auto replacement_name =
        vm.get_or_create_interned_string_value(L"__relocated_add__");
    auto originals = vm_roots(vm);
    std::array<OperatorDispatchTable, table_count> tables;
    std::array<std::array<OperatorStep, 6>, table_count> rows;
    for(size_t index = 0; index < table_count; ++index)
    {
        tables[index] =
            vm.operator_dispatch_table_by_id(static_cast<uint32_t>(index));
        for(size_t row = 0; row < 6; ++row)
            rows[index][row] = tables[index].steps[row];
    }
    size_t rewrites = 0;
    vm.visit_persistent_root_slots([&](HeapReferenceSlot slot) {
        if(slot.load_pointer() == original_name.extract())
        {
            slot.rewrite_pointer(replacement_name.extract());
            ++rewrites;
        }
    });
    EXPECT_GT(rewrites, 0u);
    EXPECT_EQ(
        std::count(originals.begin(), originals.end(), original_name.extract()),
        rewrites);
    for(size_t index = 0; index < table_count; ++index)
    {
        const auto &table =
            vm.operator_dispatch_table_by_id(static_cast<uint32_t>(index));
        EXPECT_EQ(tables[index].steps, table.steps);
        EXPECT_EQ(tables[index].n_steps, table.n_steps);
        for(size_t row = 0; row < 6; ++row)
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
    restore_vm_roots(vm, originals);
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

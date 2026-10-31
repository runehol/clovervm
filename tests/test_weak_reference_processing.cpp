#include "builtin_types/float.h"
#include "bytecode/code_object.h"
#include "memory/native_layout_descriptor.h"
#include "object_model/function.h"
#include "object_model/shape.h"
#include "object_model/validity_cell.h"
#include "runtime/expanded_call.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

using namespace cl;

namespace
{
    Value native_handler(ThreadState *, Value value, Value) { return value; }

    class WeakReferenceProcessing : public testing::Test
    {
    protected:
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope activation{thread};
        OwnedHeapPtr<CodeObject> owner{context.compile_file(L"pass")};
        OwnedHeapPtr<CodeObject> old_code{context.compile_file(L"pass")};
        OwnedHeapPtr<CodeObject> new_code{context.compile_file(L"pass")};
        OwnedHeapPtr<Function> old_function{thread->make_object_raw<Function>(
            TValue<CodeObject>::from_oop(old_code.get()),
            Optional<TValue<String>>::none())};
        OwnedHeapPtr<Function> new_function{thread->make_object_raw<Function>(
            TValue<CodeObject>::from_oop(new_code.get()),
            Optional<TValue<String>>::none())};
        OwnedHeapPtr<ModuleObject> old_module{context.make_test_module_object(
            context.vm().get_or_create_interned_string_value(L"old"),
            Value::None())};
        OwnedHeapPtr<ModuleObject> new_module{context.make_test_module_object(
            context.vm().get_or_create_interned_string_value(L"new"),
            Value::None())};
        OwnedHeapPtr<Shape> old_shape{thread->make_internal_raw<Shape>(
            TValue<ClassObject>::from_oop(context.vm().object_class()), nullptr,
            0, 0, 0, mutable_attribute_shape_flags(), 0)};
        OwnedHeapPtr<Shape> new_shape{
            old_shape.get()->clone_with_flags(old_shape.get()->flags())};
        OwnedHeapPtr<Shape> old_next{
            old_shape.get()->clone_with_flags(old_shape.get()->flags())};
        OwnedHeapPtr<Shape> new_next{
            old_shape.get()->clone_with_flags(old_shape.get()->flags())};
        OwnedHeapPtr<ValidityCell> old_cell{
            thread->make_internal_raw<ValidityCell>(
                ValidityCellDependencyMutability::Mutable)};
        OwnedHeapPtr<ValidityCell> new_cell{
            thread->make_internal_raw<ValidityCell>(
                ValidityCellDependencyMutability::Mutable)};
        Owned<TValue<String>> old_value{
            thread->make_object_value<String>(L"old-value")};
        Owned<TValue<String>> new_value{
            thread->make_object_value<String>(L"new-value")};

        std::array<HeapObject *, 7> sources()
        {
            return {old_code.get(),     old_function.get(), old_module.get(),
                    old_shape.get(),    old_next.get(),     old_cell.get(),
                    old_value.extract()};
        }

        std::array<HeapObject *, 7> destinations()
        {
            return {new_code.get(),     new_function.get(), new_module.get(),
                    new_shape.get(),    new_next.get(),     new_cell.get(),
                    new_value.extract()};
        }

        HeapObject *resolve(HeapObject *pointer, bool reverse = false)
        {
            auto from = reverse ? destinations() : sources();
            auto to = reverse ? sources() : destinations();
            for(size_t idx = 0; idx < from.size(); ++idx)
            {
                if(pointer == from[idx])
                {
                    return to[idx];
                }
            }
            ADD_FAILURE() << "resolver received an inactive or non-heap field";
            return pointer;
        }

        void populate()
        {
            InlineCacheTables &caches = owner.get()->inline_caches;
            caches = InlineCacheTables{};
            caches.attribute_read_caches.resize(5);
            auto &constant = caches.attribute_read_caches[0];
            constant.receiver_shape = old_shape.get();
            constant.lookup_validity_cell = old_cell.get();
            constant.plan = AttributeReadPlan::constant(old_value.raw_value());
            // This inactive field must not influence constant-plan liveness.
            constant.plan.storage_owner = old_module.get();
            std::array read_kinds{AttributeReadPlanKind::ReceiverSlot,
                                  AttributeReadPlanKind::BindFunctionReceiver,
                                  AttributeReadPlanKind::DataDescriptorGet,
                                  AttributeReadPlanKind::NonDataDescriptorGet};
            for(size_t idx = 0; idx < read_kinds.size(); ++idx)
            {
                auto &cache = caches.attribute_read_caches[idx + 1];
                cache.receiver_shape = old_shape.get();
                cache.lookup_validity_cell = old_cell.get();
                cache.plan = AttributeReadPlan::from_storage(
                    AttributeReadPlanPath::InstanceClassChain, read_kinds[idx],
                    old_module.get(), {3, StorageKind::Inline});
                cache.plan.constant_value = old_value.raw_value();
            }
            caches.attribute_mutation_caches.resize(4);
            caches.attribute_mutation_caches[0].plan =
                AttributeMutationPlan::store_existing(old_module.get(),
                                                      {3, StorageKind::Inline});
            caches.attribute_mutation_caches[1].plan =
                AttributeMutationPlan::add_own_property(
                    old_next.get(), {3, StorageKind::Inline});
            caches.attribute_mutation_caches[2].plan =
                AttributeMutationPlan::delete_own_property(
                    old_next.get(), {3, StorageKind::Inline});
            caches.attribute_mutation_caches[3].plan =
                AttributeMutationPlan::change_class();
            // ChangeClass has no active pointer arm. Detect accidental visits
            // without dereferencing junk or relying on object destruction.
            caches.attribute_mutation_caches[3].plan.next_shape =
                reinterpret_cast<Shape *>(uintptr_t{1});
            for(auto &cache: caches.attribute_mutation_caches)
            {
                cache.receiver_shape = old_shape.get();
                cache.lookup_validity_cell = old_cell.get();
            }
            caches.module_global_read_caches.resize(1);
            caches.module_global_read_caches[0].lookup_validity_cell =
                old_cell.get();
            caches.module_global_read_caches[0].slot = {
                old_module.get(), {3, StorageKind::Inline}};
            caches.module_global_mutation_caches.resize(1);
            caches.module_global_mutation_caches[0].lookup_validity_cell =
                old_cell.get();
            caches.module_global_mutation_caches[0].store_existing =
                ModuleGlobalStoreExistingPlan::make(old_module.get(),
                                                    {3, StorageKind::Inline});
            caches.function_call_caches.resize(1);
            auto &call = caches.function_call_caches[0];
            call.guard_value = Value::from_oop(old_function.get());
            call.function = old_function.get();
            call.code_object = old_code.get();
            call.validity_cell = old_cell.get();
            call.n_args = 3;
            caches.keyword_call_caches.resize(1);
            auto &keyword = caches.keyword_call_caches[0];
            keyword.guard_value = call.guard_value;
            keyword.function = old_function.get();
            keyword.code_object = old_code.get();
            keyword.validity_cell = old_cell.get();
            keyword.n_pos_args = 3;
            keyword.n_kw_args = 2;
            keyword.keyword_dest_regs = std::make_unique<int8_t[]>(2);
            keyword.keyword_dest_regs[0] = -2;
            keyword.keyword_dest_regs[1] = -3;
            caches.expanded_call_caches.resize(1);
            auto &expanded = caches.expanded_call_caches[0];
            expanded.guard_value = call.guard_value;
            expanded.function = old_function.get();
            expanded.validity_cell = old_cell.get();
            expanded.mode = ExpandedCallMode::PureCollector;
            expanded.binding_plan = std::make_unique<ExpandedCallCachedPlan>();
            expanded.binding_plan->positional_count = 3;
            caches.operator_caches.resize(1);
            auto &op = caches.operator_caches[0];
            op.operand_shape_keys[0] = ShapeKey::from_shape(old_shape.get());
            op.operand_shape_keys[1] = ShapeKey::from_shape(old_next.get());
            op.operand_lookup_validity_cells[0] = nullptr;
            op.operand_lookup_validity_cells[1] = old_cell.get();
            op.function = old_function.get();
            op.code_object = old_code.get();
            op.trusted_handler.binary = native_handler;
            op.n_args = 3;
            op.resume_index = 9;
        }
    };
}  // namespace

TEST_F(WeakReferenceProcessing, RepairsAllCacheFamiliesWithoutOwnershipChanges)
{
    populate();
    auto &caches = owner.get()->inline_caches;
    auto *keyword_storage =
        caches.keyword_call_caches[0].keyword_dest_regs.get();
    auto *plan = caches.expanded_call_caches[0].binding_plan.get();
    std::array<int32_t, 7> source_refs;
    std::array<int32_t, 7> destination_refs;
    for(size_t idx = 0; idx < sources().size(); ++idx)
    {
        source_refs[idx] = sources()[idx]->refcount;
        destination_refs[idx] = destinations()[idx]->refcount;
    }
    size_t zct_size = thread->zero_count_table_size();
    process_weak_references(
        owner.get(), [&](HeapObject *object) { return resolve(object); });
    for(size_t idx = 0; idx < caches.attribute_read_caches.size(); ++idx)
    {
        const auto &cache = caches.attribute_read_caches[idx];
        EXPECT_EQ(new_shape.get(), cache.receiver_shape);
        EXPECT_EQ(new_cell.get(), cache.lookup_validity_cell);
        if(idx == 0)
        {
            EXPECT_EQ(new_value.raw_value(), cache.plan.constant_value);
            EXPECT_EQ(old_module.get(), cache.plan.storage_owner);
        }
        else
        {
            EXPECT_EQ(new_module.get(), cache.plan.storage_owner);
            EXPECT_EQ(old_value.raw_value(), cache.plan.constant_value);
            EXPECT_EQ(3, cache.plan.storage_location.physical_idx);
        }
    }
    for(size_t idx = 0; idx < caches.attribute_mutation_caches.size(); ++idx)
    {
        const auto &cache = caches.attribute_mutation_caches[idx];
        EXPECT_EQ(new_shape.get(), cache.receiver_shape);
        EXPECT_EQ(new_cell.get(), cache.lookup_validity_cell);
        if(idx == 0)
            EXPECT_EQ(new_module.get(), cache.plan.storage_owner);
        else if(idx < 3)
            EXPECT_EQ(new_next.get(), cache.plan.next_shape);
        else
            EXPECT_TRUE(cache.plan.is_change_class());
    }
    EXPECT_EQ(new_module.get(),
              caches.module_global_read_caches[0].slot.storage_owner);
    EXPECT_EQ(new_cell.get(),
              caches.module_global_read_caches[0].lookup_validity_cell);
    EXPECT_EQ(
        new_module.get(),
        caches.module_global_mutation_caches[0].store_existing.storage_owner);
    EXPECT_EQ(new_cell.get(),
              caches.module_global_mutation_caches[0].lookup_validity_cell);
    const auto &call = caches.function_call_caches[0];
    EXPECT_EQ(Value::from_oop(new_function.get()), call.guard_value);
    EXPECT_EQ(new_function.get(), call.function);
    EXPECT_EQ(new_code.get(), call.code_object);
    EXPECT_EQ(new_cell.get(), call.validity_cell);
    EXPECT_EQ(3u, call.n_args);
    const auto &keyword = caches.keyword_call_caches[0];
    EXPECT_EQ(call.guard_value, keyword.guard_value);
    EXPECT_EQ(new_function.get(), keyword.function);
    EXPECT_EQ(new_code.get(), keyword.code_object);
    EXPECT_EQ(new_cell.get(), keyword.validity_cell);
    EXPECT_EQ(keyword_storage, keyword.keyword_dest_regs.get());
    EXPECT_EQ(-3, keyword.keyword_dest_regs[1]);
    EXPECT_EQ(2, keyword.n_kw_args);
    const auto &expanded = caches.expanded_call_caches[0];
    EXPECT_EQ(call.guard_value, expanded.guard_value);
    EXPECT_EQ(new_function.get(), expanded.function);
    EXPECT_EQ(new_cell.get(), expanded.validity_cell);
    EXPECT_EQ(plan, expanded.binding_plan.get());
    EXPECT_EQ(ExpandedCallMode::PureCollector, expanded.mode);
    const auto &op = caches.operator_caches[0];
    EXPECT_EQ(ShapeKey::from_shape(new_shape.get()), op.operand_shape_keys[0]);
    EXPECT_EQ(ShapeKey::from_shape(new_next.get()), op.operand_shape_keys[1]);
    EXPECT_EQ(nullptr, op.operand_lookup_validity_cells[0]);
    EXPECT_EQ(new_cell.get(), op.operand_lookup_validity_cells[1]);
    EXPECT_EQ(new_function.get(), op.function);
    EXPECT_EQ(new_code.get(), op.code_object);
    EXPECT_EQ(native_handler, op.trusted_handler.binary);
    EXPECT_EQ(9u, op.resume_index);
    for(size_t idx = 0; idx < sources().size(); ++idx)
    {
        EXPECT_EQ(source_refs[idx], sources()[idx]->refcount);
        EXPECT_EQ(destination_refs[idx], destinations()[idx]->refcount);
    }
    EXPECT_EQ(zct_size, thread->zero_count_table_size());
    // These are simulated relocations, not actual copies. Restore references
    // before allowing ordinary refcount teardown to observe the graph.
    process_weak_references(
        owner.get(), [&](HeapObject *object) { return resolve(object, true); });
}

TEST_F(WeakReferenceProcessing, DeadTargetsResetWholeEntriesAcrossAllFamilies)
{
    for(HeapObject *dead: sources())
    {
        SCOPED_TRACE(dead);
        populate();
        process_weak_references(owner.get(), [&](HeapObject *object) {
            return object == dead ? nullptr : resolve(object);
        });
        auto &caches = owner.get()->inline_caches;
        bool receiver_dead = dead == old_shape.get() || dead == old_cell.get();
        for(size_t idx = 0; idx < caches.attribute_read_caches.size(); ++idx)
        {
            bool cleared =
                receiver_dead || (idx == 0 ? dead == old_value.extract()
                                           : dead == old_module.get());
            const auto &cache = caches.attribute_read_caches[idx];
            EXPECT_EQ(cleared, cache.receiver_shape == nullptr);
            if(cleared)
            {
                EXPECT_EQ(nullptr, cache.lookup_validity_cell);
                EXPECT_EQ(nullptr, cache.plan.storage_owner);
                EXPECT_TRUE(cache.plan.constant_value.is_not_present());
            }
        }
        for(size_t idx = 0; idx < caches.attribute_mutation_caches.size();
            ++idx)
        {
            bool cleared = receiver_dead ||
                           (idx == 0 && dead == old_module.get()) ||
                           ((idx == 1 || idx == 2) && dead == old_next.get());
            const auto &cache = caches.attribute_mutation_caches[idx];
            EXPECT_EQ(cleared, cache.receiver_shape == nullptr);
            if(cleared)
            {
                EXPECT_EQ(nullptr, cache.lookup_validity_cell);
                EXPECT_EQ(AttributeMutationPlanKind::StoreExisting,
                          cache.plan.kind);
                EXPECT_EQ(nullptr, cache.plan.storage_owner);
                EXPECT_EQ(-1, cache.plan.physical_idx);
            }
        }
        bool global_dead = dead == old_cell.get() || dead == old_module.get();
        EXPECT_EQ(global_dead,
                  caches.module_global_read_caches[0].lookup_validity_cell ==
                      nullptr);
        EXPECT_EQ(
            global_dead,
            caches.module_global_mutation_caches[0].lookup_validity_cell ==
                nullptr);
        if(global_dead)
        {
            EXPECT_EQ(nullptr,
                      caches.module_global_read_caches[0].slot.storage_owner);
            EXPECT_EQ(nullptr, caches.module_global_mutation_caches[0]
                                   .store_existing.storage_owner);
        }
        bool call_dead = dead == old_function.get() || dead == old_code.get() ||
                         dead == old_cell.get();
        const auto &call = caches.function_call_caches[0];
        const auto &keyword = caches.keyword_call_caches[0];
        EXPECT_EQ(call_dead, call.function == nullptr);
        EXPECT_EQ(call_dead, keyword.function == nullptr);
        if(call_dead)
        {
            EXPECT_TRUE(call.guard_value.is_not_present());
            EXPECT_EQ(nullptr, call.code_object);
            EXPECT_EQ(nullptr, call.validity_cell);
            EXPECT_EQ(UINT32_MAX, call.n_args);
            EXPECT_TRUE(keyword.guard_value.is_not_present());
            EXPECT_EQ(nullptr, keyword.code_object);
            EXPECT_EQ(nullptr, keyword.validity_cell);
            EXPECT_EQ(nullptr, keyword.keyword_dest_regs);
            EXPECT_EQ(UINT8_MAX, keyword.n_pos_args);
        }
        bool expanded_dead =
            dead == old_function.get() || dead == old_cell.get();
        const auto &expanded = caches.expanded_call_caches[0];
        EXPECT_EQ(expanded_dead, expanded.function == nullptr);
        if(expanded_dead)
        {
            EXPECT_TRUE(expanded.guard_value.is_not_present());
            EXPECT_EQ(nullptr, expanded.validity_cell);
            EXPECT_EQ(nullptr, expanded.binding_plan);
            EXPECT_EQ(ExpandedCallMode::General, expanded.mode);
        }
        bool operator_dead = receiver_dead || dead == old_next.get() ||
                             dead == old_function.get() ||
                             dead == old_code.get();
        const auto &op = caches.operator_caches[0];
        EXPECT_EQ(operator_dead, op.empty());
        if(operator_dead)
        {
            EXPECT_FALSE(op.operand_shape_keys[1].is_valid());
            EXPECT_EQ(nullptr, op.operand_lookup_validity_cells[0]);
            EXPECT_EQ(nullptr, op.operand_lookup_validity_cells[1]);
            EXPECT_EQ(nullptr, op.function);
            EXPECT_EQ(nullptr, op.code_object);
            EXPECT_TRUE(op.trusted_handler.is_null());
            EXPECT_EQ(UINT32_MAX, op.resume_index);
        }
        process_weak_references(owner.get(), [&](HeapObject *object) {
            return resolve(object, true);
        });
    }
}

TEST_F(WeakReferenceProcessing,
       CallGuardsAndResolvedFunctionsCanDieIndependently)
{
    for(HeapObject *dead:
        std::array<HeapObject *, 2>{old_value.extract(), old_function.get()})
    {
        SCOPED_TRACE(dead);
        populate();
        auto &caches = owner.get()->inline_caches;
        // Constructor-call caches can guard a different object from the
        // resolved function. Test each weak role without invoking the cache.
        caches.function_call_caches[0].guard_value = old_value.raw_value();
        caches.function_call_caches[0].validity_cell = nullptr;
        caches.keyword_call_caches[0].guard_value = old_value.raw_value();
        caches.keyword_call_caches[0].validity_cell = nullptr;
        caches.expanded_call_caches[0].guard_value = old_value.raw_value();
        caches.expanded_call_caches[0].validity_cell = nullptr;
        process_weak_references(owner.get(), [&](HeapObject *object) {
            return object == dead ? nullptr : resolve(object);
        });
        EXPECT_TRUE(
            caches.function_call_caches[0].guard_value.is_not_present());
        EXPECT_EQ(nullptr, caches.function_call_caches[0].function);
        EXPECT_TRUE(caches.keyword_call_caches[0].guard_value.is_not_present());
        EXPECT_EQ(nullptr, caches.keyword_call_caches[0].function);
        EXPECT_EQ(nullptr, caches.keyword_call_caches[0].keyword_dest_regs);
        EXPECT_TRUE(
            caches.expanded_call_caches[0].guard_value.is_not_present());
        EXPECT_EQ(nullptr, caches.expanded_call_caches[0].function);
        EXPECT_EQ(nullptr, caches.expanded_call_caches[0].binding_plan);
        process_weak_references(owner.get(), [&](HeapObject *object) {
            return resolve(object, true);
        });
    }
}

TEST_F(WeakReferenceProcessing,
       UnmovedTargetsAndNonPointerStatesRemainUnchanged)
{
    populate();
    auto &caches = owner.get()->inline_caches;
    caches.operator_caches[0].operand_shape_keys[0] =
        ShapeKey::from_value(Value::from_smi(7));
    caches.operator_caches[0].operand_shape_keys[1] = ShapeKey{};
    caches.function_call_caches[0].guard_value = Value::from_smi(7);
    caches.function_call_caches[0].validity_cell = nullptr;
    size_t calls = 0;
    process_weak_references(owner.get(), [&](HeapObject *object) {
        ++calls;
        resolve(
            object);  // Validates that no inline/sentinel/native bits arrive.
        return object;
    });
    EXPECT_GT(calls, 0u);
    EXPECT_EQ(ShapeKey::from_value(Value::from_smi(7)),
              caches.operator_caches[0].operand_shape_keys[0]);
    EXPECT_FALSE(caches.operator_caches[0].operand_shape_keys[1].is_valid());
    EXPECT_EQ(Value::from_smi(7), caches.function_call_caches[0].guard_value);
    EXPECT_EQ(nullptr, caches.function_call_caches[0].validity_cell);
    EXPECT_EQ(old_function.get(), caches.function_call_caches[0].function);
    caches = InlineCacheTables{};
    caches.function_call_caches.resize(1);
    caches.operator_caches.resize(1);
    process_weak_references(
        owner.get(), [&](HeapObject *object) -> HeapObject * {
            ADD_FAILURE() << "empty cache reached resolver: " << object;
            return nullptr;
        });
    Owned<TValue<Float>> floating(thread->make_object_value<Float>(1));
    process_weak_references(
        floating.extract(), [&](HeapObject *) -> HeapObject * {
            ADD_FAILURE() << "layout with no weak callback reached resolver";
            return nullptr;
        });
}

TEST_F(WeakReferenceProcessing,
       ShapeBackLinkIsRepairedOrClearedWithoutOwnership)
{
    auto name = context.vm().get_or_create_interned_string_value(L"x");
    Shape *child =
        old_shape.get()->derive_transition(name, ShapeTransitionVerb::Add);
    int32_t old_refs = old_shape.get()->refcount;
    int32_t new_refs = new_shape.get()->refcount;
    size_t zct_size = thread->zero_count_table_size();
    process_weak_references(
        child, [&](HeapObject *object) { return resolve(object); });
    EXPECT_EQ(new_shape.get(), child->get_previous_shape());
    EXPECT_EQ(old_refs, old_shape.get()->refcount);
    EXPECT_EQ(new_refs, new_shape.get()->refcount);
    EXPECT_EQ(zct_size, thread->zero_count_table_size());
    process_weak_references(
        child, [&](HeapObject *object) { return resolve(object, true); });
    EXPECT_EQ(old_shape.get(), child->get_previous_shape());
    process_weak_references(child, [&](HeapObject *object) -> HeapObject * {
        EXPECT_EQ(old_shape.get(), object);
        return nullptr;
    });
    EXPECT_EQ(nullptr, child->get_previous_shape());
    EXPECT_EQ(old_refs, old_shape.get()->refcount);
    EXPECT_EQ(new_refs, new_shape.get()->refcount);
    EXPECT_EQ(zct_size, thread->zero_count_table_size());
    process_weak_references(child, [&](HeapObject *) -> HeapObject * {
        ADD_FAILURE() << "cleared back-link reached resolver";
        return nullptr;
    });
}

TEST_F(WeakReferenceProcessing,
       ClearingExpandedPlanReleasesOnlyItsStrongOwnership)
{
    populate();
    // Keep this name exclusively in the strong plan, not also in a weak
    // constant-value cache from populate().
    owner.get()->inline_caches.attribute_read_caches.clear();
    auto &expanded = owner.get()->inline_caches.expanded_call_caches[0];
    expanded.binding_plan->keyword_names.emplace_back(old_value.value());
    String *plan_only = thread->make_object_raw<String>(L"plan-only");
    expanded.binding_plan->keyword_names.emplace_back(
        TValue<String>::from_oop(plan_only));
    ASSERT_EQ(2, old_value.extract()->refcount);
    ASSERT_EQ(1, plan_only->refcount);
    process_weak_references(owner.get(),
                            [&](HeapObject *object) -> HeapObject * {
                                // A strongly owned keyword is not weak: it must
                                // not reach this resolver.
                                EXPECT_NE(old_value.extract(), object);
                                EXPECT_NE(plan_only, object);
                                return object;
                            });
    EXPECT_NE(nullptr, expanded.binding_plan);
    EXPECT_EQ(2, old_value.extract()->refcount);
    size_t zct_size = thread->zero_count_table_size();
    process_weak_references(owner.get(), [&](HeapObject *object) {
        return object == old_function.get() ? nullptr : object;
    });
    EXPECT_EQ(nullptr, expanded.binding_plan);
    EXPECT_TRUE(expanded.guard_value.is_not_present());
    EXPECT_EQ(1, old_value.extract()->refcount);
    EXPECT_EQ(zct_size + 1, thread->zero_count_table_size());
    EXPECT_EQ(0, plan_only->refcount);
    EXPECT_EQ(HeapLifecycleState::InZct, plan_only->lifecycle_state);
    EXPECT_TRUE(thread->zero_count_table_contains_for_testing(plan_only));
}

TEST_F(WeakReferenceProcessing, FullCacheClearIncludesKeywordAndExpandedEntries)
{
    populate();
    auto &caches = owner.get()->inline_caches;
    caches.expanded_call_caches[0].binding_plan->keyword_names.emplace_back(
        old_value.value());
    ASSERT_EQ(2, old_value.extract()->refcount);
    caches.clear();
    const auto &keyword = caches.keyword_call_caches[0];
    EXPECT_TRUE(keyword.guard_value.is_not_present());
    EXPECT_EQ(nullptr, keyword.function);
    EXPECT_EQ(nullptr, keyword.code_object);
    EXPECT_EQ(nullptr, keyword.validity_cell);
    EXPECT_EQ(nullptr, keyword.keyword_dest_regs);
    EXPECT_EQ(UINT8_MAX, keyword.n_pos_args);
    EXPECT_EQ(nullptr, caches.expanded_call_caches[0].binding_plan);
    EXPECT_EQ(1, old_value.extract()->refcount);
}

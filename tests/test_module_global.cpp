#include "module_global.h"
#include "module_object.h"
#include "shape.h"
#include "test_helpers.h"
#include "thread_state.h"
#include "validity_cell.h"
#include <gtest/gtest.h>

using namespace cl;

TEST(ModuleGlobal, ReadModuleSlotHitIsCacheable)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> module_name =
        context.vm().get_or_create_interned_string_value(L"module");
    TValue<String> global_name =
        context.vm().get_or_create_interned_string_value(L"value");
    ModuleObject *module = context.thread()->make_module_object(module_name);
    ASSERT_TRUE(module->set_own_property(global_name, Value::from_smi(42)));

    ModuleGlobalReadDescriptor descriptor =
        resolve_module_global_read_descriptor(module, global_name);

    ASSERT_TRUE(descriptor.is_found());
    EXPECT_TRUE(descriptor.is_cacheable());
    EXPECT_EQ(ModuleGlobalReadPlanKind::ModuleSlot, descriptor.plan.kind);
    EXPECT_EQ(Value::from_smi(42), descriptor.lookup_value);
    EXPECT_EQ(Value::from_smi(42),
              load_module_global_from_plan(descriptor.plan));
    EXPECT_EQ(module->current_module_globals_validity_cell(),
              descriptor.plan.lookup_validity_cell);
}

TEST(ModuleGlobal, ReadBuiltinsModuleSlotHitIsCacheableAndAttached)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> module_name =
        context.vm().get_or_create_interned_string_value(L"module");
    TValue<String> builtins_name =
        context.vm().get_or_create_interned_string_value(L"builtins");
    TValue<String> global_name =
        context.vm().get_or_create_interned_string_value(L"value");
    ModuleObject *module = context.thread()->make_module_object(module_name);
    ModuleObject *builtins =
        context.thread()->make_module_object(builtins_name);
    ASSERT_TRUE(builtins->set_own_property(global_name, Value::from_smi(7)));
    module->set_builtins_binding(Value::from_oop(builtins));

    ModuleGlobalReadDescriptor descriptor =
        resolve_module_global_read_descriptor(module, global_name);

    ASSERT_TRUE(descriptor.is_found());
    EXPECT_TRUE(descriptor.is_cacheable());
    EXPECT_EQ(ModuleGlobalReadPlanKind::BuiltinsModuleSlot,
              descriptor.plan.kind);
    EXPECT_EQ(Value::from_smi(7), descriptor.lookup_value);
    EXPECT_EQ(Value::from_smi(7),
              load_module_global_from_plan(descriptor.plan));
    EXPECT_EQ(module->current_module_builtins_validity_cell(),
              descriptor.plan.lookup_validity_cell);
    EXPECT_EQ(1u, builtins->attached_module_builtins_validity_cell_count());

    ModuleGlobalReadDescriptor second_descriptor =
        resolve_module_global_read_descriptor(module, global_name);

    EXPECT_TRUE(second_descriptor.is_found());
    EXPECT_EQ(descriptor.plan.lookup_validity_cell,
              second_descriptor.plan.lookup_validity_cell);
    EXPECT_EQ(1u, builtins->attached_module_builtins_validity_cell_count());
}

TEST(ModuleGlobal, MissingBuiltinsBindingUsesVirtualMachineDefault)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> module_name =
        context.vm().get_or_create_interned_string_value(L"module");
    TValue<String> builtins_name =
        context.vm().get_or_create_interned_string_value(L"builtins");
    TValue<String> global_name =
        context.vm().get_or_create_interned_string_value(L"value");
    ModuleObject *module = context.thread()->make_module_object(module_name);
    ModuleObject *builtins =
        context.thread()->make_module_object(builtins_name);
    ASSERT_TRUE(builtins->set_own_property(global_name, Value::from_smi(9)));
    context.vm().set_global_builtins_module(Value::from_oop(builtins));

    ModuleGlobalReadDescriptor descriptor =
        resolve_module_global_read_descriptor(module, global_name);

    ASSERT_TRUE(descriptor.is_found());
    EXPECT_TRUE(descriptor.is_cacheable());
    EXPECT_EQ(ModuleGlobalReadPlanKind::BuiltinsModuleSlot,
              descriptor.plan.kind);
    EXPECT_EQ(Value::from_smi(9),
              load_module_global_from_plan(descriptor.plan));
}

TEST(ModuleGlobal, NonModuleBuiltinsBindingProducesUncacheablePlan)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> module_name =
        context.vm().get_or_create_interned_string_value(L"module");
    TValue<String> global_name =
        context.vm().get_or_create_interned_string_value(L"value");
    ModuleObject *module = context.thread()->make_module_object(module_name);
    module->set_builtins_binding(Value::from_smi(4));

    ModuleGlobalReadDescriptor descriptor =
        resolve_module_global_read_descriptor(module, global_name);

    EXPECT_EQ(ModuleGlobalReadStatus::UncacheableBuiltinsObject,
              descriptor.status);
    EXPECT_FALSE(descriptor.is_cacheable());
    EXPECT_EQ(ModuleGlobalReadPlanKind::UncacheableBuiltinsObject,
              descriptor.plan.kind);
    EXPECT_EQ(Value::from_smi(4), descriptor.plan.builtins_object);
    EXPECT_TRUE(load_module_global_from_plan(descriptor.plan).is_not_present());
}

TEST(ModuleGlobal, MissingNameInModuleBuiltinsIsCacheableMiss)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> module_name =
        context.vm().get_or_create_interned_string_value(L"module");
    TValue<String> builtins_name =
        context.vm().get_or_create_interned_string_value(L"builtins");
    TValue<String> global_name =
        context.vm().get_or_create_interned_string_value(L"value");
    ModuleObject *module = context.thread()->make_module_object(module_name);
    ModuleObject *builtins =
        context.thread()->make_module_object(builtins_name);
    module->set_builtins_binding(Value::from_oop(builtins));

    ModuleGlobalReadDescriptor descriptor =
        resolve_module_global_read_descriptor(module, global_name);

    EXPECT_FALSE(descriptor.is_found());
    EXPECT_EQ(ModuleGlobalReadStatus::NotFound, descriptor.status);
    EXPECT_TRUE(descriptor.is_cacheable());
    EXPECT_EQ(ModuleGlobalReadPlanKind::Missing, descriptor.plan.kind);
    EXPECT_TRUE(load_module_global_from_plan(descriptor.plan).is_not_present());
}

TEST(ModuleGlobal, StoreExistingModuleSlotIsCacheableAndDoesNotInvalidate)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> module_name =
        context.vm().get_or_create_interned_string_value(L"module");
    TValue<String> global_name =
        context.vm().get_or_create_interned_string_value(L"value");
    ModuleObject *module = context.thread()->make_module_object(module_name);
    ASSERT_TRUE(module->set_own_property(global_name, Value::from_smi(1)));

    ModuleGlobalWriteDescriptor descriptor =
        resolve_module_global_write_descriptor(module, global_name);
    ASSERT_TRUE(descriptor.is_found());
    EXPECT_TRUE(descriptor.is_cacheable());
    ValidityCell *cell = descriptor.plan.lookup_validity_cell;
    ASSERT_TRUE(cell->is_valid());

    EXPECT_TRUE(store_module_global_from_plan(module, descriptor.plan,
                                              Value::from_smi(2)));

    EXPECT_TRUE(cell->is_valid());
    EXPECT_EQ(Value::from_smi(2), module->get_own_property(global_name));
}

TEST(ModuleGlobal, StoreMissingModuleSlotAddsUncacheableProperty)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> module_name =
        context.vm().get_or_create_interned_string_value(L"module");
    TValue<String> global_name =
        context.vm().get_or_create_interned_string_value(L"value");
    ModuleObject *module = context.thread()->make_module_object(module_name);

    ModuleGlobalWriteDescriptor descriptor =
        resolve_module_global_write_descriptor(module, global_name);
    ASSERT_TRUE(descriptor.is_found());
    EXPECT_FALSE(descriptor.is_cacheable());
    EXPECT_EQ(ModuleGlobalMutationPlanKind::AddOwnProperty,
              descriptor.plan.kind);

    EXPECT_TRUE(store_module_global_from_plan(module, descriptor.plan,
                                              Value::from_smi(3)));

    EXPECT_EQ(Value::from_smi(3), module->get_own_property(global_name));
}

TEST(ModuleGlobal, StoreBuiltinsBindingIsReadOnly)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> module_name =
        context.vm().get_or_create_interned_string_value(L"module");
    TValue<String> dunder_builtins =
        context.vm().get_or_create_interned_string_value(L"__builtins__");
    ModuleObject *module = context.thread()->make_module_object(module_name);

    ModuleGlobalWriteDescriptor descriptor =
        resolve_module_global_write_descriptor(module, dunder_builtins);

    EXPECT_FALSE(descriptor.is_found());
    EXPECT_EQ(ModuleGlobalWriteStatus::ReadOnly, descriptor.status);
}

TEST(ModuleGlobal, DeleteExistingModuleSlotIsUncacheable)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> module_name =
        context.vm().get_or_create_interned_string_value(L"module");
    TValue<String> global_name =
        context.vm().get_or_create_interned_string_value(L"value");
    ModuleObject *module = context.thread()->make_module_object(module_name);
    ASSERT_TRUE(module->set_own_property(global_name, Value::from_smi(1)));

    ModuleGlobalDeleteDescriptor descriptor =
        resolve_module_global_delete_descriptor(module, global_name);
    ASSERT_TRUE(descriptor.is_found());
    EXPECT_FALSE(descriptor.is_cacheable());
    EXPECT_EQ(ModuleGlobalMutationPlanKind::DeleteOwnProperty,
              descriptor.plan.kind);

    EXPECT_TRUE(delete_module_global_from_plan(module, descriptor.plan));

    EXPECT_TRUE(module->get_own_property(global_name).is_not_present());
}

TEST(ModuleGlobal, DeleteMissingModuleSlotIsNotFound)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> module_name =
        context.vm().get_or_create_interned_string_value(L"module");
    TValue<String> global_name =
        context.vm().get_or_create_interned_string_value(L"value");
    ModuleObject *module = context.thread()->make_module_object(module_name);

    ModuleGlobalDeleteDescriptor descriptor =
        resolve_module_global_delete_descriptor(module, global_name);

    EXPECT_FALSE(descriptor.is_found());
    EXPECT_EQ(ModuleGlobalDeleteStatus::NotFound, descriptor.status);
}

TEST(ModuleGlobal, DeleteBuiltinsBindingIsReadOnly)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> module_name =
        context.vm().get_or_create_interned_string_value(L"module");
    TValue<String> dunder_builtins =
        context.vm().get_or_create_interned_string_value(L"__builtins__");
    ModuleObject *module = context.thread()->make_module_object(module_name);

    ModuleGlobalDeleteDescriptor descriptor =
        resolve_module_global_delete_descriptor(module, dunder_builtins);

    EXPECT_FALSE(descriptor.is_found());
    EXPECT_EQ(ModuleGlobalDeleteStatus::ReadOnly, descriptor.status);
}

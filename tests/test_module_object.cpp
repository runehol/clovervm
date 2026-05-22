#include "module_object.h"
#include "shape.h"
#include "test_helpers.h"
#include "thread_state.h"
#include <gtest/gtest.h>

using namespace cl;

TEST(ModuleObject, ConstructedWithModuleClassAndPredefinedSlots)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> name =
        context.vm().get_or_create_interned_string_value(L"example");
    ModuleObject *module = context.thread()->make_module_object(name);

    EXPECT_EQ(NativeLayoutId::ModuleObject, module->native_layout_id());
    EXPECT_EQ(context.vm().module_class(), module->get_shape()->get_class());
    EXPECT_EQ(name.raw_value(), module->get_name_binding());
    EXPECT_TRUE(module->get_builtins_binding().is_not_present());

    TValue<String> dunder_name =
        context.vm().get_or_create_interned_string_value(L"__name__");
    TValue<String> dunder_builtins =
        context.vm().get_or_create_interned_string_value(L"__builtins__");
    EXPECT_EQ(name.raw_value(), module->get_own_property(dunder_name));
    EXPECT_TRUE(module->get_own_property(dunder_builtins).is_not_present());
    EXPECT_FALSE(module->get_shape()
                     ->resolve_present_property(dunder_builtins)
                     .is_found());
}

TEST(ModuleObject, PredefinedSlotLocationsAreStable)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> name =
        context.vm().get_or_create_interned_string_value(L"example");
    ModuleObject *module =
        context.thread()->make_module_object(name, name.raw_value());

    TValue<String> dunder_name =
        context.vm().get_or_create_interned_string_value(L"__name__");
    TValue<String> dunder_builtins =
        context.vm().get_or_create_interned_string_value(L"__builtins__");

    StorageLocation name_location =
        module->get_shape()->resolve_present_property(dunder_name);
    ASSERT_TRUE(name_location.is_found());
    EXPECT_EQ(StorageKind::Inline, name_location.kind);
    EXPECT_EQ(int32_t(ModuleObject::module_predefined_slot_name),
              name_location.physical_idx);

    StorageLocation builtins_location =
        module->get_shape()->resolve_present_property(dunder_builtins);
    ASSERT_TRUE(builtins_location.is_found());
    EXPECT_EQ(StorageKind::Inline, builtins_location.kind);
    EXPECT_EQ(int32_t(ModuleObject::module_predefined_slot_builtins),
              builtins_location.physical_idx);
}

TEST(ModuleObject, NameBindingAccessorUsesPredefinedSlotAndAllowsAnyValue)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> name =
        context.vm().get_or_create_interned_string_value(L"example");
    ModuleObject *module = context.thread()->make_module_object(name);
    Value replacement = Value::from_smi(4);

    module->set_name_binding(replacement);

    TValue<String> dunder_name =
        context.vm().get_or_create_interned_string_value(L"__name__");
    EXPECT_EQ(replacement, module->get_name_binding());
    EXPECT_EQ(replacement, module->get_own_property(dunder_name));
}

TEST(ModuleObject, BuiltinsBindingAccessorUsesPredefinedSlot)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> name =
        context.vm().get_or_create_interned_string_value(L"example");
    Value builtins = name.raw_value();
    ModuleObject *module = context.thread()->make_module_object(name, builtins);

    TValue<String> dunder_builtins =
        context.vm().get_or_create_interned_string_value(L"__builtins__");
    EXPECT_EQ(builtins, module->get_builtins_binding());
    EXPECT_EQ(builtins, module->get_own_property(dunder_builtins));

    module->delete_builtins_binding();
    EXPECT_TRUE(module->get_builtins_binding().is_not_present());
    EXPECT_TRUE(module->get_own_property(dunder_builtins).is_not_present());
}

TEST(ModuleObject, BuiltinsBindingRejectsOrdinaryMutation)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> name =
        context.vm().get_or_create_interned_string_value(L"example");
    Value builtins = name.raw_value();
    ModuleObject *module = context.thread()->make_module_object(name, builtins);

    TValue<String> dunder_builtins =
        context.vm().get_or_create_interned_string_value(L"__builtins__");
    Shape *before_shape = module->get_shape();

    EXPECT_FALSE(module->set_own_property(dunder_builtins, Value::from_smi(4)));
    EXPECT_FALSE(module->delete_own_property(dunder_builtins));
    EXPECT_EQ(before_shape, module->get_shape());
    EXPECT_EQ(builtins, module->get_builtins_binding());
    EXPECT_EQ(builtins, module->get_own_property(dunder_builtins));
}

TEST(ModuleObject, OrdinaryGlobalsStartAfterPredefinedSlots)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> name =
        context.vm().get_or_create_interned_string_value(L"example");
    TValue<String> global_name =
        context.vm().get_or_create_interned_string_value(L"value");
    ModuleObject *module = context.thread()->make_module_object(name);

    ASSERT_TRUE(module->set_own_property(global_name, Value::from_smi(42)));
    EXPECT_EQ(Value::from_smi(42), module->get_own_property(global_name));

    StorageLocation location =
        module->get_shape()->resolve_present_property(global_name);
    ASSERT_TRUE(location.is_found());
    EXPECT_EQ(StorageKind::Inline, location.kind);
    EXPECT_EQ(int32_t(ModuleObject::module_predefined_slot_count),
              location.physical_idx);
}

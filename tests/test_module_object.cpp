#include "build_config.h"
#include "dict.h"
#include "list.h"
#include "module_object.h"
#include "shape.h"
#include "str.h"
#include "test_helpers.h"
#include "thread_state.h"
#include "validity_cell.h"
#include <cassert>
#include <gtest/gtest.h>

using namespace cl;

static std::wstring value_as_wstring(Value value)
{
    return string_as_wchar_t(TValue<String>::from_value_assumed(value));
}

static ValidityCell *module_builtins_lookup_cell(ModuleObject *module)
{
    ModuleBuiltinsLookup lookup = module->get_module_builtins_lookup();
    assert(lookup.is_module());
    return lookup.lookup_validity_cell;
}

TEST(ModuleObject, ConstructedWithModuleClassAndPredefinedSlots)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> name =
        context.vm().get_or_create_interned_string_value(L"example");
    ModuleObject *module = context.thread()->make_module_object(
        name, context.vm().global_builtins_module().raw_value());

    EXPECT_EQ(NativeLayoutId::ModuleObject, module->native_layout_id());
    EXPECT_EQ(context.vm().module_class(), module->get_shape()->get_class());
    EXPECT_TRUE(module->get_shape()->has_flag(ShapeFlag::IsModuleObject));
    EXPECT_EQ(name.raw_value(), module->get_name_binding());
    EXPECT_EQ(context.vm().global_builtins_module().raw_value(),
              module->get_builtins_binding());

    TValue<String> dunder_name =
        context.vm().get_or_create_interned_string_value(L"__name__");
    TValue<String> dunder_builtins =
        context.vm().get_or_create_interned_string_value(L"__builtins__");
    EXPECT_EQ(name.raw_value(), module->get_own_property(dunder_name));
    EXPECT_EQ(context.vm().global_builtins_module().raw_value(),
              module->get_own_property(dunder_builtins));
    EXPECT_TRUE(module->get_shape()
                    ->resolve_present_property(dunder_builtins)
                    .is_found());
}

TEST(ModuleObject, SysModuleIsBootstrappedAsImmortalModule)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    ModuleObject *sys = context.vm().sys_module().extract();
    EXPECT_EQ(NativeLayoutId::ModuleObject, sys->native_layout_id());
    EXPECT_EQ(-1, sys->refcount);
    EXPECT_EQ(L"sys", value_as_wstring(sys->get_name_binding()));

    TValue<String> dunder_builtins =
        context.vm().get_or_create_interned_string_value(L"__builtins__");
    EXPECT_EQ(context.vm().global_builtins_module().raw_value(),
              sys->get_own_property(dunder_builtins));
}

TEST(ModuleObject, SysModulesIsCanonicalImportedModulesDict)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    ModuleObject *sys = context.vm().sys_module().extract();
    Dict *modules = context.vm().imported_modules().extract();
    EXPECT_EQ(-1, modules->refcount);

    TValue<String> modules_name =
        context.vm().get_or_create_interned_string_value(L"modules");
    EXPECT_EQ(Value::from_oop(modules), sys->get_own_property(modules_name));

    TValue<String> sys_name =
        context.vm().get_or_create_interned_string_value(L"sys");
    TValue<String> builtins_name =
        context.vm().get_or_create_interned_string_value(L"builtins");
    EXPECT_EQ(Value::from_oop(sys), modules->get_item(sys_name.raw_value()));
    EXPECT_EQ(context.vm().global_builtins_module().raw_value(),
              modules->get_item(builtins_name.raw_value()));
}

TEST(ModuleObject, SysPathStartsWithCurrentDirectoryAndStdlib)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    ModuleObject *sys = context.vm().sys_module().extract();
    TValue<String> path_name =
        context.vm().get_or_create_interned_string_value(L"path");
    Value path_value = sys->get_own_property(path_name);
    ASSERT_TRUE(can_convert_to<List>(path_value));
    List *path = path_value.get_ptr<List>();
    EXPECT_EQ(-1, path->refcount);
    ASSERT_EQ(2u, path->size());
    EXPECT_EQ(L".", value_as_wstring(path->item_unchecked(0)));
    EXPECT_EQ(CL_STDLIB_DIR, value_as_wstring(path->item_unchecked(1)));
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
    ModuleObject *module = context.thread()->make_module_object(
        name, context.vm().global_builtins_module().raw_value());
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
    ModuleObject *module = context.thread()->make_module_object(
        name, context.vm().global_builtins_module().raw_value());

    TValue<String> dunder_builtins =
        context.vm().get_or_create_interned_string_value(L"__builtins__");
    EXPECT_TRUE(module->get_shape()
                    ->resolve_present_property(dunder_builtins)
                    .is_found());

    module->set_builtins_binding(builtins);

    EXPECT_EQ(builtins, module->get_builtins_binding());
    EXPECT_EQ(builtins, module->get_own_property(dunder_builtins));
    EXPECT_TRUE(module->get_shape()
                    ->resolve_present_property(dunder_builtins)
                    .is_found());

    module->delete_builtins_binding();
    EXPECT_TRUE(module->get_builtins_binding().is_not_present());
    EXPECT_TRUE(module->get_own_property(dunder_builtins).is_not_present());
    EXPECT_FALSE(module->get_shape()
                     ->resolve_present_property(dunder_builtins)
                     .is_found());
}

TEST(ModuleObject, BuiltinsBindingOrdinaryMutationInvalidatesBindingCell)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> name =
        context.vm().get_or_create_interned_string_value(L"example");
    ModuleObject *module = context.thread()->make_module_object(
        name, context.vm().global_builtins_module().raw_value());
    module->set_builtins_binding(Value::from_oop(module));

    TValue<String> dunder_builtins =
        context.vm().get_or_create_interned_string_value(L"__builtins__");
    ValidityCell *globals_cell =
        module->get_or_create_module_globals_validity_cell();
    ValidityCell *builtins_cell = module_builtins_lookup_cell(module);

    EXPECT_TRUE(module->set_own_property(dunder_builtins, Value::from_smi(4)));
    EXPECT_TRUE(globals_cell->is_valid());
    EXPECT_FALSE(builtins_cell->is_valid());
    EXPECT_EQ(Value::from_smi(4), module->get_builtins_binding());
    EXPECT_EQ(Value::from_smi(4), module->get_own_property(dunder_builtins));

    module->set_builtins_binding(Value::from_oop(module));
    builtins_cell = module_builtins_lookup_cell(module);
    EXPECT_TRUE(module->delete_own_property(dunder_builtins));
    EXPECT_FALSE(globals_cell->is_valid());
    EXPECT_FALSE(builtins_cell->is_valid());
    EXPECT_TRUE(module->get_builtins_binding().is_not_present());
    EXPECT_TRUE(module->get_own_property(dunder_builtins).is_not_present());
}

TEST(ModuleObject, OrdinaryGlobalsStartAfterPredefinedSlots)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> name =
        context.vm().get_or_create_interned_string_value(L"example");
    TValue<String> global_name =
        context.vm().get_or_create_interned_string_value(L"value");
    ModuleObject *module = context.thread()->make_module_object(
        name, context.vm().global_builtins_module().raw_value());

    ASSERT_TRUE(module->set_own_property(global_name, Value::from_smi(42)));
    EXPECT_EQ(Value::from_smi(42), module->get_own_property(global_name));

    StorageLocation location =
        module->get_shape()->resolve_present_property(global_name);
    ASSERT_TRUE(location.is_found());
    EXPECT_EQ(StorageKind::Inline, location.kind);
    EXPECT_EQ(int32_t(ModuleObject::module_predefined_slot_count),
              location.physical_idx);
}

TEST(ModuleObject, ShapeChangeInvalidatesLookupCells)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> name =
        context.vm().get_or_create_interned_string_value(L"example");
    TValue<String> global_name =
        context.vm().get_or_create_interned_string_value(L"value");
    ModuleObject *module = context.thread()->make_module_object(
        name, context.vm().global_builtins_module().raw_value());
    module->set_builtins_binding(Value::from_oop(module));

    ValidityCell *globals_cell =
        module->get_or_create_module_globals_validity_cell();
    ValidityCell *builtins_cell = module_builtins_lookup_cell(module);

    ASSERT_TRUE(module->set_own_property(global_name, Value::from_smi(42)));

    EXPECT_FALSE(globals_cell->is_valid());
    EXPECT_FALSE(builtins_cell->is_valid());
    EXPECT_EQ(nullptr, module->current_module_globals_validity_cell());
    EXPECT_EQ(nullptr, module->current_module_builtins_validity_cell());
}

TEST(ModuleObject, DeleteShapeChangeInvalidatesLookupCells)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> name =
        context.vm().get_or_create_interned_string_value(L"example");
    TValue<String> global_name =
        context.vm().get_or_create_interned_string_value(L"value");
    ModuleObject *module = context.thread()->make_module_object(
        name, context.vm().global_builtins_module().raw_value());
    module->set_builtins_binding(Value::from_oop(module));

    ASSERT_TRUE(module->set_own_property(global_name, Value::from_smi(42)));
    ValidityCell *globals_cell =
        module->get_or_create_module_globals_validity_cell();
    ValidityCell *builtins_cell = module_builtins_lookup_cell(module);

    ASSERT_TRUE(module->delete_own_property(global_name));

    EXPECT_FALSE(globals_cell->is_valid());
    EXPECT_FALSE(builtins_cell->is_valid());
    EXPECT_EQ(nullptr, module->current_module_globals_validity_cell());
    EXPECT_EQ(nullptr, module->current_module_builtins_validity_cell());
}

TEST(ModuleObject, ValidityCellsAreCreatedLazilyAndReusedWhileValid)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> name =
        context.vm().get_or_create_interned_string_value(L"example");
    ModuleObject *module = context.thread()->make_module_object(
        name, context.vm().global_builtins_module().raw_value());
    module->set_builtins_binding(Value::from_oop(module));

    EXPECT_EQ(nullptr, module->current_module_globals_validity_cell());
    EXPECT_EQ(nullptr, module->current_module_builtins_validity_cell());

    ValidityCell *globals_cell =
        module->get_or_create_module_globals_validity_cell();
    ValidityCell *builtins_cell = module_builtins_lookup_cell(module);
    EXPECT_TRUE(globals_cell->is_valid());
    EXPECT_TRUE(builtins_cell->is_valid());
    EXPECT_EQ(globals_cell,
              module->get_or_create_module_globals_validity_cell());
    EXPECT_EQ(builtins_cell, module_builtins_lookup_cell(module));
}

TEST(ModuleObject, InvalidatingModuleLookupCellsKillsOwnedCells)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> name =
        context.vm().get_or_create_interned_string_value(L"example");
    ModuleObject *module = context.thread()->make_module_object(
        name, context.vm().global_builtins_module().raw_value());
    module->set_builtins_binding(Value::from_oop(module));

    ValidityCell *globals_cell =
        module->get_or_create_module_globals_validity_cell();
    ValidityCell *builtins_cell = module_builtins_lookup_cell(module);

    module->invalidate_module_lookup_validity_cells();

    EXPECT_FALSE(globals_cell->is_valid());
    EXPECT_FALSE(builtins_cell->is_valid());
    EXPECT_EQ(nullptr, module->current_module_globals_validity_cell());
    EXPECT_EQ(nullptr, module->current_module_builtins_validity_cell());

    EXPECT_TRUE(
        module->get_or_create_module_globals_validity_cell()->is_valid());
    EXPECT_TRUE(module_builtins_lookup_cell(module)->is_valid());
}

TEST(ModuleObject, InvalidatingModuleLookupCellsKillsAttachedCells)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> provider_name =
        context.vm().get_or_create_interned_string_value(L"provider");
    TValue<String> consumer_name =
        context.vm().get_or_create_interned_string_value(L"consumer");
    ModuleObject *provider = context.thread()->make_module_object(
        provider_name, context.vm().global_builtins_module().raw_value());
    ModuleObject *consumer = context.thread()->make_module_object(
        consumer_name, context.vm().global_builtins_module().raw_value());
    consumer->set_builtins_binding(Value::from_oop(provider));

    ValidityCell *consumer_builtins_cell =
        module_builtins_lookup_cell(consumer);
    EXPECT_EQ(1u, provider->attached_dependent_lookup_validity_cell_count());

    provider->invalidate_module_lookup_validity_cells();

    EXPECT_FALSE(consumer_builtins_cell->is_valid());
    EXPECT_EQ(0u, provider->attached_dependent_lookup_validity_cell_count());
}

TEST(ModuleObject, BuiltinsBindingAssignmentInvalidatesOnlyBindingCell)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> provider_name =
        context.vm().get_or_create_interned_string_value(L"provider");
    TValue<String> consumer_name =
        context.vm().get_or_create_interned_string_value(L"consumer");
    ModuleObject *provider = context.thread()->make_module_object(
        provider_name, context.vm().global_builtins_module().raw_value());
    ModuleObject *consumer = context.thread()->make_module_object(
        consumer_name, context.vm().global_builtins_module().raw_value());
    provider->set_builtins_binding(Value::from_oop(provider));
    consumer->set_builtins_binding(Value::from_oop(provider));

    ValidityCell *provider_globals_cell =
        provider->get_or_create_module_globals_validity_cell();
    ValidityCell *provider_builtins_cell =
        module_builtins_lookup_cell(provider);
    ValidityCell *consumer_builtins_cell =
        module_builtins_lookup_cell(consumer);

    provider->set_builtins_binding(provider_name.raw_value());

    EXPECT_TRUE(provider_globals_cell->is_valid());
    EXPECT_FALSE(provider_builtins_cell->is_valid());
    EXPECT_TRUE(consumer_builtins_cell->is_valid());
    EXPECT_EQ(2u, provider->attached_dependent_lookup_validity_cell_count());
}

TEST(ModuleObject, AttachDependentLookupValidityCellReusesInvalidEntries)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> provider_name =
        context.vm().get_or_create_interned_string_value(L"provider");
    TValue<String> first_name =
        context.vm().get_or_create_interned_string_value(L"first");
    TValue<String> second_name =
        context.vm().get_or_create_interned_string_value(L"second");
    ModuleObject *provider = context.thread()->make_module_object(
        provider_name, context.vm().global_builtins_module().raw_value());
    ModuleObject *first = context.thread()->make_module_object(
        first_name, context.vm().global_builtins_module().raw_value());
    ModuleObject *second = context.thread()->make_module_object(
        second_name, context.vm().global_builtins_module().raw_value());
    first->set_builtins_binding(Value::from_oop(provider));
    second->set_builtins_binding(Value::from_oop(provider));

    ValidityCell *first_cell = module_builtins_lookup_cell(first);
    EXPECT_EQ(1u, provider->attached_dependent_lookup_validity_cell_count());

    first->invalidate_module_lookup_validity_cells();
    EXPECT_FALSE(first_cell->is_valid());

    ValidityCell *second_cell = module_builtins_lookup_cell(second);

    EXPECT_EQ(1u, provider->attached_dependent_lookup_validity_cell_count());
    provider->invalidate_module_lookup_validity_cells();
    EXPECT_FALSE(first_cell->is_valid());
    EXPECT_FALSE(second_cell->is_valid());
}

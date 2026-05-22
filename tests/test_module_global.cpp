#include "class_object.h"
#include "code_object_builder.h"
#include "exception_object.h"
#include "module_global.h"
#include "module_object.h"
#include "shape.h"
#include "str.h"
#include "test_helpers.h"
#include "thread_state.h"
#include "validity_cell.h"
#include <cstdint>
#include <gtest/gtest.h>

using namespace cl;

static std::wstring module_global_test_string_to_wstring(TValue<String> string)
{
    String *str = string.extract();
    return std::wstring(str->data, size_t(str->count.extract()));
}

static std::wstring module_global_pending_python_error(ThreadState *thread)
{
    EXPECT_EQ(PendingExceptionKind::Object, thread->pending_exception_kind());
    TValue<Exception> exception = thread->pending_exception_object();
    std::wstring result = module_global_test_string_to_wstring(
        exception.extract()->get_shape()->get_class()->get_name());
    std::wstring message = module_global_test_string_to_wstring(
        exception.extract()->message.value());
    if(!message.empty())
    {
        result += L": ";
        result += message;
    }
    return result;
}

template <typename EmitBody>
static CodeObject *
make_module_global_test_code(test::VmTestContext &context, ModuleObject *module,
                             const wchar_t *code_name, EmitBody emit_body)
{
    TValue<String> name =
        context.vm().get_or_create_interned_string_value(code_name);
    CodeObjectBuilder builder(&context.vm(), nullptr,
                              TValue<ModuleObject>::from_oop(module), nullptr,
                              name);
    emit_body(builder);
    return builder.finalize();
}

static uint8_t allocate_name_constant(CodeObjectBuilder &builder,
                                      TValue<String> name)
{
    uint32_t name_idx = builder.allocate_constant(name);
    assert(name_idx <= UINT8_MAX);
    return uint8_t(name_idx);
}

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
    EXPECT_EQ(ModuleGlobalReadPlanKind::Slot, descriptor.plan.kind);
    EXPECT_EQ(module, descriptor.plan.storage_owner);
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
    EXPECT_EQ(ModuleGlobalReadPlanKind::Slot, descriptor.plan.kind);
    EXPECT_EQ(builtins, descriptor.plan.storage_owner);
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
    context.vm().set_global_builtins_module(builtins);

    ModuleGlobalReadDescriptor descriptor =
        resolve_module_global_read_descriptor(module, global_name);

    ASSERT_TRUE(descriptor.is_found());
    EXPECT_TRUE(descriptor.is_cacheable());
    EXPECT_EQ(ModuleGlobalReadPlanKind::Slot, descriptor.plan.kind);
    EXPECT_EQ(builtins, descriptor.plan.storage_owner);
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

TEST(ModuleGlobal, MissingNameInModuleBuiltinsIsUncacheableMiss)
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
    EXPECT_FALSE(descriptor.is_cacheable());
    EXPECT_EQ(ModuleGlobalReadPlanKind::Missing, descriptor.plan.kind);
    EXPECT_EQ(nullptr, descriptor.plan.lookup_validity_cell);
    EXPECT_TRUE(load_module_global_from_plan(descriptor.plan).is_not_present());
}

TEST(ModuleGlobal, DeletingModuleBindingRevealsBuiltinWithoutMutatingModule)
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
    ASSERT_TRUE(module->set_own_property(global_name, Value::from_smi(1)));
    ASSERT_TRUE(builtins->set_own_property(global_name, Value::from_smi(2)));
    module->set_builtins_binding(Value::from_oop(builtins));

    ASSERT_TRUE(delete_module_global(module, global_name));

    ModuleGlobalReadDescriptor descriptor =
        resolve_module_global_read_descriptor(module, global_name);

    ASSERT_TRUE(descriptor.is_found());
    EXPECT_TRUE(descriptor.is_cacheable());
    EXPECT_EQ(ModuleGlobalReadPlanKind::Slot, descriptor.plan.kind);
    EXPECT_EQ(builtins, descriptor.plan.storage_owner);
    EXPECT_EQ(Value::from_smi(2), descriptor.lookup_value);
    EXPECT_EQ(Value::from_smi(2),
              load_module_global_from_plan(descriptor.plan));
    EXPECT_TRUE(module->get_own_property(global_name).is_not_present());
    EXPECT_EQ(Value::from_smi(2), builtins->get_own_property(global_name));
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

TEST(ModuleGlobalBytecode, LoadModuleGlobalReadsModuleSlot)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> module_name =
        context.vm().get_or_create_interned_string_value(L"module");
    TValue<String> global_name =
        context.vm().get_or_create_interned_string_value(L"value");
    ModuleObject *module = context.thread()->make_module_object(module_name);
    ASSERT_TRUE(module->set_own_property(global_name, Value::from_smi(42)));

    CodeObject *code = make_module_global_test_code(
        context, module, L"<module-global-load-test>",
        [&](CodeObjectBuilder &builder) {
            uint8_t name_idx = allocate_name_constant(builder, global_name);
            builder.emit_lda_module_global(0, name_idx);
            builder.emit_return(0);
        });

    EXPECT_EQ(Value::from_smi(42),
              context.thread()->run_clovervm_code_object(code));
}

TEST(ModuleGlobalBytecode, LoadModuleGlobalReadsBuiltinsAfterModuleMiss)
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

    CodeObject *code = make_module_global_test_code(
        context, module, L"<module-global-builtin-test>",
        [&](CodeObjectBuilder &builder) {
            uint8_t name_idx = allocate_name_constant(builder, global_name);
            builder.emit_lda_module_global(0, name_idx);
            builder.emit_return(0);
        });

    EXPECT_EQ(Value::from_smi(7),
              context.thread()->run_clovervm_code_object(code));
}

TEST(ModuleGlobalBytecode, LoadModuleGlobalMissingNameRaisesNameError)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> module_name =
        context.vm().get_or_create_interned_string_value(L"module");
    TValue<String> global_name =
        context.vm().get_or_create_interned_string_value(L"not_present");
    ModuleObject *module = context.thread()->make_module_object(module_name);

    CodeObject *code = make_module_global_test_code(
        context, module, L"<module-global-missing-load-test>",
        [&](CodeObjectBuilder &builder) {
            uint8_t name_idx = allocate_name_constant(builder, global_name);
            builder.emit_lda_module_global(0, name_idx);
            builder.emit_return(0);
        });

    Value result = context.thread()->run_clovervm_code_object(code);

    EXPECT_TRUE(result.is_exception_marker());
    EXPECT_STREQ(L"NameError: name 'not_present' is not defined",
                 module_global_pending_python_error(context.thread()).c_str());
    context.thread()->clear_pending_exception();
}

TEST(ModuleGlobalBytecode, StoreModuleGlobalWritesModuleSlot)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> module_name =
        context.vm().get_or_create_interned_string_value(L"module");
    TValue<String> global_name =
        context.vm().get_or_create_interned_string_value(L"value");
    ModuleObject *module = context.thread()->make_module_object(module_name);

    CodeObject *code = make_module_global_test_code(
        context, module, L"<module-global-store-test>",
        [&](CodeObjectBuilder &builder) {
            uint8_t name_idx = allocate_name_constant(builder, global_name);
            builder.emit_lda_smi(0, 11);
            builder.emit_sta_module_global(0, name_idx);
            builder.emit_lda_module_global(0, name_idx);
            builder.emit_return(0);
        });

    EXPECT_EQ(Value::from_smi(11),
              context.thread()->run_clovervm_code_object(code));
    EXPECT_EQ(Value::from_smi(11), module->get_own_property(global_name));
}

TEST(ModuleGlobalBytecode, DeleteModuleGlobalDeletesModuleSlot)
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
    ASSERT_TRUE(module->set_own_property(global_name, Value::from_smi(1)));
    ASSERT_TRUE(builtins->set_own_property(global_name, Value::from_smi(2)));
    module->set_builtins_binding(Value::from_oop(builtins));

    CodeObject *code = make_module_global_test_code(
        context, module, L"<module-global-delete-test>",
        [&](CodeObjectBuilder &builder) {
            uint8_t name_idx = allocate_name_constant(builder, global_name);
            builder.emit_del_module_global(0, name_idx);
            builder.emit_lda_module_global(0, name_idx);
            builder.emit_return(0);
        });

    EXPECT_EQ(Value::from_smi(2),
              context.thread()->run_clovervm_code_object(code));
    EXPECT_TRUE(module->get_own_property(global_name).is_not_present());
    EXPECT_EQ(Value::from_smi(2), builtins->get_own_property(global_name));
}

TEST(ModuleGlobalBytecode, DeleteModuleGlobalMissingNameRaisesNameError)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> module_name =
        context.vm().get_or_create_interned_string_value(L"module");
    TValue<String> global_name =
        context.vm().get_or_create_interned_string_value(L"not_present");
    ModuleObject *module = context.thread()->make_module_object(module_name);

    CodeObject *code = make_module_global_test_code(
        context, module, L"<module-global-missing-delete-test>",
        [&](CodeObjectBuilder &builder) {
            uint8_t name_idx = allocate_name_constant(builder, global_name);
            builder.emit_del_module_global(0, name_idx);
            builder.emit_return(0);
        });

    Value result = context.thread()->run_clovervm_code_object(code);

    EXPECT_TRUE(result.is_exception_marker());
    EXPECT_STREQ(L"NameError: name 'not_present' is not defined",
                 module_global_pending_python_error(context.thread()).c_str());
    context.thread()->clear_pending_exception();
}

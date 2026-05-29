#include "class_object.h"
#include "code_object_builder.h"
#include "exception_object.h"
#include "exception_propagation.h"
#include "module_object.h"
#include "test_helpers.h"
#include "thread_state.h"
#include "virtual_machine.h"
#include <gtest/gtest.h>

using namespace cl;

namespace
{
    class MoveOnlyForTryTest
    {
    public:
        explicit MoveOnlyForTryTest(int _value) : value(_value) {}
        MoveOnlyForTryTest(const MoveOnlyForTryTest &) = delete;
        MoveOnlyForTryTest &operator=(const MoveOnlyForTryTest &) = delete;
        MoveOnlyForTryTest(MoveOnlyForTryTest &&other) noexcept
            : value(other.value)
        {
            other.value = -1;
        }
        MoveOnlyForTryTest &operator=(MoveOnlyForTryTest &&) = delete;

        int value;
    };

    Expected<MoveOnlyForTryTest> make_move_only_for_try_test()
    {
        return Expected<MoveOnlyForTryTest>::ok(MoveOnlyForTryTest(42));
    }

    Expected<int> unwrap_move_only_for_try_test()
    {
        MoveOnlyForTryTest result = CL_TRY(make_move_only_for_try_test());
        return Expected<int>::ok(result.value);
    }
}  // namespace

static Value propagate_success_for_test(Value value)
{
    CL_PROPAGATE_EXCEPTION(value);
    return Value::from_smi(42);
}

static Value propagate_expression_once_for_test(int &n_evaluations, Value value)
{
    CL_PROPAGATE_EXCEPTION((++n_evaluations, value));
    return Value::from_smi(42);
}

TEST(ExceptionPropagation, ContinuesForOrdinaryValues)
{
    EXPECT_EQ(Value::from_smi(42),
              propagate_success_for_test(Value::from_smi(1)));
}

TEST(ExceptionPropagation, ReturnsExceptionMarker)
{
    EXPECT_TRUE(propagate_success_for_test(Value::exception_marker())
                    .is_exception_marker());
}

TEST(ExceptionPropagation, EvaluatesExpressionOnce)
{
    int n_evaluations = 0;

    EXPECT_TRUE(propagate_expression_once_for_test(n_evaluations,
                                                   Value::exception_marker())
                    .is_exception_marker());
    EXPECT_EQ(1, n_evaluations);
}

TEST(ExceptionPropagation, TryMovesNoncopyableExpectedPayload)
{
    Expected<int> result = unwrap_move_only_for_try_test();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(42, result.value());
}

static CodeObject *
make_lda_active_exception_handler_code(test::VmTestContext &test_context,
                                       Value raised)
{
    TValue<String> name = test_context.vm().get_or_create_interned_string_value(
        L"<active-exception-test>");
    CodeObjectBuilder builder(
        &test_context.vm(), nullptr,
        TValue<ModuleObject>::from_oop(test_context.make_test_module_object(
            name, test_context.vm().global_builtins_module().raw_value())),
        nullptr, name);
    uint32_t constant_idx = builder.allocate_constant(raised).value();
    JumpTarget handler(&builder);

    {
        ExceptionTableRangeBuilder range(&builder, handler);
        builder.emit_lda_constant(0, uint8_t(constant_idx)).value();
        builder.emit_raise_unwind(0).value();
        range.close();
    }

    handler.resolve().value();
    builder.emit_lda_active_exception(0).value();
    builder.emit_return(0).value();
    return builder.finalize().value();
}

static CodeObject *
make_clear_active_exception_handler_code(test::VmTestContext &test_context,
                                         Value raised)
{
    TValue<String> name = test_context.vm().get_or_create_interned_string_value(
        L"<clear-active-exception-test>");
    CodeObjectBuilder builder(
        &test_context.vm(), nullptr,
        TValue<ModuleObject>::from_oop(test_context.make_test_module_object(
            name, test_context.vm().global_builtins_module().raw_value())),
        nullptr, name);
    uint32_t constant_idx = builder.allocate_constant(raised).value();
    JumpTarget handler(&builder);

    {
        ExceptionTableRangeBuilder range(&builder, handler);
        builder.emit_lda_constant(0, uint8_t(constant_idx)).value();
        builder.emit_raise_unwind(0).value();
        range.close();
    }

    handler.resolve().value();
    builder.emit_clear_active_exception(0).value();
    builder.emit_lda_smi(0, 42).value();
    builder.emit_return(0).value();
    return builder.finalize().value();
}

static CodeObject *
make_drain_active_exception_handler_code(test::VmTestContext &test_context,
                                         Value raised)
{
    TValue<String> name = test_context.vm().get_or_create_interned_string_value(
        L"<drain-active-exception-test>");
    CodeObjectBuilder builder(
        &test_context.vm(), nullptr,
        TValue<ModuleObject>::from_oop(test_context.make_test_module_object(
            name, test_context.vm().global_builtins_module().raw_value())),
        nullptr, name);
    uint32_t constant_idx = builder.allocate_constant(raised).value();
    JumpTarget handler(&builder);

    {
        ExceptionTableRangeBuilder range(&builder, handler);
        builder.emit_lda_constant(0, uint8_t(constant_idx)).value();
        builder.emit_raise_unwind(0).value();
        range.close();
    }

    handler.resolve().value();
    {
        CodeObjectBuilder::TemporaryReg saved_exception(builder);
        builder.emit_drain_active_exception_into(0, saved_exception).value();
        builder.emit_ldar(0, saved_exception).value();
    }
    builder.emit_return(0).value();
    return builder.finalize().value();
}

static CodeObject *
make_reraise_active_exception_handler_code(test::VmTestContext &test_context,
                                           Value raised)
{
    TValue<String> name = test_context.vm().get_or_create_interned_string_value(
        L"<reraise-active-exception-test>");
    CodeObjectBuilder builder(
        &test_context.vm(), nullptr,
        TValue<ModuleObject>::from_oop(test_context.make_test_module_object(
            name, test_context.vm().global_builtins_module().raw_value())),
        nullptr, name);
    uint32_t constant_idx = builder.allocate_constant(raised).value();
    JumpTarget inner_handler(&builder);
    JumpTarget outer_handler(&builder);

    {
        ExceptionTableRangeBuilder outer_range(&builder, outer_handler);
        {
            ExceptionTableRangeBuilder inner_range(&builder, inner_handler);
            builder.emit_lda_constant(0, uint8_t(constant_idx)).value();
            builder.emit_raise_unwind(0).value();
            inner_range.close();
        }

        inner_handler.resolve().value();
        builder.emit_reraise_active_exception(0).value();
        outer_range.close();
    }

    outer_handler.resolve().value();
    builder.emit_clear_active_exception(0).value();
    builder.emit_lda_smi(0, 99).value();
    builder.emit_return(0).value();
    return builder.finalize().value();
}

static CodeObject *
make_lda_active_exception_code(test::VmTestContext &test_context)
{
    TValue<String> name = test_context.vm().get_or_create_interned_string_value(
        L"<lda-active-exception-test>");
    CodeObjectBuilder builder(
        &test_context.vm(), nullptr,
        TValue<ModuleObject>::from_oop(test_context.make_test_module_object(
            name, test_context.vm().global_builtins_module().raw_value())),
        nullptr, name);
    builder.emit_lda_active_exception(0).value();
    builder.emit_return(0).value();
    return builder.finalize().value();
}

static CodeObject *
make_active_exception_is_instance_code(test::VmTestContext &test_context,
                                       Value handler_class)
{
    TValue<String> name = test_context.vm().get_or_create_interned_string_value(
        L"<active-exception-is-instance-test>");
    CodeObjectBuilder builder(
        &test_context.vm(), nullptr,
        TValue<ModuleObject>::from_oop(test_context.make_test_module_object(
            name, test_context.vm().global_builtins_module().raw_value())),
        nullptr, name);
    uint32_t constant_idx = builder.allocate_constant(handler_class).value();
    builder.emit_lda_constant(0, uint8_t(constant_idx)).value();
    builder.emit_active_exception_is_instance(0).value();
    builder.emit_return(0).value();
    return builder.finalize().value();
}

TEST(ExceptionHandling, lda_active_exception_reads_pending_exception_object)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    Value exception_class = Value::from_oop(
        test_context.thread()->class_for_builtin_name(L"Exception"));
    CodeObject *code_obj =
        make_lda_active_exception_handler_code(test_context, exception_class);

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    ASSERT_TRUE(can_convert_to<ExceptionObject>(actual));
    EXPECT_TRUE(test_context.thread()->has_pending_exception());
    test_context.thread()->clear_pending_exception();
}

TEST(ExceptionHandling,
     lda_active_exception_materializes_compact_stop_iteration)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = make_lda_active_exception_code(test_context);

    (void)test_context.thread()->set_pending_stop_iteration_value(
        Value::from_smi(7));
    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);

    Expected<TValue<StopIterationObject>> maybe_exception =
        TValue<StopIterationObject>::from_value_checked(actual);
    ASSERT_TRUE(maybe_exception.has_value());
    TValue<StopIterationObject> exception = maybe_exception.value();
    EXPECT_EQ(Value::from_smi(7), exception.extract()->value);
    EXPECT_TRUE(test_context.thread()->has_pending_exception());
    test_context.thread()->clear_pending_exception();
}

TEST(ExceptionHandling, clear_active_exception_swallows_pending_exception)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    Value exception_class = Value::from_oop(
        test_context.thread()->class_for_builtin_name(L"Exception"));
    CodeObject *code_obj =
        make_clear_active_exception_handler_code(test_context, exception_class);

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(42), actual);
    EXPECT_FALSE(test_context.thread()->has_pending_exception());
}

TEST(ExceptionHandling,
     drain_active_exception_into_stores_object_and_clears_pending_exception)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    Value exception_class = Value::from_oop(
        test_context.thread()->class_for_builtin_name(L"Exception"));
    CodeObject *code_obj =
        make_drain_active_exception_handler_code(test_context, exception_class);

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    ASSERT_TRUE(can_convert_to<ExceptionObject>(actual));
    EXPECT_FALSE(test_context.thread()->has_pending_exception());
}

TEST(ExceptionHandling,
     active_exception_is_instance_does_not_materialize_stop_iteration)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    Value stop_iteration_class = Value::from_oop(
        test_context.thread()->class_for_builtin_name(L"StopIteration"));
    CodeObject *code_obj = make_active_exception_is_instance_code(
        test_context, stop_iteration_class);

    (void)test_context.thread()->set_pending_stop_iteration_value(
        Value::from_smi(7));
    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);

    EXPECT_EQ(Value::True(), actual);
    EXPECT_EQ(PendingExceptionKind::StopIteration,
              test_context.thread()->pending_exception_kind());
    test_context.thread()->clear_pending_exception();
}

TEST(ExceptionHandling, reraise_active_exception_reenters_exception_unwind)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    Value exception_class = Value::from_oop(
        test_context.thread()->class_for_builtin_name(L"Exception"));
    CodeObject *code_obj = make_reraise_active_exception_handler_code(
        test_context, exception_class);

    Value actual = test_context.thread()->run_clovervm_code_object(code_obj);
    EXPECT_EQ(Value::from_smi(99), actual);
    EXPECT_FALSE(test_context.thread()->has_pending_exception());
}

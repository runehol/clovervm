#include "class_object.h"
#include "code_object_builder.h"
#include "exception_object.h"
#include "test_helpers.h"
#include "thread_state.h"
#include "virtual_machine.h"
#include <gtest/gtest.h>

using namespace cl;

static CodeObject *
make_lda_active_exception_handler_code(test::VmTestContext &test_context,
                                       Value raised)
{
    TValue<String> name = test_context.vm().get_or_create_interned_string_value(
        L"<active-exception-test>");
    CodeObjectBuilder builder(&test_context.vm(), nullptr, nullptr, nullptr,
                              name);
    uint32_t constant_idx = builder.allocate_constant(raised);
    JumpTarget handler(&builder);

    {
        ExceptionTableRangeBuilder range(&builder, handler);
        builder.emit_lda_constant(0, uint8_t(constant_idx));
        builder.emit_raise_unwind(0);
        range.close();
    }

    handler.resolve();
    builder.emit_lda_active_exception(0);
    builder.emit_halt(0);
    return builder.finalize();
}

static CodeObject *
make_clear_active_exception_handler_code(test::VmTestContext &test_context,
                                         Value raised)
{
    TValue<String> name = test_context.vm().get_or_create_interned_string_value(
        L"<clear-active-exception-test>");
    CodeObjectBuilder builder(&test_context.vm(), nullptr, nullptr, nullptr,
                              name);
    uint32_t constant_idx = builder.allocate_constant(raised);
    JumpTarget handler(&builder);

    {
        ExceptionTableRangeBuilder range(&builder, handler);
        builder.emit_lda_constant(0, uint8_t(constant_idx));
        builder.emit_raise_unwind(0);
        range.close();
    }

    handler.resolve();
    builder.emit_clear_active_exception(0);
    builder.emit_lda_smi(0, 42);
    builder.emit_halt(0);
    return builder.finalize();
}

static CodeObject *
make_drain_active_exception_handler_code(test::VmTestContext &test_context,
                                         Value raised)
{
    TValue<String> name = test_context.vm().get_or_create_interned_string_value(
        L"<drain-active-exception-test>");
    CodeObjectBuilder builder(&test_context.vm(), nullptr, nullptr, nullptr,
                              name);
    uint32_t constant_idx = builder.allocate_constant(raised);
    JumpTarget handler(&builder);

    {
        ExceptionTableRangeBuilder range(&builder, handler);
        builder.emit_lda_constant(0, uint8_t(constant_idx));
        builder.emit_raise_unwind(0);
        range.close();
    }

    handler.resolve();
    {
        CodeObjectBuilder::TemporaryReg saved_exception(builder);
        builder.emit_drain_active_exception_into(0, saved_exception);
        builder.emit_ldar(0, saved_exception);
    }
    builder.emit_halt(0);
    return builder.finalize();
}

static CodeObject *
make_reraise_active_exception_handler_code(test::VmTestContext &test_context,
                                           Value raised)
{
    TValue<String> name = test_context.vm().get_or_create_interned_string_value(
        L"<reraise-active-exception-test>");
    CodeObjectBuilder builder(&test_context.vm(), nullptr, nullptr, nullptr,
                              name);
    uint32_t constant_idx = builder.allocate_constant(raised);
    JumpTarget inner_handler(&builder);
    JumpTarget outer_handler(&builder);

    {
        ExceptionTableRangeBuilder outer_range(&builder, outer_handler);
        {
            ExceptionTableRangeBuilder inner_range(&builder, inner_handler);
            builder.emit_lda_constant(0, uint8_t(constant_idx));
            builder.emit_raise_unwind(0);
            inner_range.close();
        }

        inner_handler.resolve();
        builder.emit_reraise_active_exception(0);
        outer_range.close();
    }

    outer_handler.resolve();
    builder.emit_clear_active_exception(0);
    builder.emit_lda_smi(0, 99);
    builder.emit_halt(0);
    return builder.finalize();
}

static CodeObject *
make_lda_active_exception_code(test::VmTestContext &test_context)
{
    TValue<String> name = test_context.vm().get_or_create_interned_string_value(
        L"<lda-active-exception-test>");
    CodeObjectBuilder builder(&test_context.vm(), nullptr, nullptr, nullptr,
                              name);
    builder.emit_lda_active_exception(0);
    builder.emit_halt(0);
    return builder.finalize();
}

static CodeObject *
make_active_exception_is_instance_code(test::VmTestContext &test_context,
                                       Value handler_class)
{
    TValue<String> name = test_context.vm().get_or_create_interned_string_value(
        L"<active-exception-is-instance-test>");
    CodeObjectBuilder builder(&test_context.vm(), nullptr, nullptr, nullptr,
                              name);
    uint32_t constant_idx = builder.allocate_constant(handler_class);
    builder.emit_lda_constant(0, uint8_t(constant_idx));
    builder.emit_active_exception_is_instance(0);
    builder.emit_halt(0);
    return builder.finalize();
}

TEST(ExceptionHandling, lda_active_exception_reads_pending_exception_object)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    Value exception_class = Value::from_oop(
        test_context.thread()->class_for_builtin_name(L"Exception"));
    CodeObject *code_obj =
        make_lda_active_exception_handler_code(test_context, exception_class);

    Value actual = test_context.thread()->run(code_obj);
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
    Value actual = test_context.thread()->run(code_obj);

    TValue<StopIterationObject> exception =
        TValue<StopIterationObject>::from_value_checked(actual);
    EXPECT_EQ(Value::from_smi(7), exception.extract()->value.as_value());
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

    Value actual = test_context.thread()->run(code_obj);
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

    Value actual = test_context.thread()->run(code_obj);
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
    Value actual = test_context.thread()->run(code_obj);

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

    Value actual = test_context.thread()->run(code_obj);
    EXPECT_EQ(Value::from_smi(99), actual);
    EXPECT_FALSE(test_context.thread()->has_pending_exception());
}

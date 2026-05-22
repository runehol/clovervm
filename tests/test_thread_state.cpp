#include "code_object_builder.h"
#include "test_helpers.h"

#include "exception_object.h"
#include "module_object.h"
#include "owned.h"
#include "refcount.h"
#include "str.h"

#include <gtest/gtest.h>

namespace cl
{
    struct TargetSafepointRecorder
    {
        CodeObject *target = nullptr;
        SafepointScanRecord record;
        uint32_t pc_offset = 0;
    };

    static void record_target_safepoint_callback(
        void *context, ThreadState *, Value, Value *, CodeObject *code_object,
        uint32_t pc_offset, const SafepointScanRecord &scan_record)
    {
        TargetSafepointRecorder *recorder =
            static_cast<TargetSafepointRecorder *>(context);
        if(code_object != recorder->target)
        {
            return;
        }
        recorder->pc_offset = pc_offset;
        recorder->record = scan_record;
    }

    static void
    begin_recording_target_safepoints(test::VmTestContext &context,
                                      TargetSafepointRecorder *recorder,
                                      CodeObject *target)
    {
        recorder->target = target;
        recorder->record = {};
        recorder->pc_offset = 0;
        context.vm().set_fire_every_safepoint_for_testing(true);
        context.vm().set_safepoint_callback_for_testing(
            record_target_safepoint_callback, recorder);
    }

    TEST(ThreadState, PendingExceptionStartsClear)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();

        EXPECT_FALSE(thread->has_pending_exception());
        EXPECT_EQ(PendingExceptionKind::None, thread->pending_exception_kind());
    }

    TEST(ThreadState, AllocationLeavesZeroRefcountObjectOutOfZct)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);

        size_t zct_size_before = thread->zero_count_table_size();
        String *string = thread->make_object_raw<String>(L"zct");

        EXPECT_EQ(0, string->refcount);
        EXPECT_EQ(HeapLifecycleState::Normal, string->lifecycle_state);
        EXPECT_EQ(zct_size_before, thread->zero_count_table_size());
    }

    TEST(ThreadState, ZeroRefcountEnqueueIsUnique)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);

        size_t zct_size_before = thread->zero_count_table_size();
        String *string = thread->make_object_raw<String>(L"zct");
        thread->add_to_zero_count_table_if_needed(string);
        ASSERT_EQ(zct_size_before + 1, thread->zero_count_table_size());

        thread->add_to_zero_count_table_if_needed(string);

        EXPECT_EQ(HeapLifecycleState::InZct, string->lifecycle_state);
        EXPECT_EQ(zct_size_before + 1, thread->zero_count_table_size());
    }

    TEST(ThreadState, DecrefToZeroDoesNotDuplicateExistingZctEntry)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);

        size_t zct_size_before = thread->zero_count_table_size();
        String *string = thread->make_object_raw<String>(L"zct");
        thread->add_to_zero_count_table_if_needed(string);
        ASSERT_EQ(zct_size_before + 1, thread->zero_count_table_size());

        incref_heap_ptr(string);
        EXPECT_EQ(1, string->refcount);
        decref_heap_ptr(string);

        EXPECT_EQ(0, string->refcount);
        EXPECT_EQ(HeapLifecycleState::InZct, string->lifecycle_state);
        EXPECT_EQ(zct_size_before + 1, thread->zero_count_table_size());
    }

    TEST(ThreadState, AdoptReclamationStateMovesZctOwnership)
    {
        test::VmTestContext context;
        ThreadState *parent = context.thread();
        GlobalHeap &heap = context.vm().get_refcounted_global_heap();
        size_t parent_zct_size = parent->zero_count_table_size();
        String *string;
        SlabAllocator *slab;

        {
            ThreadState child(&context.vm());
            {
                ThreadState::ActivationScope active_child(&child);
                string = child.make_object_raw<String>(L"adopted-zct");
                child.add_to_zero_count_table_if_needed(string);
            }
            slab = heap.slab_for_object_unlocked(string);
            ASSERT_EQ(2u, slab->slab_pin_count());
            ASSERT_TRUE(child.zero_count_table_contains_for_testing(string));

            parent->adopt_reclamation_state_from(child);

            EXPECT_EQ(0u, child.zero_count_table_size());
            EXPECT_EQ(parent_zct_size + 1, parent->zero_count_table_size());
            EXPECT_TRUE(parent->zero_count_table_contains_for_testing(string));
        }

        EXPECT_TRUE(heap.has_slab_for_address_for_testing(string));
        EXPECT_EQ(1u, slab->slab_pin_count());
        EXPECT_TRUE(parent->zero_count_table_contains_for_testing(string));
    }

    TEST(ThreadState, OwnedValueOverwriteEnqueuesReleasedObject)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        String *old_string = thread->make_object_raw<String>(L"old-owned");
        String *new_string = thread->make_object_raw<String>(L"new-owned");
        Owned<Value> owner(Value::from_oop(old_string));
        Owned<Value> keep_new(Value::from_oop(new_string));
        ASSERT_FALSE(thread->zero_count_table_contains_for_testing(old_string));
        ASSERT_EQ(HeapLifecycleState::Normal, old_string->lifecycle_state);

        owner = Value::from_oop(new_string);

        EXPECT_EQ(0, old_string->refcount);
        EXPECT_EQ(HeapLifecycleState::InZct, old_string->lifecycle_state);
        EXPECT_TRUE(thread->zero_count_table_contains_for_testing(old_string));
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(new_string));
    }

    TEST(ThreadState, MemberValueOverwriteEnqueuesReleasedObject)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        String *old_string = thread->make_object_raw<String>(L"old-member");
        String *new_string = thread->make_object_raw<String>(L"new-member");
        Member<Value> member(Value::from_oop(old_string));
        Owned<Value> keep_new(Value::from_oop(new_string));
        ASSERT_FALSE(thread->zero_count_table_contains_for_testing(old_string));
        ASSERT_EQ(HeapLifecycleState::Normal, old_string->lifecycle_state);

        member = Value::from_oop(new_string);

        EXPECT_EQ(0, old_string->refcount);
        EXPECT_EQ(HeapLifecycleState::InZct, old_string->lifecycle_state);
        EXPECT_TRUE(thread->zero_count_table_contains_for_testing(old_string));
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(new_string));
        member.release_ref();
    }

    TEST(ThreadState, SafepointRequestReadsVmFlag)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();

        EXPECT_FALSE(thread->safepoint_requested());

        context.vm().request_safepoint();
        EXPECT_TRUE(thread->safepoint_requested());

        context.vm().clear_safepoint_request();
        EXPECT_FALSE(thread->safepoint_requested());
    }

    static CodeObject *make_return_42_test_code(test::VmTestContext &context)
    {
        TValue<String> name =
            context.vm().get_or_create_interned_string_value(L"<return-42>");
        CodeObjectBuilder builder(
            &context.vm(), nullptr,
            TValue<ModuleObject>::from_oop(
                context.thread()->make_module_object(name)),
            nullptr, name);
        builder.emit_lda_smi(0, 42);
        builder.emit_return(0);
        return builder.finalize();
    }

    TEST(ThreadState, CallSafepointPublishesCommittedCalleeFrame)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        CodeObject *code = make_return_42_test_code(context);
        TargetSafepointRecorder recorder;
        begin_recording_target_safepoints(context, &recorder, code);
        context.vm().request_safepoint();

        Value result = thread->run_clovervm_code_object(code);

        EXPECT_EQ(Value::from_smi(42), result);
        EXPECT_EQ(0u, recorder.pc_offset);
        EXPECT_NE(nullptr, recorder.record.lowest_live_stack_slot);
        EXPECT_TRUE(
            recorder.record.accumulator_or_not_present.is_not_present());
    }

    TEST(ThreadState, ReturnSafepointPublishesAccumulatorInCallerFrame)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        CodeObject *code = make_return_42_test_code(context);
        CodeObject *adapter = context.vm().clover_function_entry_adapter(0);
        TargetSafepointRecorder recorder;
        begin_recording_target_safepoints(context, &recorder, adapter);
        context.vm().request_safepoint();

        Value result = thread->run_clovervm_code_object(code);

        EXPECT_EQ(Value::from_smi(42), result);
        EXPECT_EQ(8u, recorder.pc_offset);
        EXPECT_NE(nullptr, recorder.record.lowest_live_stack_slot);
        EXPECT_EQ(Value::from_smi(42),
                  recorder.record.accumulator_or_not_present);
    }

    TEST(ThreadState, PendingExceptionObjectStoresTypedObject)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ClassObject *base_exception =
            context.vm().class_for_native_layout(NativeLayoutId::Exception);
        ThreadState::ActivationScope active_thread(thread);
        TValue<Exception> exception = make_exception_object(
            TValue<ClassObject>::from_oop(base_exception), L"boom");

        EXPECT_TRUE(thread->set_pending_exception_object(exception)
                        .is_exception_marker());

        EXPECT_TRUE(thread->has_pending_exception());
        EXPECT_EQ(PendingExceptionKind::Object,
                  thread->pending_exception_kind());
        EXPECT_EQ(exception, thread->pending_exception_object());
    }

    TEST(ThreadState, PendingStopIterationNoValueUsesNotPresent)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();

        EXPECT_TRUE(thread->set_pending_stop_iteration_no_value()
                        .is_exception_marker());

        EXPECT_TRUE(thread->has_pending_exception());
        EXPECT_EQ(PendingExceptionKind::StopIteration,
                  thread->pending_exception_kind());
        EXPECT_TRUE(thread->pending_stop_iteration_value().is_not_present());
    }

    TEST(ThreadState, PendingStopIterationValueDistinguishesNone)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();

        EXPECT_TRUE(thread->set_pending_stop_iteration_value(Value::None())
                        .is_exception_marker());

        EXPECT_TRUE(thread->has_pending_exception());
        EXPECT_EQ(PendingExceptionKind::StopIteration,
                  thread->pending_exception_kind());
        EXPECT_EQ(Value::None(), thread->pending_stop_iteration_value());
        EXPECT_FALSE(thread->pending_stop_iteration_value().is_not_present());
    }

    TEST(ThreadState, ClearPendingExceptionResetsState)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();

        EXPECT_TRUE(
            thread->set_pending_stop_iteration_value(Value::from_smi(42))
                .is_exception_marker());
        thread->clear_pending_exception();

        EXPECT_FALSE(thread->has_pending_exception());
        EXPECT_EQ(PendingExceptionKind::None, thread->pending_exception_kind());
    }

    TEST(ThreadState, PendingExceptionStringCreatesExceptionObject)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ClassObject *base_exception =
            context.vm().class_for_native_layout(NativeLayoutId::Exception);
        ThreadState::ActivationScope active_thread(thread);

        EXPECT_TRUE(
            thread
                ->set_pending_exception_string(
                    TValue<ClassObject>::from_oop(base_exception), L"boom")
                .is_exception_marker());

        EXPECT_TRUE(thread->has_pending_exception());
        EXPECT_EQ(PendingExceptionKind::Object,
                  thread->pending_exception_kind());
        TValue<Exception> exception = thread->pending_exception_object();
        EXPECT_EQ(base_exception,
                  exception.extract()->get_shape()->get_class());
        EXPECT_STREQ(L"boom", exception.extract()->message.extract()->data);
    }

    TEST(ThreadState, StopIterationObjectStoresValueSlot)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ClassObject *stop_iteration =
            context.vm().class_for_native_layout(NativeLayoutId::StopIteration);
        ThreadState::ActivationScope active_thread(thread);

        TValue<StopIterationObject> exception = make_stop_iteration_object(
            TValue<ClassObject>::from_oop(stop_iteration), Value::from_smi(42));

        EXPECT_EQ(Value::from_smi(42), exception.extract()->value);
        EXPECT_STREQ(L"", exception.extract()->message.extract()->data);
        TValue<Exception> base_exception =
            TValue<Exception>::from_value_assumed(exception.raw_value());
        EXPECT_EQ(exception.raw_value(), base_exception.raw_value());
    }

    TEST(ThreadState, StopIterationObjectDefaultsValueToNotPresent)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ClassObject *stop_iteration =
            context.vm().class_for_native_layout(NativeLayoutId::StopIteration);
        ThreadState::ActivationScope active_thread(thread);

        TValue<StopIterationObject> exception = make_stop_iteration_object(
            TValue<ClassObject>::from_oop(stop_iteration));

        EXPECT_TRUE(exception.extract()->value.value().is_not_present());
    }

    TEST(ThreadState, ClassOfValueMapsPointerAndInlineValues)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        TValue<String> string =
            context.vm().get_or_create_interned_string_value(L"value");

        EXPECT_EQ(context.vm().str_class(),
                  thread->class_of_value(string.raw_value()));
        EXPECT_EQ(context.vm().int_class(),
                  thread->class_of_value(Value::from_smi(42)));
        EXPECT_EQ(context.vm().bool_class(),
                  thread->class_of_value(Value::True()));
        EXPECT_EQ(context.vm().bool_class(),
                  thread->class_of_value(Value::False()));
        EXPECT_EQ(context.vm().none_type_class(),
                  thread->class_of_value(Value::None()));
    }

    TEST(ThreadState, ShapeOfValueMapsPointerAndInlineValues)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        TValue<String> string =
            context.vm().get_or_create_interned_string_value(L"value");

        EXPECT_EQ(string.extract()->get_shape(),
                  thread->shape_of_value(string.raw_value()));
        EXPECT_EQ(context.vm().smi_shape(),
                  thread->shape_of_value(Value::from_smi(42)));
        EXPECT_EQ(context.vm().int_class(),
                  thread->shape_of_value(Value::from_smi(42))->get_class());
        EXPECT_EQ(context.vm().bool_shape(),
                  thread->shape_of_value(Value::True()));
        EXPECT_EQ(context.vm().bool_shape(),
                  thread->shape_of_value(Value::False()));
        EXPECT_EQ(context.vm().bool_class(),
                  thread->shape_of_value(Value::True())->get_class());
        EXPECT_EQ(context.vm().none_shape(),
                  thread->shape_of_value(Value::None()));
        EXPECT_EQ(context.vm().none_type_class(),
                  thread->shape_of_value(Value::None())->get_class());

        TValue<String> dunder_class_name = context.vm().dunder_class_name();
        Shape *inline_shapes[] = {context.vm().smi_shape(),
                                  context.vm().bool_shape(),
                                  context.vm().none_shape()};
        for(Shape *shape: inline_shapes)
        {
            ASSERT_EQ(1u, shape->property_count());
            EXPECT_EQ(1u, shape->present_count());
            EXPECT_EQ(0, shape->get_next_slot_index());
            DescriptorLookup lookup =
                shape->lookup_descriptor_including_latent(dunder_class_name);
            ASSERT_TRUE(lookup.is_present());
            EXPECT_FALSE(lookup.storage_location().is_found());
            EXPECT_TRUE(lookup.info.has_flag(DescriptorFlag::ReadOnly));
            EXPECT_TRUE(lookup.info.has_flag(DescriptorFlag::StableSlot));
            EXPECT_TRUE(lookup.info.has_flag(DescriptorFlag::ShapeClassValue));
        }
    }

}  // namespace cl

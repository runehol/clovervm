#include "api/extension_handle.h"
#include "builtin_types/float.h"
#include "memory/heap_reclamation.h"
#include "object_model/owned.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

using namespace cl;

namespace
{
    void rewrite_published(ThreadState &thread, HeapObject *original,
                           HeapObject *replacement)
    {
        thread.visit_published_root_slots([&](HeapReferenceSlot slot) {
            if(slot.load_pointer() == original)
            {
                slot.rewrite_pointer(replacement);
            }
        });
    }

    struct AccumulatorRewrite
    {
        Float *replacement;
        bool rewrote = false;
        bool saw_dead_accumulator = false;
    };

    void rewrite_at_safepoint(void *opaque, ThreadState *thread, Value, Value *,
                              CodeObject *code, uint32_t,
                              const SafepointScanRecord &record)
    {
        auto &state = *static_cast<AccumulatorRewrite *>(opaque);
        EXPECT_EQ(Value::from_oop(code),
                  record.active_code_object_or_not_present);
        Value accumulator = record.accumulator_or_not_present;
        if(accumulator == Value::not_present())
        {
            state.saw_dead_accumulator = true;
            return;
        }
        if(!has_native_layout<Float>(accumulator) ||
           accumulator.get_ptr<Float>()->value() != 37.25)
        {
            return;
        }
        Float *original = accumulator.get_ptr<Float>();
        int32_t original_refs = original->refcount;
        int32_t replacement_refs = state.replacement->refcount;
        size_t zct_size = thread->zero_count_table_size();
        rewrite_published(*thread, original, state.replacement);
        EXPECT_EQ(original_refs, original->refcount);
        EXPECT_EQ(replacement_refs, state.replacement->refcount);
        EXPECT_EQ(zct_size, thread->zero_count_table_size());
        state.rewrote = true;
    }
}  // namespace

TEST(PublishedRoots, RewritesAccumulatorWithoutChangingFrontierOrCode)
{
    test::VmTestContext context;
    ThreadState &thread = *context.thread();
    ThreadState::ActivationScope activation(&thread);
    Owned<TValue<Float>> original(thread.make_object_value<Float>(1));
    Owned<TValue<Float>> replacement(thread.make_object_value<Float>(2));
    Owned<Value> code(Value::from_oop(context.compile_file(L"pass\n")));
    Value *frontier = thread.clover_frame_sentinel();
    thread.publish_safepoint_scan_record(frontier, original.raw_value(),
                                         code.value());

    rewrite_published(thread, original.extract(), replacement.extract());
    EXPECT_EQ(replacement.raw_value(),
              thread.safepoint_scan_record().accumulator_or_not_present);
    EXPECT_EQ(code.value(),
              thread.safepoint_scan_record().active_code_object_or_not_present);
    EXPECT_EQ(frontier, thread.safepoint_scan_record().lowest_live_stack_slot);
    ReclamationRootSet roots;
    collect_reclamation_roots_from_thread(roots, thread);
    EXPECT_TRUE(roots.contains(replacement.extract()));
    EXPECT_TRUE(roots.contains(code.value().get_ptr<CodeObject>()));
    EXPECT_FALSE(roots.contains(original.extract()));

    for(Value immediate:
        {Value::not_present(), Value::None(), Value::from_smi(42)})
    {
        thread.publish_safepoint_scan_record(frontier, immediate);
        thread.visit_published_root_slots([](HeapReferenceSlot slot) {
            EXPECT_EQ(nullptr, slot.load_pointer());
        });
        EXPECT_EQ(immediate,
                  thread.safepoint_scan_record().accumulator_or_not_present);
    }
}

TEST(PublishedRoots, NestedNativeReentryRestoresRewrittenSavedCells)
{
    test::VmTestContext context;
    ThreadState &thread = *context.thread();
    ThreadState::ActivationScope activation(&thread);
    Owned<TValue<Float>> outer(thread.make_object_value<Float>(1));
    Owned<TValue<Float>> inner(thread.make_object_value<Float>(2));
    Owned<TValue<Float>> replacement(thread.make_object_value<Float>(3));
    Owned<Value> code(Value::from_oop(context.compile_file(L"pass\n")));
    Value *frontier = thread.clover_frame_frontier();
    thread.publish_safepoint_scan_record(frontier, outer.raw_value(),
                                         code.value());
    int32_t outer_refs = outer.extract()->refcount;
    int32_t inner_refs = inner.extract()->refcount;
    int32_t replacement_refs = replacement.extract()->refcount;
    size_t zct_size = thread.zero_count_table_size();
    {
        NativeHandleRootRegion outer_region(&thread);
        Value *outer_frontier = thread.clover_frame_frontier();
        thread.publish_safepoint_scan_record(outer_frontier, inner.raw_value(),
                                             code.value());
        {
            NativeHandleRootRegion inner_region(&thread);
            // Reentry replaces the current record entirely. Only the scoped
            // saved cells can keep the suspended publications visible here.
            thread.publish_safepoint_scan_record(thread.clover_frame_frontier(),
                                                 Value::not_present());
            ReclamationRootSet roots;
            collect_reclamation_roots_from_thread(roots, thread);
            EXPECT_TRUE(roots.contains(outer.extract()));
            EXPECT_TRUE(roots.contains(inner.extract()));
            EXPECT_TRUE(roots.contains(code.value().get_ptr<CodeObject>()));
            thread.visit_native_root_slots([&](HeapReferenceSlot slot) {
                HeapObject *object = slot.load_pointer();
                if(object == outer.extract() || object == inner.extract())
                {
                    slot.rewrite_pointer(replacement.extract());
                }
            });
        }
        EXPECT_EQ(replacement.raw_value(),
                  thread.safepoint_scan_record().accumulator_or_not_present);
        EXPECT_EQ(
            code.value(),
            thread.safepoint_scan_record().active_code_object_or_not_present);
        EXPECT_EQ(outer_frontier, thread.clover_frame_frontier());
        EXPECT_EQ(outer_frontier,
                  thread.safepoint_scan_record().lowest_live_stack_slot);
    }
    EXPECT_EQ(replacement.raw_value(),
              thread.safepoint_scan_record().accumulator_or_not_present);
    EXPECT_EQ(code.value(),
              thread.safepoint_scan_record().active_code_object_or_not_present);
    EXPECT_EQ(frontier, thread.clover_frame_frontier());
    EXPECT_EQ(frontier, thread.safepoint_scan_record().lowest_live_stack_slot);
    EXPECT_EQ(outer_refs, outer.extract()->refcount);
    EXPECT_EQ(inner_refs, inner.extract()->refcount);
    EXPECT_EQ(replacement_refs, replacement.extract()->refcount);
    EXPECT_EQ(zct_size, thread.zero_count_table_size());
    thread.visit_native_root_slots([&](HeapReferenceSlot slot) {
        EXPECT_NE(replacement.extract(), slot.load_pointer());
    });
    thread.publish_safepoint_scan_record(frontier, Value::not_present());
}

TEST(PublishedRoots, InterpreterResumesWithRewrittenAccumulator)
{
    test::VmTestContext context;
    ThreadState &thread = *context.thread();
    ThreadState::ActivationScope activation(&thread);
    Owned<TValue<Float>> replacement(thread.make_object_value<Float>(83.5));
    CodeObject *code =
        context.compile_file(L"def value():\n    return 37.25\nvalue()\n");
    AccumulatorRewrite state{replacement.extract()};
    context.vm().set_safepoint_callback_for_testing(rewrite_at_safepoint,
                                                    &state);
    context.vm().set_fire_every_safepoint_for_testing(true);
    context.vm().request_safepoint();
    Value result = thread.run_clovervm_code_object(code);
    context.vm().set_safepoint_callback_for_testing(nullptr, nullptr);

    EXPECT_TRUE(state.rewrote);
    EXPECT_TRUE(state.saw_dead_accumulator);
    EXPECT_EQ(replacement.raw_value(), result);
}

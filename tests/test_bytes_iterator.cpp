#include "builtin_types/bytes.h"
#include "memory/native_layout_descriptor.h"
#include "object_model/function.h"
#include "test_helpers.h"
#include <fstream>
#include <iterator>

using namespace cl;

TEST(BytesIterator, SemanticsSurviveEverySafepointReclamation)
{
    std::ifstream input("tests/python/bytes_iteration.py");
    ASSERT_TRUE(input);
    std::string source((std::istreambuf_iterator<char>(input)), {});
    std::wstring wide(source.begin(), source.end());
    test::VmTestContext context;
    context.vm().set_fire_every_safepoint_for_testing(true);
    context.vm().request_safepoint();
    EXPECT_FALSE(context.run_file(wide.c_str()).is_exception_marker());
}

TEST(BytesIterator, DescriptorAndSourceSlotAreCompleteAndRewritable)
{
    test::VmTestContext context;
    auto thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    const uint8_t payload[] = {255};
    Owned<TValue<Bytes>> source(
        thread->make_object_value<Bytes>(std::span(payload)));
    Owned<TValue<Bytes>> replacement(
        thread->make_object_value<Bytes>(std::span(payload)));
    Owned<TValue<BytesIterator>> iterator(
        thread->make_object_value<BytesIterator>(source.value()));
    const auto &release = release_descriptor_for(NativeLayoutId::BytesIterator);
    EXPECT_EQ(ReleaseKind::StaticSpan, release.kind);
    EXPECT_EQ(Object::native_static_release_count() + 2,
              release.static_release_count);
    EXPECT_EQ(sizeof(BytesIterator), object_size_in_bytes(iterator.extract()));
    EXPECT_EQ(nullptr, release.custom_trace_strong);
    int32_t source_refs = source.extract()->refcount;
    int32_t replacement_refs = replacement.extract()->refcount;
    size_t source_slots = 0;
    visit_strong_reference_slots(
        iterator.extract(), [&](HeapReferenceSlot slot) {
            if(slot.load_pointer() == source.extract())
            {
                ++source_slots;
                slot.rewrite_pointer(replacement.extract());
            }
        });
    EXPECT_EQ(1u, source_slots);
    EXPECT_EQ(replacement.value(), iterator.extract()->source.value().value());
    EXPECT_EQ(source_refs, source.extract()->refcount);
    EXPECT_EQ(replacement_refs, replacement.extract()->refcount);
    // Collector slot rewriting deliberately bypasses RC; restore before RAII
    // release.
    visit_strong_reference_slots(
        iterator.extract(), [&](HeapReferenceSlot slot) {
            if(slot.load_pointer() == replacement.extract())
                slot.rewrite_pointer(source.extract());
        });
}

TEST(BytesIterator, SourceSurvivesReclamationAndIsReleasedOnExhaustion)
{
    test::VmTestContext context;
    auto thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    const uint8_t payload[] = {255};
    auto source = thread->make_object_value<Bytes>(std::span(payload));
    Owned<TValue<BytesIterator>> iterator(
        thread->make_object_value<BytesIterator>(source));
    EXPECT_EQ(1, source.extract()->refcount);
    thread->publish_safepoint_scan_record(
        SafepointScanRecord::idle(thread->clover_frame_sentinel()));
    context.vm().run_heap_reclamation();
    EXPECT_EQ(1, source.extract()->refcount);
    auto next_name =
        context.vm().get_or_create_interned_string_value(L"__next__");
    EXPECT_EQ(Value::from_smi(255),
              thread->call_clovervm_method(iterator.raw_value(), next_name));
    EXPECT_TRUE(iterator.extract()->source.value());
    {
        // Keep a separate owner while inspecting the reference count after
        // the iterator releases its edge; exception transport may safepoint.
        Owned<TValue<Bytes>> observer(source);
        EXPECT_EQ(2, source.extract()->refcount);
        ASSERT_TRUE(
            thread->call_clovervm_method(iterator.raw_value(), next_name)
                .is_exception_marker());
        EXPECT_EQ(
            thread->class_for_native_layout(NativeLayoutId::StopIteration),
            thread->class_of_value(
                thread->pending_exception_object().raw_value()));
        thread->clear_pending_exception();
        EXPECT_FALSE(iterator.extract()->source.value());
        EXPECT_EQ(1, source.extract()->refcount);
    }
    context.vm().run_heap_reclamation();
    ASSERT_TRUE(thread->call_clovervm_method(iterator.raw_value(), next_name)
                    .is_exception_marker());
    thread->clear_pending_exception();
}

TEST(BytesIterator, InternalClassAndDirectReceiverValidation)
{
    test::VmTestContext context;
    auto thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    ClassObject *cls =
        context.vm().class_for_native_layout(NativeLayoutId::BytesIterator);
    EXPECT_EQ(L"bytes_iterator",
              std::wstring(string_as_wchar_t(cls->get_name())));
    EXPECT_EQ(Value::not_present(),
              context.vm().global_builtins_module().extract()->get_own_property(
                  cls->get_name()));
    for(const wchar_t *name: {L"__iter__", L"__next__"})
    {
        Value method = cls->get_own_property(
            context.vm().get_or_create_interned_string_value(name));
        ASSERT_TRUE(has_native_layout<Function>(method));
        ASSERT_TRUE(
            thread
                ->call_clovervm_function(
                    TValue<Function>::from_value_assumed(method), Value::None())
                .is_exception_marker());
        EXPECT_EQ(thread->class_for_builtin_name(L"TypeError"),
                  thread->class_of_value(
                      thread->pending_exception_object().raw_value()));
        thread->clear_pending_exception();
    }
}

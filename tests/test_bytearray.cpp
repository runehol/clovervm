#include "builtin_types/bytearray.h"
#include "builtin_types/bytes.h"
#include "memory/global_heap.h"
#include "memory/heap_constants.h"
#include "memory/native_layout_descriptor.h"
#include "object_model/function.h"
#include "runtime/trusted_handler.h"
#include "test_helpers.h"
#include <fstream>
#include <iterator>

using namespace cl;

TEST(ByteArray, LayoutTracesOwnerBackingAndIterator)
{
    const auto &release = release_descriptor_for(NativeLayoutId::ByteArray);
    EXPECT_EQ(ReleaseKind::StaticSpan, release.kind);
    EXPECT_EQ(Object::native_static_release_count() +
                  RawArray<uint8_t>::embedded_value_count,
              release.static_release_count);
    EXPECT_EQ(nullptr, release.custom_dealloc);
    EXPECT_EQ(sizeof(ByteArray),
              object_size_descriptor_for(NativeLayoutId::ByteArray)
                  .static_size_in_bytes);
    EXPECT_EQ(Object::native_static_release_count() + 2,
              release_descriptor_for(NativeLayoutId::ByteArrayIterator)
                  .static_release_count);
    EXPECT_EQ(ReleaseKind::StaticSpan,
              release_descriptor_for(NativeLayoutId::RawArrayBacking).kind);
}

TEST(ByteArray, ExhaustionReleasesSourceAndItsBackingWhileIteratorStaysAlive)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    Owned<TValue<ByteArray>> source(thread->make_object_value<ByteArray>());
    // Give a one-item source a dedicated large backing so its release can be
    // observed independently of unrelated objects in ordinary allocation slabs.
    source.extract()->items.resize(2 * LargeAllocationSize);
    source.extract()->items.resize(1);
    source.extract()->items[0] = 7;
    const void *backing_address = source.extract()->view().data();
    Owned<TValue<ByteArrayIterator>> iterator(
        thread->make_object_value<ByteArrayIterator>(source.value()));
    auto next_name =
        context.vm().get_or_create_interned_string_value(L"__next__");
    EXPECT_EQ(2, source.extract()->refcount);
    context.vm().request_safepoint();
    EXPECT_EQ(Value::from_smi(7),
              thread->call_clovervm_method(iterator.raw_value(), next_name));
    // Returning the last item is not yet exhaustion.
    EXPECT_EQ(2, source.extract()->refcount);
    ASSERT_TRUE(thread->call_clovervm_method(iterator.raw_value(), next_name)
                    .is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object, thread->pending_exception_kind());
    EXPECT_EQ(
        thread->class_for_native_layout(NativeLayoutId::StopIteration),
        thread->class_of_value(thread->pending_exception_object().raw_value()));
    thread->clear_pending_exception();
    EXPECT_EQ(1, source.extract()->refcount);
    source.extract()->items.push_back(9);
    ASSERT_TRUE(thread->call_clovervm_method(iterator.raw_value(), next_name)
                    .is_exception_marker());
    thread->clear_pending_exception();
    EXPECT_EQ(1, source.extract()->refcount);
    source = thread->make_object_value<ByteArray>();
    ASSERT_EQ(thread->clover_frame_sentinel(), thread->clover_frame_frontier());
    thread->publish_safepoint_scan_record(thread->clover_frame_sentinel(),
                                          Value::not_present());
    context.vm().run_heap_reclamation();
    context.vm().run_heap_reclamation();
    EXPECT_FALSE(context.vm()
                     .get_refcounted_global_heap()
                     .has_slab_for_address_for_testing(backing_address));
    ASSERT_TRUE(thread->call_clovervm_method(iterator.raw_value(), next_name)
                    .is_exception_marker());
    thread->clear_pending_exception();
}

TEST(ByteArray, ConstructionAndBasicMutationSurviveCollection)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    context.vm().set_fire_every_safepoint_for_testing(true);
    context.vm().request_safepoint();
    Value result = context.run_file(L"assert len(bytearray()) == 0\n"
                                    L"assert len(bytearray(3)) == 3\n"
                                    L"a = bytearray([65, 66, 67])\n"
                                    L"copy = bytearray(a)\n"
                                    L"a.clear()\n"
                                    L"assert len(a) == 0 and len(copy) == 3\n"
                                    L"a.append(68)\n"
                                    L"copy.append(69)\n"
                                    L"copy\n");
    ASSERT_TRUE(has_native_layout<ByteArray>(result));
    const uint8_t expected[] = {'A', 'B', 'C', 'E'};
    auto view = result.get_ptr<ByteArray>()->view();
    EXPECT_TRUE(std::equal(view.begin(), view.end(), std::begin(expected),
                           std::end(expected)));
}

TEST(ByteArray, IterationRepresentationAndImmutableCopiesSurviveCollection)
{
    test::VmTestContext context;
    context.vm().set_fire_every_safepoint_for_testing(true);
    context.vm().request_safepoint();
    EXPECT_FALSE(context
                     .run_file(L"a = bytearray(b'AB')\n"
                               L"assert repr(a) == \"bytearray(b'AB')\"\n"
                               L"assert str(a) == repr(a)\n"
                               L"assert list(a) == [65, 66]\n"
                               L"b = bytes(a)\n"
                               L"assert a == b and b == a and a != b'AC'\n"
                               L"a.clear()\n"
                               L"assert b == b'AB' and a == b''\n")
                     .is_exception_marker());
}

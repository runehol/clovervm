#include "list.h"
#include "native_layout_descriptor.h"
#include "object.h"
#include "test_helpers.h"
#include "thread_state.h"

#include <gtest/gtest.h>

using namespace cl;

TEST(NativeLayoutDescriptor, ListUsesNativeStaticReleaseDescriptor)
{
    const ReleaseDescriptor &descriptor =
        release_descriptor_for(NativeLayoutId::List);

    EXPECT_EQ(ReleaseKind::StaticSpan, descriptor.kind);
    EXPECT_EQ(Object::native_value_offset_in_words(),
              descriptor.value_offset_words);
    EXPECT_EQ(List::native_static_release_count(),
              descriptor.static_release_count);
}

TEST(NativeLayoutDescriptor, ListNativeReleaseCountIncludesInheritedObjectCells)
{
    EXPECT_EQ(Object::native_static_release_count() +
                  ValueArray<Value>::embedded_value_count,
              List::native_static_release_count());
    EXPECT_EQ(List::static_value_count(), List::native_static_release_count());
}

TEST(NativeLayoutDescriptor, ListNativeReleaseSpanStartsAtInheritedObjectCells)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    List *list = context.thread()->make_object_raw<List>();

    NativeValueSpan span = value_span_for_release(list);

    EXPECT_EQ(reinterpret_cast<Value *>(list) +
                  Object::native_value_offset_in_words(),
              span.slots);
    EXPECT_EQ(List::native_static_release_count(), span.count);
}

TEST(NativeLayoutDescriptor, ListUsesNativeStaticObjectSizeDescriptor)
{
    const ObjectSizeDescriptor &descriptor =
        object_size_descriptor_for(NativeLayoutId::List);

    EXPECT_EQ(ObjectSizeKind::StaticSize, descriptor.kind);
    EXPECT_EQ(sizeof(List), descriptor.static_size_in_bytes);
}

TEST(NativeLayoutDescriptor, UnmigratedLayoutsStillUseLegacyReleaseDescriptor)
{
    EXPECT_EQ(ReleaseKind::LegacyHeapLayout,
              release_descriptor_for(NativeLayoutId::Tuple).kind);
}

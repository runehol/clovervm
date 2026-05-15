#include "dict.h"
#include "exception_object.h"
#include "function.h"
#include "list.h"
#include "list_iterator.h"
#include "native_layout_descriptor.h"
#include "object.h"
#include "range_iterator.h"
#include "test_helpers.h"
#include "thread_state.h"
#include "tuple_iterator.h"

#include <gtest/gtest.h>

using namespace cl;

namespace
{
    template <typename T> void expect_static_native_layout_descriptor()
    {
        const ReleaseDescriptor &release =
            release_descriptor_for(T::native_layout);

        EXPECT_EQ(ReleaseKind::StaticSpan, release.kind);
        EXPECT_EQ(T::static_value_offset_in_words(),
                  release.value_offset_words);
        EXPECT_EQ(T::static_value_count(), release.static_release_count);
        EXPECT_EQ(T::native_static_release_count(),
                  release.static_release_count);

        const ObjectSizeDescriptor &object_size =
            object_size_descriptor_for(T::native_layout);

        EXPECT_EQ(ObjectSizeKind::StaticSize, object_size.kind);
        EXPECT_EQ(sizeof(T), object_size.static_size_in_bytes);
    }
}  // namespace

TEST(NativeLayoutDescriptor, ListUsesNativeStaticReleaseDescriptor)
{
    expect_static_native_layout_descriptor<List>();
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

TEST(NativeLayoutDescriptor, FixedObjectSubclassesUseNativeStaticDescriptors)
{
    expect_static_native_layout_descriptor<RangeIterator>();
    expect_static_native_layout_descriptor<TupleIterator>();
    expect_static_native_layout_descriptor<ListIterator>();
    expect_static_native_layout_descriptor<ExceptionObject>();
    expect_static_native_layout_descriptor<StopIterationObject>();
    expect_static_native_layout_descriptor<Function>();
    expect_static_native_layout_descriptor<Dict>();
}

TEST(NativeLayoutDescriptor, UnmigratedLayoutsStillUseLegacyReleaseDescriptor)
{
    EXPECT_EQ(ReleaseKind::LegacyHeapLayout,
              release_descriptor_for(NativeLayoutId::Tuple).kind);
}

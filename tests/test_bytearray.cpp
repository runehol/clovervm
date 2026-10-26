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
    EXPECT_EQ(ReleaseKind::StaticSpan,
              release_descriptor_for(NativeLayoutId::RawArrayBacking).kind);
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

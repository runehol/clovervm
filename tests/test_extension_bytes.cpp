#include "api/extension_handle.h"
#include "builtin_types/bytearray.h"
#include "builtin_types/bytes.h"
#include "object_model/shape.h"
#include "runtime/exception_object.h"
#include "test_helpers.h"
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>

using namespace cl;

namespace
{
    void expect_api_error(test::VmTestContext &context, const wchar_t *type)
    {
        ASSERT_TRUE(context.thread()->has_pending_exception());
        auto exception = context.thread()->pending_exception_object();
        EXPECT_EQ(context.thread()->class_for_builtin_name(type),
                  exception.extract()->get_shape()->get_class());
        context.thread()->clear_pending_exception();
    }

    struct ByteVisit
    {
        const uint8_t *expected_data;
        size_t calls = 0;
        size_t size = 0;
        uint32_t sum = 0;
        bool same_storage = false;
    };

    void sum_bytes(const uint8_t *data, size_t size, void *state) noexcept
    {
        auto &visit = *static_cast<ByteVisit *>(state);
        ++visit.calls;
        visit.size = size;
        visit.same_storage = data == visit.expected_data;
        for(size_t i = 0; i < size; ++i)
            visit.sum += data[i];
    }

    void ignore_bytes(const uint8_t *, size_t, void *) noexcept {}
}  // namespace

TEST(ExtensionBytes, Uint32MaskReadsStoredIntegersModuloTwoTo32)
{
    struct Case
    {
        const wchar_t *source;
        uint32_t expected;
    };
    const Case cases[] = {
        {L"0", 0},
        {L"True", 1},
        {L"False", 0},
        {L"-1", UINT32_MAX},
        {L"4294967295", UINT32_MAX},
        {L"4294967296", 0},
        {L"4294967297", 1},
        {L"-4294967297", UINT32_MAX},
        {L"1267650600228229401496703205499", 123},
        {L"-1267650600228229401496703205832", UINT32_MAX - 455},
        {L"class I(int):\n"
         L"    def __index__(self):\n"
         L"        raise AssertionError('index override')\n"
         L"    def __int__(self):\n"
         L"        raise AssertionError('int override')\n"
         L"I(-1267650600228229401496703205832)\n",
         UINT32_MAX - 455},
    };
    for(const auto &item: cases)
    {
        SCOPED_TRACE(item.source);
        test::VmTestContext context;
        ThreadState::ActivationScope activation(context.thread());
        Value value = context.run_file(item.source);
        ASSERT_FALSE(value.is_exception_marker());
        NativeHandleRootRegion roots(context.thread());
        auto ctx = roots.make_context();
        auto handle = allocate_handle(&ctx, value);
        uint32_t number = 77;
        EXPECT_EQ(CLOVER_STATUS_OK,
                  clover_int_as_uint32_mask(&ctx, handle, &number));
        EXPECT_EQ(item.expected, number);
        EXPECT_FALSE(context.thread()->has_pending_exception());
    }
}

TEST(ExtensionBytes, Uint32MaskRejectsOtherTypesWithoutIndexCallbacks)
{
    for(const wchar_t *source:
        {L"None", L"1.0", L"'1'", L"[]",
         L"class Index:\n"
         L"    def __index__(self):\n"
         L"        raise AssertionError('must not be called')\n"
         L"Index()\n"})
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation(context.thread());
        Value value = context.run_file(source);
        ASSERT_FALSE(value.is_exception_marker());
        NativeHandleRootRegion roots(context.thread());
        auto ctx = roots.make_context();
        auto handle = allocate_handle(&ctx, value);
        uint32_t number = 77;
        EXPECT_EQ(CLOVER_STATUS_ERROR,
                  clover_int_as_uint32_mask(&ctx, handle, &number));
        EXPECT_EQ(0u, number);
        expect_api_error(context, L"TypeError");
    }
}

TEST(ExtensionBytes, ReadonlyCallbackBorrowsStoredPayloadWithoutCopying)
{
    for(const wchar_t *source:
        {L"b''", L"bytearray()", L"b'\\x00\\x80\\xff'",
         L"bytearray(b'\\x00\\x80\\xff')",
         L"class B(bytes):\n"
         L"    def __len__(self):\n"
         L"        raise AssertionError('length override')\n"
         L"    def __getitem__(self, key):\n"
         L"        raise AssertionError('item override')\n"
         L"B(b'\\x00\\x80\\xff')\n"})
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation(context.thread());
        Value value = context.run_file(source);
        ASSERT_FALSE(value.is_exception_marker());
        NativeHandleRootRegion roots(context.thread());
        auto ctx = roots.make_context();
        auto handle = allocate_handle(&ctx, value);
        size_t size = 99;
        ASSERT_EQ(CLOVER_STATUS_OK, clover_byteslike_size(&ctx, handle, &size));
        ByteVisit visit{nullptr};
        {
            // Compare with actual VM storage during this non-allocating borrow.
            Value payload = resolve_handle(handle);
            auto view =
                has_native_layout<Bytes>(payload)
                    ? bytes_view(TValue<Bytes>::from_value_assumed(payload))
                    : payload.get_ptr<ByteArray>()->view();
            visit.expected_data = view.data();
            ASSERT_EQ(CLOVER_STATUS_OK, clover_byteslike_with_readonly_data(
                                            &ctx, handle, sum_bytes, &visit));
            visit.expected_data = nullptr;
        }
        EXPECT_EQ(1u, visit.calls);
        EXPECT_EQ(size, visit.size);
        EXPECT_EQ(size == 0 ? 0u : 383u, visit.sum);
        EXPECT_TRUE(visit.same_storage);
        // Allocate after the borrow, including handle-chunk growth. The input
        // handle must still resolve to live storage after these API calls.
        for(uint64_t i = 0; i < 80; ++i)
            (void)clover_int_from_uint64(&ctx, i);
        size_t after = 99;
        EXPECT_EQ(CLOVER_STATUS_OK,
                  clover_byteslike_size(&ctx, handle, &after));
        EXPECT_EQ(size, after);
        EXPECT_EQ(CLOVER_STATUS_OK, clover_byteslike_with_readonly_data(
                                        &ctx, handle, ignore_bytes, nullptr));
        EXPECT_FALSE(context.thread()->has_pending_exception());
    }
}

TEST(ExtensionBytes, InvalidPayloadsNeverInvokeCallback)
{
    for(const wchar_t *source: {L"None", L"1", L"1.0", L"'abc'", L"[]"})
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation(context.thread());
        Value value = context.run_file(source);
        ASSERT_FALSE(value.is_exception_marker());
        NativeHandleRootRegion roots(context.thread());
        auto ctx = roots.make_context();
        auto handle = allocate_handle(&ctx, value);
        size_t size = 99;
        EXPECT_EQ(CLOVER_STATUS_ERROR,
                  clover_byteslike_size(&ctx, handle, &size));
        EXPECT_EQ(0u, size);
        expect_api_error(context, L"TypeError");
        ByteVisit visit{nullptr};
        EXPECT_EQ(CLOVER_STATUS_ERROR, clover_byteslike_with_readonly_data(
                                           &ctx, handle, sum_bytes, &visit));
        EXPECT_EQ(0u, visit.calls);
        expect_api_error(context, L"TypeError");
    }
}

TEST(ExtensionBytes, InvalidCArgumentsAndExceptionMarkersPropagateErrors)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value value = context.run_file(L"b'abc'");
    ASSERT_FALSE(value.is_exception_marker());
    NativeHandleRootRegion roots(context.thread());
    auto ctx = roots.make_context();
    auto handle = allocate_handle(&ctx, value);
    EXPECT_EQ(CLOVER_STATUS_ERROR,
              clover_byteslike_size(&ctx, handle, nullptr));
    expect_api_error(context, L"ValueError");
    EXPECT_EQ(CLOVER_STATUS_ERROR,
              clover_int_as_uint32_mask(&ctx, handle, nullptr));
    expect_api_error(context, L"ValueError");
    EXPECT_EQ(CLOVER_STATUS_ERROR, clover_byteslike_with_readonly_data(
                                       &ctx, handle, nullptr, nullptr));
    expect_api_error(context, L"ValueError");
    size_t size = 99;
    uint32_t number = 77;
    EXPECT_EQ(CLOVER_STATUS_ERROR, clover_byteslike_size(&ctx, 0, &size));
    EXPECT_EQ(0u, size);
    expect_api_error(context, L"TypeError");
    if constexpr(native_handle_detail::cl_indirect_handles)
    {
        EXPECT_EQ(CLOVER_STATUS_ERROR,
                  clover_int_as_uint32_mask(&ctx, 0, &number));
        EXPECT_EQ(0u, number);
        expect_api_error(context, L"TypeError");
    }
    ByteVisit visit{nullptr};
    EXPECT_EQ(CLOVER_STATUS_ERROR,
              clover_byteslike_with_readonly_data(&ctx, 0, sum_bytes, &visit));
    EXPECT_EQ(0u, visit.calls);
    expect_api_error(context, L"TypeError");

    auto error = clover_raise_value_error(&ctx, "original error");
    auto original = context.thread()->pending_exception_object();
    EXPECT_EQ(CLOVER_STATUS_ERROR, clover_byteslike_size(&ctx, error, &size));
    EXPECT_EQ(CLOVER_STATUS_ERROR,
              clover_int_as_uint32_mask(&ctx, error, &number));
    EXPECT_EQ(CLOVER_STATUS_ERROR, clover_byteslike_with_readonly_data(
                                       &ctx, error, sum_bytes, &visit));
    EXPECT_EQ(original, context.thread()->pending_exception_object());
    EXPECT_EQ(0u, visit.calls);
    context.thread()->clear_pending_exception();

    // A sentinel without pending state must create an exception rather than
    // returning an unpaired failure status to the extension caller.
    EXPECT_EQ(CLOVER_STATUS_ERROR, clover_byteslike_size(&ctx, error, &size));
    expect_api_error(context, L"TypeError");
    EXPECT_EQ(CLOVER_STATUS_ERROR,
              clover_int_as_uint32_mask(&ctx, error, &number));
    expect_api_error(context, L"TypeError");
    EXPECT_EQ(CLOVER_STATUS_ERROR, clover_byteslike_with_readonly_data(
                                       &ctx, error, sum_bytes, &visit));
    EXPECT_EQ(0u, visit.calls);
    expect_api_error(context, L"TypeError");
}

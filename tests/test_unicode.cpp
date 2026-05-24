#include "str.h"
#include "test_helpers.h"
#include "unicode.h"
#include <gtest/gtest.h>

using namespace cl;

namespace
{
    std::wstring smiley_wstring()
    {
        if constexpr(sizeof(cl_wchar) == 2)
        {
            return std::wstring{static_cast<cl_wchar>(0xd83d),
                                static_cast<cl_wchar>(0xde42)};
        }
        return std::wstring{static_cast<cl_wchar>(0x1f642)};
    }
}  // namespace

TEST(Unicode, DecodeUtf8Ascii)
{
    std::optional<std::wstring> decoded = unicode::decode_utf8("answer");
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(L"answer", *decoded);
}

TEST(Unicode, DecodeUtf8Multibyte)
{
    std::optional<std::wstring> decoded =
        unicode::decode_utf8("\xce\xbb\xe2\x82\xac\xf0\x9f\x99\x82");
    ASSERT_TRUE(decoded.has_value());
    std::wstring expected = L"\u03bb\u20ac";
    expected += smiley_wstring();
    EXPECT_EQ(expected, *decoded);
}

TEST(Unicode, RejectsInvalidUtf8)
{
    EXPECT_FALSE(unicode::decode_utf8("\xc0\x80").has_value());
    EXPECT_FALSE(unicode::decode_utf8("\xe2\x82").has_value());
    EXPECT_FALSE(unicode::decode_utf8("\xed\xa0\x80").has_value());
    EXPECT_FALSE(unicode::decode_utf8("\xf4\x90\x80\x80").has_value());
}

TEST(Unicode, EncodeUtf8)
{
    std::wstring text = L"\u03bb\u20ac";
    text += smiley_wstring();
    EXPECT_EQ(std::string("\xce\xbb\xe2\x82\xac\xf0\x9f\x99\x82"),
              unicode::encode_utf8(text));
}

TEST(Unicode, DecodeIntoWchar)
{
    std::string_view bytes("\xce\xbb\xe2\x82\xac");
    std::optional<unicode::Utf8WcharLayout> layout =
        unicode::validate_utf8_for_wchar(bytes);
    ASSERT_TRUE(layout.has_value());
    std::wstring decoded(layout->code_unit_count, L'\0');
    ASSERT_TRUE(
        unicode::decode_utf8_into_wchar(bytes, decoded.data(), decoded.size()));
    EXPECT_EQ(L"\u03bb\u20ac", decoded);
}

TEST(Unicode, TryMakeStringFromUtf8)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    std::optional<TValue<String>> string =
        try_make_string_from_utf8(context.thread(), "\xce\xbb");
    ASSERT_TRUE(string.has_value());
    EXPECT_EQ(std::wstring(L"\u03bb"), std::wstring(string_view(*string)));
}

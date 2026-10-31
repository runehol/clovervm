#include "builtin_types/line_tab.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace cl;

TEST(LineTab, CheckedExpansionLimits)
{
    std::wstring source = L"a\t\r\n\t";
    auto view = std::span<const wchar_t>(source.data(), source.size());
    auto measured = expand_tabs<TabPass::Measure>(view, 4, 10);
    ASSERT_TRUE(measured);
    EXPECT_EQ(measured->size, 10u);
    EXPECT_TRUE(measured->has_tabs);
    EXPECT_FALSE(expand_tabs<TabPass::Measure>(view, 4, 9));
    std::vector<wchar_t> output(measured->size);
    ASSERT_TRUE(expand_tabs<TabPass::Write>(view, 4, 10, std::span(output)));
    EXPECT_EQ(std::wstring(output.begin(), output.end()), L"a   \r\n    ");
    std::wstring tabs = L"\t\t";
    view = {tabs.data(), tabs.size()};
    EXPECT_TRUE(
        expand_tabs<TabPass::Measure>(view, INT32_MAX, size_t(INT32_MAX) * 2));
    EXPECT_FALSE(expand_tabs<TabPass::Measure>(view, INT32_MAX,
                                               size_t(INT32_MAX) * 2 - 1));
    EXPECT_TRUE(expand_tabs<TabPass::Write>(view, 0, 0));
    EXPECT_TRUE(expand_tabs<TabPass::Measure>(view, INT32_MIN, 0));
    std::wstring single = L"x";
    EXPECT_FALSE(expand_tabs<TabPass::Measure>(
        std::span<const wchar_t>(single.data(), 1), 4, 0));
}

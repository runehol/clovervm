#include "heap_format.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{
    using namespace cl::gc::test::format;

    static_assert(!std::is_convertible_v<ObjectId, AllocationId>);
    static_assert(!std::is_convertible_v<UnitId, AllocationId>);

    std::string fixture(std::string_view name)
    {
        std::ifstream file("tests/gc_differential/fixtures/" +
                           std::string(name) + ".json");
        if(!file)
            throw std::runtime_error("cannot open heap fixture");
        return {std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>()};
    }
    std::string replace(std::string text, std::string_view from,
                        std::string_view to)
    {
        auto pos = text.find(from);
        if(pos == std::string::npos)
            throw std::runtime_error("missing test replacement");
        text.replace(pos, from.size(), to);
        return text;
    }
    const std::string empty_heap =
        R"({"format":"clover-gc-heap","version":2,"roots":[],"objects":[],"external_weak":[],"remembered_strong":[]})";

    TEST(GCHeapFormat, RememberedCoverageIsPartOfTheHeapContract)
    {
        auto heap = parse_heap(fixture("before"));
        auto bad = heap;
        bad.remembered_strong.clear();
        EXPECT_THROW(validate_heap(bad), FormatError);
        bad = heap;
        bad.remembered_strong.push_back({0});
        EXPECT_THROW(validate_heap(bad), FormatError);
        bad = heap;
        bad.remembered_strong.push_back({1});
        EXPECT_THROW(validate_heap(bad), FormatError);
        bad = heap;
        bad.remembered_strong.push_back({99});
        EXPECT_THROW(validate_heap(bad), FormatError);
        heap.remembered_strong.push_back({2});
        EXPECT_NO_THROW(validate_heap(heap));
    }

    TEST(GCHeapFormat, DocumentedFreshAndCompletedSnapshots)
    {
        auto before = parse_heap(fixture("before"));
        auto after = parse_heap(fixture("after"));
        EXPECT_NO_THROW(validate_fresh_heap(before));
        EXPECT_THROW(validate_fresh_heap(after), FormatError);
        ASSERT_EQ(before.objects.size(), 3);
        ASSERT_EQ(after.objects.size(), 4);
        EXPECT_EQ(before.objects[1].object_id, after.objects[3].object_id);
        EXPECT_EQ(before.roots,
                  (std::vector<Cell>{Reference{AllocationId{1}},
                                     Reference{AllocationId{1}}}));
        EXPECT_EQ(after.roots, (std::vector<Cell>{Reference{AllocationId{3}},
                                                  Reference{AllocationId{3}}}));
        EXPECT_EQ(before.objects[0].state,
                  ObjectState(NonmovingOriginal{false}));
        EXPECT_EQ(after.objects[0].state, ObjectState(NonmovingOriginal{true}));
        EXPECT_EQ(before.objects[1].state,
                  (ObjectState(
                      YoungOriginal{CopyTo::Ordinary, NormalHeader{false}})));
        EXPECT_EQ(
            before.objects[2].state,
            (ObjectState(OldOriginal{CopyTo::Nonmoving, NormalHeader{false}})));
        EXPECT_EQ(after.objects[3].state, ObjectState(OrdinaryDestination{}));
        EXPECT_EQ(before.objects[1].strong,
                  (std::vector<Cell>{Reference{AllocationId{0}}, int64_t{7}}));
        EXPECT_EQ(before.objects[1].weak,
                  (std::vector<WeakUnit>{{UnitId{0}, Field{int64_t{-7}}}}));
        EXPECT_EQ(before.external_weak,
                  (std::vector<WeakUnit>{
                      {UnitId{1}, Entry{{AllocationId{1}, AllocationId{0}},
                                        Reference{AllocationId{2}}}}}));
        EXPECT_EQ(
            after.external_weak,
            (std::vector<WeakUnit>{
                {UnitId{1}, Entry{{std::nullopt, std::nullopt}, int64_t{0}}}}));
        EXPECT_EQ((std::get<ForwardedHeader>(
                       std::get<YoungOriginal>(after.objects[1].state).header)
                       .forwarded_to),
                  AllocationId{3});
        EXPECT_EQ(parse_heap(write_heap(before)), before);
        EXPECT_EQ(parse_heap(write_heap(after)), after);
        EXPECT_EQ(write_heap(parse_heap(write_heap(after))), write_heap(after));
    }

    TEST(GCHeapFormat, EmptyHeapAndIntegerExtremesKeepCellTags)
    {
        auto extremes = replace(
            empty_heap, "\"roots\":[]",
            R"("roots":[{"imm":-9223372036854775808},{"imm":9223372036854775807}])");
        EXPECT_EQ(parse_heap(extremes).roots,
                  (std::vector<Cell>{std::numeric_limits<int64_t>::min(),
                                     std::numeric_limits<int64_t>::max()}));
        auto heap = parse_heap(empty_heap);
        heap.roots = {std::numeric_limits<int64_t>::min(),
                      std::numeric_limits<int64_t>::max(), int64_t{0},
                      Reference{}};
        EXPECT_EQ(parse_heap(write_heap(heap)), heap);
        EXPECT_NE(heap.roots[2], heap.roots[3]);
        EXPECT_NO_THROW(validate_fresh_heap(heap));
    }

    TEST(GCHeapFormat, AllStateVariantsAndIndependentIdentityPairing)
    {
        auto heap = parse_heap(fixture("after"));
        heap.objects[0].object_id = ObjectId{"obj-2"};
        heap.objects[2].object_id = ObjectId{"obj-0"};
        std::get<YoungOriginal>(heap.objects[1].state).copy_to =
            CopyTo::Nonmoving;
        heap.objects[3].state = NonmovingDestination{};
        auto expected = heap;
        std::reverse(heap.objects.begin(), heap.objects.end());
        EXPECT_EQ(parse_heap(write_heap(heap)), expected);
        // Forwarding is also valid on an old movable original.
        heap.objects[2].state =
            OldOriginal{CopyTo::Nonmoving, ForwardedHeader{AllocationId{3}}};
        expected.objects[1].state =
            OldOriginal{CopyTo::Nonmoving, ForwardedHeader{AllocationId{3}}};
        EXPECT_EQ(parse_heap(write_heap(heap)), expected);
        // Also accept noncanonical record ordering on input.
        auto text = write_heap(heap);
        auto first = text.find("\"allocation_id\": 0");
        auto last = text.find("\"allocation_id\": 2");
        ASSERT_NE(first, std::string::npos);
        ASSERT_NE(last, std::string::npos);
        text[first + std::string("\"allocation_id\": ").size()] = '2';
        text[last + std::string("\"allocation_id\": ").size()] = '0';
        auto parsed = parse_heap(text);
        EXPECT_EQ(parsed.objects[0].object_id, ObjectId{"obj-0"});
        EXPECT_EQ(parsed.objects[2].object_id, ObjectId{"obj-2"});
    }

    TEST(GCHeapFormat,
         PreservesSlotAndExternalOrderDuplicatesAndInactiveEntries)
    {
        auto heap = parse_heap(fixture("before"));
        heap.roots = {Reference{AllocationId{1}}, int64_t{-7}, Reference{},
                      Reference{AllocationId{1}}};
        heap.objects[1].strong = heap.roots;
        heap.external_weak.push_back({UnitId{2}, Field{Reference{}}});
        heap.external_weak.push_back(
            {UnitId{3}, Entry{{std::nullopt, AllocationId{1}}, int64_t{42}}});
        EXPECT_EQ(parse_heap(write_heap(heap)), heap);
        EXPECT_NO_THROW(validate_fresh_heap(heap));
    }

    TEST(GCHeapFormat, RejectsNonJsonAndInformationLosingParsing)
    {
        const std::vector<std::string> bad = {
            "",
            "null",
            "[]",
            empty_heap + "{}",
            empty_heap + std::string("\0{}", 3),
            empty_heap + std::string(1, '\0'),
            "\xef\xbb\xbf" + empty_heap,
            replace(empty_heap, "\"version\":2", "\"version\":2,\"version\":2"),
            replace(empty_heap, "\"version\":2",
                    "\"version\":2,\"ver\\u0073ion\":1"),
            replace(empty_heap, "\"version\":2", "\"version\":2.0"),
            replace(empty_heap, "\"version\":2", "\"version\":2e0"),
            replace(empty_heap, "\"version\":2", "\"version\":true"),
            replace(empty_heap, "\"version\":2", "\"version\":01"),
            replace(empty_heap, "\"version\":2",
                    "\"version\":28446744073709551616"),
            replace(empty_heap, "\"roots\":[]",
                    "\"roots\":[{\"imm\":-9223372036854775809}]"),
            replace(empty_heap, "\"roots\":[]",
                    "\"roots\":[{\"imm\":9223372036854775808}]"),
            replace(empty_heap, "\"roots\":[]", "\"roots\":[{\"imm\":1E0}]"),
            replace(empty_heap, "\"roots\":[]", "\"roots\":[{\"imm\":true}]"),
            replace(empty_heap, "\"roots\":[]", "\"roots\":[{\"ref\":-1}]"),
            replace(empty_heap, "\"roots\":[]",
                    "\"roots\":[{\"ref\":\"obj-0\"}]"),
            replace(empty_heap, "\"roots\":[]",
                    "\"roots\":[{\"ref\":null,\"ref\":null}]"),
            replace(empty_heap, "\"roots\":[]",
                    "\"roots\":[{\"imm\":0,\"ref\":null}]"),
            replace(empty_heap, "\"roots\":[]",
                    "\"roots\":[{\"ref\":null,\"extra\":0}]"),
            replace(empty_heap, "\"roots\":[]", "\"roots\":{},"),
            replace(empty_heap, "\"version\":2", "\"version\":/*comment*/1"),
            replace(empty_heap, "\"version\":2", "\"version\":3"),
            replace(empty_heap, "\"version\":2", "\"version\":2,\"extra\":0"),
            replace(empty_heap, "\"version\":2,", ""),
            replace(empty_heap, "\"external_weak\":[]",
                    "\"external_weak\":[],"),
            replace(empty_heap, "clover-gc-heap", std::string("bad\xff", 4)),
        };
        for(const auto &text: bad)
        {
            SCOPED_TRACE(text);
            EXPECT_THROW(parse_heap(text), FormatError);
        }
    }

    TEST(GCHeapFormat, RejectsMalformedStatesAndUnits)
    {
        auto before = fixture("before");
        const std::vector<std::string> bad = {
            replace(before, "young_original", "unknown"),
            replace(before, "ordinary", "unknown"),
            replace(before, "normal", "unknown"),
            replace(before, "\"marked\": false", "\"marked\": 0"),
            replace(before, "\"kind\": \"normal\"",
                    "\"kind\": \"normal\", \"forwarded_to\": 0"),
            replace(before, "\"kind\": \"field\"", "\"kind\": \"unknown\""),
            replace(before, "\"kind\": \"entry\"", "\"kind\": \"field\""),
            replace(before, "\"guards\": [\n        1,\n        0\n      ]",
                    "\"guards\": [1]"),
            replace(before, "\"guards\": [\n        1,\n        0\n      ]",
                    "\"guards\": [1,0,1]"),
            replace(before, "\"guards\": [\n        1,\n        0\n      ]",
                    "\"guards\": [true,0]"),
            replace(fixture("after"), "\"kind\": \"ordinary_destination\"",
                    "\"kind\": \"ordinary_destination\", \"marked\": true"),
            replace(fixture("after"), "\"forwarded_to\": 3",
                    "\"forwarded_to\": null"),
        };
        for(const auto &text: bad)
        {
            SCOPED_TRACE(text);
            EXPECT_THROW(parse_heap(text), FormatError);
        }
        for(const auto &id: {"obj-00", "obj-", "obj--1", "obj-+1", "obj- 1",
                             "obj-1x", "obj-18446744073709551616", "other-0"})
            EXPECT_THROW(parse_heap(replace(before, "obj-0", id)), FormatError);
    }

    TEST(GCHeapFormat, RejectsBrokenIdDomainsAndReferences)
    {
        auto base = parse_heap(fixture("before"));
        auto check = [&](auto mutate) {
            auto h = base;
            mutate(h);
            EXPECT_THROW(validate_heap(h), FormatError);
            EXPECT_THROW(write_heap(h), FormatError);
        };
        check([](auto &h) { h.objects[0].allocation_id = AllocationId{3}; });
        check([](auto &h) { h.objects[1].allocation_id = AllocationId{0}; });
        check([](auto &h) { h.objects[0].object_id = ObjectId{"obj-3"}; });
        check([](auto &h) { h.objects[0].object_id = ObjectId{"obj-1"}; });
        check([](auto &h) { h.roots[0] = Reference{AllocationId{3}}; });
        check([](auto &h) {
            h.objects[1].strong[0] = Reference{AllocationId{UINT64_MAX}};
        });
        check([](auto &h) {
            std::get<Entry>(h.external_weak[0].contents).guards[0] =
                AllocationId{3};
        });
        check([](auto &h) {
            std::get<Entry>(h.external_weak[0].contents).payload =
                Reference{AllocationId{3}};
        });
        check([](auto &h) {
            std::get<Field>(h.objects[1].weak[0].contents).value =
                Reference{AllocationId{3}};
        });
        check([](auto &h) { h.external_weak[0].unit_id = UnitId{0}; });
        check([](auto &h) { h.external_weak[0].unit_id = UnitId{2}; });
        check(
            [](auto &h) { h.objects[1].weak[0].unit_id = UnitId{UINT64_MAX}; });
        check([](auto &h) {
            h.objects[1].weak.push_back(
                {UnitId{2}, Entry{{std::nullopt, std::nullopt}, int64_t{0}}});
            h.objects[1].weak.push_back({UnitId{3}, Field{int64_t{0}}});
        });
    }

    TEST(GCHeapFormat, RejectsBrokenCopyRelationships)
    {
        auto base = parse_heap(fixture("after"));
        auto check = [&](auto mutate) {
            auto h = base;
            mutate(h);
            EXPECT_THROW(validate_heap(h), FormatError);
        };
        check([](auto &h) {
            std::get<YoungOriginal>(h.objects[1].state).header =
                NormalHeader{false};
        });
        check([](auto &h) {
            std::get<ForwardedHeader>(
                std::get<YoungOriginal>(h.objects[1].state).header)
                .forwarded_to = AllocationId{0};
        });
        check([](auto &h) { h.objects[3].object_id = ObjectId{"obj-2"}; });
        check([](auto &h) { h.objects[3].state = NonmovingDestination{}; });
        check([](auto &h) { h.objects[3].weak.clear(); });
        check([](auto &h) { h.objects[3].weak[0].unit_id = UnitId{1}; });
        check([](auto &h) {
            h.objects[3].weak[0].contents =
                Entry{{std::nullopt, std::nullopt}, int64_t{0}};
        });
        check([](auto &h) { h.objects[1].state = NonmovingOriginal{false}; });
        check([](auto &h) {
            h.objects[3].allocation_id = AllocationId{1};
            h.objects[1].allocation_id = AllocationId{3};
        });
        check([](auto &h) {
            h.objects.push_back(h.objects[3]);
            h.objects.back().allocation_id = AllocationId{4};
        });
        check([](auto &h) { h.objects.pop_back(); });
    }

    TEST(GCHeapFormat, FreshValidationRejectsMarksButGeneralParsingAcceptsThem)
    {
        auto heap = parse_heap(fixture("before"));
        std::get<NonmovingOriginal>(heap.objects[0].state).marked = true;
        EXPECT_NO_THROW(parse_heap(write_heap(heap)));
        EXPECT_THROW(validate_fresh_heap(heap), FormatError);
        std::get<NonmovingOriginal>(heap.objects[0].state).marked = false;
        std::get<YoungOriginal>(heap.objects[1].state).header =
            NormalHeader{true};
        EXPECT_NO_THROW(parse_heap(write_heap(heap)));
        EXPECT_THROW(validate_fresh_heap(heap), FormatError);
        std::get<YoungOriginal>(heap.objects[1].state).header =
            NormalHeader{false};
        std::get<OldOriginal>(heap.objects[2].state).header =
            NormalHeader{true};
        EXPECT_THROW(validate_fresh_heap(heap), FormatError);
    }
}  // namespace

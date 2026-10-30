#include "codecs/codec.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <gtest/gtest.h>
#include <limits>
#include <string>
#include <vector>

extern "C" int cl_test_codec_c(void);

namespace
{
    constexpr ClCodecBytes empty{nullptr, 0};

    ClCodecBytes bytes(const std::string &s)
    {
        return {reinterpret_cast<const unsigned char *>(s.data()), s.size()};
    }

    void same_result(ClCodecResult a, ClCodecResult b)
    {
        EXPECT_EQ(a.status, b.status);
        EXPECT_EQ(a.input_consumed, b.input_consumed);
        EXPECT_EQ(a.output_count, b.output_count);
        EXPECT_EQ(a.error, b.error);
        EXPECT_EQ(a.error_length, b.error_length);
    }
#if defined(__x86_64__)
    int unavailable_backend()
    {
        if(setenv("SIMDUTF_FORCE_IMPLEMENTATION", "clover-missing-backend", 1))
            return 1;
        const unsigned char input[] = {'A'};
        char32_t words[] = {U'A'};
        unsigned char output[4]{};
        auto d = cl_codec_decode(CL_CODEC_UTF8, empty, {input, 1}, words, 1);
        auto m = cl_codec_measure_decode(CL_CODEC_UTF8, empty, {input, 1});
        auto e = cl_codec_encode(CL_CODEC_UTF8, words, 1, output, 4);
        auto n = cl_codec_measure_encode(CL_CODEC_UTF8, words, 1);
        auto l = cl_codec_decode(CL_CODEC_LATIN1, empty, {input, 1}, words, 1);
        auto w = cl_codec_encode(CL_CODEC_LATIN1, words, 1, output, 1);
        for(auto result: {d, m, e, n, l, w})
            if(result.status != CL_CODEC_BACKEND_ERROR ||
               result.input_consumed || result.output_count ||
               result.error != CL_CODEC_NO_ERROR)
                return 2;
        return 0;
    }
#endif
}  // namespace

TEST(Codec, CInterface) { EXPECT_EQ(cl_test_codec_c(), 0); }

TEST(Codec, EmptyAndArguments)
{
    for(auto encoding: {CL_CODEC_UTF8, CL_CODEC_LATIN1})
    {
        EXPECT_EQ(cl_codec_decode(encoding, empty, empty, nullptr, 0).status,
                  CL_CODEC_COMPLETE);
        EXPECT_EQ(cl_codec_encode(encoding, nullptr, 0, nullptr, 0).status,
                  CL_CODEC_COMPLETE);
        EXPECT_EQ(cl_codec_measure_decode(encoding, empty, empty).status,
                  CL_CODEC_COMPLETE);
        EXPECT_EQ(cl_codec_measure_encode(encoding, nullptr, 0).status,
                  CL_CODEC_COMPLETE);
        EXPECT_EQ(cl_codec_measure_decode(encoding, empty, {nullptr, 1}).status,
                  CL_CODEC_INVALID_ARGUMENT);
        EXPECT_EQ(cl_codec_measure_encode(encoding, nullptr, 1).status,
                  CL_CODEC_INVALID_ARGUMENT);
    }
    const unsigned char byte = 'a';
    char32_t word = U'a';
    EXPECT_EQ(
        cl_codec_decode(CL_CODEC_UTF8, empty, {&byte, 1}, nullptr, 1).status,
        CL_CODEC_INVALID_ARGUMENT);
    EXPECT_EQ(cl_codec_encode(CL_CODEC_UTF8, &word, 1, nullptr, 4).status,
              CL_CODEC_INVALID_ARGUMENT);
    EXPECT_EQ(
        cl_codec_measure_decode(static_cast<ClCodecEncoding>(99), empty, empty)
            .status,
        CL_CODEC_INVALID_ARGUMENT);
    EXPECT_EQ(
        cl_codec_measure_encode(static_cast<ClCodecEncoding>(99), nullptr, 0)
            .status,
        CL_CODEC_INVALID_ARGUMENT);
    EXPECT_EQ(cl_codec_measure_decode(CL_CODEC_UTF8, {&byte, 1}, empty).status,
              CL_CODEC_INVALID_ARGUMENT);
    EXPECT_EQ(cl_codec_measure_decode(CL_CODEC_UTF8, {&byte, 4}, empty).status,
              CL_CODEC_INVALID_ARGUMENT);
    EXPECT_EQ(
        cl_codec_measure_decode(CL_CODEC_LATIN1, {&byte, 1}, empty).status,
        CL_CODEC_INVALID_ARGUMENT);
}

TEST(Codec, CapacityAndOverflowRejectWithoutWriting)
{
    const std::string input = "\xf0\x9f\x98\x80";
    std::array<char32_t, 6> words;
    words.fill(0x12345);
    for(size_t capacity = 0; capacity < input.size(); ++capacity)
    {
        auto r = cl_codec_decode(CL_CODEC_UTF8, empty, bytes(input),
                                 words.data() + 1, capacity);
        EXPECT_EQ(r.status, CL_CODEC_INSUFFICIENT_OUTPUT);
        EXPECT_EQ(r.input_consumed, 0);
        EXPECT_EQ(r.output_count, 0);
        EXPECT_TRUE(std::all_of(words.begin(), words.end(),
                                [](char32_t w) { return w == 0x12345; }));
    }
    auto r = cl_codec_decode(CL_CODEC_UTF8, empty, bytes(input),
                             words.data() + 1, 4);
    EXPECT_EQ(r.status, CL_CODEC_COMPLETE);
    EXPECT_EQ(r.output_count, 1);
    EXPECT_EQ(words[0], 0x12345);
    EXPECT_EQ(words[1], 0x1f600);
    EXPECT_EQ(words[5], 0x12345);
    std::array<unsigned char, 6> out;
    out.fill(0xa5);
    for(size_t capacity = 0; capacity < 4; ++capacity)
    {
        auto encoded = cl_codec_encode(CL_CODEC_UTF8, &words[1], 1,
                                       out.data() + 1, capacity);
        EXPECT_EQ(encoded.status, CL_CODEC_INSUFFICIENT_OUTPUT);
        EXPECT_EQ(encoded.input_consumed, 0);
        EXPECT_TRUE(std::all_of(out.begin(), out.end(),
                                [](unsigned char b) { return b == 0xa5; }));
    }
    auto encoded =
        cl_codec_encode(CL_CODEC_UTF8, &words[1], 1, out.data() + 1, 4);
    EXPECT_EQ(encoded.status, CL_CODEC_COMPLETE);
    EXPECT_EQ(out[0], 0xa5);
    EXPECT_EQ(out[5], 0xa5);
    EXPECT_EQ(std::string(reinterpret_cast<char *>(out.data() + 1), 4), input);
    const unsigned char pending[] = {0xf0};
    auto maximum = std::numeric_limits<size_t>::max();
    EXPECT_EQ(
        cl_codec_measure_decode(CL_CODEC_UTF8, {pending, 1}, {pending, maximum})
            .status,
        CL_CODEC_SIZE_OVERFLOW);
    EXPECT_EQ(
        cl_codec_measure_encode(CL_CODEC_UTF8, words.data(), maximum / 4 + 1)
            .status,
        CL_CODEC_SIZE_OVERFLOW);
    EXPECT_EQ(cl_codec_encode(CL_CODEC_UTF8, words.data(), maximum / 4 + 1,
                              out.data(), maximum)
                  .status,
              CL_CODEC_SIZE_OVERFLOW);
}

TEST(Codec, Utf8SplitsAndProgress)
{
    const std::string values[] = {"\xc2\x80", "\xe0\xa0\x80", "\xed\x9f\xbf",
                                  "\xf0\x90\x80\x80", "\xf4\x8f\xbf\xbf"};
    for(const auto &s: values)
    {
        for(size_t split = 1; split < s.size(); ++split)
        {
            SCOPED_TRACE(split);
            auto all = s + "X\xe2\x82";
            auto input = bytes(all);
            char32_t output[16]{};
            auto prefix = cl_codec_decode(CL_CODEC_UTF8, empty,
                                          {input.data, split}, output, split);
            EXPECT_EQ(prefix.status, CL_CODEC_INCOMPLETE_INPUT);
            EXPECT_EQ(prefix.input_consumed, 0);
            EXPECT_EQ(prefix.error_length, split);
            auto measured = cl_codec_measure_decode(
                CL_CODEC_UTF8, {input.data, split},
                {input.data + split, input.size - split});
            auto r = cl_codec_decode(CL_CODEC_UTF8, {input.data, split},
                                     {input.data + split, input.size - split},
                                     output, input.size);
            same_result(measured, r);
            EXPECT_EQ(r.status, CL_CODEC_INCOMPLETE_INPUT);
            EXPECT_EQ(r.input_consumed, s.size() + 1);
            EXPECT_EQ(r.output_count, 2);
            EXPECT_EQ(r.error_length, 2);
            EXPECT_EQ(output[1], U'X');
            auto missing = cl_codec_measure_decode(CL_CODEC_UTF8,
                                                   {input.data, split}, empty);
            EXPECT_EQ(missing.status, CL_CODEC_INCOMPLETE_INPUT);
            EXPECT_EQ(missing.error_length, split);
        }
    }
}

#if defined(__x86_64__)
TEST(CodecDeathTest, UnavailableBackendDoesNotMasqueradeAsUnicodeError)
{
    // x86 chooses its implementation at runtime. Re-exec so earlier tests
    // cannot have initialized simdutf before the child's environment changes.
    auto previous = GTEST_FLAG_GET(death_test_style);
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_EXIT(std::_Exit(unavailable_backend()), ::testing::ExitedWithCode(0),
                "");
    GTEST_FLAG_SET(death_test_style, previous);
}
#endif

TEST(Codec, Utf8ErrorsMatchMeasurementsAndSplitBoundaries)
{
    struct Case
    {
        std::string input;
        ClCodecStatus status;
        ClCodecError error;
        size_t length;
    };
    const Case cases[] = {
        {"\x80", CL_CODEC_INVALID_INPUT, CL_CODEC_INVALID_START_BYTE, 1},
        {"\xc0\x80", CL_CODEC_INVALID_INPUT, CL_CODEC_INVALID_START_BYTE, 1},
        {"\xf5\x80\x80\x80", CL_CODEC_INVALID_INPUT,
         CL_CODEC_INVALID_START_BYTE, 1},
        {"\xe0\x80", CL_CODEC_INVALID_INPUT, CL_CODEC_INVALID_CONTINUATION_BYTE,
         1},
        {"\xed\xa0", CL_CODEC_INCOMPLETE_INPUT,
         CL_CODEC_INVALID_CONTINUATION_BYTE, 1},
        {"\xed\xa0\x80", CL_CODEC_INVALID_INPUT,
         CL_CODEC_INVALID_CONTINUATION_BYTE, 1},
        {"\xf4\x90\x80\x80", CL_CODEC_INVALID_INPUT,
         CL_CODEC_INVALID_CONTINUATION_BYTE, 1},
        {"\xe2X", CL_CODEC_INVALID_INPUT, CL_CODEC_INVALID_CONTINUATION_BYTE,
         1},
        {"\xf0\x90X", CL_CODEC_INVALID_INPUT,
         CL_CODEC_INVALID_CONTINUATION_BYTE, 2},
        {"\xf0\x90\x80X", CL_CODEC_INVALID_INPUT,
         CL_CODEC_INVALID_CONTINUATION_BYTE, 3},
        {"\xe2", CL_CODEC_INCOMPLETE_INPUT, CL_CODEC_UNEXPECTED_END, 1},
        {"\xf0\x90\x80", CL_CODEC_INCOMPLETE_INPUT, CL_CODEC_UNEXPECTED_END,
         3}};
    for(const auto &c: cases)
    {
        auto s = std::string("A\xc2\xa2") + c.input;
        char32_t out[32]{};
        auto r = cl_codec_decode(CL_CODEC_UTF8, empty, bytes(s), out, s.size());
        same_result(r, cl_codec_measure_decode(CL_CODEC_UTF8, empty, bytes(s)));
        EXPECT_EQ(r.status, c.status);
        EXPECT_EQ(r.error, c.error);
        EXPECT_EQ(r.input_consumed, 3);
        EXPECT_EQ(r.output_count, 2);
        EXPECT_EQ(r.error_length, c.length);
        EXPECT_EQ(out[0], U'A');
        EXPECT_EQ(out[1], U'\u00a2');
        for(size_t split = 1; split < c.input.size() && split <= 3; ++split)
        {
            auto span = bytes(c.input);
            auto pending = cl_codec_measure_decode(CL_CODEC_UTF8, empty,
                                                   {span.data, split});
            if(pending.status != CL_CODEC_INCOMPLETE_INPUT)
                continue;
            auto joined = cl_codec_decode(
                CL_CODEC_UTF8, {span.data, split},
                {span.data + split, span.size - split}, out, span.size);
            EXPECT_EQ(joined.status, c.status);
            EXPECT_EQ(joined.error, c.error);
            EXPECT_EQ(joined.error_length, c.length);
            EXPECT_EQ(joined.input_consumed, 0);
        }
    }
}

TEST(Codec, Latin1AndInvalidWords)
{
    std::array<unsigned char, 256> input;
    for(size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<unsigned char>(i);
    std::array<char32_t, 256> words{};
    auto r =
        cl_codec_decode(CL_CODEC_LATIN1, empty, {input.data(), input.size()},
                        words.data(), words.size());
    same_result(r, cl_codec_measure_decode(CL_CODEC_LATIN1, empty,
                                           {input.data(), input.size()}));
    ASSERT_EQ(r.status, CL_CODEC_COMPLETE);
    for(size_t i = 0; i < words.size(); ++i)
        EXPECT_EQ(words[i], i);
    std::array<unsigned char, 256> encoded{};
    auto e = cl_codec_encode(CL_CODEC_LATIN1, words.data(), words.size(),
                             encoded.data(), encoded.size());
    same_result(e, cl_codec_measure_encode(CL_CODEC_LATIN1, words.data(),
                                           words.size()));
    EXPECT_EQ(input, encoded);
    for(char32_t invalid: {char32_t(0x100), char32_t(0xd800), char32_t(0xdfff),
                           char32_t(0x110000), char32_t(0xffffffff)})
    {
        char32_t source[] = {U'A', invalid, U'B'};
        unsigned char output[12]{};
        for(auto encoding: {CL_CODEC_UTF8, CL_CODEC_LATIN1})
        {
            auto result =
                cl_codec_encode(encoding, source, 3, output, sizeof(output));
            same_result(result, cl_codec_measure_encode(encoding, source, 3));
            if(encoding == CL_CODEC_UTF8 && invalid == 0x100)
                EXPECT_EQ(result.status, CL_CODEC_COMPLETE);
            else
            {
                EXPECT_EQ(result.status, CL_CODEC_INVALID_INPUT);
                EXPECT_EQ(result.input_consumed, 1);
                EXPECT_EQ(result.output_count, 1);
                EXPECT_EQ(result.error_length, 1);
                EXPECT_EQ(output[0], 'A');
            }
        }
    }
}

TEST(Codec, BulkConversionsAndErrorsAcrossVectorBoundaries)
{
    for(size_t size:
        {1u, 15u, 16u, 17u, 31u, 32u, 33u, 63u, 64u, 65u, 129u, 1024u})
    {
        SCOPED_TRACE(size);
        const char32_t alphabet[] = {0,      U'A',   0xff,    0x100,
                                     0x20ac, 0xfeff, 0x1f600, 0x10ffff};
        std::vector<char32_t> words(size);
        for(size_t i = 0; i < size; ++i)
            words[i] = alphabet[i % std::size(alphabet)];
        std::vector<unsigned char> encoded(size * 4 + 2, 0xa5);
        auto e = cl_codec_encode(CL_CODEC_UTF8, words.data(), size,
                                 encoded.data() + 1, size * 4);
        same_result(e,
                    cl_codec_measure_encode(CL_CODEC_UTF8, words.data(), size));
        ASSERT_EQ(e.status, CL_CODEC_COMPLETE);
        EXPECT_EQ(encoded.front(), 0xa5);
        EXPECT_EQ(encoded.back(), 0xa5);
        std::vector<char32_t> decoded(e.output_count + 2, 0x12345);
        auto d = cl_codec_decode(CL_CODEC_UTF8, empty,
                                 {encoded.data() + 1, e.output_count},
                                 decoded.data() + 1, e.output_count);
        same_result(
            d, cl_codec_measure_decode(CL_CODEC_UTF8, empty,
                                       {encoded.data() + 1, e.output_count}));
        EXPECT_EQ(d.output_count, size);
        EXPECT_TRUE(
            std::equal(words.begin(), words.end(), decoded.begin() + 1));
        EXPECT_EQ(decoded.front(), 0x12345);
        EXPECT_EQ(decoded.back(), 0x12345);
        // An invalid word after a long valid prefix exercises SIMD error
        // positioning as well as the scalar/tail paths.
        words.push_back(0xd800);
        encoded.resize(words.size() * 4 + 2, 0xa5);
        e = cl_codec_encode(CL_CODEC_UTF8, words.data(), words.size(),
                            encoded.data() + 1, words.size() * 4);
        same_result(e, cl_codec_measure_encode(CL_CODEC_UTF8, words.data(),
                                               words.size()));
        EXPECT_EQ(e.input_consumed, size);
        EXPECT_EQ(e.error, CL_CODEC_SURROGATE);
        std::string malformed(reinterpret_cast<char *>(encoded.data() + 1),
                              e.output_count);
        malformed += "\xe2X";
        decoded.resize(malformed.size() + 2, 0x12345);
        d = cl_codec_decode(CL_CODEC_UTF8, empty, bytes(malformed),
                            decoded.data() + 1, malformed.size());
        same_result(
            d, cl_codec_measure_decode(CL_CODEC_UTF8, empty, bytes(malformed)));
        EXPECT_EQ(d.status, CL_CODEC_INVALID_INPUT);
        EXPECT_EQ(d.input_consumed, e.output_count);
        EXPECT_EQ(d.output_count, size);
        EXPECT_EQ(d.error_length, 1);
        std::vector<char32_t> latin(size, U'A');
        latin.push_back(0x100);
        encoded.assign(latin.size() + 2, 0xa5);
        auto l = cl_codec_encode(CL_CODEC_LATIN1, latin.data(), latin.size(),
                                 encoded.data() + 1, latin.size());
        same_result(l, cl_codec_measure_encode(CL_CODEC_LATIN1, latin.data(),
                                               latin.size()));
        EXPECT_EQ(l.status, CL_CODEC_INVALID_INPUT);
        EXPECT_EQ(l.input_consumed, size);
        EXPECT_EQ(l.output_count, size);
        EXPECT_EQ(l.error, CL_CODEC_UNREPRESENTABLE);
        EXPECT_EQ(encoded.front(), 0xa5);
        EXPECT_EQ(encoded.back(), 0xa5);
    }
}

TEST(Codec, DiscontiguousPendingAndIncoming)
{
    // These are distinct objects of precisely their advertised lengths. The
    // adapter cannot read a full code point by extending either input pointer.
    const unsigned char pending[] = {0xf0, 0x9f};
    const unsigned char incoming[] = {0x98, 0x80, 'X'};
    char32_t output[7]{};
    auto r =
        cl_codec_decode(CL_CODEC_UTF8, {pending, 2}, {incoming, 3}, output, 5);
    EXPECT_EQ(r.status, CL_CODEC_COMPLETE);
    EXPECT_EQ(r.input_consumed, 5);
    EXPECT_EQ(r.output_count, 2);
    EXPECT_EQ(output[0], 0x1f600);
    EXPECT_EQ(output[1], U'X');
    const unsigned char missing[] = {0x98};
    r = cl_codec_decode(CL_CODEC_UTF8, {pending, 2}, {missing, 1}, output, 3);
    EXPECT_EQ(r.status, CL_CODEC_INCOMPLETE_INPUT);
    EXPECT_EQ(r.input_consumed, 0);
    EXPECT_EQ(r.error_length, 3);
    const unsigned char invalid[] = {'X'};
    r = cl_codec_decode(CL_CODEC_UTF8, {pending, 2}, {invalid, 1}, output, 3);
    EXPECT_EQ(r.status, CL_CODEC_INVALID_INPUT);
    EXPECT_EQ(r.error_length, 2);
}

TEST(Codec, AllUnicodeScalarsRoundTrip)
{
    for(char32_t word = 0; word <= 0x10ffff; ++word)
    {
        if(word >= 0xd800 && word <= 0xdfff)
            continue;
        unsigned char encoded[4]{};
        char32_t decoded[4]{};
        auto e = cl_codec_encode(CL_CODEC_UTF8, &word, 1, encoded, 4);
        auto d = cl_codec_decode(CL_CODEC_UTF8, empty,
                                 {encoded, e.output_count}, decoded, 4);
        if(e.status != CL_CODEC_COMPLETE || d.status != CL_CODEC_COMPLETE ||
           d.input_consumed != e.output_count || d.output_count != 1 ||
           decoded[0] != word)
        {
            FAIL() << "Unicode scalar " << static_cast<uint32_t>(word);
        }
    }
}

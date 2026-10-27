#include "codecs/incremental_codec.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <random>
#include <string>
#include <vector>

namespace
{
    std::span<const unsigned char> bytes(const std::string &input)
    {
        return {reinterpret_cast<const unsigned char *>(input.data()),
                input.size()};
    }

    std::u32string words(const cl::DecodeChunk &result)
    {
        return {result.output.begin(), result.output.end()};
    }

    std::string encoded(const cl::EncodeChunk &result)
    {
        return {result.output.begin(), result.output.end()};
    }

    std::vector<unsigned char> pending(const cl::CodecDecoder &decoder)
    {
        return {decoder.pending().begin(), decoder.pending().end()};
    }
}  // namespace

TEST(IncrementalCodec, AllCharacterSplits)
{
    for(const std::string &input:
        {std::string("A"), std::string("\xc2\xa3"), std::string("\xe2\x82\xac"),
         std::string("\xf0\x9f\x98\x80")})
    {
        for(size_t split = 0; split <= input.size(); ++split)
        {
            cl::CodecDecoder decoder(CL_CODEC_UTF8);
            auto a =
                decoder.decode(bytes(input).first(split), cl::InputEnd::More);
            ASSERT_EQ(a.input_accepted, split);
            auto output = words(a);
            auto b = decoder.decode(bytes(input).subspan(split),
                                    cl::InputEnd::Final);
            ASSERT_EQ(b.status, CL_CODEC_COMPLETE);
            EXPECT_EQ(b.input_accepted, input.size() - split);
            output += words(b);
            EXPECT_EQ(output.size(), 1);
            EXPECT_TRUE(decoder.pending().empty());
            cl::CodecEncoder encoder(CL_CODEC_UTF8);
            EXPECT_EQ(encoded(encoder.encode(output)), input);
        }
    }
}

TEST(IncrementalCodec, PendingCanGrowOneByteAtATime)
{
    cl::CodecDecoder decoder(CL_CODEC_UTF8);
    const std::string input = "\xf0\x9f\x98\x80";
    for(size_t i = 0; i < input.size(); ++i)
    {
        auto result =
            decoder.decode(bytes(input).subspan(i, 1), cl::InputEnd::More);
        EXPECT_EQ(result.input_accepted, 1);
        if(i < 3)
        {
            EXPECT_EQ(result.status, CL_CODEC_INCOMPLETE_INPUT);
            EXPECT_TRUE(result.output.empty());
            EXPECT_EQ(pending(decoder),
                      std::vector<unsigned char>(input.begin(),
                                                 input.begin() + i + 1));
            auto empty = decoder.decode({}, cl::InputEnd::More);
            EXPECT_EQ(empty.status, CL_CODEC_INCOMPLETE_INPUT);
            EXPECT_EQ(empty.input_accepted, 0);
        }
        else
        {
            EXPECT_EQ(result.status, CL_CODEC_COMPLETE);
            EXPECT_EQ(words(result), U"\U0001f600");
            EXPECT_TRUE(decoder.pending().empty());
        }
    }
}

TEST(IncrementalCodec, FailureDoesNotCommitPrefixOrPending)
{
    cl::CodecDecoder decoder(CL_CODEC_UTF8);
    auto initial = decoder.decode(bytes("\xe2"), cl::InputEnd::More);
    ASSERT_EQ(initial.status, CL_CODEC_INCOMPLETE_INPUT);
    const auto saved = pending(decoder);
    // Complete the pending character and decode ASCII before a later failure.
    auto failed = decoder.decode(bytes("\x82\xac"
                                       "abc\xff"),
                                 cl::InputEnd::More);
    EXPECT_EQ(failed.status, CL_CODEC_INVALID_INPUT);
    EXPECT_EQ(failed.error_offset, 6);
    EXPECT_EQ(failed.error_length, 1);
    EXPECT_EQ(failed.input_accepted, 0);
    EXPECT_TRUE(failed.output.empty());
    EXPECT_EQ(pending(decoder), saved);
    auto retry = decoder.decode(bytes("\x82\xac"
                                      "abc"),
                                cl::InputEnd::Final);
    EXPECT_EQ(retry.status, CL_CODEC_COMPLETE);
    EXPECT_EQ(retry.input_accepted, 5);
    EXPECT_EQ(words(retry), U"\u20acabc");
    EXPECT_TRUE(decoder.pending().empty());

    decoder.reset();
    failed = decoder.decode(bytes("abc\xff"), cl::InputEnd::More);
    EXPECT_EQ(failed.input_accepted, 0);
    EXPECT_TRUE(failed.output.empty());
    EXPECT_TRUE(decoder.pending().empty());
}

TEST(IncrementalCodec, FinalErrorsResetAndReuse)
{
    for(const std::string &prefix:
        {std::string("\xe2\x82"), std::string("\xed\xa0")})
    {
        cl::CodecDecoder decoder(CL_CODEC_UTF8);
        auto initial = decoder.decode(bytes(prefix), cl::InputEnd::More);
        ASSERT_EQ(initial.status, CL_CODEC_INCOMPLETE_INPUT);
        const auto saved = pending(decoder);
        auto final = decoder.decode({}, cl::InputEnd::Final);
        EXPECT_EQ(final.status, CL_CODEC_INVALID_INPUT);
        EXPECT_EQ(final.input_accepted, 0);
        EXPECT_TRUE(final.output.empty());
        EXPECT_EQ(final.error_offset, 0);
        EXPECT_EQ(final.error_length, prefix[0] == '\xed' ? 1 : 2);
        EXPECT_EQ(final.error, prefix[0] == '\xed'
                                   ? CL_CODEC_INVALID_CONTINUATION_BYTE
                                   : CL_CODEC_UNEXPECTED_END);
        EXPECT_EQ(pending(decoder), saved);
        decoder.reset();
        EXPECT_TRUE(decoder.pending().empty());
        EXPECT_EQ(words(decoder.decode(bytes("X"), cl::InputEnd::Final)), U"X");
        EXPECT_EQ(words(decoder.decode(bytes("Y"), cl::InputEnd::Final)), U"Y");
        EXPECT_EQ(decoder.decode({}, cl::InputEnd::Final).status,
                  CL_CODEC_COMPLETE);
    }
    cl::CodecDecoder decoder(CL_CODEC_UTF8);
    auto final = decoder.decode(bytes("ok\xe2"), cl::InputEnd::Final);
    EXPECT_EQ(final.status, CL_CODEC_INVALID_INPUT);
    EXPECT_EQ(final.error_offset, 2);
    EXPECT_EQ(final.input_accepted, 0);
    EXPECT_TRUE(decoder.pending().empty());
}

TEST(IncrementalCodec, FinalAppliesOnlyAtEndOfOfferedInput)
{
    cl::CodecDecoder decoder(CL_CODEC_UTF8);
    std::string ascii(20000, 'a');
    auto probe = decoder.decode(bytes(ascii), cl::InputEnd::Final);
    ASSERT_GT(probe.input_accepted, 3);
    ASSERT_LT(probe.input_accepted, ascii.size());
    const size_t capacity = probe.input_accepted;
    for(size_t prefix = 1; prefix <= 3; ++prefix)
    {
        std::string input(capacity - prefix, 'a');
        input += "\xf0\x9f\x98\x80";
        decoder.reset();
        auto first = decoder.decode(bytes(input), cl::InputEnd::Final);
        ASSERT_EQ(first.status, CL_CODEC_INCOMPLETE_INPUT);
        EXPECT_EQ(first.input_accepted, capacity);
        EXPECT_EQ(first.output.size(), capacity - prefix);
        EXPECT_EQ(decoder.pending().size(), prefix);
        auto second = decoder.decode(bytes(input).subspan(first.input_accepted),
                                     cl::InputEnd::Final);
        EXPECT_EQ(second.status, CL_CODEC_COMPLETE);
        EXPECT_EQ(second.input_accepted, 4 - prefix);
        EXPECT_EQ(words(second), U"\U0001f600");
    }
}

TEST(IncrementalCodec, LargeRoundTripsWithRandomChunking)
{
    std::mt19937 random(417);
    for(auto encoding: {CL_CODEC_UTF8, CL_CODEC_LATIN1})
    {
        std::u32string input;
        for(size_t i = 0; i < 50000; ++i)
        {
            char32_t word =
                random() % (encoding == CL_CODEC_UTF8 ? 0x110000 : 256);
            if(word >= 0xd800 && word <= 0xdfff)
                word = 0;
            input += word;
        }
        cl::CodecEncoder encoder(encoding);
        std::string data;
        size_t offset = 0;
        while(offset < input.size())
        {
            auto result = encoder.encode(std::span(input).subspan(offset));
            ASSERT_EQ(result.status, CL_CODEC_COMPLETE);
            ASSERT_GT(result.input_accepted, 0);
            data += encoded(result);
            offset += result.input_accepted;
        }
        std::vector<unsigned char> reference(input.size() * 4);
        auto expected = cl_codec_encode(encoding, input.data(), input.size(),
                                        reference.data(), reference.size());
        ASSERT_EQ(expected.status, CL_CODEC_COMPLETE);
        EXPECT_EQ(data, std::string(reference.begin(),
                                    reference.begin() + expected.output_count));

        cl::CodecDecoder decoder(encoding);
        std::u32string output;
        offset = 0;
        while(offset < data.size())
        {
            size_t offered =
                std::min<size_t>(1 + random() % 10000, data.size() - offset);
            auto result = decoder.decode(bytes(data).subspan(offset, offered),
                                         offset + offered == data.size()
                                             ? cl::InputEnd::Final
                                             : cl::InputEnd::More);
            ASSERT_TRUE(result.status == CL_CODEC_COMPLETE ||
                        result.status == CL_CODEC_INCOMPLETE_INPUT);
            ASSERT_GT(result.input_accepted, 0);
            output += words(result);
            offset += result.input_accepted;
        }
        EXPECT_EQ(output, input);
        EXPECT_TRUE(decoder.pending().empty());
    }
}

TEST(IncrementalCodec, EncodeFailuresAreTransactional)
{
    for(auto encoding: {CL_CODEC_UTF8, CL_CODEC_LATIN1})
    {
        cl::CodecEncoder encoder(encoding);
        for(char32_t bad:
            {char32_t(0xd800), char32_t(0x110000), char32_t(0xffffffff)})
        {
            std::u32string input = U"abc";
            input += bad;
            auto failed = encoder.encode(input);
            EXPECT_EQ(failed.status, CL_CODEC_INVALID_INPUT);
            EXPECT_EQ(failed.input_accepted, 0);
            EXPECT_TRUE(failed.output.empty());
            EXPECT_EQ(failed.error_offset, 3);
            EXPECT_EQ(failed.error_length, 1);
            encoder.reset();
            EXPECT_EQ(encoded(encoder.encode(std::u32string_view(U"ok"))),
                      "ok");
        }
    }
    cl::CodecEncoder latin1(CL_CODEC_LATIN1);
    EXPECT_EQ(latin1.encode(U"\u0100").error, CL_CODEC_UNREPRESENTABLE);
}

TEST(IncrementalCodec, InvalidEncodingAndEmptyInput)
{
    auto bad = static_cast<ClCodecEncoding>(99);
    cl::CodecDecoder decoder(bad);
    EXPECT_EQ(decoder.decode(bytes("X"), cl::InputEnd::Final).status,
              CL_CODEC_INVALID_ARGUMENT);
    EXPECT_TRUE(decoder.pending().empty());
    cl::CodecEncoder encoder(bad);
    EXPECT_EQ(encoder.encode(U"X").status, CL_CODEC_INVALID_ARGUMENT);
    for(auto encoding: {CL_CODEC_UTF8, CL_CODEC_LATIN1})
    {
        cl::CodecDecoder d(encoding);
        cl::CodecEncoder e(encoding);
        auto decoded = d.decode({}, cl::InputEnd::Final);
        EXPECT_EQ(decoded.status, CL_CODEC_COMPLETE);
        EXPECT_EQ(decoded.input_accepted, 0);
        EXPECT_TRUE(decoded.output.empty());
        auto encoded = e.encode({});
        EXPECT_EQ(encoded.status, CL_CODEC_COMPLETE);
        EXPECT_EQ(encoded.input_accepted, 0);
        EXPECT_TRUE(encoded.output.empty());
    }
}

TEST(IncrementalCodec, MalformedInputAcrossEverySplit)
{
    for(const std::string &input:
        {std::string("abc\xff"), std::string("\xe2\x82"),
         std::string("\xe2\x82!"), std::string("\xed\xa0"),
         std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"),
         std::string("\xf0\x9f\x98!"), std::string("\xc0\x80"),
         std::string("\xe0\x80\x80")})
    {
        for(size_t split = 0; split <= input.size(); ++split)
        {
            for(auto end: {cl::InputEnd::More, cl::InputEnd::Final})
            {
                cl::CodecDecoder decoder(CL_CODEC_UTF8);
                auto initial = decoder.decode(bytes(input).first(split),
                                              cl::InputEnd::More);
                if(initial.status == CL_CODEC_INVALID_INPUT)
                    continue;
                auto saved = pending(decoder);
                auto incoming = bytes(input).subspan(split);
                char32_t output[32];
                auto reference = cl_codec_decode(
                    CL_CODEC_UTF8, {saved.data(), saved.size()},
                    {incoming.data(), incoming.size()}, output, 32);
                auto result = decoder.decode(incoming, end);
                bool final_error =
                    reference.status == CL_CODEC_INCOMPLETE_INPUT &&
                    end == cl::InputEnd::Final;
                EXPECT_EQ(result.status, final_error ? CL_CODEC_INVALID_INPUT
                                                     : reference.status);
                EXPECT_EQ(result.error, reference.error);
                EXPECT_EQ(result.error_length, reference.error_length);
                if(reference.status == CL_CODEC_INVALID_INPUT || final_error)
                {
                    EXPECT_EQ(result.input_accepted, 0);
                    EXPECT_TRUE(result.output.empty());
                    EXPECT_EQ(result.error_offset, reference.input_consumed);
                    EXPECT_EQ(pending(decoder), saved);
                }
                else
                {
                    EXPECT_EQ(result.input_accepted, incoming.size());
                    EXPECT_EQ(words(result),
                              std::u32string(output, reference.output_count));
                }
            }
        }
    }
}

TEST(IncrementalCodec, SuccessClearsErrorMetadataAndPreservesNuls)
{
    const std::string input("a\0\xff", 3);
    cl::CodecDecoder decoder(CL_CODEC_LATIN1);
    auto result = decoder.decode(bytes(input), cl::InputEnd::Final);
    EXPECT_EQ(words(result), std::u32string(U"a\0\xff", 3));
    EXPECT_EQ(result.input_accepted, 3);
    EXPECT_EQ(result.error, CL_CODEC_NO_ERROR);
    EXPECT_EQ(result.error_offset, 0);
    EXPECT_EQ(result.error_length, 0);
    EXPECT_TRUE(decoder.pending().empty());
    cl::CodecEncoder encoder(CL_CODEC_LATIN1);
    auto reverse = encoder.encode(result.output);
    EXPECT_EQ(encoded(reverse), input);
    EXPECT_EQ(reverse.error, CL_CODEC_NO_ERROR);
    EXPECT_EQ(reverse.error_offset, 0);
    EXPECT_EQ(reverse.error_length, 0);
}

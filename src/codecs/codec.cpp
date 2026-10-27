#include "codecs/codec.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <simdutf.h>

static_assert(sizeof(ClCodecWord) == sizeof(uint32_t));
static_assert(alignof(ClCodecWord) == alignof(uint32_t));

namespace
{
    enum class Operation
    {
        Measure,
        Convert
    };

    ClCodecResult stopped(ClCodecStatus status)
    {
        return {0, 0, status, CL_CODEC_NO_ERROR, 0};
    }

    bool valid_encoding(ClCodecEncoding encoding)
    {
        return encoding == CL_CODEC_UTF8 || encoding == CL_CODEC_LATIN1;
    }

    // Used for the small split character and precise diagnostics on the error
    // path. Bulk validation and conversion remain simdutf operations.
    ClCodecResult utf8_character(const unsigned char *input, size_t size)
    {
        unsigned char first = input[0];
        size_t width;
        if(first < 0x80)
            width = 1;
        else if(first >= 0xc2 && first <= 0xdf)
            width = 2;
        else if(first >= 0xe0 && first <= 0xef)
            width = 3;
        else if(first >= 0xf0 && first <= 0xf4)
            width = 4;
        else
            return {0, 0, CL_CODEC_INVALID_INPUT, CL_CODEC_INVALID_START_BYTE,
                    1};
        for(size_t i = 1; i < width; ++i)
        {
            if(i == size)
            {
                if(first == 0xed && size == 2 && input[1] >= 0xa0)
                    return {0, 0, CL_CODEC_INCOMPLETE_INPUT,
                            CL_CODEC_INVALID_CONTINUATION_BYTE, 1};
                return {0, 0, CL_CODEC_INCOMPLETE_INPUT,
                        CL_CODEC_UNEXPECTED_END, i};
            }
            unsigned char byte = input[i];
            if((byte & 0xc0) != 0x80 ||
               (i == 1 && ((first == 0xe0 && byte < 0xa0) ||
                           // CPython's incremental decoder waits for the
                           // third byte before rejecting surrogate-shaped
                           // sequences. A two-byte prefix remains pending.
                           (first == 0xed && byte >= 0xa0 && size >= 3) ||
                           (first == 0xf0 && byte < 0x90) ||
                           (first == 0xf4 && byte >= 0x90))))
                return {0, 0, CL_CODEC_INVALID_INPUT,
                        CL_CODEC_INVALID_CONTINUATION_BYTE, i};
        }
        return {width, 1, CL_CODEC_COMPLETE, CL_CODEC_NO_ERROR, 0};
    }

    ClCodecResult utf8_error(const unsigned char *input, size_t size)
    {
        size_t consumed = 0, written = 0;
        while(consumed < size)
        {
            auto part = utf8_character(input + consumed, size - consumed);
            if(part.status != CL_CODEC_COMPLETE)
            {
                part.input_consumed = consumed;
                part.output_count = written;
                return part;
            }
            consumed += part.input_consumed;
            ++written;
        }
        return {consumed, written, CL_CODEC_COMPLETE, CL_CODEC_NO_ERROR, 0};
    }

    template <Operation Op>
    ClCodecResult utf8_contiguous(ClCodecBytes input, ClCodecWord *output)
    {
        if(input.size == 0)
            return stopped(CL_CODEC_COMPLETE);
        const char *bytes = reinterpret_cast<const char *>(input.data);
        simdutf::result result;
        if constexpr(Op == Operation::Convert)
            result = simdutf::convert_utf8_to_utf32_with_errors(
                bytes, input.size, output);
        else
            result = simdutf::validate_utf8_with_errors(bytes, input.size);
        if(result.error == simdutf::OTHER)
            return stopped(CL_CODEC_BACKEND_ERROR);
        if(result.error == simdutf::SUCCESS)
        {
            size_t count = result.count;
            if constexpr(Op == Operation::Measure)
                count = simdutf::utf32_length_from_utf8(bytes, input.size);
            return {input.size, count, CL_CODEC_COMPLETE, CL_CODEC_NO_ERROR, 0};
        }
        auto error = utf8_error(input.data, input.size);
        if constexpr(Op == Operation::Convert)
        {
            // Reconvert only the proven valid prefix so the reported output is
            // independent of any implementation-specific SIMD error stores.
            if(error.input_consumed != 0)
                error.output_count = simdutf::convert_valid_utf8_to_utf32(
                    bytes, error.input_consumed, output);
        }
        return error;
    }

    template <Operation Op>
    ClCodecResult decode(ClCodecEncoding encoding, ClCodecBytes pending,
                         ClCodecBytes incoming, ClCodecWord *output,
                         size_t capacity)
    {
        if(!valid_encoding(encoding) || (!pending.data && pending.size) ||
           (!incoming.data && incoming.size) || pending.size > 3 ||
           (encoding == CL_CODEC_LATIN1 && pending.size))
            return stopped(CL_CODEC_INVALID_ARGUMENT);
        if(incoming.size > std::numeric_limits<size_t>::max() - pending.size)
            return stopped(CL_CODEC_SIZE_OVERFLOW);
        size_t total = pending.size + incoming.size;
        if constexpr(Op == Operation::Convert)
        {
            if(capacity < total)
                return stopped(CL_CODEC_INSUFFICIENT_OUTPUT);
            if(!output && total)
                return stopped(CL_CODEC_INVALID_ARGUMENT);
        }
        if(encoding == CL_CODEC_LATIN1)
        {
            size_t written = total;
            if constexpr(Op == Operation::Convert)
                if(total)
                    written = simdutf::convert_latin1_to_utf32(
                        reinterpret_cast<const char *>(incoming.data), total,
                        output);
            if(written != total)
                return stopped(CL_CODEC_BACKEND_ERROR);
            return {total, written, CL_CODEC_COMPLETE, CL_CODEC_NO_ERROR, 0};
        }
        if(pending.size == 0)
            return utf8_contiguous<Op>(incoming, output);
        auto prefix = utf8_character(pending.data, pending.size);
        if(prefix.status != CL_CODEC_INCOMPLETE_INPUT)
            return stopped(CL_CODEC_INVALID_ARGUMENT);
        // A valid incomplete prefix identifies the sequence width uniquely.
        size_t width = pending.data[0] < 0xe0   ? 2
                       : pending.data[0] < 0xf0 ? 3
                                                : 4;
        size_t take = std::min(incoming.size, width - pending.size);
        unsigned char boundary[4];
        std::memcpy(boundary, pending.data, pending.size);
        if(take)
            std::memcpy(boundary + pending.size, incoming.data, take);
        auto first =
            utf8_contiguous<Op>({boundary, pending.size + take}, output);
        if(first.status != CL_CODEC_COMPLETE || take == incoming.size)
            return first;
        ClCodecWord *rest_output = nullptr;
        if constexpr(Op == Operation::Convert)
            rest_output = output + first.output_count;
        auto rest = utf8_contiguous<Op>(
            {incoming.data + take, incoming.size - take}, rest_output);
        rest.input_consumed += first.input_consumed;
        rest.output_count += first.output_count;
        return rest;
    }

    ClCodecError word_error(char32_t word, ClCodecEncoding encoding)
    {
        if(word >= 0xd800 && word <= 0xdfff)
            return CL_CODEC_SURROGATE;
        if(word > 0x10ffff)
            return CL_CODEC_OUT_OF_RANGE;
        if(encoding == CL_CODEC_LATIN1 && word > 255)
            return CL_CODEC_UNREPRESENTABLE;
        return CL_CODEC_NO_ERROR;
    }

    template <Operation Op>
    ClCodecResult encode(ClCodecEncoding encoding, const ClCodecWord *input,
                         size_t count, unsigned char *output, size_t capacity)
    {
        if(!valid_encoding(encoding) || (!input && count))
            return stopped(CL_CODEC_INVALID_ARGUMENT);
        size_t multiplier = encoding == CL_CODEC_UTF8 ? 4 : 1;
        if(count > std::numeric_limits<size_t>::max() / multiplier)
            return stopped(CL_CODEC_SIZE_OVERFLOW);
        if constexpr(Op == Operation::Convert)
        {
            if(capacity < count * multiplier)
                return stopped(CL_CODEC_INSUFFICIENT_OUTPUT);
            if(!output && count)
                return stopped(CL_CODEC_INVALID_ARGUMENT);
        }
        if(count == 0)
            return stopped(CL_CODEC_COMPLETE);
        ClCodecResult result = {count, 0, CL_CODEC_COMPLETE, CL_CODEC_NO_ERROR,
                                0};
        if constexpr(Op == Operation::Convert)
        {
            simdutf::result converted;
            if(encoding == CL_CODEC_UTF8)
                converted = simdutf::convert_utf32_to_utf8_with_errors(
                    input, count, reinterpret_cast<char *>(output));
            else
                converted = simdutf::convert_utf32_to_latin1_with_errors(
                    input, count, reinterpret_cast<char *>(output));
            if(converted.error == simdutf::OTHER)
                return stopped(CL_CODEC_BACKEND_ERROR);
            if(converted.error == simdutf::SUCCESS)
            {
                result.output_count = converted.count;
                return result;
            }
            result.input_consumed = converted.count;
        }
        else
        {
            if(encoding == CL_CODEC_UTF8)
            {
                auto checked =
                    simdutf::validate_utf32_with_errors(input, count);
                if(checked.error == simdutf::OTHER)
                    return stopped(CL_CODEC_BACKEND_ERROR);
                if(checked.error != simdutf::SUCCESS)
                    result.input_consumed = checked.count;
            }
            else
            {
                for(size_t i = 0; i < count; ++i)
                    if(input[i] > 255)
                    {
                        result.input_consumed = i;
                        break;
                    }
            }
        }
        if(result.input_consumed < count)
        {
            result.status = CL_CODEC_INVALID_INPUT;
            result.error = word_error(input[result.input_consumed], encoding);
            result.error_length = 1;
        }
        size_t prefix = result.input_consumed;
        result.output_count =
            encoding == CL_CODEC_UTF8
                ? simdutf::utf8_length_from_utf32(input, prefix)
                : prefix;
        if constexpr(Op == Operation::Convert)
        {
            if(prefix)
            {
                if(encoding == CL_CODEC_UTF8)
                    result.output_count = simdutf::convert_valid_utf32_to_utf8(
                        input, prefix, reinterpret_cast<char *>(output));
                else
                    result.output_count = simdutf::convert_utf32_to_latin1(
                        input, prefix, reinterpret_cast<char *>(output));
            }
        }
        return result;
    }
}  // namespace

ClCodecResult cl_codec_decode(ClCodecEncoding encoding, ClCodecBytes pending,
                              ClCodecBytes incoming, ClCodecWord *output,
                              size_t capacity)
{
    return decode<Operation::Convert>(encoding, pending, incoming, output,
                                      capacity);
}
ClCodecResult cl_codec_measure_decode(ClCodecEncoding encoding,
                                      ClCodecBytes pending,
                                      ClCodecBytes incoming)
{
    return decode<Operation::Measure>(encoding, pending, incoming, nullptr, 0);
}
ClCodecResult cl_codec_encode(ClCodecEncoding encoding,
                              const ClCodecWord *input, size_t count,
                              unsigned char *output, size_t capacity)
{
    return encode<Operation::Convert>(encoding, input, count, output, capacity);
}
ClCodecResult cl_codec_measure_encode(ClCodecEncoding encoding,
                                      const ClCodecWord *input, size_t count)
{
    return encode<Operation::Measure>(encoding, input, count, nullptr, 0);
}

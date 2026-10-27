#include "codecs/incremental_codec.h"

#include <algorithm>
#include <cstring>

namespace cl
{
    DecodeChunk CodecDecoder::decode(std::span<const unsigned char> input,
                                     InputEnd end)
    {
        size_t take = std::min(input.size(), kScratchWords - pending_size_);
        auto result =
            cl_codec_decode(encoding_, {pending_, pending_size_},
                            {input.data(), take}, scratch_, kScratchWords);
        if(result.status == CL_CODEC_INCOMPLETE_INPUT &&
           end == InputEnd::Final && take == input.size())
            result.status = CL_CODEC_INVALID_INPUT;
        if(result.status != CL_CODEC_COMPLETE &&
           result.status != CL_CODEC_INCOMPLETE_INPUT)
            return {0,
                    {},
                    result.status,
                    result.error,
                    result.input_consumed,
                    result.error_length};

        size_t remaining = pending_size_ + take - result.input_consumed;
        // The adapter leaves at most one incomplete UTF-8 character. Stage
        // its bytes before overwriting pending, as the suffix can start there.
        unsigned char next_pending[3];
        for(size_t i = 0; i < remaining; ++i)
        {
            size_t offset = result.input_consumed + i;
            next_pending[i] = offset < pending_size_
                                  ? pending_[offset]
                                  : input[offset - pending_size_];
        }
        if(remaining)
            std::memcpy(pending_, next_pending, remaining);
        pending_size_ = remaining;
        return {take,
                {scratch_, result.output_count},
                result.status,
                result.error,
                result.status == CL_CODEC_INCOMPLETE_INPUT
                    ? result.input_consumed
                    : 0,
                result.error_length};
    }

    EncodeChunk CodecEncoder::encode(std::span<const char32_t> input)
    {
        size_t multiplier = encoding_ == CL_CODEC_UTF8 ? 4 : 1;
        size_t take = std::min(input.size(), kScratchBytes / multiplier);
        auto result = cl_codec_encode(encoding_, input.data(), take, scratch_,
                                      kScratchBytes);
        if(result.status != CL_CODEC_COMPLETE)
            return {0,
                    {},
                    result.status,
                    result.error,
                    result.input_consumed,
                    result.error_length};
        return {take,
                {scratch_, result.output_count},
                result.status,
                result.error,
                0,
                0};
    }
}  // namespace cl

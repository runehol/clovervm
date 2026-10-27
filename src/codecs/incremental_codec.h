#ifndef CL_INCREMENTAL_CODEC_H
#define CL_INCREMENTAL_CODEC_H

#include "codecs/codec.h"
#include <span>

namespace cl
{
    enum class InputEnd
    {
        More,
        Final,
    };

    // Output borrows the object's scratch until its next operation. The object
    // retains no caller storage. Input must not alias the object's buffers.
    // Each call processes only a bounded prefix. Advance by input_accepted and
    // reoffer the remainder even after COMPLETE (which describes that prefix).
    // On failure, input_accepted and output are empty and state is unchanged.
    // This transaction covers one native call, not an outer loop of calls.
    // Error offsets refer to pending + the selected incoming input for decode,
    // or to this call's input for encode. Only Unicode errors carry an offset.
    struct DecodeChunk
    {
        size_t input_accepted;
        std::span<const char32_t> output;
        ClCodecStatus status;
        ClCodecError error;
        size_t error_offset;
        size_t error_length;
    };

    struct EncodeChunk
    {
        size_t input_accepted;
        std::span<const unsigned char> output;
        ClCodecStatus status;
        ClCodecError error;
        size_t error_offset;
        size_t error_length;
    };

    class CodecDecoder
    {
        static constexpr size_t kScratchWords = 4096;
        ClCodecEncoding encoding_;
        unsigned char pending_[3]{};
        size_t pending_size_ = 0;
        char32_t scratch_[kScratchWords];

    public:
        explicit CodecDecoder(ClCodecEncoding encoding) : encoding_(encoding) {}

        // INCOMPLETE_INPUT with More is successful: all selected input has
        // been accepted, and its incomplete suffix is now pending. Final turns
        // that status into INVALID_INPUT only at the end of the offered input.
        [[nodiscard]] DecodeChunk decode(std::span<const unsigned char> input,
                                         InputEnd end);
        void reset() { pending_size_ = 0; }
        std::span<const unsigned char> pending() const
        {
            return {pending_, pending_size_};
        }
    };

    class CodecEncoder
    {
        static constexpr size_t kScratchBytes = 16384;
        ClCodecEncoding encoding_;
        unsigned char scratch_[kScratchBytes];

    public:
        explicit CodecEncoder(ClCodecEncoding encoding) : encoding_(encoding) {}

        [[nodiscard]] EncodeChunk encode(std::span<const char32_t> input);
        // Neither supported encoding retains state between calls.
        void reset() {}
    };
}  // namespace cl

#endif  // CL_INCREMENTAL_CODEC_H

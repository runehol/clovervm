#ifndef CL_TEXT_CODEC_H
#define CL_TEXT_CODEC_H

#include "codecs/codec.h"
#include "object_model/typed_value.h"

namespace cl
{
    class ThreadState;
    class VirtualMachine;
    class String;
    class Bytes;

    // Encoding must be CL_CODEC_UTF8 or CL_CODEC_LATIN1.
    [[nodiscard]] Value encode_string(ThreadState *, TValue<String>,
                                      ClCodecEncoding);
    [[nodiscard]] Value decode_bytes(ThreadState *, TValue<Bytes>,
                                     ClCodecEncoding);
    void install_text_codec_methods(VirtualMachine *);
}  // namespace cl

#endif  // CL_TEXT_CODEC_H

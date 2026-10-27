#include "codecs/codec.h"

_Static_assert(sizeof(ClCodecWord) == 4, "codec words must be 32-bit");
_Static_assert(sizeof(ClCodecEncoding) == 4,
               "codec encoding ABI must be 32-bit");

/* Compiled as C, then called from the C++ suite: checks the actual ABI, not
   just whether a C translation unit can parse the declarations. */
int cl_test_codec_c(void)
{
    const unsigned char source[] = {0x41, 0xe2, 0x82, 0xac, 0x00};
    ClCodecWord words[5] = {0};
    unsigned char encoded[12] = {0};
    ClCodecBytes empty = {NULL, 0};
    ClCodecBytes bytes = {source, sizeof(source)};
    ClCodecResult decoded =
        cl_codec_decode(CL_CODEC_UTF8, empty, bytes, words, 5);
    if(decoded.status != CL_CODEC_COMPLETE || decoded.input_consumed != 5 ||
       decoded.output_count != 3 || words[0] != 0x41 || words[1] != 0x20ac ||
       words[2] != 0)
        return 1;
    ClCodecResult result =
        cl_codec_encode(CL_CODEC_UTF8, words, 3, encoded, 12);
    if(result.status != CL_CODEC_COMPLETE || result.output_count != 5)
        return 2;
    for(size_t i = 0; i < sizeof(source); ++i)
        if(encoded[i] != source[i])
            return 3;
    if(cl_codec_measure_encode((ClCodecEncoding)99, NULL, 0).status !=
       CL_CODEC_INVALID_ARGUMENT)
        return 4;
    return 0;
}

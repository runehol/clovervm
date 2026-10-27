#ifndef CL_CODEC_H
#define CL_CODEC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
typedef char32_t ClCodecWord;
extern "C"
{
#else
typedef uint32_t ClCodecWord;
#endif

    typedef enum ClCodecEncoding
#ifdef __cplusplus
        : uint32_t
#endif
    {
        CL_CODEC_UTF8,
        CL_CODEC_LATIN1
    } ClCodecEncoding;

    typedef enum ClCodecStatus
    {
        CL_CODEC_COMPLETE,
        CL_CODEC_INCOMPLETE_INPUT,
        CL_CODEC_INVALID_INPUT,
        CL_CODEC_INSUFFICIENT_OUTPUT,
        CL_CODEC_SIZE_OVERFLOW,
        CL_CODEC_INVALID_ARGUMENT,
        CL_CODEC_BACKEND_ERROR
    } ClCodecStatus;

    typedef enum ClCodecError
    {
        CL_CODEC_NO_ERROR,
        CL_CODEC_INVALID_START_BYTE,
        CL_CODEC_INVALID_CONTINUATION_BYTE,
        CL_CODEC_UNEXPECTED_END,
        CL_CODEC_SURROGATE,
        CL_CODEC_OUT_OF_RANGE,
        CL_CODEC_UNREPRESENTABLE
    } ClCodecError;

    typedef struct ClCodecBytes
    {
        const unsigned char *data;
        size_t size;
    } ClCodecBytes;

    typedef struct ClCodecResult
    {
        size_t input_consumed;
        /* Written elements for conversion; required elements for measurement.
         */
        size_t output_count;
        ClCodecStatus status;
        /* For INVALID_INPUT, or the error to raise at EOF for INCOMPLETE_INPUT.
           The error starts at input_consumed; length is in source elements,
           not output elements. An incomplete suffix's retained length is the
           total input length minus input_consumed, not error_length. */
        ClCodecError error;
        size_t error_length;
    } ClCodecResult;

    /* No allocation, retained pointers, VM state or callbacks. Input and output
       storage must not overlap. Null pointers are allowed only for empty
       ranges. C++ word buffers must contain char32_t objects (not cast uint32_t
       arrays).

       Decode input is logically pending followed by incoming. Pending must be
       an incomplete UTF-8 prefix of 1..3 bytes, or empty; Latin-1 requires it
       empty. Decode counts bytes consumed and words produced; encode counts
       words consumed and bytes produced. Only the reported output prefix is
       meaningful on error.

       Encoding errors identify the first offending word (error_length == 1),
       rather than grouping adjacent failures into Python exception spans.

       Conversion requires maximum-expansion capacity: total bytes in words for
       decode, 4*words in bytes for UTF-8 encode, words in bytes for Latin-1
       encode. Insufficient capacity, overflow and invalid arguments return zero
       progress without writing. Neither operation consumes an
       invalid/incomplete character. UTF-8 treats a BOM as ordinary text; there
       is no final/EOF policy here. */
    ClCodecResult cl_codec_decode(ClCodecEncoding encoding,
                                  ClCodecBytes pending, ClCodecBytes incoming,
                                  ClCodecWord *output, size_t output_capacity);
    ClCodecResult cl_codec_encode(ClCodecEncoding encoding,
                                  const ClCodecWord *input, size_t input_count,
                                  unsigned char *output,
                                  size_t output_capacity);
    ClCodecResult cl_codec_measure_decode(ClCodecEncoding encoding,
                                          ClCodecBytes pending,
                                          ClCodecBytes incoming);
    ClCodecResult cl_codec_measure_encode(ClCodecEncoding encoding,
                                          const ClCodecWord *input,
                                          size_t input_count);

#ifdef __cplusplus
}
#endif
#endif /* CL_CODEC_H */

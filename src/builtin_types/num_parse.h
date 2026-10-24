#ifndef CL_NUM_PARSE_H
#define CL_NUM_PARSE_H

#include "object_model/typed_value.h"
#include "object_model/value.h"
#include <optional>
#include <string_view>

namespace cl
{
    class ThreadState;

    // Parse decimal integer text into an SMI or heap BigInt. Invalid text
    // raises ValueError; values beyond int64 remain valid integers.
    [[nodiscard]] Expected<Value> parse_int_string_view(ThreadState *thread,
                                                        std::wstring_view text);

    // Parse an explicit base (0 or 2..36), including matching Python
    // prefixes. The caller must validate the base. Base 10 uses the
    // existing decimal parsing path.
    [[nodiscard]] Expected<Value> parse_int_string_view(ThreadState *thread,
                                                        std::wstring_view text,
                                                        int base);

    // Byte-storage counterparts use ASCII digits/whitespace only, without
    // decoding or widening. Keep decimal SMI parsing separate from base
    // parsing.
    [[nodiscard]] Expected<Value> parse_int_string_view(ThreadState *thread,
                                                        std::string_view text);
    [[nodiscard]] Expected<Value>
    parse_int_string_view(ThreadState *thread, std::string_view text, int base);

    // Parse Python float text, including surrounding numeric whitespace,
    // digit separators, and Unicode decimal digits. Ordinary input is parsed
    // directly; normalization is attempted only after direct parsing fails.
    // Overflow and underflow produce infinity and signed zero respectively.
    [[nodiscard]] std::optional<double> try_parse_float(std::wstring_view text);

    // Parse ASCII float text directly from byte storage. The same grammar
    // applies, but non-ASCII digits and whitespace are rejected.
    [[nodiscard]] std::optional<double> try_parse_float(std::string_view text);
}  // namespace cl

#endif  // CL_NUM_PARSE_H

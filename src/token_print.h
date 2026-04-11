#ifndef CL_TOKEN_PRINT_H
#define CL_TOKEN_PRINT_H

#include "token.h"
#include "tokenizer.h"
#include <fmt/format.h>
#include <fmt/xchar.h>
#include <string>

static inline std::string narrow_wstring_view(std::wstring_view s)
{
    return std::string(s.begin(), s.end());
}

template <> struct fmt::formatter<cl::Token>
{
    constexpr auto parse(format_parse_context &ctx) { return ctx.end(); }

    template <typename FormatContext>
    auto format(const cl::Token t, FormatContext &ctx) const
        -> decltype(ctx.out())
    {
        auto &&out = ctx.out();
        return format_to(out, "{}", cl::to_string(t));
    }
};

template <> struct fmt::formatter<cl::TokenVector>
{
    constexpr auto parse(format_parse_context &ctx) { return ctx.end(); }

    template <typename FormatContext>
    auto format(const cl::TokenVector &tv, FormatContext &ctx) const
        -> decltype(ctx.out())
    {
        auto &&out = ctx.out();
        for(size_t i = 0; i < tv.size(); ++i)
        {
            format_to(out, "{:05d} {:10s}", tv.source_offsets[i],
                      cl::to_string(tv.tokens[i]));

            switch(tv.tokens[i])
            {
                case cl::Token::NAME:
                    format_to(out, " {}",
                              narrow_wstring_view(string_for_name_token(
                                  *tv.compilation_unit, tv.source_offsets[i])));
                    break;
                case cl::Token::NUMBER:
                    format_to(out, " {}",
                              narrow_wstring_view(string_for_number_token(
                                  *tv.compilation_unit, tv.source_offsets[i])));
                    break;
                case cl::Token::STRING:
                    format_to(out, " {}",
                              narrow_wstring_view(string_for_string_token(
                                  *tv.compilation_unit, tv.source_offsets[i])));
                    break;
                default:
                    break;
            }
            format_to(out, "\n");
        }
        return out;
    }
};

#endif  // CL_TOKEN_PRINT_H

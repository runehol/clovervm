#include "parser.h"

#include <string>
#include "ast.h"
#include "token.h"

namespace cl
{

    struct ParserState
    {
        ParserState(const TokenVector &_token_vec)
            : token_vec(_token_vec)
        {}


        Token peek_token()
        {
            assert(token_pos < token_vec.size());
            return token_vec.tokens[token_pos];
        };

        Token get_token()
        {
            assert(token_pos < token_vec.size());
            return token_vec.tokens[token_pos++];
        };


        void expect_token(Token expected)
        {
            Token actual = get_token();
            if(expected != actual)
            {
                throw std::runtime_error(std::string("Expected token ") + to_string(expected) + ", got " + to_string(actual));
            }
        }

        uint32_t source_pos_for_last_token()
        {
            return token_vec.source_offsets[token_pos-1];
        }

        AstVector ast;
        const TokenVector &token_vec;
        size_t token_pos = 0;

    };


    int32_t statements(ParserState &state)
    {
        return -1;

    }

    int32_t file(ParserState &state)
    {
        int32_t idx = -1;
        if(state.peek_token() != Token::ENDMARKER)
        {
            idx = statements(state);
        }
        state.expect_token(Token::ENDMARKER);
        return idx;
    }



    AstVector parse(const TokenVector &t, StartRule start_rule)
    {
        ParserState state(t);

        switch(start_rule)
        {
        case StartRule::File:
            break;
        case StartRule::Interactive:
            break;
        case StartRule::Eval:
            break;
        case StartRule::FuncType:
            break;
        case StartRule::FString:
            break;
        }


        return std::move(state.ast);
    }

}

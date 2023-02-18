#include "parser.h"

#include <string>
#include "ast.h"
#include "token.h"
#include "compilation_unit.h"

namespace cl
{


    class Parser
    {
    public:
        Parser(const TokenVector &_token_vec)
            : token_vec(_token_vec), ast(token_vec.compilation_unit)
        {}

        AstVector parse(StartRule start_rule)
        {
            switch(start_rule)
            {
            case StartRule::File:
                ast.root_node = file();
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


            return std::move(ast);

        }

    private:

        const TokenVector &token_vec;
        AstVector ast;
        size_t token_pos = 0;

        Token peek()
        {
            assert(token_pos < token_vec.size());
            return token_vec.tokens[token_pos];
        };

        Token advance()
        {
            assert(token_pos < token_vec.size());
            return token_vec.tokens[token_pos++];
        };

        uint32_t source_pos_and_advance()
        {
            assert(token_pos < token_vec.size());
            return token_vec.source_offsets[token_pos++];

        }


        void consume(Token expected)
        {
            Token actual = advance();
            if(expected != actual)
            {
                throw std::runtime_error(std::string("Expected token ") + to_string(expected) + ", got " + to_string(actual));
            }
        }

        bool match(Token expected)
        {
            Token actual = peek();
            if(actual == expected)
            {
                ++token_pos;
                return true;
            }
            return false;
        }

        uint32_t source_pos_for_last_token()
        {
            return token_vec.source_offsets[token_pos-1];
        }

        bool is_at_end()
        {
            return peek() == Token::ENDMARKER;
        }




        // now the parser itself

        // expressions



        int32_t sum()
        {
            int32_t result = term();
            while(true)
            {
                AstOperatorKind op_kind = AstOperatorKind::UNUSED;
                switch(peek())
                {
                case Token::PLUS:
                    op_kind = AstOperatorKind::ADD;
                    break;
                case Token::MINUS:
                    op_kind = AstOperatorKind::SUBTRACT;
                    break;

                default:
                    return result;
                }


                uint32_t source_pos = source_pos_and_advance();
                int32_t rhs = factor();
                result = ast.emplace_back(
                    AstKind(AstNodeKind::EXPRESSION_BINARY, op_kind),
                    source_pos, result, rhs);

            }
        }

        int32_t term()
        {
            int32_t result = factor();
            while(true)
            {
                AstOperatorKind op_kind = AstOperatorKind::UNUSED;
                switch(peek())
                {
                case Token::STAR:
                    op_kind = AstOperatorKind::MULTIPLY;
                    break;
                case Token::SLASH:
                    op_kind = AstOperatorKind::DIVIDE;
                    break;
                case Token::DOUBLESLASH:
                    op_kind = AstOperatorKind::INT_DIVIDE;
                    break;
                case Token::PERCENT:
                    op_kind = AstOperatorKind::MODULO;
                    break;
                case Token::AT:
                    op_kind = AstOperatorKind::MATMULT;
                    break;

                default:
                    return result;
                }


                uint32_t source_pos = source_pos_and_advance();
                int32_t rhs = factor();
                result = ast.emplace_back(
                    AstKind(AstNodeKind::EXPRESSION_BINARY, op_kind),
                    source_pos, result, rhs);

            }
            return result;
        }

        int32_t factor()
        {
            AstOperatorKind op_kind = AstOperatorKind::UNUSED;
            switch(peek())
            {
            case Token::PLUS:
                op_kind = AstOperatorKind::PLUS;
                break;
            case Token::MINUS:
                op_kind = AstOperatorKind::NEGATE;
                break;
            case Token::TILDE:
                op_kind = AstOperatorKind::INVERT;
                break;

            default:
                return power();
            }

            uint32_t source_pos = source_pos_and_advance();
            int32_t inner = factor();
            return ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_UNARY, op_kind), source_pos, inner);
        }

        int32_t power()
        {
            int32_t lhs = await_primary();
            if(peek() == Token::DOUBLESTAR)
            {
                uint32_t source_pos = source_pos_and_advance();
                int32_t rhs = factor();
                return ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_BINARY, AstOperatorKind::POWER), source_pos, lhs, rhs);
            } else {
                return lhs;
            }
        }

        int32_t await_primary()
        {
            return primary();
        }

        int32_t primary()
        {
            int32_t first = atom();
            // todo parse atom.name, atom[slices], atom(arguments) here
            return first;
        }

        int32_t atom()
        {
            switch(peek())
            {
            case Token::NUMBER:
                consume(Token::NUMBER);
                return ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_LITERAL, AstOperatorKind::NUMBER), source_pos_for_last_token());
            case Token::STRING:
                consume(Token::STRING);
                return ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_LITERAL, AstOperatorKind::STRING), source_pos_for_last_token());
            case Token::NONE:
                consume(Token::NONE);
                return ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_LITERAL, AstOperatorKind::NONE), source_pos_for_last_token());
            case Token::TRUE:
                consume(Token::TRUE);
                return ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_LITERAL, AstOperatorKind::TRUE), source_pos_for_last_token());
            case Token::FALSE:
                consume(Token::FALSE);
                return ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_LITERAL, AstOperatorKind::FALSE), source_pos_for_last_token());
            default:
                throw std::runtime_error(std::string("Unexpected token") + to_string(peek()));

                // TODO NAME, STRING, parenthesis, tuples, lists, dicts, ... (ELLIPSIS)
            }
        }

        int32_t simple_statements()
        {
            return -1;
        }


        int32_t compound_statement()
        {
            return -1;
        }



        int32_t statement()
        {
            switch(peek())
            {
            case Token::DEF:
            case Token::AT:
            case Token::ASYNC:
            case Token::IF:
            case Token::CLASS:
            case Token::WITH:
            case Token::FOR:
            case Token::TRY:
            case Token::WHILE:
                return compound_statement();

            default:
                return simple_statements();

            }

        }


        int32_t statements()
        {
            return -1;
        }

        int32_t file()
        {
            int32_t idx = -1;
            if(!is_at_end())
            {
                idx = statements();
            }
            consume(Token::ENDMARKER);
            return idx;
        }

    };


    AstVector parse(const TokenVector &tv, StartRule start_rule)
    {
        Parser parser(tv);

        return parser.parse(start_rule);
    }

}

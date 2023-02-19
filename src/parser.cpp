#include "parser.h"

#include <string>
#include "ast.h"
#include "token.h"
#include "tokenizer.h"
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
                ast.root_node = eval();
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

        uint32_t source_pos_for_token()
        {
            return token_vec.source_offsets[token_pos];
        }

        uint32_t source_pos_for_previous_token()
        {
            return token_vec.source_offsets[token_pos-1];
        }

        bool is_at_end()
        {
            return peek() == Token::ENDMARKER;
        }




        // now the parser itself

        // helper to build sequences
        int32_t sequence_until_stop_token(int32_t (Parser::*parse_member)(), Token separator, Token stop)
        {
            if(match(separator))
            {
                if(peek() != stop)
                {
                    uint32_t pos = source_pos_and_advance();
                    int32_t member = (this->*parse_member)();
                    return ast.emplace_back(AstNodeKind::SEQUENCE, pos, member, sequence_until_stop_token(parse_member, separator, stop));

                }
            }
            return -1;

        }

        // expressions

        int32_t genexp()
        {
            return expression();
        }

        int32_t star_expressions()
        {
            uint32_t tuple_start_pos = source_pos_for_token();
            int32_t result = star_expression();
            if(peek() == Token::COMMA)
            {
                return ast.emplace_back(AstNodeKind::EXPRESSION_TUPLE, tuple_start_pos, result, sequence_until_stop_token(&Parser::star_expression, Token::COMMA, Token::NEWLINE));
            } else {
                return result;
            }
        }

        int32_t star_expression()
        {
            return expression();
        }

        int32_t expressions()
        {
            uint32_t tuple_start_pos = source_pos_for_token();
            int32_t result = expression();
            if(peek() == Token::COMMA)
            {
                return ast.emplace_back(AstNodeKind::EXPRESSION_TUPLE, tuple_start_pos, result, sequence_until_stop_token(&Parser::expression, Token::COMMA, Token::NEWLINE));
            } else {
                return result;
            }
        }


        int32_t expression()
        {
            return disjunction();
        }

        int32_t disjunction()
        {
            return conjunction();

        }


        int32_t conjunction()
        {
            return inversion();
        }

        int32_t inversion()
        {
            if(match(Token::NOT))
            {
                return ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_UNARY, AstOperatorKind::NOT), source_pos_for_previous_token(), inversion());
            }
            return comparison();
        }


        int32_t comparison()
        {
            return bitwise_or();
        }


        int32_t bitwise_or()
        {
            int32_t result = bitwise_xor();
            while(match(Token::VBAR))
            {
                uint32_t source_pos = source_pos_for_previous_token();
                int32_t rhs = bitwise_xor();
                result = ast.emplace_back(
                    AstKind(AstNodeKind::EXPRESSION_BINARY, AstOperatorKind::BITWISE_OR),
                    source_pos, result, rhs);
            }
            return result;
        }

        int32_t bitwise_xor()
        {
            int32_t result = bitwise_and();
            while(match(Token::CIRCUMFLEX))
            {
                uint32_t source_pos = source_pos_for_previous_token();
                int32_t rhs = bitwise_and();
                result = ast.emplace_back(
                    AstKind(AstNodeKind::EXPRESSION_BINARY, AstOperatorKind::BITWISE_XOR),
                    source_pos, result, rhs);
            }
            return result;
        }

        int32_t bitwise_and()
        {
            int32_t result = shift_expr();
            while(match(Token::CIRCUMFLEX))
            {
                uint32_t source_pos = source_pos_for_previous_token();
                int32_t rhs = shift_expr();
                result = ast.emplace_back(
                    AstKind(AstNodeKind::EXPRESSION_BINARY, AstOperatorKind::BITWISE_AND),
                    source_pos, result, rhs);
            }
            return result;
        }


        int32_t shift_expr()
        {
            int32_t result = sum();
            while(true)
            {
                AstOperatorKind op_kind = AstOperatorKind::NOP;
                switch(peek())
                {
                case Token::LEFTSHIFT:
                    op_kind = AstOperatorKind::LEFTSHIFT;
                    break;
                case Token::RIGHTSHIFT:
                    op_kind = AstOperatorKind::RIGHTSHIFT;
                    break;

                default:
                    return result;
                }


                uint32_t source_pos = source_pos_and_advance();
                int32_t rhs = sum();
                result = ast.emplace_back(
                    AstKind(AstNodeKind::EXPRESSION_BINARY, op_kind),
                    source_pos, result, rhs);

            }
        }


        int32_t sum()
        {
            int32_t result = term();
            while(true)
            {
                AstOperatorKind op_kind = AstOperatorKind::NOP;
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
                int32_t rhs = term();
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
                AstOperatorKind op_kind = AstOperatorKind::NOP;
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
            AstOperatorKind op_kind = AstOperatorKind::NOP;
            switch(peek())
            {
            case Token::PLUS:
                op_kind = AstOperatorKind::PLUS;
                break;
            case Token::MINUS:
                op_kind = AstOperatorKind::NEGATE;
                break;
            case Token::TILDE:
                op_kind = AstOperatorKind::BITWISE_NOT;
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
            {
                int64_t iv = std::stoll(std::wstring(string_for_number_token(*ast.compilation_unit, source_pos_for_token())));
                CLValue v = value_make_smi(iv);
                return ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_LITERAL, AstOperatorKind::NUMBER), source_pos_and_advance(), -1, -1, v);
            }
            case Token::STRING:
                return ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_LITERAL, AstOperatorKind::STRING), source_pos_and_advance(), -1, -1, cl_None);
            case Token::NONE:
                return ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_LITERAL, AstOperatorKind::NONE), source_pos_and_advance(), -1, -1, cl_None);
            case Token::TRUE:
                return ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_LITERAL, AstOperatorKind::TRUE), source_pos_and_advance(), -1, -1, cl_True);
            case Token::FALSE:
                return ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_LITERAL, AstOperatorKind::FALSE), source_pos_and_advance(), -1, -1, cl_False);
            case Token::LPAR:
            {
                advance();
                int32_t result = genexp();
                consume(Token::RPAR);
                return result;
            }

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

        int32_t eval()
        {
            int32_t result = expressions();
            while(match(Token::NEWLINE))
            {}

            consume(Token::ENDMARKER);
            return result;

        }

    };


    AstVector parse(const TokenVector &tv, StartRule start_rule)
    {
        Parser parser(tv);

        return parser.parse(start_rule);
    }

}

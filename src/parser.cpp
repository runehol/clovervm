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
        Parser(CompilationUnit &_cu, const TokenVector &_token_vec)
            : cu(_cu), token_vec(_token_vec)
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

        AstVector ast;
        CompilationUnit &cu;
        const TokenVector &token_vec;
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


        int32_t literal_expr()
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


    AstVector parse(CompilationUnit &cu, const TokenVector &t, StartRule start_rule)
    {
        Parser parser(cu, t);

        return parser.parse(start_rule);
    }

}

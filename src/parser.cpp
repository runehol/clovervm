#include "parser.h"

#include <string>
#include "ast.h"
#include "token.h"
#include "tokenizer.h"
#include "compilation_unit.h"
#include "virtual_machine.h"

namespace cl
{


    class Parser
    {
    public:
        Parser(VirtualMachine &_vm, const TokenVector &_token_vec)
            : vm(_vm), token_vec(_token_vec), ast(token_vec.compilation_unit)
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

        VirtualMachine &vm;
        const TokenVector &token_vec;
        AstVector ast;
        size_t token_pos = 0;

        Token peek()
        {
            assert(token_pos < token_vec.size());
            return token_vec.tokens[token_pos];
        };

        Token peek2()
        {
            size_t pos2 = token_pos+1;
            if(pos2 < token_vec.size()) return token_vec.tokens[pos2];
            return Token::ENDMARKER;
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
        AstChildren sequence_until_stop_token(int32_t initial, int32_t (Parser::*parse_member)(), Token separator, Token stop)
        {
            AstChildren children;
            children.push_back(initial);
            while(match(separator))
            {
                if(peek() == stop) break;

                int32_t member = (this->*parse_member)();
                children.push_back(member);
            }
            return children;

        }


        int32_t block()
        {
            int32_t stmts = -1;
            if(match(Token::NEWLINE))
            {
                consume(Token::INDENT);
                stmts = statements();
                consume(Token::DEDENT);
            } else {
                stmts = simple_stmts();
            }
            return stmts;


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
                AstChildren children = sequence_until_stop_token(result, &Parser::star_expression, Token::COMMA, Token::NEWLINE);
                return ast.emplace_back(AstNodeKind::EXPRESSION_TUPLE, tuple_start_pos, children);
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
                 AstChildren children = sequence_until_stop_token(result, &Parser::expression, Token::COMMA, Token::NEWLINE);
                return ast.emplace_back(AstNodeKind::EXPRESSION_TUPLE, tuple_start_pos, children);
            } else {
                return result;
            }
        }


        int32_t expression()
        {
            return disjunction();
        }

        int32_t assignment_expression()
        {
            if(peek() != Token::NAME)
            {
                throw std::runtime_error(std::string("Expected token ") + to_string(Token::NAME) + ", got " + to_string(peek()));
            }
            int32_t lhs = atom(); // smallest rule that just consumes a name and makes a nice node for us
            int32_t source_pos = source_pos_for_token();
            consume(Token::COLONEQUAL);
            int32_t rhs = expression();
            return ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_ASSIGN, AstOperatorKind::NOP), source_pos, lhs, rhs);
        }


        int32_t named_expression()
        {
            if(peek() == Token::NAME && peek2() == Token::COLONEQUAL)
            {
                return assignment_expression();
            } else {
                return expression();
            }
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
            case Token::NAME:
            {
                std::wstring name = std::wstring(string_for_name_token(*ast.compilation_unit, source_pos_for_token()));
                Value v = vm.get_or_create_interned_string(name);
                return ast.emplace_back(AstNodeKind::EXPRESSION_VARIABLE_REFERENCE, source_pos_and_advance(), v);
            }

            case Token::NUMBER:
            {
                int64_t iv = std::stoll(std::wstring(string_for_number_token(*ast.compilation_unit, source_pos_for_token())));
                Value v = Value::from_smi(iv);
                return ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_LITERAL, AstOperatorKind::NUMBER), source_pos_and_advance(), v);
            }
            case Token::STRING:
                return ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_LITERAL, AstOperatorKind::STRING), source_pos_and_advance(), Value::None());
            case Token::NONE:
                return ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_LITERAL, AstOperatorKind::NONE), source_pos_and_advance(), Value::None());
            case Token::TRUE:
                return ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_LITERAL, AstOperatorKind::TRUE), source_pos_and_advance(), Value::True());
            case Token::FALSE:
                return ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_LITERAL, AstOperatorKind::FALSE), source_pos_and_advance(), Value::False());
            case Token::LPAR:
            {
                advance();
                int32_t result = genexp();
                consume(Token::RPAR);
                return result;
            }

            default:
                throw std::runtime_error(std::string("Unexpected token ") + to_string(peek()));

                // TODO NAME, STRING, parenthesis, tuples, lists, dicts, ... (ELLIPSIS)
            }
        }

        int32_t assignment()
        {
            int32_t lhs = star_expressions();
            /* TODO check if single target */
            AstOperatorKind op_kind = AstOperatorKind::FALSE;
            switch(peek())
            {
            case Token::EQUAL:
                op_kind = AstOperatorKind::NOP;
                break;
            case Token::PLUSEQUAL:
                op_kind = AstOperatorKind::ADD;
                break;
            case Token::MINEQUAL:
                op_kind = AstOperatorKind::SUBTRACT;
                break;
            case Token::STAREQUAL:
                op_kind = AstOperatorKind::MULTIPLY;
                break;
            case Token::ATEQUAL:
                op_kind = AstOperatorKind::MATMULT;
                break;
            case Token::SLASHEQUAL:
                op_kind = AstOperatorKind::DIVIDE;
                break;
            case Token::PERCENTEQUAL:
                op_kind = AstOperatorKind::MODULO;
                break;
            case Token::AMPEREQUAL:
                op_kind = AstOperatorKind::BITWISE_AND;
                break;
            case Token::VBAREQUAL:
                op_kind = AstOperatorKind::BITWISE_OR;
                break;
            case Token::CIRCUMFLEXEQUAL:
                op_kind = AstOperatorKind::BITWISE_XOR;
                break;
            case Token::LEFTSHIFTEQUAL:
                op_kind = AstOperatorKind::LEFTSHIFT;
                break;
            case Token::RIGHTSHIFTEQUAL:
                op_kind = AstOperatorKind::RIGHTSHIFT;
                break;
            case Token::DOUBLESTAREQUAL:
                op_kind = AstOperatorKind::POWER;
                break;
            case Token::DOUBLESLASHEQUAL:
                op_kind = AstOperatorKind::INT_DIVIDE;
                break;
            default:
                return lhs;
            }

            int32_t source_pos = source_pos_and_advance();
            int32_t rhs = annotated_rhs();
            return ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_ASSIGN, op_kind), source_pos, lhs, rhs);

        }

        int32_t yield_expr()
        {
            return -1;
        }

        int32_t annotated_rhs()
        {
            switch(peek())
            {
            case Token::YIELD:
                return yield_expr();
            default:
                return star_expressions();
            }
        }


        int32_t return_stmt()
        {
            return -1;
        }

        int32_t import_stmt()
        {
            return -1;
        }

        int32_t del_stmt()
        {
            return -1;
        }

        int32_t yield_stmt()
        {
            return -1;
        }

        int32_t assert_stmt()
        {
            return -1;
        }

        int32_t break_stmt()
        {
            int32_t source_pos = source_pos_for_token();
            consume(Token::BREAK);
            return ast.emplace_back(AstNodeKind::STATEMENT_BREAK, source_pos, {});
        }

        int32_t continue_stmt()
        {
            int32_t source_pos = source_pos_for_token();
            consume(Token::CONTINUE);
            return ast.emplace_back(AstNodeKind::STATEMENT_CONTINUE, source_pos, {});
        }

        int32_t global_stmt()
        {
            return -1;
        }

        int32_t nonlocal_stmt()
        {
            return -1;
        }


        int32_t simple_stmt()
        {
            switch(peek())
            {
            case Token::RETURN:
                return return_stmt();
            case Token::IMPORT:
            case Token::FROM:
                return import_stmt();

            case Token::DEL:
                return del_stmt();
            case Token::YIELD:
                return yield_stmt();
            case Token::ASSERT:
                return assert_stmt();
            case Token::BREAK:
                return break_stmt();
            case Token::CONTINUE:
                return continue_stmt();
            case Token::GLOBAL:
                return global_stmt();
            case Token::NONLOCAL:
                return nonlocal_stmt();

            default:
                return assignment();
            }
        }

        int32_t simple_stmts()
        {
            AstChildren children;
            int32_t source_pos = source_pos_for_token();
            do {

                children.emplace_back(simple_stmt());

            } while(match(Token::SEMI));

            consume(Token::NEWLINE);

            if(children.size() == 1)
            {
                return children[0];
            } else {
                return ast.emplace_back(AstNodeKind::STATEMENT_SEQUENCE, source_pos, children);
            }
        }

        int32_t function_def()
        {
            return -1;
        }

        int32_t if_stmt()
        {
            AstChildren children;
            int32_t source_pos = source_pos_for_token();
            consume(Token::IF);
            children.push_back(named_expression());
            consume(Token::COLON);
            children.push_back(block());
            while(peek() == Token::ELIF)
            {
                consume(Token::ELIF);
                children.push_back(named_expression());
                consume(Token::COLON);
                children.push_back(block());
            }
            if(peek() == Token::ELSE)
            {
                consume(Token::ELSE);
                consume(Token::COLON);
                children.push_back(block());
            }
            return ast.emplace_back(AstNodeKind::STATEMENT_IF, source_pos, children);
        }

        int32_t class_def()
        {
            return -1;
        }

        int32_t with_stmt()
        {
            return -1;
        }

        int32_t for_stmt()
        {
            return -1;
        }

        int32_t try_stmt()
        {
            return -1;
        }

        int32_t while_stmt()
        {
            AstChildren children;
            int32_t source_pos = source_pos_for_token();
            consume(Token::WHILE);
            children.push_back(named_expression());
            consume(Token::COLON);
            children.push_back(block());

            if(peek() == Token::ELSE)
            {
                consume(Token::ELSE);
                consume(Token::COLON);
                children.push_back(block());
            }
            return ast.emplace_back(AstNodeKind::STATEMENT_WHILE, source_pos, children);
        }

        int32_t match_stmt()
        {
            return -1;
        }

        int32_t compound_statement()
        {
            switch(peek())
            {
            case Token::DEF:
            case Token::AT:
            case Token::ASYNC:
                return function_def();
            case Token::IF:
                return if_stmt();
            case Token::CLASS:
                return class_def();
            case Token::WITH:
                return with_stmt();
            case Token::FOR:
                return for_stmt();
            case Token::TRY:
                return try_stmt();
            case Token::WHILE:
                return while_stmt();

            default:
                return match_stmt();


            }
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
                return simple_stmts();

            }

        }


        int32_t statements()
        {
            int32_t source_pos = source_pos_for_token();
            AstChildren children;
            do {
                children.push_back(statement());
            } while(peek() != Token::DEDENT && peek() != Token::ENDMARKER);
            return ast.emplace_back(AstNodeKind::STATEMENT_SEQUENCE, source_pos, children);
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


    AstVector parse(VirtualMachine &vm, const TokenVector &tv, StartRule start_rule)
    {
        Parser parser(vm, tv);

        return parser.parse(start_rule);
    }

}

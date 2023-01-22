#include "tokenizer.h"
#include "compilation_unit.h"
#include "token.h"
#include <limits>


namespace cl
{

    void tokenise(CompilationUnit &cu)
    {
        const std::wstring &source_code = cu.source_code;

        std::vector<uint32_t> indents;
        indents.push_back(0);
        if(source_code.size() > std::numeric_limits<uint32_t>::max())
        {
            throw std::runtime_error("Too large file");
        }
        uint32_t pos = 0;
        uint32_t max = source_code.size();
        static constexpr uint32_t tabsize = 8;

        enum {
            START_LINE,
            NORMAL,
            IN_PAREN,
            CONTINUED,
            CONTSTR
        } state = START_LINE;


        while(pos < max)
        {
            switch(state)
            {
            case START_LINE:
            {
                uint32_t column = 0;
                while(pos < max)
                {
                    wchar_t c = source_code[pos];
                    if(c == ' ')
                    {
                        ++column;
                    } else if(c == '\t')
                    {
                        column = (column/tabsize + 1)*tabsize;
                    } else if(c == '\f')
                    {
                        column = 0;
                    } else {
                        break;
                    }
                    ++pos;


                }
                if(pos == max) break;

                wchar_t c = source_code[pos];
                if(c == '#' || c == '\r' || c == '\n')
                {
                    // comments or blank lines don't count for the indentation algorithm
                    break;
                }


                break;

            }


            }


        }




    }



}

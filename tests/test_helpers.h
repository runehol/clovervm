#ifndef CL_TEST_HELPERS_H
#define CL_TEST_HELPERS_H

#include "code_object.h"
#include "compilation_unit.h"
#include "parser.h"
#include "thread_state.h"
#include "token.h"
#include "tokenizer.h"
#include "value.h"
#include "virtual_machine.h"

namespace cl::test
{
    class VmTestContext
    {
    public:
        ThreadState *thread() { return vm.get_default_thread(); }

        CodeObject *compile_file(const wchar_t *source)
        {
            return thread()->compile(source, StartRule::File);
        }

        Value run_file(const wchar_t *source)
        {
            return thread()->run(compile_file(source));
        }

    private:
        VirtualMachine vm;
    };

    struct ParsedFile
    {
        explicit ParsedFile(const wchar_t *source)
            : compilation_unit(source), tokens(tokenize(compilation_unit)),
              ast(cl::parse(vm, tokens, StartRule::File))
        {
        }

        VirtualMachine vm;
        CompilationUnit compilation_unit;
        TokenVector tokens;
        AstVector ast;
    };

}  // namespace cl::test

#endif  // CL_TEST_HELPERS_H

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
        VirtualMachine &vm() { return vm_; }
        ThreadState *thread() { return vm_.get_default_thread(); }

        CodeObject *compile_file(const wchar_t *source)
        {
            return thread()->compile(source, StartRule::File);
        }

        Value run_file(const wchar_t *source)
        {
            return thread()->run(compile_file(source));
        }

    private:
        VirtualMachine vm_;
    };

    class FileRunner
    {
    public:
        explicit FileRunner(const wchar_t *source)
            : return_value(test_context_.run_file(source))
        {
        }

        VmTestContext &test_context() { return test_context_; }
        const VmTestContext &test_context() const { return test_context_; }

    private:
        VmTestContext test_context_;

    public:
        Value return_value;
    };

    struct ParsedFile
    {
        explicit ParsedFile(const wchar_t *source)
            : active_thread(vm.get_default_thread()), compilation_unit(source),
              tokens(tokenize(compilation_unit)),
              ast(cl::parse(vm, tokens, StartRule::File))
        {
        }

        VirtualMachine vm;
        ThreadState::ActivationScope active_thread;
        CompilationUnit compilation_unit;
        TokenVector tokens;
        AstVector ast;
    };

}  // namespace cl::test

#endif  // CL_TEST_HELPERS_H

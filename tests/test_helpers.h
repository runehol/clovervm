#ifndef CL_TEST_HELPERS_H
#define CL_TEST_HELPERS_H

#include "builtin_types/module_object.h"
#include "builtin_types/str.h"
#include "bytecode/code_object.h"
#include "compiler/compilation_unit.h"
#include "compiler/parser.h"
#include "compiler/token.h"
#include "compiler/tokenizer.h"
#include "object_model/typed_value.h"
#include "object_model/value.h"
#include "runtime/exception_object.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <gtest/gtest.h>
#include <string>

namespace cl::test
{
    class VmTestContext
    {
    public:
        VirtualMachine &vm() { return vm_; }
        ThreadState *thread() { return vm_.get_default_thread(); }

        ModuleObject *make_test_module_object(TValue<String> name,
                                              Value builtins)
        {
            return thread()->make_module_object(
                name, builtins, Value::None(), Value::None(), Value::None(),
                Value::None(), Value::not_present());
        }

        CodeObject *compile_file(const wchar_t *source)
        {
            return thread()->compile(source, StartRule::File).value();
        }

        Value run_file(const wchar_t *source)
        {
            Expected<CodeObject *> code =
                thread()->compile(source, StartRule::File);
            if(code.has_exception())
            {
                return Value::exception_marker();
            }
            return thread()->run_clovervm_code_object(code.value());
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

    inline TValue<Exception>
    expect_compile_python_error(VmTestContext &context, const wchar_t *source,
                                const wchar_t *expected_type_name,
                                const wchar_t *expected_message = nullptr)
    {
        Expected<CodeObject *> code =
            context.thread()->compile(source, StartRule::File);
        EXPECT_TRUE(code.has_exception());
        EXPECT_TRUE(context.thread()->has_pending_exception());
        EXPECT_EQ(PendingExceptionKind::Object,
                  context.thread()->pending_exception_kind());
        TValue<Exception> exception =
            context.thread()->pending_exception_object();
        EXPECT_EQ(
            std::wstring(expected_type_name),
            string_as_wchar_t(
                exception.extract()->get_shape()->get_class()->get_name()));
        if(expected_message != nullptr)
        {
            EXPECT_EQ(std::wstring(expected_message),
                      string_as_wchar_t(exception.extract()->message.value()));
        }
        return exception;
    }

    struct ParsedFile
    {
        explicit ParsedFile(const wchar_t *source)
            : active_thread(vm.get_default_thread()), compilation_unit(source),
              tokens(tokenize(compilation_unit).value()),
              ast(cl::parse(vm, tokens, StartRule::File).value())
        {
        }

        VirtualMachine vm;
        ThreadState::ActivationScope active_thread;
        CompilationUnit compilation_unit;
        TokenVector tokens;
        AstVector ast;
    };

}  // namespace cl::test

#define CL_ASSERT_CONVERT_TO(type, value)                                      \
    ({                                                                         \
        ::cl::Value cl_assert_convert_value = (value);                         \
        ASSERT_TRUE(::cl::can_convert_to<type>(cl_assert_convert_value));      \
        ::cl::assume_convert_to<type>(cl_assert_convert_value);                \
    })

#endif  // CL_TEST_HELPERS_H

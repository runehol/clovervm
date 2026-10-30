#include "object_model/class_object.h"
#include "object_model/function.h"
#include "runtime/trusted_handler.h"
#include "runtime/virtual_machine.h"
#include "test_helpers.h"
#include "util/fixed_wide_string.h"

#include <gtest/gtest.h>

using namespace cl;

namespace
{
    template <FixedWideString String> struct FixedWideStringProbe
    {
        static constexpr auto value = String;
    };

    static_assert(FixedWideStringProbe<L"dunder method">::value.size() == 13);
    static_assert(FixedWideStringProbe<L"dunder method">::value.c_str()[6] ==
                  L' ');

    Value test_binary_handler(ThreadState *, Value, Value)
    {
        return Value::None();
    }

    using TestHandler =
        TrustedHandlerDefinition<test_binary_handler,
                                 TrustedHandlerEffects::Allocate |
                                     TrustedHandlerEffects::Raise,
                                 TrustedHandlerSemantics::Add>;
}  // namespace

TEST(TrustedHandler, DefinitionRegistersMetadata)
{
    VirtualMachine vm;

    TestHandler::register_with(vm);
    TestHandler::register_with(vm);

    std::optional<TrustedHandlerMetadata> metadata =
        vm.trusted_handler_metadata(
            erase_trusted_handler_target(test_binary_handler));
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(TrustedHandlerArity::Binary, metadata->arity);
    EXPECT_EQ(TrustedHandlerEffects::Allocate | TrustedHandlerEffects::Raise,
              metadata->effects);
    EXPECT_EQ(TrustedHandlerSemantics::Add, metadata->semantics);

    TrustedResolution resolution = TestHandler::resolution();
    EXPECT_EQ(TrustedResolutionKind::TrustedHandler, resolution.kind);
    EXPECT_EQ(TrustedHandlerArity::Binary, resolution.arity);
    EXPECT_EQ(test_binary_handler, resolution.binary);
}

TEST(TrustedHandler, ConflictingRegistrationIsFatal)
{
    EXPECT_DEATH(
        {
            VirtualMachine vm;
            vm.register_trusted_handler(test_binary_handler,
                                        TrustedHandlerEffects::None,
                                        TrustedHandlerSemantics::Add);
            vm.register_trusted_handler(test_binary_handler,
                                        TrustedHandlerEffects::Raise,
                                        TrustedHandlerSemantics::Add);
        },
        "conflicting trusted handler registration");
}

namespace
{
    Value reenter_setitem(ThreadState *thread, Value self, Value key,
                          Value value)
    {
        Owned<Value> owner(self);
        Owned<Value> index(key);
        Owned<Value> replacement(value);
        return thread->call_clovervm_method(
            owner.value(),
            thread->get_machine()->get_or_create_interned_string_value(
                L"apply"),
            index.value(), replacement.value());
    }

    Value reenter_enter(ThreadState *thread, Value self)
    {
        return reenter_setitem(thread, self, Value::from_smi(0),
                               Value::from_smi(7));
    }

    TrustedResolution resolve_reenter_setitem(VirtualMachine *, ShapeKey,
                                              ShapeKey,
                                              TrustedHandlerOperandOrder order,
                                              TrustedHandlerArity arity)
    {
        if(order == TrustedHandlerOperandOrder::Normal &&
           arity == TrustedHandlerArity::Ternary)
            return TrustedResolution::call_trusted(reenter_setitem);
        if(order == TrustedHandlerOperandOrder::Normal &&
           arity == TrustedHandlerArity::Unary)
            return TrustedResolution::call_trusted(reenter_enter);
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }
}  // namespace

TEST(TrustedHandler,
     TernaryAndSpecialMethodReplayPreserveCallerAcrossManagedReentry)
{
    for(bool direct_method: {false, true})
    {
        SCOPED_TRACE(direct_method);
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope activation(thread);
        Owned<TValue<ClassObject>> cls(
            TValue<ClassObject>::from_value_assumed(context.run_file(
                L"class Target:\n"
                L"    def __setitem__(self, key, value):\n"
                L"        raise AssertionError('trusted handler was skipped')\n"
                L"    def __enter__(self):\n"
                L"        raise AssertionError('trusted enter handler was "
                L"skipped')\n"
                L"    def __exit__(self, kind, value, tb):\n"
                L"        pass\n"
                L"    def apply(self, key, value):\n"
                L"        scratch = [value, key, value]\n"
                L"        self.result = scratch[0]\n"
                L"Target\n")));
        auto method = cls.extract()->get_own_property(
            context.vm().get_or_create_interned_string_value(
                direct_method ? L"__enter__" : L"__setitem__"));
        ASSERT_TRUE(has_native_layout<Function>(method));
        method.get_ptr<Function>()
            ->code_object.extract()
            ->trusted_handler_resolver = resolve_reenter_setitem;
        context.vm().register_trusted_handler(
            reenter_setitem, TrustedHandlerEffects::Allocate |
                                 TrustedHandlerEffects::Safepoint |
                                 TrustedHandlerEffects::Raise |
                                 TrustedHandlerEffects::CallPython);
        context.vm().register_trusted_handler(
            reenter_enter, TrustedHandlerEffects::Allocate |
                               TrustedHandlerEffects::Safepoint |
                               TrustedHandlerEffects::Raise |
                               TrustedHandlerEffects::CallPython);
        Owned<Value> receiver(Value::None());
        // Construct through Python to exercise the normal class call boundary.
        Owned<TValue<Function>> factory(TValue<Function>::from_value_assumed(
            context.run_file(L"def make(cls):\n    return cls()\nmake\n")));
        receiver =
            thread->call_clovervm_function(factory.value(), cls.raw_value());
        ASSERT_FALSE(receiver.value().is_exception_marker());
        const wchar_t *source = direct_method ? LR"(
def exercise(target):
    saved = [10, 20, 30]
    for n in range(3):
        with target:
            assert saved == [10, 20, 30]
            assert target.result == 7
exercise
)"
                                              : LR"(
def exercise(target):
    saved = [10, 20, 30]
    for n in range(3):
        target[0] = n
        assert saved == [10, 20, 30]
        assert target.result == n
exercise
)";
        Owned<TValue<Function>> exercise(
            TValue<Function>::from_value_assumed(context.run_file(source)));
        context.vm().set_fire_every_safepoint_for_testing(true);
        context.vm().request_safepoint();
        Value result =
            thread->call_clovervm_function(exercise.value(), receiver.value());
        if(result.is_exception_marker())
        {
            ADD_FAILURE() << string_as_wchar_t(test::native_exception_message(
                thread->pending_exception_object()));
            return;
        }
        ASSERT_EQ(Value::None(), result);
        ASSERT_EQ(Value::None(), thread->call_clovervm_function(
                                     exercise.value(), receiver.value()));
    }
}

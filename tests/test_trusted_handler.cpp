#include "runtime/trusted_handler.h"
#include "runtime/virtual_machine.h"

#include <gtest/gtest.h>

using namespace cl;

namespace
{
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

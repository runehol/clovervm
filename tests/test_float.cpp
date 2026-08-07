#include "builtin_types/float.h"

#include "object_model/owned.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

namespace cl
{
    TEST(FloatRuntime, BoxFloatCreatesFreshExactFloat)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation_scope(context.thread());

        Owned<Value> first(box_float(context.thread(), 2.5));
        Owned<Value> second(box_float(context.thread(), 2.5));

        ASSERT_TRUE(can_convert_to<Float>(first.raw_value()));
        ASSERT_TRUE(can_convert_to<Float>(second.raw_value()));
        EXPECT_DOUBLE_EQ(2.5, first.raw_value().get_ptr<Float>()->value());
        EXPECT_DOUBLE_EQ(2.5, second.raw_value().get_ptr<Float>()->value());
        EXPECT_NE(first.raw_value(), second.raw_value());
        EXPECT_FALSE(context.thread()->has_pending_exception());
    }
}  // namespace cl

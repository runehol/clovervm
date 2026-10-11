#include "builtin_types/float.h"

#include "object_model/class_object.h"
#include "object_model/owned.h"
#include "test_helpers.h"
#include <cmath>

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
    TEST(FloatRuntime, SubclassPayloadPreservesSignedZeroAndZeroInlineCapacity)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation(context.thread());
        Owned<Value> result(
            context.run_file(L"class F(float): pass\nF(-0.0)\n"));
        ASSERT_FALSE(result.value().is_exception_marker());
        ASSERT_TRUE(can_convert_to<Float>(result.value()));
        Float *value = result.value().get_ptr<Float>();
        EXPECT_TRUE(std::signbit(value->value()));
        EXPECT_DOUBLE_EQ(0.0, value->value());
        EXPECT_NE(context.vm().float_class(), value->get_shape()->get_class());
        EXPECT_EQ(0u, value->get_shape()->get_inline_slot_capacity());
    }
}  // namespace cl

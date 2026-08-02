#include "jit/tagged_value_facts.h"
#include "object_model/class_object.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

namespace cl::jit
{
    TEST(JitTaggedValueClass, EncodesNamedClasses)
    {
        TaggedValueClass boolean = TaggedValueClass::boolean();
        EXPECT_EQ(TaggedValueClassKind::MaskedEqual, boolean.kind());
        EXPECT_EQ(value_tag_mask, boolean.mask());
        EXPECT_EQ(value_boolean_tag, boolean.expected());

        TaggedValueClass pointer = TaggedValueClass::pointer();
        EXPECT_EQ(TaggedValueClassKind::MaskedNonZero, pointer.kind());
        EXPECT_EQ(value_ptr_mask, pointer.mask());
        EXPECT_EQ(0u, pointer.expected());
        EXPECT_EQ(boolean, TaggedValueClass::from_encoded(boolean.encoded()));
        EXPECT_EQ(pointer, TaggedValueClass::from_encoded(pointer.encoded()));
    }

    TEST(JitTaggedValueSet, ConstructsAndCombinesFacts)
    {
        TaggedValueSet smi = TaggedValueSet::smi();
        TaggedValueSet boolean = TaggedValueSet::boolean();
        TaggedValueSet smi_or_boolean = TaggedValueSet::smi_or_boolean();

        EXPECT_TRUE(TaggedValueSet::never().is_never());
        EXPECT_EQ(valid_tagged_value_set_bits,
                  TaggedValueSet::unknown().bits());
        EXPECT_TRUE(smi.is_subset_of(smi_or_boolean));
        EXPECT_TRUE(boolean.is_subset_of(smi_or_boolean));
        EXPECT_TRUE(smi.is_disjoint_from(boolean));
        EXPECT_EQ(smi_or_boolean, smi.merge(boolean));
        EXPECT_TRUE(smi.intersect(boolean).is_never());

        EXPECT_EQ(smi, TaggedValueSet::from_class(TaggedValueClass::smi()));
        EXPECT_EQ(boolean,
                  TaggedValueSet::from_class(TaggedValueClass::boolean()));
        EXPECT_EQ(smi_or_boolean, TaggedValueSet::from_class(
                                      TaggedValueClass::smi_or_boolean()));

        TaggedValueSet inline_values =
            TaggedValueSet::from_class(TaggedValueClass::any_inline());
        TaggedValueSet pointers =
            TaggedValueSet::from_class(TaggedValueClass::pointer());
        EXPECT_EQ(TaggedValueSet::unknown(), inline_values.merge(pointers));
        EXPECT_TRUE(inline_values.is_disjoint_from(pointers));

        TaggedValueClass broad_boolean = TaggedValueClass::masked_equal(
            uint8_t(value_special_mask), uint8_t(value_boolean_tag));
        EXPECT_EQ(boolean, TaggedValueSet::from_class(broad_boolean));

        TaggedValueClass invalid_exact = TaggedValueClass::masked_equal(
            uint8_t(value_tag_mask), uint8_t(0x07));
        EXPECT_TRUE(TaggedValueSet::from_class(invalid_exact).is_never());
    }

    TEST(JitTaggedValueSet, ConstructsFromShapeKeys)
    {
        EXPECT_EQ(TaggedValueSet::boolean(),
                  TaggedValueSet::from_shape_key(
                      ShapeKey::from_value(Value::True())));

        test::VmTestContext context;
        ShapeKey float_shape = ShapeKey::from_shape(
            context.vm().float_class()->get_instance_root_shape());
        EXPECT_EQ(TaggedValueSet::pointer(),
                  TaggedValueSet::from_shape_key(float_shape));
    }

}  // namespace cl::jit

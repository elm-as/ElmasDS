#include "primitives/bit_operations.hpp"

#include <gtest/gtest.h>

namespace elmasds::bits {
namespace {

TEST(BitOperationsTest, ExtractBitsSelectsContiguousRange) {
    constexpr u32 word = 0xABCD1234U;
    EXPECT_EQ(extract_bits(word, 0, 4), 0x4U);
    EXPECT_EQ(extract_bits(word, 4, 8), 0x23U);
    EXPECT_EQ(extract_bits(word, 24, 8), 0xABU);
    EXPECT_EQ(extract_bits(word, 0, 32), word);
}

TEST(BitOperationsTest, TestAndWriteBitRespectImmuteSemantics) {
    constexpr u32 original = 0b1010U;
    EXPECT_TRUE(test_bit(original, 1));
    EXPECT_FALSE(test_bit(original, 2));

    EXPECT_EQ(write_bit(original, 2, true), 0b1110U);
    EXPECT_EQ(write_bit(original, 1, false), 0b1000U);
}

TEST(BitOperationsTest, SignExtendHandlesNegativeAndPositiveWidths) {
    EXPECT_EQ(sign_extend(0x7FU, 8), 127);
    EXPECT_EQ(sign_extend(0x80U, 8), -128);
    EXPECT_EQ(sign_extend(0x00FFFFFFU, 24), -1);
    EXPECT_EQ(sign_extend(0x007FFFFFU, 24), 0x007FFFFF);
}

TEST(BitOperationsTest, RotateRightAndCountLeadingZerosMatchArmSemantics) {
    EXPECT_EQ(rotate_right_32(0x00000001U, 1), 0x80000000U);
    EXPECT_EQ(rotate_right_32(0x12345678U, 32), 0x12345678U);
    EXPECT_EQ(count_leading_zeros_32(0U), 32U);
    EXPECT_EQ(count_leading_zeros_32(0x80000000U), 0U);
    EXPECT_EQ(count_leading_zeros_32(0x00000010U), 27U);
}

}  // namespace
}  // namespace elmasds::bits

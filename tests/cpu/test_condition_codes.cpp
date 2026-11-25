#include "cpu/arm_architecture/condition_codes.hpp"

#include <gtest/gtest.h>

namespace elmasds::cpu {
namespace {

TEST(ConditionCodesTest, EvaluatesFlagsCorrectly) {
    const StatusRegister all_clear = StatusRegister(0U).with_nzcv(false, false, false, false);
    const StatusRegister z_set = StatusRegister(0U).with_nzcv(false, true, false, false);
    const StatusRegister c_set = StatusRegister(0U).with_nzcv(false, false, true, false);
    const StatusRegister n_set = StatusRegister(0U).with_nzcv(true, false, false, false);
    const StatusRegister nv_set = StatusRegister(0U).with_nzcv(true, false, false, true);

    EXPECT_TRUE(evaluate_condition(ConditionCode::Eq, z_set));
    EXPECT_FALSE(evaluate_condition(ConditionCode::Eq, all_clear));
    EXPECT_TRUE(evaluate_condition(ConditionCode::Ne, all_clear));

    EXPECT_TRUE(evaluate_condition(ConditionCode::Hi, c_set));
    EXPECT_FALSE(evaluate_condition(ConditionCode::Hi, z_set));
    EXPECT_TRUE(evaluate_condition(ConditionCode::Ls, z_set));

    EXPECT_TRUE(evaluate_condition(ConditionCode::Ge, nv_set));
    EXPECT_TRUE(evaluate_condition(ConditionCode::Lt, n_set));
    EXPECT_TRUE(evaluate_condition(ConditionCode::Gt, all_clear));
    EXPECT_FALSE(evaluate_condition(ConditionCode::Gt, z_set));
    EXPECT_TRUE(evaluate_condition(ConditionCode::Le, n_set));

    EXPECT_TRUE(evaluate_condition(ConditionCode::Al, all_clear));
    EXPECT_FALSE(evaluate_condition(ConditionCode::Nv, nv_set));
}

}  // namespace
}  // namespace elmasds::cpu

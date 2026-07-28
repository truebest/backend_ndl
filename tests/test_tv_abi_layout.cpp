/* SPDX-License-Identifier: MIT */
#include "backend_ndl_window_id_abi.h"

#include <gtest/gtest.h>

#include <stddef.h>

TEST(BackendNdlWindowIdAbi, HdrLayout) {
    EXPECT_EQ(sizeof(NDL_DIRECTVIDEO_HDR_INFO_T), 60u);
    EXPECT_EQ(offsetof(NDL_DIRECTVIDEO_HDR_INFO_T, matrixCoeffs), 56u);
}

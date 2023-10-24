/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "meminfo.h"

#include <cstdio>

#include "gtest/gtest.h"

namespace OHOS {
namespace MemInfo {
using namespace testing;
using namespace testing::ext;

class MemInfoTest : public testing::Test {
public:
    static void SetUpTestCase();
    static void TearDownTestCase();
    void SetUp();
    void TearDown();
};

void MemInfoTest::SetUpTestCase()
{
}

void MemInfoTest::TearDownTestCase()
{
}

void MemInfoTest::SetUp()
{
}

void MemInfoTest::TearDown()
{
}

HWTEST_F(MemInfoTest, GetRssByPid_Test_001, TestSize.Level1)
{
    int pid = 1;
    uint64_t size = 0;
    size = GetRssByPid(pid);
    std::cout << "size = " << size << std::endl;
    ASSERT_EQ(size > 0, true);
}

HWTEST_F(MemInfoTest, GetRssByPid_Test_002, TestSize.Level1)
{
    int pid = -1;
    uint64_t size = 0;
    size = GetRssByPid(pid);
    ASSERT_EQ(size == 0, true);
}

HWTEST_F(MemInfoTest, GetPssByPid_Test_001, TestSize.Level1)
{
    int pid = 1;
    uint64_t size = 0;
    size = GetPssByPid(pid);
    std::cout << "size = " << size << std::endl;
    system("cat /proc/1/smaps_rollup");
    ASSERT_EQ(size > 0, true);
}

HWTEST_F(MemInfoTest, GetPssByPid_Test_002, TestSize.Level1)
{
    int pid = -1;
    uint64_t size = 0;
    size = GetPssByPid(pid);
    ASSERT_EQ(size == 0, true);
}

HWTEST_F(MemInfoTest, GetSwapPssByPid_Test_001, TestSize.Level1)
{
    int pid = 1;
    uint64_t size = 0;
    size = GetSwapPssByPid(pid);
    std::cout << "size = " << size << std::endl;
    system("cat /proc/1/smaps_rollup");
    ASSERT_EQ(size >= 0, true);
}

HWTEST_F(MemInfoTest, GetSwapPssByPid_Test_002, TestSize.Level1)
{
    int pid = -1;
    uint64_t size = 0;
    size = GetSwapPssByPid(pid);
    ASSERT_EQ(size == 0, true);
}

HWTEST_F(MemInfoTest, GetGraphicsMemory_Test, TestSize.Level1)
{
    int pid = 1;
    uint64_t gl = 0;
    uint64_t graph = 0;
    GetGraphicsMemory(pid, gl, graph);
    ASSERT_EQ(gl == 0, true);
}
}
}

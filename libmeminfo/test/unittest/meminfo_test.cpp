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

#include <cstdio>
#include <dlfcn.h>

#include "gtest/gtest.h"
#include "meminfo.h"

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

bool isDlopenSucc(std::string soName, std::string funcName)
{
    auto libMemClientHandle = dlopen(soName.c_str(), RTLD_NOW);
    if (!libMemClientHandle) {
        return false;
    }
    using GetDmaVecFunc = DmaNodeInfo* (*)(int*, int);
    auto getDmaInfoFunc = reinterpret_cast<GetDmaVecFunc>(dlsym(libMemClientHandle, funcName.c_str()));
    if (!getDmaInfoFunc) {
        dlclose(libMemClientHandle);
        return false;
    }
    dlclose(libMemClientHandle);
    return true;
}

HWTEST_F(MemInfoTest, GetDmaInfo_Test_001, TestSize.Level1)
{
    if (!isDlopenSucc("libmemmgrclient.z.so", "GetDmaArr")) {
        return;
    }

    int pid = -1;
    std::vector<DmaNodeInfoWrapper> dmaVec = GetDmaInfo(pid);
    uint64_t size = dmaVec.size();
    std::cout << "size = " << size << std::endl;
    ASSERT_EQ(size > 0, true);
}

HWTEST_F(MemInfoTest, GetDmaValueByPidList_Test_001, TestSize.Level1)
{
    if (!isDlopenSucc("libmemmgrclient.z.so", "GetDmaValueByPidList")) {
        return;
    }

    std::vector<int> pidList;
    for (int i = 1; i <= 3000; ++i) {
        pidList.push_back(i);
    }
    int64_t dmaSum = GetDmaValueByPidList(pidList);
    std::cout << "dmaSum = " << dmaSum << std::endl;
    ASSERT_EQ(dmaSum >= 0, true);
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

HWTEST_F(MemInfoTest, GetPssAndSwapPssByPid_Test_001, TestSize.Level1)
{
    int pid = 1;
    uint64_t size = 0;
    size = GetPssAndSwapPssByPid(pid);
    std::cout << "size = " << size << std::endl;
    system("cat /proc/1/smaps_rollup");
    ASSERT_EQ(size >= 0, true);
}

HWTEST_F(MemInfoTest, GetPssAndSwapPssByPid_Test_002, TestSize.Level1)
{
    int pid = -1;
    uint64_t size = 0;
    size = GetPssAndSwapPssByPid(pid);
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

HWTEST_F(MemInfoTest, GetAppsTotalMemory_Test, TestSize.Level1)
{
    if (!isDlopenSucc("libmemmgrclient.z.so", "GetDmaValueByPidList")) {
        return;
    }

    std::vector<int> pidList;
    for (int i = 1; i <= 3000; ++i) {
        pidList.push_back(i);
    }
    int64_t totalMem = GetAppsTotalMemory(pidList);
    std::cout << "totalMem = " << totalMem << std::endl;
    ASSERT_EQ(totalMem >= 0, true);
}

}
}

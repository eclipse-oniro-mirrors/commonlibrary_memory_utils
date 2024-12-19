/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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
#include <climits>
#include <thread>

#include "gtest/gtest.h"
#include "purgeable_memory.h"

namespace {
using namespace testing;
using namespace testing::ext;

struct AlphabetInitParam {
    char start;
    char end;
};

struct AlphabetModifyParam {
    char src;
    char dst;
};

static constexpr int PRINT_INTERVAL_SECONDS = 1;
static constexpr int RECLAIM_INTERVAL_SECONDS = 1;
static constexpr int MODIFY_INTERVAL_SECONDS = 2;

bool InitData(void *data, size_t size, char start, char end);
bool ModifyData(void *data, size_t size, char src, char dst);
bool InitAlphabet(void *data, size_t size, void *param);
bool ModifyAlphabetX2Y(void *data, size_t size, void *param);
void LoopPrintAlphabet(OH_PurgeableMemory *pdata, unsigned int loopCount);
bool ReclaimPurgeable(void);
void LoopReclaimPurgeable(unsigned int loopCount);
void ModifyPurgMemByFunc(OH_PurgeableMemory *pdata, OH_PurgeableMemory_ModifyFunc Modfunc, void *param);

class PurgeableMemoryTest : public testing::Test {
public:
    static void SetUpTestCase();
    static void TearDownTestCase();
    void SetUp() override;
    void TearDown() override;
};

void PurgeableMemoryTest::SetUpTestCase()
{
}

void PurgeableMemoryTest::TearDownTestCase()
{
}

void PurgeableMemoryTest::SetUp()
{
}

void PurgeableMemoryTest::TearDown()
{
}

HWTEST_F(PurgeableMemoryTest, MultiObjCreateTest, TestSize.Level1)
{
    const char alphabetFinal[] = "BBCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    struct AlphabetInitParam initPara = {'A', 'Z'};
    OH_PurgeableMemory *pobj1 = OH_PurgeableMemory_Create(27, InitAlphabet, &initPara);
    LoopPrintAlphabet(pobj1, 1);
    struct AlphabetModifyParam a2b = {'A', 'B'};
    ModifyPurgMemByFunc(pobj1, ModifyAlphabetX2Y, static_cast<void *>(&a2b));
    LoopPrintAlphabet(pobj1, 1);
    LoopReclaimPurgeable(1);

    if (OH_PurgeableMemory_BeginRead(pobj1)) {
        ASSERT_STREQ(alphabetFinal, static_cast<char *>(OH_PurgeableMemory_GetContent(pobj1)));
        OH_PurgeableMemory_EndRead(pobj1);
    }

    EXPECT_EQ(OH_PurgeableMemory_Destroy(pobj1), true);
}


HWTEST_F(PurgeableMemoryTest, WriteTest, TestSize.Level1)
{
    const char alphabet[] = "CCCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    struct AlphabetInitParam initPara = {'A', 'Z'};
    OH_PurgeableMemory *pobj = OH_PurgeableMemory_Create(27, InitAlphabet, &initPara);
    LoopReclaimPurgeable(1);

    struct AlphabetModifyParam a2b = {'A', 'B'};
    struct AlphabetModifyParam b2c = {'B', 'C'};
    ModifyPurgMemByFunc(pobj, ModifyAlphabetX2Y, static_cast<void *>(&a2b));
    ModifyPurgMemByFunc(pobj, ModifyAlphabetX2Y, static_cast<void *>(&b2c));

    if (OH_PurgeableMemory_BeginRead(pobj)) {
        ASSERT_STREQ(alphabet, static_cast<char *>(OH_PurgeableMemory_GetContent(pobj)));
        OH_PurgeableMemory_EndRead(pobj);
    } else {
        std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
    }

    OH_PurgeableMemory_Destroy(pobj);
    LoopReclaimPurgeable(3);
}

bool InitData(void *data, size_t size, char start, char end)
{
    char *str = (char *)data;
    size_t len = 0;
    for (char ch = start; ch <= end && len < size; ch++) {
        str[len++] = ch;
    }
    str[len] = 0;
    return true;
}

bool InitAlphabet(void *data, size_t size, void *param)
{
    struct AlphabetInitParam *para = (struct AlphabetInitParam *)param;
    std::cout << "inter " << __func__ << std::endl;
    bool ret = InitData(data, size, para->start, para->end);
    std::cout << "quit " << __func__ << ": " << para->start << "-" << para->end <<
        ", data=[" << (char *)data << "]" << ", ret=" << (ret ? "true" : "false") << std::endl;
    return ret;
}

bool ModifyData(void *data, size_t size, char src, char dst)
{
    char *str = (char *)data;
    size_t i = 0;
    for (; i < size && str[i]; i++) {
        if (str[i] == src) {
            str[i] = dst;
        }
    }
    str[i] = 0;
    return true;
}

bool ModifyAlphabetX2Y(void *data, size_t size, void *param)
{
    struct AlphabetModifyParam *para = (struct AlphabetModifyParam *)param;
    std::cout << "inter " << __func__ << ": " << para->src << "->" << para->dst <<
        ", data=[" << (char *)data << "]" << std::endl;
    bool ret = ModifyData(data, size, para->src, para->dst);
    std::cout << "quit , data=[" << (char *)data << "]" << __func__ <<
        ", ret=" << (ret ? "true" : "false") << std::endl;
    return ret;
}

void LoopPrintAlphabet(OH_PurgeableMemory *pdata, unsigned int loopCount)
{
    std::cout << "inter " << __func__ << std::endl;
    for (unsigned int i = 0; i < loopCount; i++) {
        if (!OH_PurgeableMemory_BeginRead(pdata)) {
            std::cout << __func__ << ": " << i << ". ERROR! BeginRead failed." << std::endl;
            break;
        }
        std::cout << __func__ << ": " << i << ". data=[" <<
            (char *)OH_PurgeableMemory_GetContent(pdata) << "]" << std::endl;
        OH_PurgeableMemory_EndRead(pdata);
        std::this_thread::sleep_for(std::chrono::seconds(PRINT_INTERVAL_SECONDS));
    }
    std::cout << "quit " << __func__ << std::endl;
}

bool ReclaimPurgeable(void)
{
    FILE *f = fopen("/proc/sys/kernel/purgeable", "w");
    if (!f) {
        std::cout << __func__ << ": open file failed" << std::endl;
        return false;
    }
    bool succ = true;
    if (fputs("1", f) == EOF) {
        succ = false;
    }

    if (fclose(f) == EOF) {
        std::cout << __func__ << ": close file failed" << std::endl;
    }

    return succ;
}

void LoopReclaimPurgeable(unsigned int loopCount)
{
    bool ret = false;
    std::cout << "inter " << __func__ << std::endl;
    for (unsigned int i = 0; i < loopCount; i++) {
        ret = ReclaimPurgeable();
        std::cout << __func__ << ": " << i << ". Reclaim result=" << (ret ? "succ" : "fail") << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(RECLAIM_INTERVAL_SECONDS)); /* wait reclaim finish */
    }
    std::cout << "quit " << __func__ << std::endl;
}

void ModifyPurgMemByFunc(OH_PurgeableMemory *pdata, OH_PurgeableMemory_ModifyFunc Modfunc, void *param)
{
    if (OH_PurgeableMemory_BeginWrite(pdata)) {
        std::this_thread::sleep_for(std::chrono::seconds(MODIFY_INTERVAL_SECONDS));
        OH_PurgeableMemory_AppendModify(pdata, Modfunc, param);
        std::cout<< __func__ << " after mod data=[" << (char *)OH_PurgeableMemory_GetContent(pdata) << "]" << std::endl;

        std::cout << __func__ << " data=[" << (char *)OH_PurgeableMemory_GetContent(pdata) << "]" << std::endl;
        OH_PurgeableMemory_EndWrite(pdata);
    }
}
} /* namespace */

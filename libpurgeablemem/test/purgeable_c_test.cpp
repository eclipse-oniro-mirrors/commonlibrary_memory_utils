/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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
#include "purgeable_mem_c.h"

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

bool InitData_(void *data, size_t size, char start, char end);
bool ModifyData_(void *data, size_t size, char src, char dst);
bool InitAlphabet(void *data, size_t size, void *param);
bool ModifyAlphabetX2Y(void *data, size_t size, void *param);
void LoopPrintAlphabet(struct PurgMem *pdata, unsigned int loopCount);
bool ReclaimPurgeable(void);
void LoopReclaimPurgeable(unsigned int loopCount);
void ModifyPurgMemByFunc(struct PurgMem *pdata, PurgMemModifyFunc Modfunc, void *param);

class PurgeableCTest : public testing::Test {
public:
    static void SetUpTestCase();
    static void TearDownTestCase();
    void SetUp();
    void TearDown();
};

void PurgeableCTest::SetUpTestCase()
{
}

void PurgeableCTest::TearDownTestCase()
{
}

void PurgeableCTest::SetUp()
{
}

void PurgeableCTest::TearDown()
{
}

HWTEST_F(PurgeableCTest, MultiObjCreateTest, TestSize.Level1)
{
    const char alphabetFinal[] = "BBCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    struct AlphabetInitParam initPara = {'A', 'Z'};
    PurgMemCreate(0, InitAlphabet, &initPara);
    struct PurgMem *pobj1 = PurgMemCreate(27, InitAlphabet, &initPara);
    LoopPrintAlphabet(pobj1, 1);
    struct AlphabetModifyParam a2b = {'A', 'B'};
    ModifyPurgMemByFunc(pobj1, ModifyAlphabetX2Y, static_cast<void *>(&a2b));
    LoopPrintAlphabet(pobj1, 1);
    LoopReclaimPurgeable(1);

    struct PurgMem *pobj2 = PurgMemCreate(27, InitAlphabet, &initPara);
    LoopPrintAlphabet(pobj2, 1);
    ModifyPurgMemByFunc(pobj2, ModifyAlphabetX2Y, static_cast<void *>(&a2b));
    LoopPrintAlphabet(pobj2, 1);

    if (PurgMemBeginRead(pobj1)) {
        ASSERT_STREQ(alphabetFinal, static_cast<char *>(PurgMemGetContent(pobj1)));
        PurgMemEndRead(pobj1);
    } else {
        std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
    }

    if (PurgMemBeginRead(pobj2)) {
        ASSERT_STREQ(alphabetFinal, static_cast<char *>(PurgMemGetContent(pobj2)));
        PurgMemEndRead(pobj2);
    } else {
        std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
    }

    PurgMemDestroy(pobj2);
    PurgMemDestroy(pobj1);
}

HWTEST_F(PurgeableCTest, ReadTest, TestSize.Level1)
{
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    struct AlphabetInitParam initPara = {'A', 'Z'};
    struct PurgMem *pobj = PurgMemCreate(27, InitAlphabet, &initPara);
    LoopReclaimPurgeable(1);

    unsigned int loopCount = 3;
    /* loop read content */
    for (unsigned int i = 0; i < loopCount; i++) {
        if (!PurgMemBeginRead(pobj)) {
            std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
            continue;
        }
        ASSERT_STREQ(alphabet, static_cast<char *>(PurgMemGetContent(pobj)));
        PurgMemEndRead(pobj);
    }

    PurgMemDestroy(pobj);
}

HWTEST_F(PurgeableCTest, WriteTest, TestSize.Level1)
{
    const char alphabet[] = "CCCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    struct AlphabetInitParam initPara = {'A', 'Z'};
    struct PurgMem *pobj = PurgMemCreate(27, InitAlphabet, &initPara);
    LoopReclaimPurgeable(1);

    struct AlphabetModifyParam a2b = {'A', 'B'};
    struct AlphabetModifyParam b2c = {'B', 'C'};
    ModifyPurgMemByFunc(pobj, ModifyAlphabetX2Y, static_cast<void *>(&a2b));
    ModifyPurgMemByFunc(pobj, ModifyAlphabetX2Y, static_cast<void *>(&b2c));

    if (PurgMemBeginRead(pobj)) {
        ASSERT_STREQ(alphabet, static_cast<char *>(PurgMemGetContent(pobj)));
        PurgMemEndRead(pobj);
    } else {
        std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
    }

    PurgMemDestroy(pobj);
    LoopReclaimPurgeable(3);
}

HWTEST_F(PurgeableCTest, ReadWriteTest, TestSize.Level1)
{
    const char alphabet[] = "DDDDEFGHIJKLMNOPQRSTUVWXYZ\0";
    struct AlphabetInitParam initPara = {'A', 'Z'};
    struct PurgMem *pobj = PurgMemCreate(27, InitAlphabet, &initPara);
    LoopReclaimPurgeable(1);
    LoopPrintAlphabet(pobj, 1);

    struct AlphabetModifyParam a2b = {'A', 'B'};
    struct AlphabetModifyParam b2c = {'B', 'C'};
    struct AlphabetModifyParam c2d = {'C', 'D'};
    ModifyPurgMemByFunc(pobj, ModifyAlphabetX2Y, static_cast<void *>(&a2b));
    ModifyPurgMemByFunc(pobj, ModifyAlphabetX2Y, static_cast<void *>(&b2c));
    ModifyPurgMemByFunc(pobj, ModifyAlphabetX2Y, static_cast<void *>(&c2d));

    if (PurgMemBeginRead(pobj)) {
        ASSERT_STREQ(alphabet, static_cast<char *>(PurgMemGetContent(pobj)));
        PurgMemEndRead(pobj);
    } else {
        std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
    }

    PurgMemDestroy(pobj);
}

bool InitData_(void *data, size_t size, char start, char end)
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
    bool ret = InitData_(data, size, para->start, para->end);
    std::cout << "quit " << __func__ << ": " << para->start << "-" << para->end <<
        ", data=[" << (char *)data << "]" << ", ret=" << (ret ? "true" : "false") << std::endl;
    return ret;
}

bool ModifyData_(void *data, size_t size, char src, char dst)
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
    bool ret = ModifyData_(data, size, para->src, para->dst);
    std::cout << "quit , data=[" << (char *)data << "]" << __func__ <<
        ", ret=" << (ret ? "true" : "false") << std::endl;
    return ret;
}

void LoopPrintAlphabet(struct PurgMem *pdata, unsigned int loopCount)
{
    std::cout << "inter " << __func__ << std::endl;
    for (unsigned int i = 0; i < loopCount; i++) {
        if (!PurgMemBeginRead(pdata)) {
            std::cout << __func__ << ": " << i << ". ERROR! BeginRead failed." << std::endl;
            break;
        }
        std::cout << __func__ << ": " << i << ". data=[" <<
            (char *)PurgMemGetContent(pdata) << "]" << std::endl;
        PurgMemEndRead(pdata);
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

void ModifyPurgMemByFunc(struct PurgMem *pdata, PurgMemModifyFunc Modfunc, void *param)
{
    if (!PurgMemBeginWrite(pdata)) {
        std::cout << __func__ << ": ERROR! BeginWrite failed." << std::endl;
        return;
    }
    std::this_thread::sleep_for(std::chrono::seconds(MODIFY_INTERVAL_SECONDS));
    std::cout << __func__ << " before mod data=[" << (char *)PurgMemGetContent(pdata) << "]" << std::endl;
    PurgMemAppendModify(pdata, Modfunc, param);
    std::cout<< __func__ << " after mod data=[" << (char *)PurgMemGetContent(pdata) << "]" << std::endl;

    std::cout << __func__ << " data=[" << (char *)PurgMemGetContent(pdata) << "]" << std::endl;
    PurgMemEndWrite(pdata);
}
} /* namespace */

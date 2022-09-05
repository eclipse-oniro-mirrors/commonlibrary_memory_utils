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
#include <thread>
#include <memory> /* unique_ptr */

#include "gtest/gtest.h"
#include "purgeable_mem.h"

namespace OHOS {
namespace PurgeableMem {
using namespace testing;
using namespace testing::ext;

static constexpr int PRINT_INTERVAL_SECONDS = 1;
static constexpr int RECLAIM_INTERVAL_SECONDS = 1;
static constexpr int MODIFY_INTERVAL_SECONDS = 2;
void LoopPrintAlphabet(PurgeableMem *pdata, unsigned int loopCount);
bool ReclaimPurgeable(void);
void LoopReclaimPurgeable(unsigned int loopCount);
void ModifyPurgMemByBuilder(PurgeableMem *pdata, std::unique_ptr<PurgeableMemBuilder> mod);

class TestDataBuilder : public PurgeableMemBuilder {
public:
    TestDataBuilder(char start, char end)
    {
        this->start = start;
        this->end = end;
    }

    bool Build(void *data, size_t size)
    {
        if (size <= 0) {
            return true;
        }
        char *str = (char *)data;
        size_t len = 0;
        for (char ch = start; ch <= end && len < size; ch++) {
            str[len++] = ch;
        }
        str[size - 1] = 0;
        std::cout << "rebuild addr("<< (unsigned long long)str <<") " <<
            start << "~" << end << ", data=[" << str << "]" << std::endl;
        return true;
    }

    ~TestDataBuilder()
    {
        std::cout << "~TestDataBuilder" << std::endl;
    }

private:
    char start, end;
};

class TestDataModifier : public PurgeableMemBuilder {
public:
    TestDataModifier(char from, char to)
    {
        this->from = from;
        this->to = to;
    }

    bool Build(void *data, size_t size)
    {
        char *str = (char *)data;
        for (size_t i = 0; str[i] && i < size; i++) {
            if (str[i] == from) {
                str[i] = to;
            }
        }
        std::cout << "modify addr("<< (unsigned long long)str <<") " <<
            from << "->" << to << ", data=[" << str << "]" << std::endl;
        return true;
    }

    ~TestDataModifier()
    {
        std::cout << "~TestDataModifier" << std::endl;
    }

private:
    char from, to;
};

class TestBigDataBuilder : public PurgeableMemBuilder {
public:
    explicit TestBigDataBuilder(char target)
    {
        this->target = target;
    }

    bool Build(void *data, size_t size)
    {
        if (size <= 0) {
            return true;
        }
        char *str = (char *)data;
        size_t len = 0;
        for (char ch = target; len < size;) {
            str[len++] = ch;
        }
        str[size - 1] = 0;
        return true;
    }

    ~TestBigDataBuilder()
    {
        std::cout << "~TestBigDataBuilder" << std::endl;
    }

private:
    char target;
};

class PurgeableCppTest : public testing::Test {
public:
    static void SetUpTestCase();
    static void TearDownTestCase();
    void SetUp();
    void TearDown();
};

void PurgeableCppTest::SetUpTestCase()
{
}

void PurgeableCppTest::TearDownTestCase()
{
}

void PurgeableCppTest::SetUp()
{
}

void PurgeableCppTest::TearDown()
{
}

HWTEST_F(PurgeableCppTest, MultiObjCreateTest, TestSize.Level1)
{
    const char alphabetFinal[] = "BBCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<TestDataBuilder>('A', 'Z');
    std::unique_ptr<PurgeableMemBuilder> builder2 = std::make_unique<TestDataBuilder>('A', 'Z');
    PurgeableMem pobj1(27, std::move(builder));
    std::unique_ptr<PurgeableMemBuilder> mod = std::make_unique<TestDataModifier>('A', 'B');
    std::unique_ptr<PurgeableMemBuilder> mod2 = std::make_unique<TestDataModifier>('A', 'B');

    LoopPrintAlphabet(&pobj1, 1);
    ModifyPurgMemByBuilder(&pobj1, std::move(mod));
    LoopPrintAlphabet(&pobj1, 1);
    LoopReclaimPurgeable(1);

    PurgeableMem pobj2(27, std::move(builder2));
    LoopPrintAlphabet(&pobj2, 1);
    ModifyPurgMemByBuilder(&pobj2, std::move(mod2));
    LoopPrintAlphabet(&pobj2, 1);

    if (pobj1.BeginRead()) {
        ASSERT_STREQ(alphabetFinal, (char *)(pobj1.GetContent()));
        pobj1.EndRead();
    } else {
        std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
    }

    if (pobj2.BeginRead()) {
        ASSERT_STREQ(alphabetFinal, (char *)(pobj2.GetContent()));
        pobj2.EndRead();
    } else {
        std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
    }
}

HWTEST_F(PurgeableCppTest, ReadTest, TestSize.Level1)
{
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<TestDataBuilder>('A', 'Z');
    PurgeableMem *pobj = new PurgeableMem(27, std::move(builder));

    LoopReclaimPurgeable(1);

    unsigned int loopCount = 3;
    /* loop read content */
    for (unsigned int i = 0; i < loopCount; i++) {
        if (!pobj->BeginRead()) {
            std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
            continue;
        }
        ASSERT_STREQ(alphabet, (char *)(pobj->GetContent()));
        pobj->EndRead();
    }

    delete pobj;
    pobj = nullptr;
}

HWTEST_F(PurgeableCppTest, WriteTest, TestSize.Level1)
{
    const char alphabet[] = "CCCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<TestDataBuilder>('A', 'Z');
    PurgeableMem *pobj = new PurgeableMem(27, std::move(builder));
    LoopReclaimPurgeable(1);

    std::unique_ptr<PurgeableMemBuilder> modA2B = std::make_unique<TestDataModifier>('A', 'B');
    std::unique_ptr<PurgeableMemBuilder> modB2C = std::make_unique<TestDataModifier>('B', 'C');
    ModifyPurgMemByBuilder(pobj, std::move(modA2B));
    ModifyPurgMemByBuilder(pobj, std::move(modB2C));

    if (pobj->BeginRead()) {
        ASSERT_STREQ(alphabet, (char *)(pobj->GetContent()));
        pobj->EndRead();
    } else {
        std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
    }

    delete pobj;
    pobj = nullptr;
}

HWTEST_F(PurgeableCppTest, ReadWriteTest, TestSize.Level1)
{
    const char alphabet[] = "DDDDEFGHIJKLMNOPQRSTUVWXYZ\0";
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<TestDataBuilder>('A', 'Z');
    PurgeableMem *pobj = new PurgeableMem(27, std::move(builder));

    LoopReclaimPurgeable(1);
    LoopPrintAlphabet(pobj, 1);

    std::unique_ptr<PurgeableMemBuilder> modA2B = std::make_unique<TestDataModifier>('A', 'B');
    std::unique_ptr<PurgeableMemBuilder> modB2C = std::make_unique<TestDataModifier>('B', 'C');
    std::unique_ptr<PurgeableMemBuilder> modC2D = std::make_unique<TestDataModifier>('C', 'D');
    ModifyPurgMemByBuilder(pobj, std::move(modA2B));
    ModifyPurgMemByBuilder(pobj, std::move(modB2C));
    ModifyPurgMemByBuilder(pobj, std::move(modC2D));

    if (pobj->BeginRead()) {
        ASSERT_STREQ(alphabet, (char *)(pobj->GetContent()));
        pobj->EndRead();
    } else {
        std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::seconds(2 * PRINT_INTERVAL_SECONDS));
    delete pobj;
    pobj = nullptr;
}

HWTEST_F(PurgeableCppTest, MutiPageReadTest, TestSize.Level1)
{
    char alphabet[4098];
    size_t len = 0;
    for (char ch = 'A'; len < 4098;) {
        alphabet[len++] = ch;
    }
    alphabet[4097] = 0;
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<TestBigDataBuilder>('A');
    PurgeableMem *pobj = new PurgeableMem(4098, std::move(builder));

    LoopReclaimPurgeable(1);

    unsigned int loopCount = 3;
    /* loop read content */
    for (unsigned int i = 0; i < loopCount; i++) {
        if (!pobj->BeginRead()) {
            std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
            continue;
        }
        ASSERT_STREQ(alphabet, (char *)(pobj->GetContent()));
        pobj->EndRead();
    }

    delete pobj;
    pobj = nullptr;
}

HWTEST_F(PurgeableCppTest, MutiPageWriteTest, TestSize.Level1)
{
    char alphabet[4098];
    size_t len = 0;
    for (char ch = 'C'; len < 4098;) {
        alphabet[len++] = ch;
    }
    alphabet[4097] = 0;
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<TestBigDataBuilder>('A');
    PurgeableMem *pobj = new PurgeableMem(4098, std::move(builder));

    LoopReclaimPurgeable(1);

    std::unique_ptr<PurgeableMemBuilder> modA2B = std::make_unique<TestDataModifier>('A', 'B');
    std::unique_ptr<PurgeableMemBuilder> modB2C = std::make_unique<TestDataModifier>('B', 'C');
    ModifyPurgMemByBuilder(pobj, std::move(modA2B));
    ModifyPurgMemByBuilder(pobj, std::move(modB2C));

    if (pobj->BeginRead()) {
        ASSERT_STREQ(alphabet, (char *)(pobj->GetContent()));
        pobj->EndRead();
    } else {
        std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
    }

    delete pobj;
    pobj = nullptr;
}

HWTEST_F(PurgeableCppTest, MutiPageReadWriteTest, TestSize.Level1)
{
    char alphabet[4098];
    size_t len = 0;
    for (char ch = 'D'; len < 4098;) {
        alphabet[len++] = ch;
    }
    alphabet[4097] = 0;
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<TestBigDataBuilder>('A');
    PurgeableMem *pobj = new PurgeableMem(4098, std::move(builder));
    LoopReclaimPurgeable(1);
    LoopPrintAlphabet(pobj, 1);

    std::unique_ptr<PurgeableMemBuilder> modA2B = std::make_unique<TestDataModifier>('A', 'B');
    std::unique_ptr<PurgeableMemBuilder> modB2C = std::make_unique<TestDataModifier>('B', 'C');
    std::unique_ptr<PurgeableMemBuilder> modC2D = std::make_unique<TestDataModifier>('C', 'D');
    ModifyPurgMemByBuilder(pobj, std::move(modA2B));
    ModifyPurgMemByBuilder(pobj, std::move(modB2C));
    ModifyPurgMemByBuilder(pobj, std::move(modC2D));

    if (pobj->BeginRead()) {
        ASSERT_STREQ(alphabet, (char *)(pobj->GetContent()));
        pobj->EndRead();
    } else {
        std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::seconds(2 * PRINT_INTERVAL_SECONDS));
    delete pobj;
    pobj = nullptr;
}

HWTEST_F(PurgeableCppTest, MutiMorePageReadWriteTest, TestSize.Level1)
{
    char alphabet[8194];
    size_t len = 0;
    for (char ch = 'D'; len < 8194;) {
        alphabet[len++] = ch;
    }
    alphabet[8193] = 0;
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<TestBigDataBuilder>('A');
    PurgeableMem *pobj = new PurgeableMem(8194, std::move(builder));

    LoopReclaimPurgeable(1);
    LoopPrintAlphabet(pobj, 1);

    std::unique_ptr<PurgeableMemBuilder> modA2B = std::make_unique<TestDataModifier>('A', 'B');
    std::unique_ptr<PurgeableMemBuilder> modB2C = std::make_unique<TestDataModifier>('B', 'C');
    std::unique_ptr<PurgeableMemBuilder> modC2D = std::make_unique<TestDataModifier>('C', 'D');
    ModifyPurgMemByBuilder(pobj, std::move(modA2B));
    ModifyPurgMemByBuilder(pobj, std::move(modB2C));
    ModifyPurgMemByBuilder(pobj, std::move(modC2D));

    if (pobj->BeginRead()) {
        ASSERT_STREQ(alphabet, (char *)(pobj->GetContent()));
        pobj->EndRead();
    } else {
        std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::seconds(2 * PRINT_INTERVAL_SECONDS));
    delete pobj;
    pobj = nullptr;
}

HWTEST_F(PurgeableCppTest, InvalidInputSizeTest, TestSize.Level1)
{
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<TestDataBuilder>('A', 'Z');
    PurgeableMem *pobj = new PurgeableMem(0, std::move(builder));
    LoopReclaimPurgeable(1);
    bool ret = pobj->BeginRead();

    EXPECT_EQ(ret, false);

    delete pobj;
    pobj = nullptr;
}

HWTEST_F(PurgeableCppTest, InvalidInputBuilderTest, TestSize.Level1)
{
    PurgeableMem *pobj = new PurgeableMem(27, nullptr);
    LoopReclaimPurgeable(1);
    bool ret = pobj->BeginRead();

    EXPECT_EQ(ret, false);

    delete pobj;
    pobj = nullptr;
}

void LoopPrintAlphabet(PurgeableMem *pdata, unsigned int loopCount)
{
    std::cout << "inter " << __func__ << std::endl;
    for (unsigned int i = 0; i < loopCount; i++) {
        if (!pdata->BeginRead()) {
            std::cout << __func__ << ": " << i << ". ERROR! BeginRead failed." << std::endl;
            break;
        }
        std::cout << __func__ << ": " << i << ". data=[" <<
            (char *)(pdata->GetContent()) << "]" << std::endl;
        pdata->EndRead();
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

void ModifyPurgMemByBuilder(PurgeableMem *pdata, std::unique_ptr<PurgeableMemBuilder> mod)
{
    if (!pdata->BeginWrite()) {
        std::cout << __func__ << ": ERROR! BeginWrite failed." << std::endl;
        return;
    }
    std::this_thread::sleep_for(std::chrono::seconds(MODIFY_INTERVAL_SECONDS));
    std::cout << __func__ << " before mod data=[" << (char *)(pdata->GetContent()) << "]" << std::endl;
    pdata->ModifyContentByBuilder(std::move(mod));
    std::cout<< __func__ << " after mod data=[" << (char *)(pdata->GetContent()) << "]" << std::endl;

    std::cout << __func__ << " data=[" << (char *)(pdata->GetContent()) << "]" << std::endl;
    pdata->EndWrite();
}
} /* namespace PurgeableMem */
} /* namespace OHOS */

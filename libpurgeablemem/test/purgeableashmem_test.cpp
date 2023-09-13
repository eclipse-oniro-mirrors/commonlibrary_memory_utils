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
#include <cstring>
#include <memory>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>

#include <linux/ashmem.h>

#include "gtest/gtest.h"
#include "ashmem.h"
#include "securec.h"
#include "pm_util.h"

#define private public
#define protected public
#include "purgeable_ashmem.h"
#undef private
#undef protected

namespace OHOS {
namespace PurgeableMem {
using namespace testing;
using namespace testing::ext;

static constexpr int PRINT_INTERVAL_SECONDS = 1;
static constexpr int RECLAIM_INTERVAL_SECONDS = 1;
static constexpr int MODIFY_INTERVAL_SECONDS = 2;
void LoopPrintAlphabet(PurgeableAshMem *pdata, unsigned int loopCount);
bool ReclaimPurgeable(void);
void LoopReclaimPurgeable(unsigned int loopCount);
void ModifyPurgMemByBuilder(PurgeableAshMem *pdata, std::unique_ptr<PurgeableMemBuilder> mod);

class TestDataBuilder : public PurgeableMemBuilder {
public:
    TestDataBuilder(char start, char end)
    {
        this->start_ = start;
        this->end_ = end;
    }

    bool Build(void *data, size_t size)
    {
        if (size <= 0) {
            return true;
        }
        char *str = static_cast<char *>(data);
        size_t len = 0;
        for (char ch = start_; ch <= end_ && len < size; ch++) {
            str[len++] = ch;
        }
        str[size - 1] = 0;
        std::cout << "rebuild addr("<< (unsigned long long)str <<") " <<
            start_ << "~" << end_ << ", data=[" << str << "]" << std::endl;
        return true;
    }

    ~TestDataBuilder()
    {
        std::cout << "~TestDataBuilder" << std::endl;
    }

private:
    char start_;
    char end_;
};

class TestDataModifier : public PurgeableMemBuilder {
public:
    TestDataModifier(char from, char to)
    {
        this->from_ = from;
        this->to_ = to;
    }

    bool Build(void *data, size_t size)
    {
        char *str = static_cast<char *>(data);
        for (size_t i = 0; i < size && str[i]; i++) {
            if (str[i] == from_) {
                str[i] = to_;
            }
        }
        return true;
    }

    ~TestDataModifier()
    {
        std::cout << "~TestDataModifier" << std::endl;
    }

private:
    char from_;
    char to_;
};

class TestBigDataBuilder : public PurgeableMemBuilder {
public:
    explicit TestBigDataBuilder(char target)
    {
        this->target_ = target;
    }

    bool Build(void *data, size_t size)
    {
        if (size <= 0) {
            return true;
        }
        char *str = static_cast<char *>(data);
        size_t len = 0;
        for (char ch = target_; len < size;) {
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
    char target_;
};

class PurgeableAshmemTest : public testing::Test {
public:
    static void SetUpTestCase();
    static void TearDownTestCase();
    void SetUp();
    void TearDown();
};

void PurgeableAshmemTest::SetUpTestCase()
{
}

void PurgeableAshmemTest::TearDownTestCase()
{
}

void PurgeableAshmemTest::SetUp()
{
}

void PurgeableAshmemTest::TearDown()
{
}

HWTEST_F(PurgeableAshmemTest, KernelInterfaceTest, TestSize.Level1)
{
    size_t size = 4096 * 100;
    int fd = AshmemCreate("Purgeable Ashmem", size);
    ASSERT_GT(fd, 0);
    if (AshmemSetProt(fd, PROT_READ | PROT_WRITE) < 0) {
        close(fd);
        return;
    }
    void *dataPtr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (dataPtr == MAP_FAILED) {
        dataPtr = nullptr;
        close(fd);
        return;
    }
    char *str = static_cast<char *>(dataPtr);
    for (size_t i = 0; i < size; i++) {
        str[i] = 'a';
    }
    str[size - 1] = '\0';
    ashmem_pin pin_ = { static_cast<uint32_t>(0), static_cast<uint32_t>(0) };
    EXPECT_EQ(ioctl(fd, ASHMEM_GET_PURGEABLE), -1);
    EXPECT_EQ(ioctl(fd, ASHMEM_SET_PURGEABLE), 0);
    EXPECT_EQ(ioctl(fd, ASHMEM_GET_PURGEABLE), 1);
    EXPECT_EQ(ioctl(fd, ASHMEM_GET_PIN_STATUS, &pin_), 1);
    ioctl(fd, ASHMEM_PIN, &pin_);
    EXPECT_EQ(ioctl(fd, ASHMEM_GET_PIN_STATUS, &pin_), 2);
    ioctl(fd, ASHMEM_PIN, &pin_);
    EXPECT_EQ(ioctl(fd, ASHMEM_GET_PIN_STATUS, &pin_), 3);
    ioctl(fd, ASHMEM_UNPIN, &pin_);
    EXPECT_EQ(ioctl(fd, ASHMEM_GET_PIN_STATUS, &pin_), 2);
    ioctl(fd, ASHMEM_UNPIN, &pin_);
    EXPECT_EQ(ioctl(fd, ASHMEM_GET_PIN_STATUS, &pin_), 1);
    EXPECT_EQ(ioctl(fd, ASHMEM_PURGE_ALL_CACHES), 0);
    EXPECT_EQ(ioctl(fd, PURGEABLE_ASHMEM_IS_PURGED), 0);
    ioctl(fd, ASHMEM_UNPIN, &pin_);
    EXPECT_EQ(ioctl(fd, ASHMEM_GET_PIN_STATUS, &pin_), 0);
    EXPECT_EQ(ioctl(fd, PURGEABLE_ASHMEM_IS_PURGED), 0);
    ioctl(fd, ASHMEM_PURGE_ALL_CACHES);
    EXPECT_EQ(ioctl(fd, PURGEABLE_ASHMEM_IS_PURGED), 1);
    ioctl(fd, ASHMEM_PIN, &pin_);
    EXPECT_EQ(ioctl(fd, ASHMEM_GET_PIN_STATUS, &pin_), 1);
    EXPECT_EQ(ioctl(fd, PURGEABLE_ASHMEM_IS_PURGED), 1);
    ioctl(fd, ASHMEM_UNPIN, &pin_);
    EXPECT_EQ(ioctl(fd, ASHMEM_GET_PIN_STATUS, &pin_), 0);
    ioctl(fd, PURGEABLE_ASHMEM_REBUILD_SUCCESS);
    EXPECT_EQ(ioctl(fd, PURGEABLE_ASHMEM_IS_PURGED), 0);
}

HWTEST_F(PurgeableAshmemTest, MultiObjCreateTest, TestSize.Level1)
{
    const char alphabetFinal[] = "BBCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    std::unique_ptr<PurgeableMemBuilder> builder1 = std::make_unique<TestDataBuilder>('A', 'Z');
    std::unique_ptr<PurgeableMemBuilder> builder2 = std::make_unique<TestDataBuilder>('A', 'Z');
    std::unique_ptr<PurgeableMemBuilder> mod1 = std::make_unique<TestDataModifier>('A', 'B');
    std::unique_ptr<PurgeableMemBuilder> mod2 = std::make_unique<TestDataModifier>('A', 'B');

    PurgeableAshMem pobj1(27, std::move(builder1));
    LoopPrintAlphabet(&pobj1, 1);
    ModifyPurgMemByBuilder(&pobj1, std::move(mod1));
    LoopPrintAlphabet(&pobj1, 1);
    LoopReclaimPurgeable(1);

    PurgeableAshMem pobj2(27, std::move(builder2));
    LoopPrintAlphabet(&pobj2, 1);
    ModifyPurgMemByBuilder(&pobj2, std::move(mod2));
    LoopPrintAlphabet(&pobj2, 1);
    LoopReclaimPurgeable(1);

    int ret1 = 1;
    int ret2 = 1;
    int times1 = 0;
    int times2 = 0;
    while (times1++ < 10) {
        if (pobj1.BeginRead()) {
            ret1 = strncmp(alphabetFinal, static_cast<char *>(pobj1.GetContent()), 26);
            pobj1.EndRead();
            break;
        } else {
            std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
        }
    }

    while (times2++ < 10) {
        if (pobj2.BeginRead()) {
            ret2 = strncmp(alphabetFinal, static_cast<char *>(pobj2.GetContent()), 26);
            pobj2.EndRead();
            break;
        } else {
            std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
        }
    }

    EXPECT_EQ(ret1, 0);
    EXPECT_EQ(ret2, 0);
}

HWTEST_F(PurgeableAshmemTest, ReadTest, TestSize.Level1)
{
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<TestDataBuilder>('A', 'Z');
    PurgeableAshMem *pobj = new (std::nothrow) PurgeableAshMem(27, std::move(builder));
    ASSERT_NE(pobj, nullptr);
    LoopReclaimPurgeable(1);

    int times = 0;
    int ret = 1;
    while (times++ < 10) {
        if (pobj->BeginRead()) {
            ret = strncmp(alphabet, static_cast<char *>(pobj->GetContent()), 26);
            pobj->EndRead();
            break;
        } else {
            std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
        }
    }
    delete pobj;
    pobj = nullptr;
    EXPECT_EQ(ret, 0);
}

HWTEST_F(PurgeableAshmemTest, WriteTest, TestSize.Level1)
{
    const char alphabet[] = "CCCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<TestDataBuilder>('A', 'Z');
    PurgeableAshMem *pobj = new (std::nothrow) PurgeableAshMem(27, std::move(builder));
    ASSERT_NE(pobj, nullptr);
    LoopReclaimPurgeable(1);

    std::unique_ptr<PurgeableMemBuilder> modA2B = std::make_unique<TestDataModifier>('A', 'B');
    std::unique_ptr<PurgeableMemBuilder> modB2C = std::make_unique<TestDataModifier>('B', 'C');
    ModifyPurgMemByBuilder(pobj, std::move(modA2B));
    ModifyPurgMemByBuilder(pobj, std::move(modB2C));

    int times = 0;
    int ret = 1;
    while (times++ < 10) {
        if (pobj->BeginRead()) {
            ret = strncmp(alphabet, static_cast<char *>(pobj->GetContent()), 26);
            pobj->EndRead();
            break;
        } else {
            std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
        }
    }
    delete pobj;
    pobj = nullptr;
    EXPECT_EQ(ret, 0);
}

HWTEST_F(PurgeableAshmemTest, ReadWriteTest, TestSize.Level1)
{
    const char alphabet[] = "DDDDEFGHIJKLMNOPQRSTUVWXYZ\0";
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<TestDataBuilder>('A', 'Z');
    PurgeableAshMem *pobj = new (std::nothrow) PurgeableAshMem(27, std::move(builder));
    ASSERT_NE(pobj, nullptr);

    LoopReclaimPurgeable(1);
    LoopPrintAlphabet(pobj, 1);

    std::unique_ptr<PurgeableMemBuilder> modA2B = std::make_unique<TestDataModifier>('A', 'B');
    std::unique_ptr<PurgeableMemBuilder> modB2C = std::make_unique<TestDataModifier>('B', 'C');
    std::unique_ptr<PurgeableMemBuilder> modC2D = std::make_unique<TestDataModifier>('C', 'D');
    ModifyPurgMemByBuilder(pobj, std::move(modA2B));
    ModifyPurgMemByBuilder(pobj, std::move(modB2C));
    ModifyPurgMemByBuilder(pobj, std::move(modC2D));

    int times = 0;
    int ret = 1;
    while (times++ < 10) {
        if (pobj->BeginRead()) {
            ret = strncmp(alphabet, static_cast<char *>(pobj->GetContent()), 26);
            pobj->EndRead();
            break;
        } else {
            std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
        }
    }
    delete pobj;
    pobj = nullptr;
    EXPECT_EQ(ret, 0);
}

HWTEST_F(PurgeableAshmemTest, MutiPageReadTest, TestSize.Level1)
{
    char alphabet[4098];
    size_t len = 0;
    for (char ch = 'A'; len < 4098;) {
        alphabet[len++] = ch;
    }
    alphabet[4097] = 0;
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<TestBigDataBuilder>('A');
    PurgeableAshMem *pobj = new (std::nothrow) PurgeableAshMem(4098, std::move(builder));
    ASSERT_NE(pobj, nullptr);

    LoopReclaimPurgeable(1);

    int times = 0;
    int ret = 1;
    while (times++ < 10) {
        if (pobj->BeginRead()) {
            ret = strncmp(alphabet, static_cast<char *>(pobj->GetContent()), 4097);
            pobj->EndRead();
            break;
        } else {
            std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
        }
    }
    delete pobj;
    pobj = nullptr;
    EXPECT_EQ(ret, 0);
}

HWTEST_F(PurgeableAshmemTest, MutiPageWriteTest, TestSize.Level1)
{
    char alphabet[4098];
    size_t len = 0;
    for (char ch = 'C'; len < 4098;) {
        alphabet[len++] = ch;
    }
    alphabet[4097] = 0;
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<TestBigDataBuilder>('A');
    PurgeableAshMem *pobj = new (std::nothrow) PurgeableAshMem(4098, std::move(builder));
    ASSERT_NE(pobj, nullptr);

    LoopReclaimPurgeable(1);

    std::unique_ptr<PurgeableMemBuilder> modA2B = std::make_unique<TestDataModifier>('A', 'B');
    std::unique_ptr<PurgeableMemBuilder> modB2C = std::make_unique<TestDataModifier>('B', 'C');
    ModifyPurgMemByBuilder(pobj, std::move(modA2B));
    ModifyPurgMemByBuilder(pobj, std::move(modB2C));

    int times = 0;
    int ret = 1;
    while (times++ < 10) {
        if (pobj->BeginRead()) {
            ret = strncmp(alphabet, static_cast<char *>(pobj->GetContent()), 4097);
            pobj->EndRead();
            break;
        } else {
            std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
        }
    }
    delete pobj;
    pobj = nullptr;
    EXPECT_EQ(ret, 0);
}

HWTEST_F(PurgeableAshmemTest, MutiPageReadWriteTest, TestSize.Level1)
{
    char alphabet[4098];
    size_t len = 0;
    for (char ch = 'D'; len < 4098;) {
        alphabet[len++] = ch;
    }
    alphabet[4097] = 0;
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<TestBigDataBuilder>('A');
    PurgeableAshMem *pobj = new (std::nothrow) PurgeableAshMem(4098, std::move(builder));
    ASSERT_NE(pobj, nullptr);
    LoopReclaimPurgeable(1);
    LoopPrintAlphabet(pobj, 1);

    std::unique_ptr<PurgeableMemBuilder> modA2B = std::make_unique<TestDataModifier>('A', 'B');
    std::unique_ptr<PurgeableMemBuilder> modB2C = std::make_unique<TestDataModifier>('B', 'C');
    std::unique_ptr<PurgeableMemBuilder> modC2D = std::make_unique<TestDataModifier>('C', 'D');
    ModifyPurgMemByBuilder(pobj, std::move(modA2B));
    ModifyPurgMemByBuilder(pobj, std::move(modB2C));
    ModifyPurgMemByBuilder(pobj, std::move(modC2D));

    int times = 0;
    int ret = 1;
    while (times++ < 10) {
        if (pobj->BeginRead()) {
            ret = strncmp(alphabet, static_cast<char *>(pobj->GetContent()), 4097);
            pobj->EndRead();
            break;
        } else {
            std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
        }
    }
    delete pobj;
    pobj = nullptr;
    EXPECT_EQ(ret, 0);
}

HWTEST_F(PurgeableAshmemTest, MutiMorePageReadWriteTest, TestSize.Level1)
{
    size_t size = 5 * 1024 * 1024;
    char *alphabet = static_cast<char *>(malloc(size));
    size_t len = 0;
    for (char ch = 'D'; len < size;) {
        alphabet[len++] = ch;
    }
    alphabet[size - 1] = 0;
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<TestBigDataBuilder>('A');
    PurgeableAshMem *pobj = new (std::nothrow) PurgeableAshMem(size, std::move(builder));
    ASSERT_NE(pobj, nullptr);

    LoopReclaimPurgeable(1);
    LoopPrintAlphabet(pobj, 1);

    std::unique_ptr<PurgeableMemBuilder> modA2B = std::make_unique<TestDataModifier>('A', 'B');
    std::unique_ptr<PurgeableMemBuilder> modB2C = std::make_unique<TestDataModifier>('B', 'C');
    std::unique_ptr<PurgeableMemBuilder> modC2D = std::make_unique<TestDataModifier>('C', 'D');
    ModifyPurgMemByBuilder(pobj, std::move(modA2B));
    ModifyPurgMemByBuilder(pobj, std::move(modB2C));
    ModifyPurgMemByBuilder(pobj, std::move(modC2D));

    int times = 0;
    int ret = 1;
    while (times++ < 10) {
        if (pobj->BeginRead()) {
            ret = strncmp(alphabet, static_cast<char *>(pobj->GetContent()), size - 1);
            pobj->EndRead();
            break;
        } else {
            std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
        }
    }
    delete pobj;
    pobj = nullptr;
    free(alphabet);
    alphabet = nullptr;
    EXPECT_EQ(ret, 0);
}

HWTEST_F(PurgeableAshmemTest, StableMutiMorePageReadWriteTest, TestSize.Level1)
{
    size_t size = 5 * 1024 * 1024;
    char *alphabet = static_cast<char *>(malloc(size));
    size_t len = 0;
    for (char ch = 'D'; len < size;) {
        alphabet[len++] = ch;
    }
    alphabet[size - 1] = 0;
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<TestBigDataBuilder>('A');
    PurgeableAshMem *pobj = new (std::nothrow) PurgeableAshMem(size, std::move(builder));
    ASSERT_NE(pobj, nullptr);

    std::thread reclaimThread(LoopReclaimPurgeable, 10);
    std::thread readThread(LoopPrintAlphabet, pobj, 10);

    std::unique_ptr<PurgeableMemBuilder> modA2B = std::make_unique<TestDataModifier>('A', 'B');
    std::unique_ptr<PurgeableMemBuilder> modB2C = std::make_unique<TestDataModifier>('B', 'C');
    std::unique_ptr<PurgeableMemBuilder> modC2D = std::make_unique<TestDataModifier>('C', 'D');
    ModifyPurgMemByBuilder(pobj, std::move(modA2B));
    ModifyPurgMemByBuilder(pobj, std::move(modB2C));
    ModifyPurgMemByBuilder(pobj, std::move(modC2D));

    int times = 0;
    int ret = 1;
    while (times++ < 10) {
        if (pobj->BeginRead()) {
            ret = strncmp(alphabet, static_cast<char *>(pobj->GetContent()), size - 1);
            pobj->EndRead();
            break;
        } else {
            std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
        }
    }
    reclaimThread.join();
    readThread.join();
    delete pobj;
    pobj = nullptr;
    free(alphabet);
    alphabet = nullptr;
    EXPECT_EQ(ret, 0);
}

HWTEST_F(PurgeableAshmemTest, InvalidInputSizeTest, TestSize.Level1)
{
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<TestDataBuilder>('A', 'Z');
    PurgeableAshMem *pobj = new (std::nothrow) PurgeableAshMem(0, std::move(builder));
    ASSERT_NE(pobj, nullptr);
    bool ret = pobj->BeginRead();
    if (ret) {
        pobj->EndRead();
    }
    delete pobj;
    pobj = nullptr;
    EXPECT_EQ(ret, false);
}

HWTEST_F(PurgeableAshmemTest, InvalidInputBuilderTest, TestSize.Level1)
{
    PurgeableAshMem *pobj = new (std::nothrow) PurgeableAshMem(27, nullptr);
    ASSERT_NE(pobj, nullptr);
    bool ret = pobj->BeginRead();
    if (ret) {
        pobj->EndRead();
    }
    delete pobj;
    pobj = nullptr;
    EXPECT_EQ(ret, false);
}

HWTEST_F(PurgeableAshmemTest, IsPurgedTest, TestSize.Level1)
{
    std::unique_ptr<PurgeableMemBuilder> builder1 = std::make_unique<TestDataBuilder>('A', 'Z');
    std::unique_ptr<PurgeableMemBuilder> modA2B = std::make_unique<TestDataModifier>('A', 'B');
    PurgeableAshMem pobj(std::move(builder1));
    pobj.isSupport_ = 0;
    EXPECT_EQ(pobj.IsPurged(), false);
    EXPECT_EQ(pobj.Pin(), true);
    EXPECT_EQ(pobj.Unpin(), true);
    EXPECT_EQ(pobj.GetPinStatus(), false);
    pobj.isSupport_ = 1;
    pobj.ashmemFd_ = 0;
    EXPECT_EQ(pobj.Pin(), false);
    EXPECT_EQ(pobj.Unpin(), false);
    pobj.dataSizeInput_ = 0;
    EXPECT_EQ(pobj.CreatePurgeableData_(), false);
    pobj.dataPtr_ = nullptr;
    ModifyPurgMemByBuilder(&pobj, std::move(modA2B));
    pobj.isDataValid_ = false;
    pobj.BeginReadWithDataLock();
    pobj.isDataValid_ = true;
    pobj.EndReadWithDataLock();
}

HWTEST_F(PurgeableAshmemTest, GetPinStatusTest, TestSize.Level1)
{
    std::unique_ptr<PurgeableMemBuilder> builder1 = std::make_unique<TestDataBuilder>('A', 'Z');
    PurgeableAshMem pobj(std::move(builder1));
    pobj.isSupport_ = 1;
    EXPECT_NE(pobj.GetPinStatus(), 0);
    pobj.isSupport_ = 0;
    EXPECT_EQ(pobj.GetPinStatus(), 0);
}

HWTEST_F(PurgeableAshmemTest, ChangeAshmemDataTest, TestSize.Level1)
{
    std::unique_ptr<PurgeableMemBuilder> builder1 = std::make_unique<TestDataBuilder>('A', 'Z');
    PurgeableAshMem pobj1(27, std::move(builder1));
    PurgeableAshMem pobj2(27, std::move(builder1));
    PurgeableAshMem pobj3(27, std::move(builder1));
    PurgeableAshMem pobj4(27, std::move(builder1));
    size_t newSize = 0;
    size_t size = 123;
    int fd = 5;
    int intdata = 12345;
    void *data = &intdata;
    size_t pageSize = PAGE_SIZE;
    pobj1.ResizeData(newSize);
    newSize = 1;
    pobj1.ResizeData(newSize);
    pobj2.ashmemFd_ = 0;
    pobj2.ResizeData(newSize);
    pobj2.dataPtr_ = data;
    pobj2.ResizeData(newSize);

    pobj3.ChangeAshmemData(size, fd, data);
    pobj4.ashmemFd_ = 0;
    pobj4.ChangeAshmemData(size, fd, data);
    pobj4.dataPtr_ = data;
    pobj4.ChangeAshmemData(size, fd, data);
    size = ((pobj4.dataSizeInput_ + pageSize - 1) / pageSize) * pageSize;
    fd = AshmemCreate("PurgeableAshmem", size);
    EXPECT_EQ(pobj4.ChangeAshmemData(size, fd, data), true);
}

HWTEST_F(PurgeableAshmemTest, GetContentSizeTest, TestSize.Level1)
{
    std::unique_ptr<PurgeableMemBuilder> builder1 = std::make_unique<TestDataBuilder>('A', 'Z');
    PurgeableAshMem pobj(27, std::move(builder1));
    EXPECT_EQ(pobj.GetContentSize(), 27);
    bool target = true;
    pobj.SetDataValid(target);
    EXPECT_EQ(pobj.IsDataValid(), target);
    EXPECT_NE(pobj.GetAshmemFd(), -1);
}

void LoopPrintAlphabet(PurgeableAshMem *pdata, unsigned int loopCount)
{
    std::cout << "inter " << __func__ << std::endl;
    for (unsigned int i = 0; i < loopCount; i++) {
        if (!pdata->BeginRead()) {
            std::cout << __func__ << ": " << i << ". ERROR! BeginRead failed." << std::endl;
            break;
        }
        pdata->EndRead();
        std::this_thread::sleep_for(std::chrono::seconds(PRINT_INTERVAL_SECONDS));
    }
    std::cout << "quit " << __func__ << std::endl;
}

bool ReclaimPurgeable(void)
{
    FILE *f = fopen("/proc/sys/vm/drop_caches", "w");
    if (!f) {
        std::cout << __func__ << ": kernel not support" << std::endl;
        return false;
    }
    bool succ = true;
    if (fputs("3", f) == EOF) {
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

void ModifyPurgMemByBuilder(PurgeableAshMem *pdata, std::unique_ptr<PurgeableMemBuilder> mod)
{
    if (!pdata->BeginWrite()) {
        std::cout << __func__ << ": ERROR! BeginWrite failed." << std::endl;
        return;
    }
    std::this_thread::sleep_for(std::chrono::seconds(MODIFY_INTERVAL_SECONDS));
    pdata->ModifyContentByBuilder(std::move(mod));
    pdata->EndWrite();
}
} /* namespace PurgeableAshMem */
} /* namespace OHOS */

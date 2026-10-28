/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>

#include "gtest/gtest.h"
#include "securec.h"
#include "pm_util.h"
#include "dmabuf_alloc.h"

#define private public
#define protected public
#include "purgeable_dmabuf.h"
#undef private
#undef protected

namespace OHOS {
namespace PurgeableMem {
using namespace testing;
using namespace testing::ext;

/* DMA_BUF_PURGEABLE_PIN returns this when the buffer was purged before pin */
static constexpr int DMA_BUF_PURGEABLE_WAS_PURGED = 1;

static constexpr int PRINT_INTERVAL_SECONDS = 1;
static constexpr int RECLAIM_INTERVAL_SECONDS = 1;
static constexpr int MODIFY_INTERVAL_SECONDS = 2;
void LoopPrintAlphabetDma(PurgeableDmaBuf *pdata, unsigned int loopCount);
bool ReclaimPurgeableDma(void);
void LoopReclaimPurgeableDma(unsigned int loopCount);
void ModifyPurgMemByBuilderDma(PurgeableDmaBuf *pdata, std::unique_ptr<PurgeableMemBuilder> mod);

/*
 * Helper: discover the system dma-heap name by scanning /dev/dma_heap/ .
 * Returns the heap name containing "system", or empty string if not found.
 */
static std::string FindSystemDmaHeapName()
{
    const std::string rootDir = "/dev/dma_heap/";
    DIR *dir = opendir(rootDir.c_str());
    if (dir == nullptr) {
        return "";
    }
    struct dirent *ptr;
    std::string heapName;
    while ((ptr = readdir(dir)) != nullptr) {
        std::string fileName = ptr->d_name;
        if (fileName.find("system") != std::string::npos) {
            heapName = fileName;
            break;
        }
    }
    closedir(dir);
    return heapName;
}

/*
 * Helper: allocate a DMA buffer via DmabufHeapOpen + DmabufHeapBufferAlloc.
 * Returns true on success, fills outData with mmaped pointer and outHeapFd/outBuffer.
 */
static bool AllocDmabuf(size_t size, void **outData, int *outHeapFd, DmabufHeapBuffer *outBuffer)
{
    if (outData == nullptr || outHeapFd == nullptr || outBuffer == nullptr || size == 0) {
        return false;
    }
    std::string heapName = FindSystemDmaHeapName();
    if (heapName.empty()) {
        std::cout << __func__ << ": no system dma heap on device" << std::endl;
        return false;
    }
    int heapFd = DmabufHeapOpen(heapName.c_str());
    if (heapFd < 0) {
        std::cout << __func__ << ": DmabufHeapOpen " << heapName << " failed" << std::endl;
        return false;
    }
    DmabufHeapBuffer buffer = {};
    buffer.size = size;
    buffer.heapFlags = DMA_HEAP_FLAG_DIRECT_RECLAIM;
    if (DmabufHeapBufferAlloc(heapFd, &buffer) != 0) {
        std::cout << __func__ << ": DmabufHeapBufferAlloc failed, size=" << size << std::endl;
        DmabufHeapClose(heapFd);
        return false;
    }
    void *data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, buffer.fd, 0);
    if (data == MAP_FAILED) {
        std::cout << __func__ << ": mmap failed" << std::endl;
        DmabufHeapBufferFree(&buffer);
        DmabufHeapClose(heapFd);
        return false;
    }
    *outData = data;
    *outHeapFd = heapFd;
    *outBuffer = buffer;
    return true;
}

static void FreeDmaBuf(void *data, size_t size, int heapFd, DmabufHeapBuffer *buffer)
{
    if (data != nullptr && size > 0) {
        munmap(data, size);
    }
    if (buffer != nullptr) {
        DmabufHeapBufferFree(buffer); /* closes buffer->fd */
    }
    if (heapFd >= 0) {
        DmabufHeapClose(heapFd);
    }
}

/*
 * Helper: check whether the kernel exposes the purgeable dmabuf trigger
 * interface. Returns the trigger path, or empty string if not available.
 */
static std::string FindPurgeableDmaTriggerPath()
{
    const std::string triggerPath = "/proc/purgeable_dmabuf_trigger";
    if (access(triggerPath.c_str(), F_OK) != 0) {
        return "";
    }
    return triggerPath;
}

/*
 * Helper: whether the kernel supports the purgeable dma mechanism.
 * The open-source emulator kernel has no such support, test cases
 * silently pass on it.
 */
static bool IsPurgeableDmaSupported()
{
    return !FindPurgeableDmaTriggerPath().empty();
}

/*
 * SkipDmabufTest: whether the current test should be skipped — the kernel
 * has no purgeable dma trigger interface, or the device has no system dma heap.
 */
static bool SkipDmabufTest()
{
    if (!IsPurgeableDmaSupported()) {
        return true;
    }
    std::string heapName = FindSystemDmaHeapName();
    if (heapName.empty()) {
        std::cout << "SkipDmabufTest: no dma_heap on device, skip" << std::endl;
        return true;
    }
    return false;
}

class DmaTestDataBuilder : public PurgeableMemBuilder {
public:
    DmaTestDataBuilder(char start, char end)
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
        std::cout << "rebuild addr(" << (unsigned long long)str <<") " <<
            start_ << "~" << end_ << ", data=[" << str << "]" << std::endl;
        return true;
    }

    ~DmaTestDataBuilder()
    {
        std::cout << "~DmaTestDataBuilder" << std::endl;
    }

private:
    char start_;
    char end_;
};

class DmaTestDataModifier : public PurgeableMemBuilder {
public:
    DmaTestDataModifier(char from, char to)
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

    ~DmaTestDataModifier()
    {
        std::cout << "~DmaTestDataModifier" << std::endl;
    }

private:
    char from_;
    char to_;
};

class DmaTestBigDataBuilder : public PurgeableMemBuilder {
public:
    explicit DmaTestBigDataBuilder(char target)
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

    ~DmaTestBigDataBuilder()
    {
        std::cout << "~DmaTestBigDataBuilder" << std::endl;
    }

private:
    char target_;
};

class PurgeableDmaBufTest : public testing::Test {
public:
    static void SetUpTestCase();
    static void TearDownTestCase();
    void SetUp();
    void TearDown();
};

void PurgeableDmaBufTest::SetUpTestCase()
{
}

void PurgeableDmaBufTest::TearDownTestCase()
{
}

void PurgeableDmaBufTest::SetUp()
{
}

void PurgeableDmaBufTest::TearDown()
{
}

/* content size of the "A~Z" alphabet test data: 26 chars + '\0' */
static constexpr size_t ALPHABET_BUF_SIZE = 27;
/* compare length of the "A~Z" alphabet test data */
static constexpr size_t ALPHABET_CMP_LEN = 26;
/* size crossing the page boundary: 2 pages + 2 bytes */
static constexpr size_t PAGE_CROSS_SIZE = 4098;
static constexpr size_t TWO_PAGE_SIZE = 2 * PAGE_SIZE;
static constexpr size_t FIVE_MB_SIZE = 5 * 1024 * 1024;
/* retry times of the read-verify loop */
static constexpr int READ_RETRY_TIMES = 10;
/* expected pin counts of the dma-buf purgeable state machine:
 * SET_PURGEABLE sets the default count, PIN/UNPIN adjust it one by one */
static constexpr int PIN_COUNT_RELEASED = 0; /* all pins released */
static constexpr int PIN_COUNT_DEFAULT = 1; /* default count set by SET_PURGEABLE */
static constexpr int PIN_COUNT_ONE_PIN = 2; /* default + one extra PIN */
static constexpr int PIN_COUNT_TWO_PINS = 3; /* default + two extra PINs */

/* CheckAllBytes: whether all @len bytes of @str equal @expect. */
static bool CheckAllBytes(const char *str, size_t len, char expect)
{
    for (size_t i = 0; i < len; i++) {
        if (str[i] != expect) {
            return false;
        }
    }
    return true;
}

/* KernelCheckPinCounting: the pin count is 1 after SET_PURGEABLE, PIN/UNPIN adjust it one by one. */
static void KernelCheckPinCounting(int fd)
{
    struct DmaBufPurgeablePin pin = { 0, 0 };
    EXPECT_EQ(ioctl(fd, DMA_BUF_GET_PIN_STATUS), PIN_COUNT_DEFAULT);
    EXPECT_EQ(ioctl(fd, DMA_BUF_PURGEABLE_PIN, &pin), 0);
    EXPECT_EQ(ioctl(fd, DMA_BUF_GET_PIN_STATUS), PIN_COUNT_ONE_PIN);
    EXPECT_EQ(ioctl(fd, DMA_BUF_PURGEABLE_PIN, &pin), 0);
    EXPECT_EQ(ioctl(fd, DMA_BUF_GET_PIN_STATUS), PIN_COUNT_TWO_PINS);
    EXPECT_EQ(ioctl(fd, DMA_BUF_PURGEABLE_UNPIN, &pin), 0);
    EXPECT_EQ(ioctl(fd, DMA_BUF_GET_PIN_STATUS), PIN_COUNT_ONE_PIN);
    EXPECT_EQ(ioctl(fd, DMA_BUF_PURGEABLE_UNPIN, &pin), 0);
    EXPECT_EQ(ioctl(fd, DMA_BUF_GET_PIN_STATUS), PIN_COUNT_DEFAULT);
}

/*
 * KernelPurgeAndRebuild: one purge-rebuild cycle of the ioctl state machine —
 * UNPIN to count 0, trigger the purge, PIN returns WAS_PURGED, the purged
 * pages read as zeros, rewrite @rebuilt, REBUILD_SUCCESS clears the flag.
 */
static void KernelPurgeAndRebuild(int fd, char *str, size_t size, char rebuilt, int round)
{
    struct DmaBufPurgeablePin pin = { 0, 0 };
    EXPECT_EQ(ioctl(fd, DMA_BUF_PURGEABLE_UNPIN, &pin), 0) << "round " << round;
    EXPECT_EQ(ioctl(fd, DMA_BUF_GET_PIN_STATUS), PIN_COUNT_RELEASED) << "round " << round;
    ReclaimPurgeableDma();
    EXPECT_EQ(ioctl(fd, DMA_BUF_PURGEABLE_IS_PURGED), 1) << "round " << round;
    /* pin reports WAS_PURGED, page fault provides zeroed pages */
    EXPECT_EQ(ioctl(fd, DMA_BUF_PURGEABLE_PIN, &pin), DMA_BUF_PURGEABLE_WAS_PURGED) << "round " << round;
    EXPECT_EQ(ioctl(fd, DMA_BUF_GET_PIN_STATUS), PIN_COUNT_DEFAULT) << "round " << round;
    EXPECT_EQ(ioctl(fd, DMA_BUF_PURGEABLE_IS_PURGED), 1) << "round " << round;
    EXPECT_TRUE(CheckAllBytes(str, size, 0)) << "round " << round;
    /* rebuild: rewrite content, purged flag clears */
    for (size_t i = 0; i < size; i++) {
        str[i] = rebuilt;
    }
    str[size - 1] = '\0';
    EXPECT_EQ(ioctl(fd, DMA_BUF_PURGEABLE_REBUILD_SUCCESS), 0) << "round " << round;
    EXPECT_EQ(ioctl(fd, DMA_BUF_PURGEABLE_IS_PURGED), 0) << "round " << round;
    EXPECT_TRUE(CheckAllBytes(str, size - 1, rebuilt)) << "round " << round;
}

/* ReadVerifyDmabufContent: BeginRead and compare the content with @expected. */
static bool ReadVerifyDmabufContent(PurgeableDmaBuf *pobj, const char *expected, size_t cmpLen)
{
    int times = 0;
    while (times++ < READ_RETRY_TIMES) {
        if (pobj->BeginRead()) {
            bool same = strncmp(expected, static_cast<char *>(pobj->GetContent()), cmpLen) == 0;
            pobj->EndRead();
            return same;
        }
        std::cout << "ReadVerifyDmabufContent: ERROR! BeginRead failed." << std::endl;
    }
    return false;
}

/* VerifyNonDmabufFdTakeover: a non-dmabuf fd takeover succeeds, purgeable features degrade. */
/* VerifyNonDmabufFdTakeover: a non-dmabuf fd takeover fails (marking fails). */
static void VerifyNonDmabufFdTakeover(PurgeableDmaBuf *pobj, size_t size)
{
    int plainFd = open("/dev/null", O_RDWR);
    ASSERT_GE(plainFd, 0);
    void *anonData = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_TRUE(anonData != MAP_FAILED);
    EXPECT_FALSE(pobj->ChangeDmaBufData(ALPHABET_BUF_SIZE, plainFd, anonData))
        << "non-dmabuf fd takeover must fail";
    munmap(anonData, size);
    close(plainFd);
}

static constexpr int CONCURRENT_LOOP_TIMES = 20;

/* ConcurrentReader: loop BeginRead and verify the content of @pobj. */
static void ConcurrentReader(PurgeableDmaBuf *pobj, const char *alphabet, size_t size)
{
    for (int i = 0; i < CONCURRENT_LOOP_TIMES; i++) {
        if (pobj->BeginRead()) {
            EXPECT_EQ(strncmp(alphabet, static_cast<char *>(pobj->GetContent()), size - 1), 0);
            pobj->EndRead();
        }
    }
}

/* ConcurrentWriter: loop BeginWrite and touch the content of @pobj. */
static void ConcurrentWriter(PurgeableDmaBuf *pobj, size_t size)
{
    for (int i = 0; i < CONCURRENT_LOOP_TIMES; i++) {
        if (pobj->BeginWrite()) {
            /* write the same value as the builder output, keeps content stable */
            static_cast<char *>(pobj->GetContent())[static_cast<size_t>(i) % (size - 1)] = 'A';
            pobj->EndWrite();
        }
    }
}

/* DisableWithoutPurgeCase: disable a present buffer, reclaim cannot purge it afterwards. */
static void DisableWithoutPurgeCase()
{
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestDataBuilder>('A', 'Z');
    PurgeableDmaBuf pobj(std::move(builder));
    ASSERT_TRUE(pobj.CreatePurgeableData(ALPHABET_BUF_SIZE));
    EXPECT_TRUE(ReadVerifyDmabufContent(&pobj, alphabet, ALPHABET_CMP_LEN));
    EXPECT_TRUE(pobj.DisablePurgeable());
    EXPECT_FALSE(pobj.isSupport_);
    /* BeginRead still succeeds (content present), EndRead returns false
     * (content is permanently protected, not reclaimable) */
    ASSERT_TRUE(pobj.BeginRead());
    EXPECT_FALSE(pobj.EndRead()) << "EndRead must return false after disable (not reclaimable)";
    /* the pin is held forever after disable, reclaim cannot purge the content */
    LoopReclaimPurgeableDma(1);
    EXPECT_TRUE(ReadVerifyDmabufContent(&pobj, alphabet, ALPHABET_CMP_LEN));
}

/* DisableAfterPurgeCase: disable a purged buffer, the content is rebuilt during disable. */
static void DisableAfterPurgeCase()
{
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestDataBuilder>('A', 'Z');
    PurgeableDmaBuf *pobj = new (std::nothrow) PurgeableDmaBuf(std::move(builder));
    ASSERT_NE(pobj, nullptr);
    ASSERT_TRUE(pobj->CreatePurgeableData(ALPHABET_BUF_SIZE));
    EXPECT_TRUE(ReadVerifyDmabufContent(pobj, alphabet, ALPHABET_CMP_LEN));
    if (!pobj->isSupport_) {
        std::cout << __func__ << ": kernel not support purgeable, skip" << std::endl;
        delete pobj;
        return;
    }
    if (!ReclaimPurgeableDma()) {
        std::cout << __func__ << ": purgeable_dmabuf_trigger not available, skip" << std::endl;
        delete pobj;
        return;
    }
    std::this_thread::sleep_for(std::chrono::seconds(RECLAIM_INTERVAL_SECONDS)); /* wait reclaim finish */
    EXPECT_EQ(pobj->IsPurged(), true);
    unsigned int buildCountBefore = pobj->buildDataCount_;
    /* DisablePurgeable rebuilds the purged content first, then disables the feature */
    EXPECT_TRUE(pobj->DisablePurgeable());
    EXPECT_FALSE(pobj->isSupport_);
    EXPECT_EQ(pobj->buildDataCount_, buildCountBefore + 1);
    EXPECT_TRUE(ReadVerifyDmabufContent(pobj, alphabet, ALPHABET_CMP_LEN));
    /* after disable, reclaim cannot destroy the content, no rebuild happens */
    LoopReclaimPurgeableDma(1);
    unsigned int buildCountAfterDisable = pobj->buildDataCount_;
    EXPECT_TRUE(ReadVerifyDmabufContent(pobj, alphabet, ALPHABET_CMP_LEN));
    EXPECT_EQ(pobj->buildDataCount_, buildCountAfterDisable);
    delete pobj;
}

/*
 * DisableThenResizeCase: after DisablePurgeable, ResizeData must be rejected
 * (the object is permanently disabled, cannot get a new purgeable buffer).
 */
static void DisableThenResizeCase()
{
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestDataBuilder>('A', 'Z');
    PurgeableDmaBuf pobj(std::move(builder));
    ASSERT_TRUE(pobj.CreatePurgeableData(ALPHABET_BUF_SIZE));
    EXPECT_TRUE(ReadVerifyDmabufContent(&pobj, alphabet, ALPHABET_CMP_LEN));
    EXPECT_TRUE(pobj.DisablePurgeable());
    EXPECT_FALSE(pobj.isSupport_);
    EXPECT_TRUE(pobj.disabled_);

    /* resize must be rejected: disabled objects cannot get a new buffer */
    size_t sizeBefore = pobj.GetContentSize();
    pobj.ResizeData(PAGE_CROSS_SIZE);
    EXPECT_EQ(pobj.GetContentSize(), sizeBefore) << "resize must be rejected when disabled";
    /* original content still intact */
    EXPECT_TRUE(ReadVerifyDmabufContent(&pobj, alphabet, ALPHABET_CMP_LEN));
}

/*
 * DisableThenChangeCase: after DisablePurgeable, ChangeDmaBufData must be
 * rejected (the object is permanently disabled).
 */
static void DisableThenChangeCase()
{
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestDataBuilder>('A', 'Z');
    PurgeableDmaBuf pobj(std::move(builder));
    ASSERT_TRUE(pobj.CreatePurgeableData(ALPHABET_BUF_SIZE));
    EXPECT_TRUE(pobj.DisablePurgeable());
    EXPECT_TRUE(pobj.disabled_);

    /* change must be rejected */
    size_t size = PAGE_SIZE;
    void *data = nullptr;
    int heapFd = -1;
    DmabufHeapBuffer buffer = {};
    ASSERT_TRUE(AllocDmabuf(size, &data, &heapFd, &buffer));
    int fd = static_cast<int>(buffer.fd);
    EXPECT_FALSE(pobj.ChangeDmaBufData(ALPHABET_BUF_SIZE, fd, data)) << "change must fail when disabled";
    /* object state unchanged */
    EXPECT_NE(pobj.GetDmaBufFd(), fd);
    /* original content still intact */
    EXPECT_TRUE(ReadVerifyDmabufContent(&pobj, alphabet, ALPHABET_CMP_LEN));
    FreeDmaBuf(data, size, heapFd, &buffer);
}

HWTEST_F(PurgeableDmaBufTest, KernelInterfaceTest, TestSize.Level1)
{
    if (SkipDmabufTest()) {
        return;
    }
    size_t size = PAGE_SIZE;
    void *dataPtr = nullptr;
    int heapFd = -1;
    DmabufHeapBuffer buffer = {};
    ASSERT_TRUE(AllocDmabuf(size, &dataPtr, &heapFd, &buffer));
    int fd = static_cast<int>(buffer.fd);
    char *str = static_cast<char *>(dataPtr);
    for (size_t i = 0; i < size; i++) {
        str[i] = 'a';
    }
    str[size - 1] = '\0';

    struct DmaBufPurgeablePin pin = { 0, 0 };
    if (ioctl(fd, DMA_BUF_SET_PURGEABLE) != 0) {
        std::cout << __func__ << ": kernel not support purgeable dma-buf ioctl, skip" << std::endl;
        FreeDmaBuf(dataPtr, size, heapFd, &buffer);
        return;
    }
    /* SET_PURGEABLE succeeded, default pin count is 1 */
    EXPECT_EQ(ioctl(fd, DMA_BUF_GET_PURGEABLE), 1);
    KernelCheckPinCounting(fd);
    /* reclaim-dependent assertions below need the kernel trigger interface */
    if (!ReclaimPurgeableDma()) {
        std::cout << __func__ << ": purgeable_dmabuf_trigger not available, skip" << std::endl;
        FreeDmaBuf(dataPtr, size, heapFd, &buffer);
        return;
    }
    /* pinned (count 1): reclaim does not purge it */
    EXPECT_EQ(ioctl(fd, DMA_BUF_PURGEABLE_IS_PURGED), 0);
    /* single purge-rebuild cycle: UNPIN triggers the purge, PIN returns
     * WAS_PURGED, page fault reads zeros, rebuild clears the purged flag */
    KernelPurgeAndRebuild(fd, str, size, 'b', 0);
    /* state machine is not stuck: unpin returns the pin count to 0 */
    EXPECT_EQ(ioctl(fd, DMA_BUF_PURGEABLE_UNPIN, &pin), 0);
    EXPECT_EQ(ioctl(fd, DMA_BUF_GET_PIN_STATUS), PIN_COUNT_RELEASED);
    FreeDmaBuf(dataPtr, size, heapFd, &buffer);
}

HWTEST_F(PurgeableDmaBufTest, KernelInterfaceTest002, TestSize.Level1)
{
    if (SkipDmabufTest()) {
        return;
    }
    size_t size = PAGE_SIZE;
    void *dataPtr = nullptr;
    int heapFd = -1;
    DmabufHeapBuffer buffer = {};
    ASSERT_TRUE(AllocDmabuf(size, &dataPtr, &heapFd, &buffer));
    int fd = static_cast<int>(buffer.fd);
    char *str = static_cast<char *>(dataPtr);
    for (size_t i = 0; i < size; i++) {
        str[i] = 'a';
    }
    str[size - 1] = '\0';
    struct DmaBufPurgeablePin pin = { 0, 0 };
    if (ioctl(fd, DMA_BUF_SET_PURGEABLE) != 0) {
        std::cout << __func__ << ": kernel not support purgeable dma-buf ioctl, skip" << std::endl;
        FreeDmaBuf(dataPtr, size, heapFd, &buffer);
        return;
    }
    EXPECT_EQ(ioctl(fd, DMA_BUF_GET_PURGEABLE), 1);
    EXPECT_EQ(ioctl(fd, DMA_BUF_PURGEABLE_IS_PURGED), 0);
    /* the purge/rebuild rounds below need the kernel trigger interface */
    if (!ReclaimPurgeableDma()) {
        std::cout << __func__ << ": purgeable_dmabuf_trigger not available, skip" << std::endl;
        FreeDmaBuf(dataPtr, size, heapFd, &buffer);
        return;
    }

    /* loop the state machine: build -> purge -> rebuild, again and again */
    constexpr int ROUNDS = 3;
    for (int round = 0; round < ROUNDS; round++) {
        KernelPurgeAndRebuild(fd, str, size, static_cast<char>('b' + round), round);
    }
    /* state machine is not stuck: unpin returns the pin count to 0 */
    EXPECT_EQ(ioctl(fd, DMA_BUF_PURGEABLE_UNPIN, &pin), 0);
    EXPECT_EQ(ioctl(fd, DMA_BUF_GET_PIN_STATUS), PIN_COUNT_RELEASED);
    FreeDmaBuf(dataPtr, size, heapFd, &buffer);
}


HWTEST_F(PurgeableDmaBufTest, CreatePurgeableDataTest, TestSize.Level1)
{
    if (SkipDmabufTest()) {
        return;
    }
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestDataBuilder>('A', 'Z');
    PurgeableDmaBuf pobj(std::move(builder));
    /* empty shell before create */
    EXPECT_EQ(pobj.GetDmaBufFd(), -1);
    EXPECT_EQ(pobj.GetFd(), -1);
    EXPECT_EQ(pobj.GetIno(), static_cast<unsigned long>(0));
    EXPECT_EQ(pobj.GetPurgMemType(), PURG_MEM_TYPE_DMA_BUF);
    EXPECT_EQ(pobj.GetContentSize(), static_cast<size_t>(0));

    ASSERT_TRUE(pobj.CreatePurgeableData(ALPHABET_BUF_SIZE));
    EXPECT_GE(pobj.GetDmaBufFd(), 0);
    EXPECT_EQ(pobj.GetFd(), pobj.GetDmaBufFd());
    EXPECT_EQ(pobj.GetContentSize(), static_cast<size_t>(ALPHABET_BUF_SIZE));
    if (pobj.isSupport_) {
        EXPECT_GT(pobj.GetIno(), static_cast<unsigned long>(0));
        /* SET_PURGEABLE sets pin count to 1, CreatePurgeableData calls Unpin */
        EXPECT_EQ(pobj.GetPinStatus(), PIN_COUNT_RELEASED);
    }
    /* SetDataValid/IsDataValid round-trip */
    pobj.SetDataValid(false);
    EXPECT_FALSE(pobj.IsDataValid());
    pobj.SetDataValid(true);
    EXPECT_TRUE(pobj.IsDataValid());

    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    int times = 0;
    int ret = 1;
    while (times++ < READ_RETRY_TIMES) {
        if (pobj.BeginRead()) {
            ret = strncmp(alphabet, static_cast<char *>(pobj.GetContent()), ALPHABET_CMP_LEN);
            pobj.EndRead();
            break;
        } else {
            std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
        }
    }
    EXPECT_EQ(ret, 0);
    EXPECT_FALSE(pobj.ToString().empty());
}

HWTEST_F(PurgeableDmaBufTest, CreatePurgeableDataFallbackTest, TestSize.Level1)
{
    if (SkipDmabufTest()) {
        return;
    }
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestDataBuilder>('A', 'Z');
    PurgeableDmaBuf pobj(std::move(builder));
    /* no such heap, CreatePurgeableData falls back to scan /dev/dma_heap/
     * for a heap whose name contains "system" */
    ASSERT_TRUE(pobj.CreatePurgeableData(ALPHABET_BUF_SIZE, "no_such_heap"));
    EXPECT_GE(pobj.GetDmaBufFd(), 0);
    if (pobj.BeginRead()) {
        pobj.EndRead();
    }
}

HWTEST_F(PurgeableDmaBufTest, MultiObjCreateTest, TestSize.Level1)
{
    if (SkipDmabufTest()) {
        return;
    }
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    std::unique_ptr<PurgeableMemBuilder> builder1 = std::make_unique<DmaTestDataBuilder>('A', 'Z');
    std::unique_ptr<PurgeableMemBuilder> builder2 = std::make_unique<DmaTestDataBuilder>('A', 'Z');

    PurgeableDmaBuf pobj1(std::move(builder1));
    ASSERT_TRUE(pobj1.CreatePurgeableData(ALPHABET_BUF_SIZE));
    LoopReclaimPurgeableDma(1);

    PurgeableDmaBuf pobj2(std::move(builder2));
    ASSERT_TRUE(pobj2.CreatePurgeableData(ALPHABET_BUF_SIZE));
    LoopReclaimPurgeableDma(1);

    int times1 = 0;
    int ret1 = 1;
    while (times1++ < READ_RETRY_TIMES) {
        if (pobj1.BeginRead()) {
            ret1 = strncmp(alphabet, static_cast<char *>(pobj1.GetContent()), ALPHABET_CMP_LEN);
            pobj1.EndRead();
            break;
        } else {
            std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
        }
    }
    int times2 = 0;
    int ret2 = 1;
    while (times2++ < READ_RETRY_TIMES) {
        if (pobj2.BeginRead()) {
            ret2 = strncmp(alphabet, static_cast<char *>(pobj2.GetContent()), ALPHABET_CMP_LEN);
            pobj2.EndRead();
            break;
        } else {
            std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
        }
    }
    EXPECT_EQ(ret1, 0);
    EXPECT_EQ(ret2, 0);
}

HWTEST_F(PurgeableDmaBufTest, ResizeDataTest, TestSize.Level1)
{
    if (SkipDmabufTest()) {
        return;
    }
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestDataBuilder>('A', 'Z');
    PurgeableDmaBuf *pobj = new (std::nothrow) PurgeableDmaBuf(std::move(builder));
    ASSERT_NE(pobj, nullptr);
    ASSERT_TRUE(pobj->CreatePurgeableData(ALPHABET_BUF_SIZE));
    EXPECT_EQ(pobj->GetContentSize(), static_cast<size_t>(ALPHABET_BUF_SIZE));

    /* resize the self-allocated buffer, content rebuilt at next access */
    pobj->ResizeData(PAGE_CROSS_SIZE);
    EXPECT_EQ(pobj->GetContentSize(), static_cast<size_t>(PAGE_CROSS_SIZE));
    EXPECT_GE(pobj->GetDmaBufFd(), 0);
    int times = 0;
    int ret = 1;
    while (times++ < READ_RETRY_TIMES) {
        if (pobj->BeginRead()) {
            ret = strncmp(alphabet, static_cast<char *>(pobj->GetContent()), ALPHABET_CMP_LEN);
            pobj->EndRead();
            break;
        } else {
            std::cout << __func__ << ": ERROR! BeginRead failed." << std::endl;
        }
    }
    EXPECT_EQ(ret, 0);

    /* invalid resize size: no-op */
    pobj->ResizeData(0);
    pobj->ResizeData(OHOS_MAXIMUM_PURGEABLE_MEMORY);
    EXPECT_EQ(pobj->GetContentSize(), static_cast<size_t>(PAGE_CROSS_SIZE));
    delete pobj;
    pobj = nullptr;
}

HWTEST_F(PurgeableDmaBufTest, ReadTest, TestSize.Level1)
{
    if (SkipDmabufTest()) {
        return;
    }
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestDataBuilder>('A', 'Z');
    PurgeableDmaBuf *pobj = new (std::nothrow) PurgeableDmaBuf(std::move(builder));
    ASSERT_NE(pobj, nullptr);
    ASSERT_TRUE(pobj->CreatePurgeableData(ALPHABET_BUF_SIZE));
    /* the empty buffer is unpinned, reclaim it before the first access */
    LoopReclaimPurgeableDma(1);

    int times = 0;
    int ret = 1;
    while (times++ < READ_RETRY_TIMES) {
        if (pobj->BeginRead()) {
            ret = strncmp(alphabet, static_cast<char *>(pobj->GetContent()), ALPHABET_CMP_LEN);
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

HWTEST_F(PurgeableDmaBufTest, PurgeAndRebuildTest, TestSize.Level1)
{
    if (SkipDmabufTest()) {
        return;
    }
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestDataBuilder>('A', 'Z');
    PurgeableDmaBuf *pobj = new (std::nothrow) PurgeableDmaBuf(std::move(builder));
    ASSERT_NE(pobj, nullptr);
    ASSERT_TRUE(pobj->CreatePurgeableData(ALPHABET_BUF_SIZE));

    /* first access builds the content */
    ASSERT_TRUE(ReadVerifyDmabufContent(pobj, alphabet, ALPHABET_CMP_LEN));
    if (!pobj->isSupport_) {
        std::cout << __func__ << ": kernel not support purgeable, skip" << std::endl;
        delete pobj;
        pobj = nullptr;
        return;
    }

    /* EndRead unpinned the buffer, kernel purges it on reclaim */
    if (!ReclaimPurgeableDma()) {
        std::cout << __func__ << ": purgeable_dmabuf_trigger not available, skip" << std::endl;
        delete pobj;
        pobj = nullptr;
        return;
    }
    std::this_thread::sleep_for(std::chrono::seconds(RECLAIM_INTERVAL_SECONDS)); /* wait reclaim finish */
    EXPECT_EQ(pobj->IsPurged(), true);
    unsigned int buildCountBefore = pobj->buildDataCount_;

    /* BeginRead on the purged buffer triggers a full rebuild */
    EXPECT_TRUE(ReadVerifyDmabufContent(pobj, alphabet, ALPHABET_CMP_LEN));
    EXPECT_EQ(pobj->buildDataCount_, buildCountBefore + 1);
    /* AfterRebuildSucc told kernel the data is rebuilt, purged flag cleared */
    EXPECT_EQ(pobj->IsPurged(), false);
    delete pobj;
    pobj = nullptr;
}

HWTEST_F(PurgeableDmaBufTest, DisablePurgeableTest, TestSize.Level1)
{
    if (SkipDmabufTest()) {
        return;
    }
    /* case 1: disable without purge, content keeps intact after reclaim */
    DisableWithoutPurgeCase();
    /* case 2: disable after purge, the purged content is rebuilt during disable */
    DisableAfterPurgeCase();
    /* case 3: disable then resize, the new buffer must NOT be purgeable */
    DisableThenResizeCase();
    /* case 4: disable then change to another buffer, the new buffer must NOT be purgeable */
    DisableThenChangeCase();
}

HWTEST_F(PurgeableDmaBufTest, WriteTest, TestSize.Level1)
{
    if (SkipDmabufTest()) {
        return;
    }
    const char alphabet[] = "CCCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestDataBuilder>('A', 'Z');
    PurgeableDmaBuf *pobj = new (std::nothrow) PurgeableDmaBuf(std::move(builder));
    ASSERT_NE(pobj, nullptr);
    ASSERT_TRUE(pobj->CreatePurgeableData(ALPHABET_BUF_SIZE));
    LoopReclaimPurgeableDma(1);

    std::unique_ptr<PurgeableMemBuilder> modA2B = std::make_unique<DmaTestDataModifier>('A', 'B');
    std::unique_ptr<PurgeableMemBuilder> modB2C = std::make_unique<DmaTestDataModifier>('B', 'C');
    ModifyPurgMemByBuilderDma(pobj, std::move(modA2B));
    ModifyPurgMemByBuilderDma(pobj, std::move(modB2C));

    int times = 0;
    int ret = 1;
    while (times++ < READ_RETRY_TIMES) {
        if (pobj->BeginRead()) {
            ret = strncmp(alphabet, static_cast<char *>(pobj->GetContent()), ALPHABET_CMP_LEN);
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

HWTEST_F(PurgeableDmaBufTest, MutiPageReadTest, TestSize.Level1)
{
    if (SkipDmabufTest()) {
        return;
    }
    size_t size = PAGE_CROSS_SIZE;
    char *alphabet = static_cast<char *>(malloc(size));
    ASSERT_NE(alphabet, nullptr);
    size_t len = 0;
    for (char ch = 'A'; len < size;) {
        alphabet[len++] = ch;
    }
    alphabet[size - 1] = 0;
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestBigDataBuilder>('A');
    PurgeableDmaBuf *pobj = new (std::nothrow) PurgeableDmaBuf(std::move(builder));
    ASSERT_NE(pobj, nullptr);
    ASSERT_TRUE(pobj->CreatePurgeableData(size));
    LoopReclaimPurgeableDma(1);

    int times = 0;
    int ret = 1;
    while (times++ < READ_RETRY_TIMES) {
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

HWTEST_F(PurgeableDmaBufTest, MutiPageWriteTest, TestSize.Level1)
{
    if (SkipDmabufTest()) {
        return;
    }
    size_t size = PAGE_CROSS_SIZE;
    char *alphabet = static_cast<char *>(malloc(size));
    ASSERT_NE(alphabet, nullptr);
    size_t len = 0;
    for (char ch = 'C'; len < size;) {
        alphabet[len++] = ch;
    }
    alphabet[size - 1] = 0;
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestBigDataBuilder>('A');
    PurgeableDmaBuf *pobj = new (std::nothrow) PurgeableDmaBuf(std::move(builder));
    ASSERT_NE(pobj, nullptr);
    ASSERT_TRUE(pobj->CreatePurgeableData(size));
    LoopReclaimPurgeableDma(1);

    std::unique_ptr<PurgeableMemBuilder> modA2B = std::make_unique<DmaTestDataModifier>('A', 'B');
    std::unique_ptr<PurgeableMemBuilder> modB2C = std::make_unique<DmaTestDataModifier>('B', 'C');
    ModifyPurgMemByBuilderDma(pobj, std::move(modA2B));
    ModifyPurgMemByBuilderDma(pobj, std::move(modB2C));

    int times = 0;
    int ret = 1;
    while (times++ < READ_RETRY_TIMES) {
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

HWTEST_F(PurgeableDmaBufTest, MutiPageReadWriteTest, TestSize.Level1)
{
    if (SkipDmabufTest()) {
        return;
    }
    size_t size = TWO_PAGE_SIZE;
    char *alphabet = static_cast<char *>(malloc(size));
    ASSERT_NE(alphabet, nullptr);
    size_t len = 0;
    for (char ch = 'D'; len < size;) {
        alphabet[len++] = ch;
    }
    alphabet[size - 1] = 0;
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestBigDataBuilder>('A');
    PurgeableDmaBuf *pobj = new (std::nothrow) PurgeableDmaBuf(std::move(builder));
    ASSERT_NE(pobj, nullptr);
    ASSERT_TRUE(pobj->CreatePurgeableData(size));
    LoopReclaimPurgeableDma(1);
    LoopPrintAlphabetDma(pobj, 1);

    std::unique_ptr<PurgeableMemBuilder> modA2B = std::make_unique<DmaTestDataModifier>('A', 'B');
    std::unique_ptr<PurgeableMemBuilder> modB2C = std::make_unique<DmaTestDataModifier>('B', 'C');
    std::unique_ptr<PurgeableMemBuilder> modC2D = std::make_unique<DmaTestDataModifier>('C', 'D');
    ModifyPurgMemByBuilderDma(pobj, std::move(modA2B));
    ModifyPurgMemByBuilderDma(pobj, std::move(modB2C));
    ModifyPurgMemByBuilderDma(pobj, std::move(modC2D));

    int times = 0;
    int ret = 1;
    while (times++ < READ_RETRY_TIMES) {
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

HWTEST_F(PurgeableDmaBufTest, MutiMorePageReadWriteTest, TestSize.Level1)
{
    if (SkipDmabufTest()) {
        return;
    }
    size_t size = FIVE_MB_SIZE;
    char *alphabet = static_cast<char *>(malloc(size));
    ASSERT_NE(alphabet, nullptr);
    size_t len = 0;
    for (char ch = 'D'; len < size;) {
        alphabet[len++] = ch;
    }
    alphabet[size - 1] = 0;
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestBigDataBuilder>('A');
    PurgeableDmaBuf *pobj = new (std::nothrow) PurgeableDmaBuf(std::move(builder));
    ASSERT_NE(pobj, nullptr);
    ASSERT_TRUE(pobj->CreatePurgeableData(size));
    LoopReclaimPurgeableDma(1);
    LoopPrintAlphabetDma(pobj, 1);

    std::unique_ptr<PurgeableMemBuilder> modA2B = std::make_unique<DmaTestDataModifier>('A', 'B');
    std::unique_ptr<PurgeableMemBuilder> modB2C = std::make_unique<DmaTestDataModifier>('B', 'C');
    std::unique_ptr<PurgeableMemBuilder> modC2D = std::make_unique<DmaTestDataModifier>('C', 'D');
    ModifyPurgMemByBuilderDma(pobj, std::move(modA2B));
    ModifyPurgMemByBuilderDma(pobj, std::move(modB2C));
    ModifyPurgMemByBuilderDma(pobj, std::move(modC2D));

    int times = 0;
    int ret = 1;
    while (times++ < READ_RETRY_TIMES) {
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

HWTEST_F(PurgeableDmaBufTest, TakeOverDmaBufTest, TestSize.Level1)
{
    if (SkipDmabufTest()) {
        return;
    }
    size_t size = PAGE_SIZE;
    void *data = nullptr;
    int heapFd = -1;
    DmabufHeapBuffer buffer = {};
    ASSERT_TRUE(AllocDmabuf(size, &data, &heapFd, &buffer));
    int fd = static_cast<int>(buffer.fd);

    /* caller builds data which is different from builder's output */
    const char callerData[] = "ZZZZZZZZZZZZZZZZZZZZZZZZZ";
    auto retStrcpy = strcpy_s(static_cast<char *>(data), size, callerData);
    ASSERT_EQ(retStrcpy, 0);

    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestDataBuilder>('A', 'Z');
    PurgeableDmaBuf pobj(std::move(builder));

    /* empty shell: no data attached yet */
    EXPECT_EQ(pobj.GetDmaBufFd(), -1);
    EXPECT_FALSE(pobj.BeginRead());

    /* take over the external DMA buffer */
    EXPECT_TRUE(pobj.ChangeDmaBufData(ALPHABET_BUF_SIZE, fd, data));
    EXPECT_EQ(pobj.GetDmaBufFd(), fd);
    EXPECT_EQ(pobj.GetFd(), fd);
    EXPECT_EQ(pobj.GetContentSize(), static_cast<size_t>(ALPHABET_BUF_SIZE));
    if (pobj.isSupport_) {
        EXPECT_GT(pobj.GetIno(), static_cast<unsigned long>(0));
        EXPECT_EQ(ioctl(fd, DMA_BUF_GET_PURGEABLE), 1);
        /* takeover keeps the buffer pinned, it is not purged */
        EXPECT_EQ(ioctl(fd, DMA_BUF_PURGEABLE_IS_PURGED), 0);
        /* ChangeDmaBufData does NOT call Unpin, pin count stays default */
        EXPECT_EQ(pobj.GetPinStatus(), PIN_COUNT_DEFAULT);
    }

    /* data has been built by caller, so it is not rebuilt at first access */
    EXPECT_TRUE(ReadVerifyDmabufContent(&pobj, callerData, ALPHABET_CMP_LEN));

    /* force rebuild: mark data as never built */
    pobj.buildDataCount_ = 0;
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    EXPECT_TRUE(ReadVerifyDmabufContent(&pobj, alphabet, ALPHABET_CMP_LEN));
    FreeDmaBuf(data, size, heapFd, &buffer);
}

HWTEST_F(PurgeableDmaBufTest, ChangeDmaBufDataTwiceTest, TestSize.Level1)
{
    if (SkipDmabufTest()) {
        return;
    }
    size_t size = PAGE_SIZE;
    void *data1 = nullptr;
    void *data2 = nullptr;
    int heapFd1 = -1;
    int heapFd2 = -1;
    DmabufHeapBuffer buffer1 = {};
    DmabufHeapBuffer buffer2 = {};
    ASSERT_TRUE(AllocDmabuf(size, &data1, &heapFd1, &buffer1));
    ASSERT_TRUE(AllocDmabuf(size, &data2, &heapFd2, &buffer2));
    int fd1 = static_cast<int>(buffer1.fd);
    int fd2 = static_cast<int>(buffer2.fd);

    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestDataBuilder>('A', 'Z');
    PurgeableDmaBuf pobj(std::move(builder));
    EXPECT_TRUE(pobj.ChangeDmaBufData(ALPHABET_BUF_SIZE, fd1, data1));
    EXPECT_EQ(pobj.GetDmaBufFd(), fd1);
    if (pobj.BeginRead()) {
        pobj.EndRead();
    }
    /* take over another DMA buffer, the first one still belongs to caller */
    EXPECT_TRUE(pobj.ChangeDmaBufData(ALPHABET_BUF_SIZE, fd2, data2));
    EXPECT_EQ(pobj.GetDmaBufFd(), fd2);
    if (pobj.BeginRead()) {
        pobj.EndRead();
    }
    FreeDmaBuf(data1, size, heapFd1, &buffer1);
    FreeDmaBuf(data2, size, heapFd2, &buffer2);
}

/*
 * ChangeToNonPurgeableTest: switching from a purgeable-capable buffer to a
 * non-dma-buf fd must fail (marking fails), the object stays unchanged.
 */
HWTEST_F(PurgeableDmaBufTest, ChangeToNonPurgeableTest, TestSize.Level1)
{
    if (SkipDmabufTest()) {
        return;
    }
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestDataBuilder>('A', 'Z');
    PurgeableDmaBuf pobj(std::move(builder));
    ASSERT_TRUE(pobj.CreatePurgeableData(ALPHABET_BUF_SIZE));
    int originalFd = pobj.GetDmaBufFd();
    EXPECT_TRUE(ReadVerifyDmabufContent(&pobj, alphabet, ALPHABET_CMP_LEN));

    /* take over a non-dma-buf fd: marking must fail, object unchanged */
    size_t size = PAGE_SIZE;
    int plainFd = open("/dev/null", O_RDWR);
    ASSERT_GE(plainFd, 0);
    void *anonData = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_TRUE(anonData != MAP_FAILED);
    EXPECT_FALSE(pobj.ChangeDmaBufData(ALPHABET_BUF_SIZE, plainFd, anonData))
        << "change to non-purgeable fd must fail";
    /* object state unchanged: still has the original buffer */
    EXPECT_EQ(pobj.GetDmaBufFd(), originalFd);
    EXPECT_TRUE(ReadVerifyDmabufContent(&pobj, alphabet, ALPHABET_CMP_LEN));
    munmap(anonData, size);
    close(plainFd);
}

/*
 * PinReturnOneTest: PIN returns 1 (WAS_PURGED) when the buffer was purged,
 * BeginRead must succeed and rebuild the content (not treat it as failure).
 */
HWTEST_F(PurgeableDmaBufTest, PinReturnOneTest, TestSize.Level1)
{
    if (SkipDmabufTest()) {
        return;
    }
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ\0";
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestDataBuilder>('A', 'Z');
    PurgeableDmaBuf *pobj = new (std::nothrow) PurgeableDmaBuf(std::move(builder));
    ASSERT_NE(pobj, nullptr);
    ASSERT_TRUE(pobj->CreatePurgeableData(ALPHABET_BUF_SIZE));
    if (!pobj->isSupport_) {
        std::cout << __func__ << ": kernel not support purgeable, skip" << std::endl;
        delete pobj;
        return;
    }
    /* first access builds the content */
    ASSERT_TRUE(ReadVerifyDmabufContent(pobj, alphabet, ALPHABET_CMP_LEN));
    /* EndRead unpinned the buffer, kernel purges it on reclaim */
    if (!ReclaimPurgeableDma()) {
        std::cout << __func__ << ": trigger not available, skip" << std::endl;
        delete pobj;
        return;
    }
    std::this_thread::sleep_for(std::chrono::seconds(RECLAIM_INTERVAL_SECONDS));
    EXPECT_EQ(pobj->IsPurged(), true);

    /* raw ioctl: PIN must return 1 (WAS_PURGED), not an error */
    struct DmaBufPurgeablePin pin = { 0, 0 };
    int ret = ioctl(pobj->GetDmaBufFd(), DMA_BUF_PURGEABLE_PIN, &pin);
    EXPECT_EQ(ret, DMA_BUF_PURGEABLE_WAS_PURGED);
    /* undo the raw pin to restore the state for the object-level test */
    ioctl(pobj->GetDmaBufFd(), DMA_BUF_PURGEABLE_UNPIN, &pin);

    /* object-level BeginRead must succeed and rebuild (not fail on ret==1) */
    EXPECT_TRUE(ReadVerifyDmabufContent(pobj, alphabet, ALPHABET_CMP_LEN));
    EXPECT_FALSE(pobj->IsPurged());
    delete pobj;
    pobj = nullptr;
}

HWTEST_F(PurgeableDmaBufTest, DestructorKeepExternalResourceTest, TestSize.Level1)
{
    if (SkipDmabufTest()) {
        return;
    }
    size_t size = PAGE_SIZE;
    void *data = nullptr;
    int heapFd = -1;
    DmabufHeapBuffer buffer = {};
    ASSERT_TRUE(AllocDmabuf(size, &data, &heapFd, &buffer));
    int fd = static_cast<int>(buffer.fd);

    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestDataBuilder>('A', 'Z');
    PurgeableDmaBuf *pobj = new (std::nothrow) PurgeableDmaBuf(std::move(builder));
    ASSERT_NE(pobj, nullptr);
    ASSERT_TRUE(pobj->ChangeDmaBufData(ALPHABET_BUF_SIZE, fd, data));
    if (pobj->BeginRead()) {
        pobj->EndRead();
    }
    delete pobj;
    pobj = nullptr;

    /* the DMA buffer is owned by caller, it is still usable after destroyed */
    volatile char *str = static_cast<volatile char *>(data);
    str[0] = 'x';
    EXPECT_EQ(str[0], 'x');
    /* fd is not closed by the lib */
    EXPECT_NE(fcntl(fd, F_GETFD), -1);
    FreeDmaBuf(data, size, heapFd, &buffer);
}

HWTEST_F(PurgeableDmaBufTest, IsPurgedTest, TestSize.Level1)
{
    if (!IsPurgeableDmaSupported()) {
        return;
    }
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestDataBuilder>('A', 'Z');
    PurgeableDmaBuf pobj(std::move(builder));
    ASSERT_TRUE(pobj.CreatePurgeableData(ALPHABET_BUF_SIZE));
    pobj.isSupport_ = false;
    EXPECT_EQ(pobj.IsPurged(), false);
    /* Pin returns true (valid fd + not purgeable = inherently protected);
     * Unpin returns false (not purgeable, permanently not reclaimable) */
    EXPECT_EQ(pobj.Pin(), true);
    EXPECT_EQ(pobj.Unpin(), false);
    /* GetPinStatus returns -1 when the purgeable feature is not active */
    EXPECT_EQ(pobj.GetPinStatus(), -1);
    pobj.isSupport_ = true;
    /* fd 0 (stdin): ioctl fails on a non-dma-buf fd, Pin returns true
     * (disable fallback = content inherently protected), Unpin returns false */
    pobj.dmaBufFd_ = 0;
    EXPECT_EQ(pobj.Pin(), true);
    /* Pin ioctl failure disabled the object as safety fallback */
    EXPECT_FALSE(pobj.isSupport_);
    EXPECT_TRUE(pobj.disabled_);
    /* restore for the Unpin test on the same fd */
    pobj.isSupport_ = true;
    pobj.disabled_ = false;
    EXPECT_EQ(pobj.Unpin(), false);
    /* Unpin failure also disables the object */
    EXPECT_FALSE(pobj.isSupport_);
    EXPECT_TRUE(pobj.disabled_);
    /* restore for the fd -1 test */
    pobj.isSupport_ = true;
    pobj.disabled_ = false;
    /* fd -1: rejected by the >= 0 guard (no disable side effect) */
    pobj.dmaBufFd_ = -1;
    EXPECT_EQ(pobj.Pin(), false);
    EXPECT_EQ(pobj.Unpin(), false);
    EXPECT_NE(pobj.GetPinStatus(), 0);
    /* ToString should contain the backend state fields */
    std::string stateStr = pobj.ToString();
    EXPECT_NE(stateStr.find("isSupport:"), std::string::npos);
    EXPECT_NE(stateStr.find("isChange:"), std::string::npos);
}

HWTEST_F(PurgeableDmaBufTest, InvalidInputTest, TestSize.Level1)
{
    if (SkipDmabufTest()) {
        return;
    }
    /* nullptr builder */
    PurgeableDmaBuf *pobj = new (std::nothrow) PurgeableDmaBuf(nullptr);
    ASSERT_NE(pobj, nullptr);
    EXPECT_FALSE(pobj->BeginRead());
    EXPECT_FALSE(pobj->BeginWrite());
    delete pobj;
    pobj = nullptr;

    size_t size = PAGE_SIZE;
    void *data = nullptr;
    int heapFd = -1;
    DmabufHeapBuffer buffer = {};
    ASSERT_TRUE(AllocDmabuf(size, &data, &heapFd, &buffer));
    int fd = static_cast<int>(buffer.fd);

    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestDataBuilder>('A', 'Z');
    pobj = new (std::nothrow) PurgeableDmaBuf(std::move(builder));
    ASSERT_NE(pobj, nullptr);
    /* invalid size */
    EXPECT_FALSE(pobj->CreatePurgeableData(0));
    EXPECT_FALSE(pobj->CreatePurgeableData(OHOS_MAXIMUM_PURGEABLE_MEMORY));
    EXPECT_FALSE(pobj->ChangeDmaBufData(0, fd, data));
    EXPECT_FALSE(pobj->ChangeDmaBufData(OHOS_MAXIMUM_PURGEABLE_MEMORY, fd, data));
    EXPECT_FALSE(pobj->ChangeDmaBufData(OHOS_MAXIMUM_PURGEABLE_MEMORY + 1, fd, data));
    /* invalid fd or data */
    EXPECT_FALSE(pobj->ChangeDmaBufData(ALPHABET_BUF_SIZE, -1, data));
    EXPECT_FALSE(pobj->ChangeDmaBufData(ALPHABET_BUF_SIZE, fd, nullptr));
    /* invalid heapName */
    EXPECT_FALSE(pobj->CreatePurgeableData(ALPHABET_BUF_SIZE, nullptr));
    /* alloc failure: size beyond any dma heap capacity */
    EXPECT_FALSE(pobj->CreatePurgeableData(SIZE_MAX));
    /* still empty shell */
    EXPECT_EQ(pobj->GetDmaBufFd(), -1);

    /* a non-dmabuf fd: takeover succeeds, purgeable features degrade */
    VerifyNonDmabufFdTakeover(pobj, size);
    delete pobj;
    pobj = nullptr;
    FreeDmaBuf(data, size, heapFd, &buffer);
}

HWTEST_F(PurgeableDmaBufTest, StableMutiPageReadWriteTest, TestSize.Level1)
{
    if (SkipDmabufTest()) {
        return;
    }
    size_t size = TWO_PAGE_SIZE;
    char *alphabet = static_cast<char *>(malloc(size));
    ASSERT_NE(alphabet, nullptr);
    size_t len = 0;
    for (char ch = 'D'; len < size;) {
        alphabet[len++] = ch;
    }
    alphabet[size - 1] = 0;
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestBigDataBuilder>('A');
    PurgeableDmaBuf *pobj = new (std::nothrow) PurgeableDmaBuf(std::move(builder));
    ASSERT_NE(pobj, nullptr);
    ASSERT_TRUE(pobj->CreatePurgeableData(size));

    std::thread reclaimThread(LoopReclaimPurgeableDma, 10);
    std::thread readThread(LoopPrintAlphabetDma, pobj, 10);

    std::unique_ptr<PurgeableMemBuilder> modA2B = std::make_unique<DmaTestDataModifier>('A', 'B');
    std::unique_ptr<PurgeableMemBuilder> modB2C = std::make_unique<DmaTestDataModifier>('B', 'C');
    std::unique_ptr<PurgeableMemBuilder> modC2D = std::make_unique<DmaTestDataModifier>('C', 'D');
    ModifyPurgMemByBuilderDma(pobj, std::move(modA2B));
    ModifyPurgMemByBuilderDma(pobj, std::move(modB2C));
    ModifyPurgMemByBuilderDma(pobj, std::move(modC2D));

    int times = 0;
    int ret = 1;
    while (times++ < READ_RETRY_TIMES) {
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

HWTEST_F(PurgeableDmaBufTest, ConcurrentReadWriteTest, TestSize.Level1)
{
    if (SkipDmabufTest()) {
        return;
    }
    size_t size = TWO_PAGE_SIZE;
    char *alphabet = static_cast<char *>(malloc(size));
    ASSERT_NE(alphabet, nullptr);
    size_t len = 0;
    for (char ch = 'A'; len < size;) {
        alphabet[len++] = ch;
    }
    alphabet[size - 1] = 0;
    std::unique_ptr<PurgeableMemBuilder> builder = std::make_unique<DmaTestBigDataBuilder>('A');
    PurgeableDmaBuf *pobj = new (std::nothrow) PurgeableDmaBuf(std::move(builder));
    ASSERT_NE(pobj, nullptr);
    ASSERT_TRUE(pobj->CreatePurgeableData(size));

    /* concurrent BeginRead/BeginWrite on the same object */
    std::thread readThread1(ConcurrentReader, pobj, alphabet, size);
    std::thread readThread2(ConcurrentReader, pobj, alphabet, size);
    std::thread writeThread1(ConcurrentWriter, pobj, size);
    std::thread writeThread2(ConcurrentWriter, pobj, size);
    readThread1.join();
    readThread2.join();
    writeThread1.join();
    writeThread2.join();

    EXPECT_TRUE(ReadVerifyDmabufContent(pobj, alphabet, size - 1));
    delete pobj;
    pobj = nullptr;
    free(alphabet);
    alphabet = nullptr;
}

void LoopPrintAlphabetDma(PurgeableDmaBuf *pdata, unsigned int loopCount)
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

bool ReclaimPurgeableDma(void)
{
    std::string triggerPath = FindPurgeableDmaTriggerPath();
    if (triggerPath.empty()) {
        std::cout << __func__ << ": kernel not support" << std::endl;
        return false;
    }
    /* the trigger interface is root-only: elevate before writing, restore after */
    uid_t originalUid = geteuid();
    if (seteuid(0) != 0) {
        std::cout << __func__ << ": seteuid(0) fail" << std::endl;
        return false;
    }
    bool succ = true;
    FILE *f = fopen(triggerPath.c_str(), "w");
    if (!f) {
        std::cout << __func__ << ": open " << triggerPath << " fail" << std::endl;
        succ = false;
    } else {
        if (fputs("0 0", f) == EOF) {
            succ = false;
        }
        if (fclose(f) == EOF) {
            std::cout << __func__ << ": close file failed" << std::endl;
        }
    }
    if (seteuid(originalUid) != 0) {
        std::cout << __func__ << ": seteuid restore fail" << std::endl;
    }
    return succ;
}

void LoopReclaimPurgeableDma(unsigned int loopCount)
{
    bool ret = false;
    std::cout << "inter " << __func__ << std::endl;
    for (unsigned int i = 0; i < loopCount; i++) {
        ret = ReclaimPurgeableDma();
        std::cout << __func__ << ": " << i << ". Reclaim result=" << (ret ? "succ" : "fail") << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(RECLAIM_INTERVAL_SECONDS)); /* wait reclaim finish */
    }
    std::cout << "quit " << __func__ << std::endl;
}

void ModifyPurgMemByBuilderDma(PurgeableDmaBuf *pdata, std::unique_ptr<PurgeableMemBuilder> mod)
{
    if (!pdata->BeginWrite()) {
        std::cout << __func__ << ": ERROR! BeginWrite failed." << std::endl;
        return;
    }
    std::this_thread::sleep_for(std::chrono::seconds(MODIFY_INTERVAL_SECONDS));
    pdata->ModifyContentByBuilder(std::move(mod));
    pdata->EndWrite();
}
} /* namespace PurgeableMem */
} /* namespace OHOS */

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
#include <cstdlib>
#include <fcntl.h>
#include <cerrno>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <climits>
#include <dirent.h>
#include <string>
#include "securec.h"
#include "gtest/gtest.h"
#include "dmabuf_alloc.h"

using namespace testing;
using namespace testing::ext;

namespace {
const int BUFFER_SIZE = 128;

class DmabufAllocTest : public testing::Test {
public:
    static void SetUpTestCase();
    static void TearDownTestCase();
    void SetUp();
    void TearDown();
    std::string heapName;
};

void DmabufAllocTest::SetUpTestCase()
{
}

void DmabufAllocTest::TearDownTestCase()
{
}

void DmabufAllocTest::SetUp()
{
    std::string rootDir = "/dev/dma_heap/";
    DIR *dir = opendir(rootDir.c_str());
    if (dir == nullptr) {
        return;
    }
    struct dirent *ptr;
    while ((ptr = readdir(dir)) != nullptr) {
        std::string fileName = ptr->d_name;
        std::string::size_type idx = fileName.find("system");
        if (idx != std::string::npos) {
            heapName = fileName;
            break;
        }
    }
    closedir(dir);
}

void DmabufAllocTest::TearDown()
{
}

HWTEST_F(DmabufAllocTest, AllocSingleBuffer, TestSize.Level1)
{
    ASSERT_STRNE(heapName.c_str(), "");

    int heapFd = DmabufHeapOpen(heapName.c_str());
    ASSERT_GE(heapFd, 0);

    DmabufHeapBuffer buffer = { .size = BUFFER_SIZE, .heapFlags = 0 };
    ASSERT_EQ(0, DmabufHeapBufferAlloc(heapFd, &buffer));

    void *ptr = mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, buffer.fd, 0);
    ASSERT_TRUE(ptr != NULL);

    ASSERT_EQ(0, DmabufHeapBufferSyncStart(buffer.fd, DMA_BUF_HEAP_BUF_SYNC_RW));

    ASSERT_GE(sprintf_s((char *)ptr, BUFFER_SIZE, "libdmabufheap"), 0);

    ASSERT_EQ(0, DmabufHeapBufferSyncEnd(buffer.fd, DMA_BUF_HEAP_BUF_SYNC_RW));

    ASSERT_STREQ("libdmabufheap", (char *)ptr);

    ASSERT_EQ(0, munmap(ptr, BUFFER_SIZE));

    ASSERT_EQ(0, DmabufHeapBufferFree(&buffer));

    ASSERT_EQ(0, DmabufHeapClose(heapFd));
}

HWTEST_F(DmabufAllocTest, ShareBufferBetweenProcess, Function|MediumTest|Level1)
{
    ASSERT_STRNE(heapName.c_str(), "");

    int heapFd = DmabufHeapOpen(heapName.c_str());
    ASSERT_GE(heapFd, 0);

    DmabufHeapBuffer buffer = { .size = BUFFER_SIZE, .heapFlags = 0 };
    ASSERT_EQ(0, DmabufHeapBufferAlloc(heapFd, &buffer));

    void *ptr = mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, buffer.fd, 0);
    ASSERT_TRUE(ptr != NULL);

    ASSERT_EQ(0, DmabufHeapBufferSyncStart(buffer.fd, DMA_BUF_HEAP_BUF_SYNC_RW));

    ASSERT_GE(sprintf_s((char *)ptr, BUFFER_SIZE, "parent"), 0);

    ASSERT_EQ(0, DmabufHeapBufferSyncEnd(buffer.fd, DMA_BUF_HEAP_BUF_SYNC_RW));

    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    /* child process */
    if (pid == 0) {
        ptr = mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, buffer.fd, 0);
        ASSERT_TRUE(ptr != NULL);

        ASSERT_EQ(0, DmabufHeapBufferSyncStart(buffer.fd, DMA_BUF_HEAP_BUF_SYNC_RW));

        ASSERT_STREQ("parent", (char *)ptr);

        ASSERT_GE(sprintf_s((char *)ptr, BUFFER_SIZE, "child"), 0);

        ASSERT_EQ(0, DmabufHeapBufferSyncEnd(buffer.fd, DMA_BUF_HEAP_BUF_SYNC_RW));

        ASSERT_EQ(0, munmap(ptr, BUFFER_SIZE));

        ASSERT_EQ(0, DmabufHeapBufferFree(&buffer));

        exit(EXIT_SUCCESS);
    }
    /* parent process */
    waitpid(pid, nullptr, 0);

    ASSERT_EQ(0, DmabufHeapBufferSyncStart(buffer.fd, DMA_BUF_HEAP_BUF_SYNC_RW));

    ASSERT_STREQ("child", (char *)ptr);

    ASSERT_EQ(0, DmabufHeapBufferSyncEnd(buffer.fd, DMA_BUF_HEAP_BUF_SYNC_RW));

    ASSERT_EQ(0, munmap(ptr, BUFFER_SIZE));

    ASSERT_EQ(0, DmabufHeapBufferFree(&buffer));
    
    ASSERT_EQ(0, DmabufHeapClose(heapFd));
}

HWTEST_F(DmabufAllocTest, OpenInvalidNameHeap, Function|MediumTest|Level1)
{
    int i;
    std::string invalidName = "invalid";
    int heapFd = DmabufHeapOpen(invalidName.c_str());
    ASSERT_EQ(-EPERM, heapFd);

    for (i = 0; i < 20; i++) {
        invalidName += "invalid";
    }
    heapFd = DmabufHeapOpen(invalidName.c_str());
    ASSERT_EQ(-EINVAL, heapFd);
}

HWTEST_F(DmabufAllocTest, AllocInvalidSizeBuffer, Function|MediumTest|Level1)
{
    ASSERT_STRNE(heapName.c_str(), "");

    int heapFd = DmabufHeapOpen(heapName.c_str());
    ASSERT_GE(heapFd, 0);

    DmabufHeapBuffer buffer = { .size = 0 };
    ASSERT_EQ(-EINVAL, DmabufHeapBufferAlloc(heapFd, &buffer));

    ASSERT_EQ(0, DmabufHeapClose(heapFd));
}

HWTEST_F(DmabufAllocTest, BufferSyncWithWrongFd, Function|MediumTest|Level1)
{
    ASSERT_STRNE(heapName.c_str(), "");

    const unsigned int WRONG_FD = UINT_MAX;

    int heapFd = DmabufHeapOpen(heapName.c_str());
    ASSERT_GE(heapFd, 0);

    DmabufHeapBuffer buffer = { .size = BUFFER_SIZE, .heapFlags = 0 };
    ASSERT_EQ(0, DmabufHeapBufferAlloc(heapFd, &buffer));

    void *ptr = mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, buffer.fd, 0);
    ASSERT_TRUE(ptr != NULL);

    ASSERT_NE(0, DmabufHeapBufferSyncStart(WRONG_FD, DMA_BUF_HEAP_BUF_SYNC_RW));

    ASSERT_NE(0, DmabufHeapBufferSyncEnd(WRONG_FD, DMA_BUF_HEAP_BUF_SYNC_RW));

    ASSERT_EQ(0, munmap(ptr, BUFFER_SIZE));

    ASSERT_EQ(0, DmabufHeapBufferFree(&buffer));

    ASSERT_EQ(0, DmabufHeapClose(heapFd));
}

HWTEST_F(DmabufAllocTest, BufferSyncWithWrongSyncType, Function|MediumTest|Level1)
{
    ASSERT_STRNE(heapName.c_str(), "");

    const unsigned int WRONG_SYNC_TYPE = UINT_MAX;

    int heapFd = DmabufHeapOpen(heapName.c_str());
    ASSERT_GE(heapFd, 0);

    DmabufHeapBuffer buffer = { .size = BUFFER_SIZE, .heapFlags = 0 };
    ASSERT_EQ(0, DmabufHeapBufferAlloc(heapFd, &buffer));

    void *ptr = mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, buffer.fd, 0);
    ASSERT_TRUE(ptr != NULL);

    ASSERT_NE(0, DmabufHeapBufferSyncEnd(buffer.fd, (DmabufHeapBufferSyncType)WRONG_SYNC_TYPE));

    ASSERT_NE(0, DmabufHeapBufferSyncStart(buffer.fd, (DmabufHeapBufferSyncType)WRONG_SYNC_TYPE));

    ASSERT_EQ(0, munmap(ptr, BUFFER_SIZE));

    ASSERT_EQ(0, DmabufHeapBufferFree(&buffer));

    ASSERT_EQ(0, DmabufHeapClose(heapFd));
}

HWTEST_F(DmabufAllocTest, SyncBufferTwice, Function|MediumTest|Level1)
{
    ASSERT_STRNE(heapName.c_str(), "");

    int heapFd = DmabufHeapOpen(heapName.c_str());
    ASSERT_GE(heapFd, 0);

    DmabufHeapBuffer buffer = { .size = BUFFER_SIZE, .heapFlags = 0 };
    ASSERT_EQ(0, DmabufHeapBufferAlloc(heapFd, &buffer));

    void *ptr = mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, buffer.fd, 0);
    ASSERT_TRUE(ptr != NULL);

    ASSERT_EQ(0, DmabufHeapBufferSyncStart(buffer.fd, DMA_BUF_HEAP_BUF_SYNC_RW));

    ASSERT_EQ(0, DmabufHeapBufferSyncStart(buffer.fd, DMA_BUF_HEAP_BUF_SYNC_RW));

    ASSERT_GE(sprintf_s((char *)ptr, BUFFER_SIZE, "libdmabufheap"), 0);

    ASSERT_EQ(0, DmabufHeapBufferSyncEnd(buffer.fd, DMA_BUF_HEAP_BUF_SYNC_RW));

    ASSERT_EQ(0, DmabufHeapBufferSyncEnd(buffer.fd, DMA_BUF_HEAP_BUF_SYNC_RW));

    ASSERT_STREQ("libdmabufheap", (char *)ptr);

    ASSERT_EQ(0, munmap(ptr, BUFFER_SIZE));

    ASSERT_EQ(0, DmabufHeapBufferFree(&buffer));

    ASSERT_EQ(0, DmabufHeapClose(heapFd));
}

HWTEST_F(DmabufAllocTest, ExchangeBufferSyncOrder, Function|MediumTest|Level1)
{
    ASSERT_STRNE(heapName.c_str(), "");

    int heapFd = DmabufHeapOpen(heapName.c_str());
    ASSERT_GE(heapFd, 0);

    DmabufHeapBuffer buffer = { .size = BUFFER_SIZE, .heapFlags = 0 };
    ASSERT_EQ(0, DmabufHeapBufferAlloc(heapFd, &buffer));

    void *ptr = mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, buffer.fd, 0);
    ASSERT_TRUE(ptr != NULL);

    ASSERT_EQ(0, DmabufHeapBufferSyncEnd(buffer.fd, DMA_BUF_HEAP_BUF_SYNC_RW));

    ASSERT_EQ(0, DmabufHeapBufferSyncStart(buffer.fd, DMA_BUF_HEAP_BUF_SYNC_RW));

    ASSERT_EQ(0, munmap(ptr, BUFFER_SIZE));

    ASSERT_EQ(0, DmabufHeapBufferFree(&buffer));

    ASSERT_EQ(0, DmabufHeapClose(heapFd));
}
}
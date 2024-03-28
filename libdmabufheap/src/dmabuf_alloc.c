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

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "securec.h"
#include "hilog/log.h"
#include "dmabuf_alloc.h"
#include "memory_trace.h"

#define DMA_BUF_HEAP_ROOT "/dev/dma_heap/"
#define HEAP_ROOT_LEN strlen(DMA_BUF_HEAP_ROOT)
#define HEAP_NAME_MAX_LEN 128
#define HEAP_PATH_LEN (HEAP_ROOT_LEN + HEAP_NAME_MAX_LEN + 1)

static bool IsHeapNameValid(const char *heapName)
{
    if ((heapName == NULL) || (strlen(heapName) == 0) ||
        (strlen(heapName) > HEAP_NAME_MAX_LEN)) {
        return false;
    }

    return true;
}

static bool IsSyncTypeValid(DmabufHeapBufferSyncType syncType)
{
    switch (syncType) {
        case DMA_BUF_HEAP_BUF_SYNC_RW:
        case DMA_BUF_HEAP_BUF_SYNC_READ:
        case DMA_BUF_HEAP_BUF_SYNC_WRITE:
            return true;
        default:
            return false;
    }
}

void SetOwnerIdForHeapFlags(DmabufHeapBuffer *buffer, enum DmaHeapFlagOwnerId ownerId)
{
    if (buffer) {
        set_owner_id_for_heap_flags(&buffer->heapFlags, ownerId);
    }
}

int DmabufHeapOpen(const char *heapName)
{
    if (!IsHeapNameValid(heapName)) {
        HILOG_ERROR(LOG_CORE, "heapName is wrong, name = %s.", (heapName == NULL) ? "NULL" : heapName);
        return -EINVAL;
    }
    char heapPath[HEAP_PATH_LEN] = DMA_BUF_HEAP_ROOT;
    errno_t ret = strcat_s(heapPath, HEAP_PATH_LEN, heapName);
    if (ret != EOK) {
        HILOG_ERROR(LOG_CORE, "strcat_s is wrong, heapName = %s, ret = %d.", heapName, ret);
        return -EINVAL;
    }
    int fd = open(heapPath, O_RDONLY | O_CLOEXEC);
    long newFd = fd;
    memtrace((void *)newFd, HEAP_NAME_MAX_LEN, "DmabufHeap", true);
    return fd;
}

int DmabufHeapClose(unsigned int fd)
{
    long newFd = fd;
    memtrace((void *)newFd, HEAP_NAME_MAX_LEN, "DmabufHeap", false);
    return close(fd);
}

int DmabufHeapBufferAlloc(unsigned int heapFd, DmabufHeapBuffer *buffer)
{
    if (buffer == NULL) {
        HILOG_ERROR(LOG_CORE, "%{public}s: buffer is NULL!", __func__);
        return -EINVAL;
    }
    if (buffer->size == 0) {
        HILOG_ERROR(LOG_CORE, "alloc buffer size is wrong.");
        return -EINVAL;
    }

    struct dma_heap_allocation_data data = {
        .len = buffer->size,
        .fd_flags = O_RDWR | O_CLOEXEC,
        .heap_flags = buffer->heapFlags,
    };
    int ret = ioctl(heapFd, DMA_HEAP_IOCTL_ALLOC, &data);
    if (ret < 0) {
        HILOG_ERROR(LOG_CORE, "alloc buffer failed, size = %zu, ret = %d.", buffer->size, ret);
        return ret;
    }
    memtrace((void *)buffer, buffer->size, "DmabufHeap", true);
    buffer->fd = data.fd;
    return ret;
}

int DmabufHeapBufferFree(DmabufHeapBuffer *buffer)
{
    if (buffer == NULL) {
        HILOG_ERROR(LOG_CORE, "%{public}s: buffer is NULL!", __func__);
        return -EINVAL;
    }
    memtrace((void *)buffer, buffer->size, "DmabufHeap", false);
    return close(buffer->fd);
}

int DmabufHeapBufferSyncStart(unsigned int fd, DmabufHeapBufferSyncType syncType)
{
    if (!IsSyncTypeValid(syncType)) {
        HILOG_ERROR(LOG_CORE, "buffer start syncType is wrong, syncType = %u.", syncType);
        return -EINVAL;
    }

    struct dma_buf_sync sync = {0};
    sync.flags = DMA_BUF_SYNC_START | syncType;
    return ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
}

int DmabufHeapBufferSyncEnd(unsigned int fd, DmabufHeapBufferSyncType syncType)
{
    if (!IsSyncTypeValid(syncType)) {
        HILOG_ERROR(LOG_CORE, "buffer end syncType is wrong, syncType = %u.", syncType);
        return -EINVAL;
    }

    struct dma_buf_sync sync = {0};
    sync.flags = DMA_BUF_SYNC_END | syncType;
    return ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
}
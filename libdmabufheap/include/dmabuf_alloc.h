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

#ifndef LIB_DMA_BUF_HEAP_H
#define LIB_DMA_BUF_HEAP_H

#include <stdlib.h>
#include <linux/dma-buf.h>

#undef LOG_TAG
#define LOG_TAG "dmabufheap"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* End of #if __cplusplus */
#endif /* End of #ifdef __cplusplus */

typedef enum {
    DMA_BUF_HEAP_BUF_SYNC_RW = DMA_BUF_SYNC_RW,
    DMA_BUF_HEAP_BUF_SYNC_READ = DMA_BUF_SYNC_READ,
    DMA_BUF_HEAP_BUF_SYNC_WRITE = DMA_BUF_SYNC_WRITE,
} DmabufHeapBufferSyncType;

typedef struct {
    unsigned int fd;
    size_t size;
} DmabufHeapBuffer;

int DmabufHeapOpen(const char *heapName);

int DmabufHeapClose(unsigned int fd);

int DmabufHeapBufferAlloc(unsigned int heapFd, DmabufHeapBuffer *buffer);

int DmabufHeapBufferFree(DmabufHeapBuffer *buffer);

int DmabufHeapBufferSyncStart(unsigned int bufferFd, DmabufHeapBufferSyncType syncType);

int DmabufHeapBufferSyncEnd(unsigned int bufferFd, DmabufHeapBufferSyncType syncType);

#ifdef __cplusplus
#if __cplusplus
}
#endif /* End of #if __cplusplus */
#endif /* End of #ifdef __cplusplus */

#endif /* LIB_DMA_BUF_HEAP_H */
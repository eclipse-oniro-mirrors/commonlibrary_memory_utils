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

#include <cerrno> /* errno, EINTR */
#include <sys/ioctl.h> /* ioctl */
#include <unistd.h> /* TEMP_FAILURE_RETRY */

#include "purgeable_dmabuf_c.h"

/*
 * Bridge of the purgeable extension ioctls of dma-buf.
 * Implemented in C++ and exported as "C" functions, so the C code
 * (such as purgeable_mem_c.c) can also call them.
 */

int PurgDmaBufSetPurgeable(int dmabufFd)
{
    return TEMP_FAILURE_RETRY(ioctl(dmabufFd, DMA_BUF_SET_PURGEABLE));
}

int PurgDmaBufGetPurgeable(int dmabufFd)
{
    return TEMP_FAILURE_RETRY(ioctl(dmabufFd, DMA_BUF_GET_PURGEABLE));
}

int PurgDmaBufPin(int dmabufFd, struct DmaBufPurgeablePin *pin)
{
    if (pin == nullptr) {
        return -1;
    }
    return TEMP_FAILURE_RETRY(ioctl(dmabufFd, DMA_BUF_PURGEABLE_PIN, pin));
}

int PurgDmaBufUnpin(int dmabufFd, struct DmaBufPurgeablePin *pin)
{
    if (pin == nullptr) {
        return -1;
    }
    return TEMP_FAILURE_RETRY(ioctl(dmabufFd, DMA_BUF_PURGEABLE_UNPIN, pin));
}

int PurgDmaBufGetPinStatus(int dmabufFd)
{
    return TEMP_FAILURE_RETRY(ioctl(dmabufFd, DMA_BUF_GET_PIN_STATUS));
}

int PurgDmaBufIsPurged(int dmabufFd)
{
    return TEMP_FAILURE_RETRY(ioctl(dmabufFd, DMA_BUF_PURGEABLE_IS_PURGED));
}

int PurgDmaBufRebuildSuccess(int dmabufFd)
{
    return TEMP_FAILURE_RETRY(ioctl(dmabufFd, DMA_BUF_PURGEABLE_REBUILD_SUCCESS));
}

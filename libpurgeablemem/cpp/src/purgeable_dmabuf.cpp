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

#include "purgeable_dmabuf.h"

#include <cerrno> /* errno, EINTR */
#include <dirent.h> /* opendir, readdir, closedir */
#include <sys/mman.h> /* mmap, munmap, PROT_READ, PROT_WRITE, MAP_SHARED, MAP_FAILED */
#include <unistd.h> /* TEMP_FAILURE_RETRY */
#include "securec.h"
#include "dmabuf_alloc.h" /* DmabufHeapOpen, DmabufHeapBufferAlloc */
#include "pm_util.h"
#include "pm_state_c.h"
#include "pm_smartptr_util.h"
#include "pm_log.h"

namespace OHOS {
namespace PurgeableMem {
#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "PurgeableMem"

/* Scan /dev/dma_heap/ for a heap whose name contains "system".
 * Used as fallback when DmabufHeapOpen("system") fails — the actual
 * heap name may be "system-secure" or similar on some devices. */
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

/* OpenDmaHeap: open @heapName, fall back to the scanned system heap on failure. */
static int OpenDmaHeap(const char *heapName)
{
    int heapFd = DmabufHeapOpen(heapName);
    if (heapFd >= 0) {
        return heapFd;
    }
    /* Fallback: scan /dev/dma_heap/ for a heap whose name contains "system",
     * since the actual heap name may not be exactly "system" (e.g. "system-secure"). */
    std::string foundHeap = FindSystemDmaHeapName();
    if (foundHeap.empty() || foundHeap == heapName) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: DmabufHeapOpen failed, heapName=%{public}s", __func__, heapName);
        return -1;
    }
    heapFd = DmabufHeapOpen(foundHeap.c_str());
    if (heapFd < 0) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: DmabufHeapOpen fallback failed, heapName=%{public}s",
                       __func__, foundHeap.c_str());
    }
    return heapFd;
}

/*
 * AllocAndMapDmaBuffer: alloc a DMA buffer via libdmabufheap and mmap it.
 * The heap fd is always closed before returning.
 */
static bool AllocAndMapDmaBuffer(int heapFd, size_t size, DmabufHeapBuffer *buffer, void **data)
{
    buffer->fd = 0;
    buffer->size = size;
    buffer->heapFlags = DMA_HEAP_FLAG_DIRECT_RECLAIM;
    if (DmabufHeapBufferAlloc(heapFd, buffer) != 0) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: DmabufHeapBufferAlloc failed, size=%{public}zu", __func__, size);
        DmabufHeapClose(heapFd);
        return false;
    }
    /* the buffer fd is independent of the heap fd, close the heap fd in time */
    DmabufHeapClose(heapFd);
    *data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, buffer->fd, 0);
    if (*data == MAP_FAILED) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: mmap failed", __func__);
        DmabufHeapBufferFree(buffer);
        return false;
    }
    return true;
}

/* MarkDmaBufPurgeable: set the DMA buffer purgeable, return whether the kernel supports it. */
static bool MarkDmaBufPurgeable(int dmabufFd)
{
    TEMP_FAILURE_RETRY(ioctl(dmabufFd, DMA_BUF_SET_PURGEABLE));
    return TEMP_FAILURE_RETRY(ioctl(dmabufFd, DMA_BUF_GET_PURGEABLE)) == PURGEABLE_DMA_BUF_PURGEABLE;
}

/* QueryDmaBufIno: query the dmabuf internal inode number, 0 on failure. */
static unsigned long QueryDmaBufIno(int dmabufFd)
{
    struct DmaBufPurgeableIno inoInfo = {};
    if (TEMP_FAILURE_RETRY(ioctl(dmabufFd, DMA_BUF_PURGEABLE_GET_INO, &inoInfo)) != 0) {
        return 0;
    }
    return static_cast<unsigned long>(inoInfo.ino);
}

PurgeableDmaBuf::PurgeableDmaBuf(std::unique_ptr<PurgeableMemBuilder> builder)
{
    dataPtr_ = nullptr;
    builder_ = nullptr;
    dmaBufFd_ = -1;
    buildDataCount_ = 0;
    isSupport_ = false;
    isChange_ = false;
    pin_ = { static_cast<uint32_t>(0), static_cast<uint32_t>(0) };
    IF_NULL_LOG_ACTION(builder, "%{public}s: input builder nullptr", return);
    builder_ = std::move(builder);
    PM_HILOG_DEBUG(LOG_CORE, "%{public}s init succ. %{public}s", __func__, ToString().c_str());
}

PurgeableDmaBuf::~PurgeableDmaBuf()
{
    PM_HILOG_DEBUG(LOG_CORE, "%{public}s %{public}s", __func__, ToString().c_str());
    if (isChange_) {
        /* Buffer taken over via ChangeDmaBufData — do NOT munmap/close. */
        builder_.reset();
    } else {
        /* Buffer allocated by CreatePurgeableData — munmap and close. */
        if (dataPtr_ != nullptr) {
            munmap(dataPtr_, dataSizeInput_);
            dataPtr_ = nullptr;
        }
        if (dmaBufFd_ > 0) {
            close(dmaBufFd_);
        }
        builder_.reset();
    }
}

int PurgeableDmaBuf::GetDmaBufFd() const
{
    return dmaBufFd_;
}

/*
 * Pin: protect the content from being reclaimed by the kernel.
 * Return:  true if the content is protected — either the kernel reference
 *          count was incremented, or the buffer is not purgeable and thus
 *          inherently protected from reclamation;
 *          false if there is no valid buffer (fd guard).
 * Note: on ioctl failure the object is disabled as a safety fallback,
 * and Pin still returns true — the buffer just lost its purgeable status,
 * so the content is inherently protected from now on.
 * PIN ioctl return values: <0 = failure (disable fallback), 0 = success
 * (not purged), 1 = success (was purged, rebuild needed — handled by
 * IsPurged(), not by Pin's return value).
 */
bool PurgeableDmaBuf::Pin()
{
    if (dmaBufFd_ < 0) {
        PM_HILOG_DEBUG(LOG_CORE, "dmaBufFd_ not exist!!");
        return false;
    }
    if (!isSupport_) {
        return true;
    }
    int ret = TEMP_FAILURE_RETRY(ioctl(dmaBufFd_, DMA_BUF_PURGEABLE_PIN, &pin_));
    if (ret < 0) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: Pin ioctl fail (disable applied), fd:%{public}d ret:%{public}d",
            __func__, dmaBufFd_, ret);
        disabled_ = true;
        isSupport_ = false;
    }
    return true;
}

/*
 * UNPIN ioctl return values: <0 = failure, 0 = success, >0 = pin count
 * underflow (the caller violated the pin/unpin pairing constraint).
 * Any failure disables the object as a safety fallback. On underflow,
 * the content is at pin count 0 and may have been purged — pin once
 * more and rebuild if needed to protect the content before disabling.
 */
/*
 * Unpin: release one reference on the content.
 * Return:  true if the reference count was successfully decremented;
 *          false means the purgeable mechanism is permanently disabled
 *          and the content will never be reclaimed by the kernel
 *          (no valid buffer, not purgeable, ioctl error, or underflow).
 */
bool PurgeableDmaBuf::Unpin()
{
    if (dmaBufFd_ < 0) {
        PM_HILOG_DEBUG(LOG_CORE, "dmaBufFd_ not exist!!");
        return false;
    }
    if (!isSupport_) {
        return false;
    }
    int ret = TEMP_FAILURE_RETRY(ioctl(dmaBufFd_, DMA_BUF_PURGEABLE_UNPIN, &pin_));
    if (ret < 0) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: Unpin fail, fd:%{public}d ret:%{public}d",
            __func__, dmaBufFd_, ret);
        disabled_ = true;
        isSupport_ = false;
        return false;
    }
    if (ret > 0) {
        /*
         * Underflow: pin count was 0, content may have been purged.
         * Pin and rebuild to protect the content before disabling.
         * This mirrors DisablePurgeable's internal BeginReadUnlocked
         * logic, but skips its failure-path Unpin — the pin is
         * intentionally kept because the object is being disabled.
         */
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: Unpin underflow, fd:%{public}d",
            __func__, dmaBufFd_);
        if (Pin() && IfNeedRebuild()) {
            BuildContent();
        }
        disabled_ = true;
        isSupport_ = false;
        return false;
    }
    return true;
}

bool PurgeableDmaBuf::IsPurged()
{
    if (!isSupport_) {
        return false;
    }
    int ret = TEMP_FAILURE_RETRY(ioctl(dmaBufFd_, DMA_BUF_PURGEABLE_IS_PURGED));
    return ret > 0 ? true : false;
}

int PurgeableDmaBuf::GetPinStatus() const
{
    if (!isSupport_) {
        return -1;
    }
    return TEMP_FAILURE_RETRY(ioctl(dmaBufFd_, DMA_BUF_GET_PIN_STATUS));
}

void PurgeableDmaBuf::AfterRebuildSucc()
{
    TEMP_FAILURE_RETRY(ioctl(dmaBufFd_, DMA_BUF_PURGEABLE_REBUILD_SUCCESS));
}

/* ReleaseSelfAllocatedBuffer: release the previous self-allocated buffer, if any. */
void PurgeableDmaBuf::ReleaseSelfAllocatedBuffer()
{
    if (!isChange_ && dataPtr_) {
        if (munmap(dataPtr_, dataSizeInput_) != 0) {
            PM_HILOG_ERROR(LOG_CORE, "%{public}s: munmap dataPtr fail", __func__);
        }
        dataPtr_ = nullptr;
        if (dmaBufFd_ > 0) {
            close(dmaBufFd_);
            dmaBufFd_ = -1;
        }
    }
}

bool PurgeableDmaBuf::CreatePurgeableData(size_t size, const char *heapName)
{
    PM_HILOG_DEBUG(LOG_CORE, "%{public}s", __func__);
    if (size == 0 || size >= OHOS_MAXIMUM_PURGEABLE_MEMORY) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: invalid size %{public}zu", __func__, size);
        return false;
    }
    if (heapName == nullptr) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: input heapName nullptr", __func__);
        return false;
    }

    /* reject if permanently disabled */
    if (disabled_) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: object disabled", __func__);
        return false;
    }

    /* release the previous self-allocated buffer, if any */
    ReleaseSelfAllocatedBuffer();

    int heapFd = OpenDmaHeap(heapName);
    if (heapFd < 0) {
        return false;
    }
    DmabufHeapBuffer buffer = {};
    void *data = nullptr;
    if (!AllocAndMapDmaBuffer(heapFd, size, &buffer, &data)) {
        return false;
    }

    /* mark as purgeable; release the buffer if the kernel does not support it */
    isSupport_ = MarkDmaBufPurgeable(static_cast<int>(buffer.fd));
    if (!isSupport_) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: mark purgeable fail, release buffer", __func__);
        DmabufHeapBufferFree(&buffer);
        munmap(data, size);
        return false;
    }

    dataSizeInput_ = size;
    dmaBufFd_ = static_cast<int>(buffer.fd);
    pin_ = { static_cast<uint32_t>(0), static_cast<uint32_t>(0) };
    dataPtr_ = data;
    isChange_ = false;
    /* new empty buffer, the first access rebuilds it by builder */
    buildDataCount_ = 0;
    /* empty buffer: unpin so the kernel can reclaim it until first access */
    Unpin();
    /* Cache ino once — stable for the lifetime of the dma-buf fd */
    ino_ = QueryDmaBufIno(dmaBufFd_);
    return true;
}

bool PurgeableDmaBuf::ChangeDmaBufData(size_t size, int fd, void *data)
{
    if (size <= 0 || size >= OHOS_MAXIMUM_PURGEABLE_MEMORY) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: invalid size %{public}zu", __func__, size);
        return false;
    }
    if (fd < 0) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: invalid dma-buf fd", __func__);
        return false;
    }
    if (data == nullptr) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: data is nullptr", __func__);
        return false;
    }
    /* reject if permanently disabled */
    if (disabled_) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: object disabled", __func__);
        return false;
    }

    /*
     * Probe the new fd before committing: mark it as purgeable first,
     * so a failure leaves the object and the caller's buffer untouched.
     */
    if (!MarkDmaBufPurgeable(fd)) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: mark purgeable fail, fd:%{public}d", __func__, fd);
        return false;
    }

    /* commit: release the previous self-allocated buffer, then take over */
    ReleaseSelfAllocatedBuffer();
    dataSizeInput_ = size;
    dmaBufFd_ = fd;
    dataPtr_ = data;
    buildDataCount_++;
    isChange_ = true;
    isSupport_ = true;
    /* Cache ino once — stable for the lifetime of the dma-buf fd */
    ino_ = QueryDmaBufIno(dmaBufFd_);

    PM_HILOG_INFO(LOG_CORE, "%{public}s: succ, fd:%{public}d ino:%{public}lu size:%{public}zu",
                  __func__, dmaBufFd_, ino_, dataSizeInput_);
    return true;
}

void PurgeableDmaBuf::ResizeData(size_t newSize)
{
    if (newSize == 0 || newSize >= OHOS_MAXIMUM_PURGEABLE_MEMORY) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: invalid size %{public}zu", __func__, newSize);
        return;
    }
    if (isChange_) {
        /* the taken-over DMA buffer is owned by caller, cannot resize it */
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: external buffer cannot be resized", __func__);
        return;
    }
    if (disabled_) {
        /* disabled objects cannot get a new purgeable buffer */
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: object disabled", __func__);
        return;
    }
    if (dataPtr_) {
        if (munmap(dataPtr_, dataSizeInput_) != 0) {
            PM_HILOG_ERROR(LOG_CORE, "%{public}s: munmap dataPtr fail", __func__);
        }
        dataPtr_ = nullptr;
    }
    if (dmaBufFd_ > 0) {
        close(dmaBufFd_);
        dmaBufFd_ = -1;
    }
    if (!CreatePurgeableData(newSize)) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: resize to %{public}zu fail", __func__, newSize);
    }
}

std::string PurgeableDmaBuf::ToString() const
{
    return PurgeableMemBase::ToString() +
           " isSupport:" + std::to_string(isSupport_) +
           " isChange:" + std::to_string(isChange_);
}

PurgMemType PurgeableDmaBuf::GetPurgMemType() const
{
    return PURG_MEM_TYPE_DMA_BUF;
}

int PurgeableDmaBuf::GetFd() const
{
    return dmaBufFd_;
}

unsigned long PurgeableDmaBuf::GetIno() const
{
    return ino_;
}
} /* namespace PurgeableMem */
} /* namespace OHOS */

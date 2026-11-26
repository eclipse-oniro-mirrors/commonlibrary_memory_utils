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

/*
 * MarkDmaBufPurgeable: enable the purgeable feature of the DMA buffer
 * (SET_PURGEABLE with MODE_DISCARD), then verify it via GET_PURGEABLE.
 * On success @ino receives the dmabuf internal inode number returned by
 * the kernel (positive, for dfx caching); it is left untouched on failure.
 * Return: whether the buffer is purgeable now.
 */
static bool MarkDmaBufPurgeable(int dmabufFd, unsigned long *ino)
{
    __u32 mode = PURGEABLE_DMA_BUF_MODE_DISCARD;
    int ret = TEMP_FAILURE_RETRY(ioctl(dmabufFd, DMA_BUF_SET_PURGEABLE, &mode));
    if (ret < 0) {
        return false;
    }
    if (ino != nullptr) {
        /* the positive return of SET is the dmabuf internal inode number */
        *ino = static_cast<unsigned long>(ret);
    }
    return TEMP_FAILURE_RETRY(ioctl(dmabufFd, DMA_BUF_GET_PURGEABLE)) == PURGEABLE_DMA_BUF_MODE_DISCARD;
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
 * (not purged), 1 = success (was purged) — the snapshot is recorded in
 * wasPurged_ and read by IsPurged() (the IS_PURGED ioctl was removed).
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
        (void)DisablePurgeableKernel();
        return true;
    }
    wasPurged_ = (ret > 0);
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
/*
 * TryRebuildAfterUnderflow: guard-style best-effort content recovery.
 * Each check is an early return on failure.
 * Return: true if content is present (or rebuilt), false on failure.
 */
bool PurgeableDmaBuf::TryRebuildAfterUnderflow()
{
    /* guard: cannot re-pin, skip rebuild */
    if (!Pin()) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: re-pin fail", __func__);
        return false;
    }
    /* guard: content present, no rebuild needed */
    if (!IfNeedRebuild()) {
        return true;
    }
    /* guard: rebuild failed */
    if (!BuildContent()) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: rebuild fail", __func__);
        return false;
    }
    /* rebuild succeeded: clear the kernel purged flag */
    AfterRebuildSucc();
    return true;
}

/*
 * HandleUnpinUnderflow: the pin count underflowed (was already 0).
 * Best-effort recovery, then permanently disable the object.
 * DisablePurgeableKernel is called only when the content is successfully
 * protected — the kernel rejects the disable ioctl on non-intact content.
 */
void PurgeableDmaBuf::HandleUnpinUnderflow()
{
    PM_HILOG_ERROR(LOG_CORE, "%{public}s: Unpin underflow, fd:%{public}d",
        __func__, dmaBufFd_);
    bool rebuilt = TryRebuildAfterUnderflow();
    disabled_ = true;
    isSupport_ = false;
    if (rebuilt) {
        (void)DisablePurgeableKernel();
    }
}

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
        (void)DisablePurgeableKernel();
        return false;
    }
    if (ret > 0) {
        HandleUnpinUnderflow();
        return false;
    }
    return true;
}

/*
 * IsPurged: whether the content was purged, reported by the snapshot of
 * the last PIN ioctl return — see the pin-window contract in the header
 * (purgeable_dmabuf.h). The IS_PURGED ioctl was removed.
 */
bool PurgeableDmaBuf::IsPurged()
{
    if (!isSupport_) {
        return false;
    }
    return wasPurged_;
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
    int ret = TEMP_FAILURE_RETRY(ioctl(dmaBufFd_, DMA_BUF_PURGEABLE_REBUILD_SUCCESS));
    if (ret < 0) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: ioctl fail, fd:%{public}d ino:%{public}lu ret:%{public}d",
            __func__, dmaBufFd_, ino_, ret);
        return;
    }
    /* rebuild accepted: clear the purged snapshot */
    wasPurged_ = false;
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
    std::lock_guard<std::mutex> lock(dataLock_);
    return CreatePurgeableDataUnlocked(size, heapName);
}

/*
 * ValidateCreatePurgeableData: parameter and state validation for
 * CreatePurgeableDataUnlocked. Return: true if all parameters and state
 * are valid.
 */
bool PurgeableDmaBuf::ValidateCreatePurgeableData(size_t size, const char *heapName)
{
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
    /* reject rebind: the object already holds an external buffer */
    if (isChange_) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: already in change state, rebind forbidden", __func__);
        return false;
    }
    return true;
}

/*
 * BuildAndMarkPurgeable: build the content by builder BEFORE marking the
 * buffer purgeable, so the kernel SET records its baseline checksum over
 * the real content. The SET return (dmabuf inode) is cached in ino_.
 * Return: true on success; on failure the caller must release the buffer
 * and roll back the member state.
 * The caller must hold dataLock_.
 */
bool PurgeableDmaBuf::BuildAndMarkPurgeable()
{
    if (builder_ != nullptr && !BuildContent()) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: build content fail, release buffer", __func__);
        return false;
    }
    /* mark as purgeable; the kernel does not support it if this fails */
    isSupport_ = MarkDmaBufPurgeable(dmaBufFd_, &ino_);
    if (!isSupport_) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: mark purgeable fail, release buffer", __func__);
        return false;
    }
    return true;
}

bool PurgeableDmaBuf::CreatePurgeableDataUnlocked(size_t size, const char *heapName)
{
    PM_HILOG_DEBUG(LOG_CORE, "%{public}s", __func__);
    if (!ValidateCreatePurgeableData(size, heapName)) {
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

    dataSizeInput_ = size;
    dmaBufFd_ = static_cast<int>(buffer.fd);
    pin_ = { static_cast<uint32_t>(0), static_cast<uint32_t>(0) };
    dataPtr_ = data;
    isChange_ = false;
    /* build the content and mark purgeable; roll back the state on failure */
    if (!BuildAndMarkPurgeable()) {
        DmabufHeapBufferFree(&buffer);
        munmap(data, size);
        dataPtr_ = nullptr;
        dmaBufFd_ = -1;
        dataSizeInput_ = 0;
        return false;
    }
    /* content built: unpin so the kernel can reclaim it (refcount 0) */
    Unpin();
    return true;
}

bool PurgeableDmaBuf::ChangeDmaBufData(size_t size, int fd, void *data)
{
    std::lock_guard<std::mutex> lock(dataLock_);
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
    /* reject rebind: the object already holds an external buffer */
    if (isChange_) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: already in change state, rebind forbidden", __func__);
        return false;
    }
    /* same fd: no-op, avoid re-marking and resetting the kernel pin count */
    if (fd == dmaBufFd_) {
        PM_HILOG_DEBUG(LOG_CORE, "%{public}s: same fd, skip", __func__);
        return true;
    }

    /*
     * Probe the new fd before committing: mark it as purgeable first,
     * so a failure leaves the object and the caller's buffer untouched.
     * The SET return (dmabuf inode) is committed only on success.
     */
    unsigned long newIno = 0;
    if (!MarkDmaBufPurgeable(fd, &newIno)) {
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
    /* ino returned by the SET_PURGEABLE probe, stable for the fd lifetime */
    ino_ = newIno;

    PM_HILOG_INFO(LOG_CORE, "%{public}s: succ, fd:%{public}d ino:%{public}lu size:%{public}zu",
                  __func__, dmaBufFd_, ino_, dataSizeInput_);
    return true;
}

int PurgeableDmaBuf::DisablePurgeableKernel()
{
    if (dmaBufFd_ < 0) {
        return -1;
    }
    return PurgDmaBufDisablePurgeable(dmaBufFd_);
}

/*
 * AlignPinCount: pin the new fd until its kernel pin count matches
 * @targetCount. The new buffer starts at count 1 after SET_PURGEABLE.
 * Return: true if aligned, false on ioctl failure.
 */
bool PurgeableDmaBuf::AlignPinCount(int newFd, int targetCount)
{
    for (int i = 1; i < targetCount; i++) {
        struct DmaBufPurgeablePin pin = {0, 0};
        if (TEMP_FAILURE_RETRY(ioctl(newFd, DMA_BUF_PURGEABLE_PIN, &pin)) < 0) {
            return false;
        }
    }
    return true;
}

/*
 * ValidateReplacePurgeableMem: parameter and state validation for ReplacePurgeableMem.
 * Return: true if all parameters and state are valid.
 */
bool PurgeableDmaBuf::ValidateReplacePurgeableMem(size_t size, int newFd, void *newData,
    const std::unique_ptr<PurgeableMemBuilder> &newBuilder)
{
    if (size == 0 || size >= OHOS_MAXIMUM_PURGEABLE_MEMORY) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: invalid size %{public}zu", __func__, size);
        return false;
    }
    if (newFd < 0) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: invalid new fd", __func__);
        return false;
    }
    if (newData == nullptr) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: new data is nullptr", __func__);
        return false;
    }
    if (newBuilder == nullptr) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: new builder is nullptr", __func__);
        return false;
    }
    if (disabled_) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: object disabled", __func__);
        return false;
    }
    /* self-allocated buffers must use ResizeData (otherwise fd/mmap leak) */
    if (!isChange_) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: self-allocated, use ResizeData", __func__);
        return false;
    }
    /* old buffer must be pinned (caller is inside a Begin access section) */
    if (GetPinStatus() < 1) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: old buffer not pinned", __func__);
        return false;
    }
    /* content never built: use ChangeDmaBufData for the empty-takeover path */
    if (buildDataCount_ == 0) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: content never built, use ChangeDmaBufData", __func__);
        return false;
    }
    return true;
}

/*
 * ReplacePurgeableMem: atomically swap the underlying DMA buffer.
 * The caller must be inside a Begin access section (old buffer pinned >= 1).
 * The new buffer must have its content already built by the caller.
 */
bool PurgeableDmaBuf::ReplacePurgeableMem(size_t size, int newFd, void *newData,
    std::unique_ptr<PurgeableMemBuilder> newBuilder)
{
    std::lock_guard<std::mutex> lock(dataLock_);
    int oldPinCount = GetPinStatus();
    if (!ValidateReplacePurgeableMem(size, newFd, newData, newBuilder)) {
        return false;
    }

    /* probe the new fd: mark as purgeable (kernel sets count to 1) */
    unsigned long newIno = 0;
    if (!MarkDmaBufPurgeable(newFd, &newIno)) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: mark new fd fail, fd:%{public}d", __func__, newFd);
        return false;
    }

    /* align the new fd's pin count to match the old */
    if (!AlignPinCount(newFd, oldPinCount)) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: align pin count fail, best-effort disable", __func__);
        (void)PurgDmaBufDisablePurgeable(newFd);
        return false;
    }

    /*
     * Commit: swap all internal state. The old fd is NOT closed — the
     * caller retains ownership. buildDataCount_ is NOT reset — the new
     * buffer's content is already built by the caller.
     */
    dmaBufFd_ = newFd;
    dataPtr_ = newData;
    dataSizeInput_ = size;
    isChange_ = true;
    isSupport_ = true;
    pin_ = { static_cast<uint32_t>(0), static_cast<uint32_t>(0) };
    builder_ = std::move(newBuilder);
    /* ino returned by the SET_PURGEABLE probe, stable for the fd lifetime */
    ino_ = newIno;

    PM_HILOG_INFO(LOG_CORE, "%{public}s: succ, oldPin:%{public}d fd:%{public}d ino:%{public}lu size:%{public}zu",
        __func__, oldPinCount, dmaBufFd_, ino_, dataSizeInput_);
    return true;
}

void PurgeableDmaBuf::ResizeData(size_t newSize)
{
    std::lock_guard<std::mutex> lock(dataLock_);
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
    if (!CreatePurgeableDataUnlocked(newSize, "system")) {
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

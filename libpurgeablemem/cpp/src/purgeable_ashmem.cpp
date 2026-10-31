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

#include <sys/mman.h> /* mmap */

#include "securec.h"
#include "pm_util.h"
#include "pm_state_c.h"
#include "pm_smartptr_util.h"
#include "pm_log.h"

#include "purgeable_ashmem.h"

namespace OHOS {
namespace PurgeableMem {
#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "PurgeableMem"

static inline size_t RoundUp(size_t val, size_t align)
{
    if (val + align < val || val + align < align) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: Addition overflow!", __func__);
        return val;
    }
    if (align == 0) {
        return val;
    }
    return ((val + align - 1) / align) * align;
}

PurgeableAshMem::PurgeableAshMem(std::unique_ptr<PurgeableMemBuilder> builder)
{
    dataPtr_ = nullptr;
    builder_ = nullptr;
    ashmemFd_ = -1;
    buildDataCount_ = 0;
    isSupport_ = false;
    isChange_ = false;
    IF_NULL_LOG_ACTION(builder, "%{public}s: input builder nullptr", return);
    builder_ = std::move(builder);
    PM_HILOG_DEBUG(LOG_CORE, "%{public}s init succ. %{public}s", __func__, ToString().c_str());
}

PurgeableAshMem::PurgeableAshMem(size_t dataSize, std::unique_ptr<PurgeableMemBuilder> builder)
{
    dataPtr_ = nullptr;
    builder_ = nullptr;
    ashmemFd_ = -1;
    buildDataCount_ = 0;
    isSupport_ = false;
    isChange_ = false;
    if (dataSize == 0) {
        return;
    }
    dataSizeInput_ = dataSize;
    IF_NULL_LOG_ACTION(builder, "%{public}s: input builder nullptr", return);

    if (!CreatePurgeableData()) {
        PM_HILOG_DEBUG(LOG_CORE, "Failed to create purgeabledata");
        return;
    }
    builder_ = std::move(builder);
    PM_HILOG_DEBUG(LOG_CORE, "%{public}s init succ. %{public}s", __func__, ToString().c_str());
}

PurgeableAshMem::~PurgeableAshMem()
{
    PM_HILOG_DEBUG(LOG_CORE, "%{public}s %{public}s", __func__, ToString().c_str());
    if (!isChange_ && dataPtr_) {
        if (munmap(dataPtr_, RoundUp(dataSizeInput_, PAGE_SIZE)) != 0) {
            PM_HILOG_ERROR(LOG_CORE, "%{public}s: munmap dataPtr fail", __func__);
        } else {
            if (UxpteIsEnabled() && !IsPurged()) {
                PM_HILOG_ERROR(LOG_CORE, "%{public}s: munmap dataPtr succ, but uxpte present", __func__);
            }
            dataPtr_ = nullptr;
            close(ashmemFd_);
        }
    }
    builder_.reset();
}

int PurgeableAshMem::GetAshmemFd()
{
    return ashmemFd_;
}

bool PurgeableAshMem::IsPurged()
{
    if (!isSupport_) {
        return false;
    }
    int ret = ioctl(ashmemFd_, PURGEABLE_ASHMEM_IS_PURGED);
    PM_HILOG_DEBUG(LOG_CORE, "%{public}s: IsPurged %{public}d", __func__, ret);
    return ret > 0 ? true : false;
}

/*
 * AllocAndMapAshmem: create an ashmem region of @size and mmap it.
 * The region is released on failure.
 */
static bool AllocAndMapAshmem(size_t size, int *fd, void **data)
{
    *fd = AshmemCreate("PurgeableAshmem", size);
    if (*fd < 0) {
        return false;
    }
    if (AshmemSetProt(*fd, PROT_READ | PROT_WRITE) < 0) {
        close(*fd);
        *fd = -1;
        return false;
    }
    *data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, *fd, 0);
    if (*data == MAP_FAILED) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: mmap fail", __func__);
        close(*fd);
        *fd = -1;
        return false;
    }
    return true;
}

bool PurgeableAshMem::CreatePurgeableData()
{
    std::lock_guard<std::mutex> lock(dataLock_);
    return CreatePurgeableDataUnlocked();
}

bool PurgeableAshMem::CreatePurgeableDataUnlocked()
{
    PM_HILOG_DEBUG(LOG_CORE, "%{public}s", __func__);
    if (dataSizeInput_ == 0) {
        return false;
    }
    /* reject if permanently disabled */
    if (disabled_) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: object disabled", __func__);
        return false;
    }
    /* reject rebind: the object already holds an external region */
    if (isChange_) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: already in change state, rebind forbidden", __func__);
        return false;
    }
    /* release the previous self-allocated ashmem region, if any */
    if (!isChange_ && dataPtr_) {
        if (munmap(dataPtr_, RoundUp(dataSizeInput_, PAGE_SIZE)) != 0) {
            PM_HILOG_ERROR(LOG_CORE, "%{public}s: munmap dataPtr fail", __func__);
        }
        dataPtr_ = nullptr;
        if (ashmemFd_ > 0) {
            close(ashmemFd_);
            ashmemFd_ = -1;
        }
    }
    size_t size = RoundUp(dataSizeInput_, PAGE_SIZE);
    void *data = nullptr;
    if (!AllocAndMapAshmem(size, &ashmemFd_, &data)) {
        return false;
    }
    dataPtr_ = data;
    pin_ = { static_cast<uint32_t>(0), static_cast<uint32_t>(0) };
    isChange_ = false;
    /* new empty region, the first access rebuilds it by builder */
    buildDataCount_ = 0;
    /* mark as purgeable; release the region if the kernel does not support it */
    TEMP_FAILURE_RETRY(ioctl(ashmemFd_, ASHMEM_SET_PURGEABLE));
    isSupport_ = (TEMP_FAILURE_RETRY(ioctl(ashmemFd_, ASHMEM_GET_PURGEABLE)) == 1);
    if (!isSupport_) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: mark purgeable fail, release region", __func__);
        munmap(dataPtr_, size);
        dataPtr_ = nullptr;
        close(ashmemFd_);
        ashmemFd_ = -1;
        return false;
    }
    Unpin();
    return true;
}

/*
 * Pin: protect the content from being reclaimed by the kernel.
 * Return:  true if the content is protected — either the kernel reference
 *          count was incremented, or the buffer is not purgeable and thus
 *          inherently protected from reclamation;
 *          false if there is no valid buffer (fd guard).
 * Note: on ioctl failure the object is disabled as a safety fallback,
 * and Pin still returns true — the content is inherently protected.
 * The rebuild decision is made separately by IsPurged().
 */
bool PurgeableAshMem::Pin()
{
    if (ashmemFd_ <= 0) {
        PM_HILOG_DEBUG(LOG_CORE, "ashmemFd_ not exist!!");
        return false;
    }
    if (!isSupport_) {
        return true;
    }
    int ret = TEMP_FAILURE_RETRY(ioctl(ashmemFd_, ASHMEM_PIN, &pin_));
    if (ret < 0) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: Pin ioctl fail (disable applied), fd:%{public}d ret:%{public}d",
            __func__, ashmemFd_, ret);
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
bool PurgeableAshMem::Unpin()
{
    if (ashmemFd_ <= 0) {
        PM_HILOG_DEBUG(LOG_CORE, "ashmemFd_ not exist!!");
        return false;
    }
    if (!isSupport_) {
        return false;
    }
    int ret = TEMP_FAILURE_RETRY(ioctl(ashmemFd_, ASHMEM_UNPIN, &pin_));
    if (ret < 0) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: Unpin fail, fd:%{public}d ret:%{public}d",
            __func__, ashmemFd_, ret);
        disabled_ = true;
        isSupport_ = false;
        return false;
    }
    if (ret > 0) {
        /*
         * Underflow: pin count was 0, content may have been purged.
         * Pin and rebuild to protect the content before disabling.
         */
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: Unpin underflow, fd:%{public}d",
            __func__, ashmemFd_);
        if (Pin() && IfNeedRebuild()) {
            BuildContent();
        }
        disabled_ = true;
        isSupport_ = false;
        return false;
    }
    return true;
}

int PurgeableAshMem::GetPinStatus() const
{
    if (!isSupport_) {
        return -1;
    }
    return ioctl(ashmemFd_, ASHMEM_GET_PIN_STATUS, &pin_);
}

void PurgeableAshMem::AfterRebuildSucc()
{
    TEMP_FAILURE_RETRY(ioctl(ashmemFd_, PURGEABLE_ASHMEM_REBUILD_SUCCESS));
}

void PurgeableAshMem::ResizeData(size_t newSize)
{
    std::lock_guard<std::mutex> lock(dataLock_);
    if (newSize == 0 || newSize >= OHOS_MAXIMUM_PURGEABLE_MEMORY) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: invalid size %{public}zu", __func__, newSize);
        return;
    }
    if (isChange_) {
        /* the taken-over ashmem region is owned by caller, cannot resize it */
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: external buffer cannot be resized", __func__);
        return;
    }
    if (disabled_) {
        /* disabled objects cannot get a new purgeable buffer */
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: object disabled", __func__);
        return;
    }
    if (dataPtr_) {
        if (munmap(dataPtr_, RoundUp(dataSizeInput_, PAGE_SIZE)) != 0) {
            PM_HILOG_ERROR(LOG_CORE, "%{public}s: munmap dataPtr fail", __func__);
        }
        dataPtr_ = nullptr;
    }
    if (ashmemFd_ > 0) {
        close(ashmemFd_);
        ashmemFd_ = -1;
    }
    dataSizeInput_ = newSize;
    if (!CreatePurgeableDataUnlocked()) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: resize to %{public}zu fail", __func__, newSize);
    }
}

bool PurgeableAshMem::ChangeAshmemData(size_t size, int fd, void *data)
{
    std::lock_guard<std::mutex> lock(dataLock_);
    if (size <= 0 || size >= OHOS_MAXIMUM_PURGEABLE_MEMORY) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: invalid size %{public}zu", __func__, size);
        return false;
    }
    if (fd < 0) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: invalid fd", __func__);
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
    /* reject rebind: the object already holds an external region */
    if (isChange_) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: already in change state, rebind forbidden", __func__);
        return false;
    }
    /* same fd: no-op, avoid re-marking and resetting the kernel pin count */
    if (fd == ashmemFd_) {
        PM_HILOG_DEBUG(LOG_CORE, "%{public}s: same fd, skip", __func__);
        return true;
    }

    /* probe the new fd before committing: mark it as purgeable first */
    TEMP_FAILURE_RETRY(ioctl(fd, ASHMEM_SET_PURGEABLE));
    if (TEMP_FAILURE_RETRY(ioctl(fd, ASHMEM_GET_PURGEABLE)) != 1) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: mark purgeable fail, fd:%{public}d", __func__, fd);
        return false;
    }

    /* commit: release the previous self-allocated region, then take over */
    if (!isChange_ && dataPtr_) {
        if (munmap(dataPtr_, RoundUp(dataSizeInput_, PAGE_SIZE)) != 0) {
            PM_HILOG_ERROR(LOG_CORE, "%{public}s: munmap dataPtr fail", __func__);
        }
        dataPtr_ = nullptr;
        if (ashmemFd_ > 0) {
            close(ashmemFd_);
            ashmemFd_ = -1;
        }
    }
    dataSizeInput_ = size;
    ashmemFd_ = fd;
    dataPtr_ = data;
    buildDataCount_++;
    isChange_ = true;
    isSupport_ = true;
    return true;
}

/*
 * ValidateReplacePurgeableMem: parameter and state validation for ReplacePurgeableMem.
 * Return: true if all parameters and state are valid.
 */
bool PurgeableAshMem::ValidateReplacePurgeableMem(size_t size, int newFd, void *newData,
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
    /* self-allocated regions must use ResizeData (otherwise fd/mmap leak) */
    if (!isChange_) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: self-allocated, use ResizeData", __func__);
        return false;
    }
    /* old region must be pinned (caller is inside a Begin access section) */
    if (GetPinStatus() < 1) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: old region not pinned", __func__);
        return false;
    }
    /* content never built: use ChangeAshmemData for the empty-takeover path */
    if (buildDataCount_ == 0) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: content never built, use ChangeAshmemData", __func__);
        return false;
    }
    return true;
}

/*
 * AlignPinCount: pin the new fd until its kernel pin count matches
 * @targetCount. The new region starts at count 1 after SET_PURGEABLE.
 * Return: true if aligned, false on ioctl failure.
 */
bool PurgeableAshMem::AlignPinCount(int newFd, int targetCount)
{
    for (int i = 1; i < targetCount; i++) {
        struct ashmem_pin pin = { static_cast<uint32_t>(0), static_cast<uint32_t>(0) };
        if (TEMP_FAILURE_RETRY(ioctl(newFd, ASHMEM_PIN, &pin)) < 0) {
            return false;
        }
    }
    return true;
}

/*
 * ReplacePurgeableMem: atomically swap the underlying ashmem region.
 * The caller must be inside a Begin access section (old region pinned >= 1).
 * The new region must have its content already built by the caller.
 */
bool PurgeableAshMem::ReplacePurgeableMem(size_t size, int newFd, void *newData,
    std::unique_ptr<PurgeableMemBuilder> newBuilder)
{
    std::lock_guard<std::mutex> lock(dataLock_);
    int oldPinCount = GetPinStatus();
    if (!ValidateReplacePurgeableMem(size, newFd, newData, newBuilder)) {
        return false;
    }

    /* probe the new fd: mark as purgeable (kernel sets count to 1) */
    TEMP_FAILURE_RETRY(ioctl(newFd, ASHMEM_SET_PURGEABLE));
    if (TEMP_FAILURE_RETRY(ioctl(newFd, ASHMEM_GET_PURGEABLE)) != 1) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: mark new fd fail, fd:%{public}d", __func__, newFd);
        return false;
    }

    /* align the new fd's pin count to match the old. ashmem has no kernel
     * disable ioctl, so on failure the new region stays at the default
     * count — same state a failed ChangeAshmemData probe leaves behind */
    if (!AlignPinCount(newFd, oldPinCount)) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: align pin count fail", __func__);
        return false;
    }

    /*
     * Commit: swap all internal state. The old fd is NOT closed — the
     * caller retains ownership. buildDataCount_ is NOT reset — the new
     * region's content is already built by the caller.
     */
    ashmemFd_ = newFd;
    dataPtr_ = newData;
    dataSizeInput_ = size;
    isChange_ = true;
    isSupport_ = true;
    pin_ = { static_cast<uint32_t>(0), static_cast<uint32_t>(0) };
    builder_ = std::move(newBuilder);

    PM_HILOG_INFO(LOG_CORE, "%{public}s: succ, oldPin:%{public}d fd:%{public}d size:%{public}zu",
        __func__, oldPinCount, ashmemFd_, dataSizeInput_);
    return true;
}

std::string PurgeableAshMem::ToString() const
{
    return PurgeableMemBase::ToString() +
           " isSupport:" + std::to_string(isSupport_) +
           " isChange:" + std::to_string(isChange_);
}

PurgMemType PurgeableAshMem::GetPurgMemType() const
{
    return PURG_MEM_TYPE_ASHMEM;
}

int PurgeableAshMem::GetFd() const
{
    return ashmemFd_;
}
} /* namespace PurgeableMem */
} /* namespace OHOS */

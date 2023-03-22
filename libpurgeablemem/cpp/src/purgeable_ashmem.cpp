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
#include "purgeable_ashmem.h"
#include "pm_log.h"

namespace OHOS {
namespace PurgeableMem {
#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "PurgeableMem"

static inline size_t RoundUp_(size_t val, size_t align)
{
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
    IF_NULL_LOG_ACTION(builder, "%{public}s: input builder nullptr", return);
    builder_ = std::move(builder);
    PM_HILOG_DEBUG(LOG_CORE, "%{public}s init succ. %{public}s", __func__, ToString_().c_str());
}

PurgeableAshMem::PurgeableAshMem(size_t dataSize, std::unique_ptr<PurgeableMemBuilder> builder)
{
    dataPtr_ = nullptr;
    builder_ = nullptr;
    ashmemFd_ = -1;
    buildDataCount_ = 0;
    isSupport_ = false;
    if (dataSize == 0) {
        return;
    }
    dataSizeInput_ = dataSize;
    IF_NULL_LOG_ACTION(builder, "%{public}s: input builder nullptr", return);

    CreatePurgeableData_();
    builder_ = std::move(builder);
    PM_HILOG_DEBUG(LOG_CORE, "%{public}s init succ. %{public}s", __func__, ToString_().c_str());
}

PurgeableAshMem::~PurgeableAshMem()
{
    PM_HILOG_DEBUG(LOG_CORE, "%{public}s %{public}s", __func__, ToString_().c_str());
    if (dataPtr_) {
        if (munmap(dataPtr_, RoundUp_(dataSizeInput_, PAGE_SIZE)) != 0) {
            PM_HILOG_ERROR(LOG_CORE, "%{public}s: munmap dataPtr fail", __func__);
        } else {
            if (UxpteIsEnabled() && !IsPurged_()) {
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

bool PurgeableAshMem::IsPurged_()
{
    if (!isSupport_) {
        return false;
    }
    int ret = ioctl(ashmemFd_, PURGEABLE_ASHMEM_IS_PURGED);
    PM_HILOG_DEBUG(LOG_CORE, "%{public}s: IsPurged_ %{public}d", __func__, ret);
    return ret ? true : false;
}

bool PurgeableAshMem::CreatePurgeableData_()
{
    PM_HILOG_DEBUG(LOG_CORE, "%{public}s", __func__);
    if (dataSizeInput_ == 0) {
        return false;
    }
    size_t size = RoundUp_(dataSizeInput_, PAGE_SIZE);
    int fd = AshmemCreate("PurgeableAshmem", size);
    if (fd < 0) {
        return false;
    }
    if (AshmemSetProt(fd, PROT_READ | PROT_WRITE) < 0) {
        close(fd);
        return false;
    }
    ashmemFd_ = fd;
    pin_ = { static_cast<uint32_t>(0), static_cast<uint32_t>(0) };
    dataPtr_ = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, ashmemFd_, 0);
    if (dataPtr_ == MAP_FAILED) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: mmap fail", __func__);
        dataPtr_ = nullptr;
        close(ashmemFd_);
        return false;
    }
    TEMP_FAILURE_RETRY(ioctl(ashmemFd_, ASHMEM_SET_PURGEABLE));
    if (TEMP_FAILURE_RETRY(ioctl(ashmemFd_, ASHMEM_GET_PURGEABLE)) == 1) {
        isSupport_ = true;
    }
    Unpin_();
    return true;
}

bool PurgeableAshMem::Pin_()
{
    if (!isSupport_) {
        return true;
    }
    if (ashmemFd_ > 0) {
        TEMP_FAILURE_RETRY(ioctl(ashmemFd_, ASHMEM_PIN, &pin_));
        PM_HILOG_DEBUG(LOG_CORE, "%{public}s: fd:%{pubilc}d PURGEABLE_GET_PIN_STATE: %{public}d",
                       __func__, ashmemFd_, ioctl(ashmemFd_, ASHMEM_GET_PIN_STATUS, &pin_));
    } else {
        PM_HILOG_DEBUG(LOG_CORE, "ashmemFd_ not exist!!");
        return false;
    }
    return true;
}

bool PurgeableAshMem::Unpin_()
{
    if (!isSupport_) {
        return true;
    }
    if (ashmemFd_ > 0) {
        TEMP_FAILURE_RETRY(ioctl(ashmemFd_, ASHMEM_UNPIN, &pin_));
        PM_HILOG_DEBUG(LOG_CORE, "%{public}s: fd:%{pubilc}d PURGEABLE_GET_PIN_STATE: %{public}d",
                       __func__, ashmemFd_, ioctl(ashmemFd_, ASHMEM_GET_PIN_STATUS, &pin_));
    } else {
        PM_HILOG_DEBUG(LOG_CORE, "ashmemFd_ not exist!!");
        return false;
    }
    return true;
}

void PurgeableAshMem::AfterRebuildSucc_()
{
    TEMP_FAILURE_RETRY(ioctl(ashmemFd_, PURGEABLE_ASHMEM_REBUILD_SUCCESS));
}

void PurgeableAshMem::ResizeData(size_t newSize)
{
    if (newSize <= 0) {
        return;
    }
    if (dataPtr_) {
        if (munmap(dataPtr_, RoundUp_(dataSizeInput_, PAGE_SIZE)) != 0) {
            PM_HILOG_ERROR(LOG_CORE, "%{public}s: munmap dataPtr fail", __func__);
        } else {
            dataPtr_ = nullptr;
            if (ashmemFd_ > 0) {
                close(ashmemFd_);
            }
        }
    }
    dataSizeInput_ = newSize;
    CreatePurgeableData_();
}

void PurgeableAshMem::ChangeAshmemData(size_t size, int fd, void *data)
{
    if (dataPtr_) {
        if (munmap(dataPtr_, RoundUp_(dataSizeInput_, PAGE_SIZE)) != 0) {
            PM_HILOG_ERROR(LOG_CORE, "%{public}s: munmap dataPtr fail", __func__);
        } else {
            dataPtr_ = nullptr;
            if (ashmemFd_ > 0) {
                close(ashmemFd_);
            }
        }
    }
    dataSizeInput_ = size;
    ashmemFd_ = fd;
    dataPtr_ = data;
    buildDataCount_++;
    TEMP_FAILURE_RETRY(ioctl(ashmemFd_, ASHMEM_SET_PURGEABLE));
    if (TEMP_FAILURE_RETRY(ioctl(ashmemFd_, ASHMEM_GET_PURGEABLE)) == 1) {
        isSupport_ = true;
    }
    Unpin_();
}

inline std::string PurgeableAshMem::ToString_() const
{
    return "";
}
} /* namespace PurgeableMem */
} /* namespace OHOS */

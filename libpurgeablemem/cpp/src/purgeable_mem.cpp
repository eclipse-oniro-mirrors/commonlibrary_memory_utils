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

#include <sys/mman.h> /* mmap */

#include "securec.h"
#include "pm_util.h"
#include "pm_state_c.h"
#include "pm_smartptr_util.h"
#include "purgeable_mem.h"
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

PurgeableMem::PurgeableMem(size_t dataSize, std::unique_ptr<PurgeableMemBuilder> builder)
{
    dataPtr_ = nullptr;
    builder_ = nullptr;
    pageTable_ = nullptr;
    buildDataCount_ = 0;

    if (dataSize == 0) {
        return;
    }
    dataSizeInput_ = dataSize;
    IF_NULL_LOG_ACTION(builder, "%{public}s: input builder nullptr", return);

    CreatePurgeableData_();
    builder_ = std::move(builder);
    PM_HILOG_DEBUG(LOG_CORE, "%{public}s init succ. %{public}s", __func__, ToString_().c_str());
}

PurgeableMem::~PurgeableMem()
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
        }
    }
    builder_.reset();
    pageTable_.reset();
}

bool PurgeableMem::IsPurged_()
{
    IF_NULL_LOG_ACTION(pageTable_, "pageTable_ is nullptrin BeginWrite", return false);
    return !(pageTable_->CheckPresent((uint64_t)dataPtr_, dataSizeInput_));
}

bool PurgeableMem::CreatePurgeableData_()
{
    PM_HILOG_DEBUG(LOG_CORE, "%{public}s", __func__);
    pageTable_ = nullptr;
    size_t size = RoundUp_(dataSizeInput_, PAGE_SIZE);
    int type = MAP_ANONYMOUS;
    type |= (UxpteIsEnabled() ? MAP_PURGEABLE : MAP_PRIVATE);

    dataPtr_ = mmap(nullptr, size, PROT_READ | PROT_WRITE, type, -1, 0);
    if (dataPtr_ == MAP_FAILED) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: mmap fail", __func__);
        dataPtr_ = nullptr;
        return false;
    }
    MAKE_UNIQUE(pageTable_, UxPageTable, "constructor uxpt make_unique fail", return false, (uint64_t)dataPtr_, size);
    return true;
}

bool PurgeableMem::Pin_()
{
    IF_NULL_LOG_ACTION(pageTable_, "pageTable_ is nullptrin BeginWrite", return false);
    pageTable_->GetUxpte((uint64_t)dataPtr_, dataSizeInput_);
    return true;
}

bool PurgeableMem::Unpin_()
{
    IF_NULL_LOG_ACTION(pageTable_, "pageTable_ is nullptrin BeginWrite", return false);
    pageTable_->PutUxpte((uint64_t)dataPtr_, dataSizeInput_);
    return true;
}

void PurgeableMem::AfterRebuildSucc_()
{
}

void PurgeableMem::ResizeData(size_t newSize)
{
    if (newSize <= 0) {
        return;
    }
    if (dataPtr_) {
        if (munmap(dataPtr_, RoundUp_(dataSizeInput_, PAGE_SIZE)) != 0) {
            PM_HILOG_ERROR(LOG_CORE, "%{public}s: munmap dataPtr fail", __func__);
        } else {
            dataPtr_ = nullptr;
        }
    }
    dataSizeInput_ = newSize;
    CreatePurgeableData_();
}

inline std::string PurgeableMem::ToString_() const
{
    std::string dataptrStr = dataPtr_ ? std::to_string((unsigned long long)dataPtr_) : "0";
    std::string pageTableStr = pageTable_ ? pageTable_->ToString() : "0";
    return "dataAddr:" + dataptrStr + " dataSizeInput:" + std::to_string(dataSizeInput_) +
        " " + pageTableStr;
}
} /* namespace PurgeableMem */
} /* namespace OHOS */

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

#include "purgeable_mem_base.h"

namespace OHOS {
namespace PurgeableMem {
#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "PurgeableMem"
const int MAX_BUILD_TRYTIMES = 3;

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

PurgeableMemBase::PurgeableMemBase()
{
}

PurgeableMemBase::~PurgeableMemBase()
{
}

bool PurgeableMemBase::BeginRead()
{
    std::lock_guard<std::mutex> lock(dataLock_);
    if (!isDataValid_) {
        return false;
    }

    bool ret = false;
    int tryTimes = 0;

    PM_HILOG_DEBUG(LOG_CORE, "%{public}s %{public}s", __func__, ToString().c_str());
    IF_NULL_LOG_ACTION(dataPtr_, "dataPtr is nullptr in BeginRead", return false);
    IF_NULL_LOG_ACTION(builder_, "builder_ is nullptr in BeginRead", return false);
    Pin();
    PMState err = PM_OK;
    while (true) {
        if (!IfNeedRebuild()) {
            PM_HILOG_DEBUG(LOG_CORE, "%{public}s: not purged, return true. MAP_PUR=0x%{public}x",
                __func__, MAP_PURGEABLE);
            ret = true;
            break;
        }

        bool succ = BuildContent();
        if (succ) {
            AfterRebuildSucc();
        }
        PM_HILOG_DEBUG(LOG_CORE, "%{public}s: purged, built %{public}s", __func__, succ ? "succ" : "fail");

        tryTimes++;
        if (!succ || tryTimes > MAX_BUILD_TRYTIMES) {
            err = PMB_BUILD_ALL_FAIL;
            break;
        }
    }

    if (!ret) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: err %{public}s, UxptePut. tryTime:%{public}d",
            __func__, GetPMStateName(err), tryTimes);
        Unpin();
    }
    return ret;
}

void PurgeableMemBase::EndRead()
{
    std::lock_guard<std::mutex> lock(dataLock_);
    if (isDataValid_) {
        Unpin();
    }

    return;
}

bool PurgeableMemBase::BeginWrite()
{
    PM_HILOG_DEBUG(LOG_CORE, "%{public}s %{public}s", __func__, ToString().c_str());
    std::lock_guard<std::mutex> lock(dataLock_);
    if (dataPtr_ == nullptr) {
        return false;
    }
    IF_NULL_LOG_ACTION(dataPtr_, "dataPtr is nullptr in BeginWrite", return false);
    IF_NULL_LOG_ACTION(builder_, "builder_ is nullptr in BeginWrite", return false);

    Pin();
    PMState err = PM_OK;
    do {
        if (!IfNeedRebuild()) {
            /* data is not purged, return true */
            break;
        }
        /* data purged, rebuild it */
        if (BuildContent()) {
            /* data rebuild succ, return true */
            AfterRebuildSucc();
            break;
        }
        err = PMB_BUILD_ALL_FAIL;
    } while (0);

    if (err == PM_OK) {
        return true;
    }

    PM_HILOG_ERROR(LOG_CORE, "%{public}s: err %{public}s, UxptePut.", __func__, GetPMStateName(err));
    Unpin();
    return false;
}

void PurgeableMemBase::EndWrite()
{
    std::lock_guard<std::mutex> lock(dataLock_);
    PM_HILOG_DEBUG(LOG_CORE, "%{public}s %{public}s", __func__, ToString().c_str());
    Unpin();
}

bool PurgeableMemBase::ModifyContentByBuilder(std::unique_ptr<PurgeableMemBuilder> modifier)
{
    IF_NULL_LOG_ACTION(modifier, "input modifier is nullptr", return false);
    std::lock_guard<std::mutex> lock(dataLock_);
    if (!modifier->Build(dataPtr_, dataSizeInput_)) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s: modify content by builder fail!!", __func__);
        return false;
    }
    /* log modify */
    if (builder_) {
        builder_->AppendBuilder(std::move(modifier));
    } else {
        builder_ = std::move(modifier);
    }
    return true;
}

bool PurgeableMemBase::IfNeedRebuild()
{
    if (buildDataCount_ == 0 || IsPurged()) {
        return true;
    }
    return false;
}

void PurgeableMemBase::AfterRebuildSucc()
{
}

void *PurgeableMemBase::GetContent()
{
    std::lock_guard<std::mutex> lock(dataLock_);
    return dataPtr_;
}

size_t PurgeableMemBase::GetContentSize()
{
    std::lock_guard<std::mutex> lock(dataLock_);
    return dataSizeInput_;
}

bool PurgeableMemBase::IsPurged()
{
    return false;
}

bool PurgeableMemBase::BuildContent()
{
    bool succ = false;
    /* clear content before rebuild */
    if (memset_s(dataPtr_, RoundUp(dataSizeInput_, PAGE_SIZE), 0, dataSizeInput_) != EOK) {
        PM_HILOG_ERROR(LOG_CORE, "%{public}s, clear content fail", __func__);
        return succ;
    }
    /* builder_ and dataPtr_ is never nullptr since it is checked by BeginAccess() before */
    succ = builder_->BuildAll(dataPtr_, dataSizeInput_);
    if (succ) {
        buildDataCount_++;
    }
    return succ;
}

void PurgeableMemBase::ResizeData(size_t newSize)
{
}

bool PurgeableMemBase::Pin()
{
    return false;
}

bool PurgeableMemBase::Unpin()
{
    return false;
}

int PurgeableMemBase::GetPinStatus() const
{
    return 0;
}

inline std::string PurgeableMemBase::ToString() const
{
    return "";
}

void PurgeableMemBase::SetRebuildSuccessCallback(std::function<void()> &callback)
{
    std::lock_guard<std::mutex> lock(dataLock_);
    if (builder_) {
        builder_->SetRebuildSuccessCallback(callback);
    }
}

bool PurgeableMemBase::IsDataValid()
{
    std::lock_guard<std::mutex> lock(dataLock_);
    return isDataValid_;
}

void PurgeableMemBase::SetDataValid(bool target)
{
    std::lock_guard<std::mutex> lock(dataLock_);
    isDataValid_ = target;
}
} /* namespace PurgeableMem */
} /* namespace OHOS */

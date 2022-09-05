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
#include "../../common/include/pm_util.h"
#include "../../common/include/pm_state_c.h"
#include "pm_smartptr_util.h"
#include "purgeable_mem.h"

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

    size_t size = RoundUp_(dataSizeInput_, PAGE_SIZE);
    int type = MAP_ANONYMOUS;
    type |= (UxpteIsEnabled() ? MAP_PURGEABLE : MAP_PRIVATE);
    dataPtr_ = mmap(nullptr, size, PROT_READ | PROT_WRITE, type, -1, 0);
    if (dataPtr_ == MAP_FAILED) {
        HILOG_ERROR(LOG_CORE, "%{public}s: mmap fail", __func__);
        dataPtr_ = nullptr;
        return;
    }

    builder_ = std::move(builder);

    MAKE_UNIQUE(pageTable_, UxPageTable, "constructor uxpt make_unique fail", return, (uint64_t)dataPtr_, size);
    HILOG_DEBUG(LOG_CORE, "%{public}s init succ. %{public}s", __func__, ToString_().c_str());
}

PurgeableMem::~PurgeableMem()
{
    HILOG_DEBUG(LOG_CORE, "%{public}s %{public}s", __func__, ToString_().c_str());
    if (dataPtr_) {
        if (munmap(dataPtr_, RoundUp_(dataSizeInput_, PAGE_SIZE)) != 0) {
            HILOG_ERROR(LOG_CORE, "%{public}s: munmap dataPtr fail", __func__);
        } else {
            if (UxpteIsEnabled() && !IsPurged_()) {
                HILOG_ERROR(LOG_CORE, "%{public}s: munmap dataPtr succ, but uxpte present", __func__);
            }
            dataPtr_ = nullptr;
        }
    }
    builder_.reset();
    pageTable_.reset();
}

bool PurgeableMem::BeginRead()
{
    bool succ = false;
    bool ret = false;
    HILOG_DEBUG(LOG_CORE, "%{public}s %{public}s", __func__, ToString_().c_str());
    IF_NULL_LOG_ACTION(dataPtr_, "dataPtr is nullptr in BeginRead", return false);
    IF_NULL_LOG_ACTION(pageTable_, "pageTable_ is nullptr in BeginRead", return false);
    IF_NULL_LOG_ACTION(builder_, "builder_ is nullptr in BeginRead", return false);
    pageTable_->GetUxpte((uint64_t)dataPtr_, dataSizeInput_);
    PMState err = PM_OK;
    while (true) {
        try {
            rwlock_.lock_shared();
        } catch (...) {
            err = PM_LOCK_READ_FAIL;
            break;
        }
        if (!IsPurged_()) {
            HILOG_INFO(LOG_CORE, "%{public}s: not purged, return true. MAP_PUR=0x%{public}x",
                __func__, MAP_PURGEABLE);
            ret = true;
            break;
        }
        /* data is purged, will rebuild it */
        rwlock_.unlock_shared();
        try {
            rwlock_.lock();
        } catch (...) {
            err = PM_LOCK_WRITE_FAIL;
            break;
        }
        if (IsPurged_()) {
            succ = BuildContent_();
            HILOG_INFO(LOG_CORE, "%{public}s: purged, built %{public}s", __func__, succ ? "succ" : "fail");
        }
        rwlock_.unlock();
        if (!succ) {
            err = PMB_BUILD_ALL_FAIL;
            break;
        }
    }

    if (!ret) {
        HILOG_ERROR(LOG_CORE, "%{public}s: err %{public}s, UxptePut.", __func__, GetPMStateName(err));
        pageTable_->PutUxpte((uint64_t)dataPtr_, dataSizeInput_);
    }
    return ret;
}

void PurgeableMem::EndRead()
{
    HILOG_DEBUG(LOG_CORE, "%{public}s %{public}s", __func__, ToString_().c_str());
    IF_NULL_LOG_ACTION(pageTable_, "pageTable_ is nullptr in EndRead", return);
    rwlock_.unlock_shared();
    pageTable_->PutUxpte((uint64_t)dataPtr_, dataSizeInput_);
}

bool PurgeableMem::BeginWrite()
{
    HILOG_DEBUG(LOG_CORE, "%{public}s %{public}s", __func__, ToString_().c_str());

    IF_NULL_LOG_ACTION(dataPtr_, "dataPtr is nullptr in BeginWrite", return false);
    IF_NULL_LOG_ACTION(pageTable_, "pageTable_ is nullptrin BeginWrite", return false);
    IF_NULL_LOG_ACTION(builder_, "builder_ is nullptr in BeginWrite", return false);

    pageTable_->GetUxpte((uint64_t)dataPtr_, dataSizeInput_);
    PMState err = PM_OK;
    do {
        try {
            rwlock_.lock();
        } catch (...) {
            err = PM_LOCK_WRITE_FAIL;
            break;
        }
        if (!IsPurged_()) {
            /* data is not purged, return true */
            break;
        }
        /* data purged, rebuild it */
        if (BuildContent_()) {
            /* data rebuild succ, return true */
            break;
        }
        err = PMB_BUILD_ALL_FAIL;
    } while (0);

    if (err == PM_OK) {
        return true;
    }

    rwlock_.unlock();
    HILOG_ERROR(LOG_CORE, "%{public}s: err %{public}s, UxptePut.", __func__, GetPMStateName(err));
    pageTable_->PutUxpte((uint64_t)dataPtr_, dataSizeInput_);
    return false;
}

void PurgeableMem::EndWrite()
{
    HILOG_DEBUG(LOG_CORE, "%{public}s %{public}s", __func__, ToString_().c_str());
    IF_NULL_LOG_ACTION(pageTable_, "pageTable_ is nullptr in EndWrite", return);
    rwlock_.unlock();
    pageTable_->PutUxpte((uint64_t)dataPtr_, dataSizeInput_);
}

void* PurgeableMem::GetContent()
{
    return dataPtr_;
}

size_t PurgeableMem::GetContentSize()
{
    return dataSizeInput_;
}

bool PurgeableMem::ModifyContentByBuilder(std::unique_ptr<PurgeableMemBuilder> modifier)
{
    IF_NULL_LOG_ACTION(modifier, "input modifier is nullptr", return false);
    /* apply modify */
    bool succ = modifier->Build(dataPtr_, dataSizeInput_);
    if (!succ) {
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

bool PurgeableMem::IsPurged_()
{
    if (buildDataCount_ == 0) {
        HILOG_INFO(LOG_CORE, "%{public}s, has never built, return true", __func__);
        return true;
    }
    return !(pageTable_->CheckPresent((uint64_t)dataPtr_, dataSizeInput_));
}

bool PurgeableMem::BuildContent_()
{
    bool succ = false;
    /* clear content before rebuild */
    if (memset_s(dataPtr_, RoundUp_(dataSizeInput_, PAGE_SIZE), 0, dataSizeInput_) != EOK) {
        HILOG_ERROR(LOG_CORE, "%{public}s, clear content fail", __func__);
        return succ;
    }
    /* builder_ and dataPtr_ is never nullptr since it is checked by BeginAccess() before */
    succ = builder_->BuildAll(dataPtr_, dataSizeInput_);
    if (succ) {
        buildDataCount_++;
    }
    return succ;
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

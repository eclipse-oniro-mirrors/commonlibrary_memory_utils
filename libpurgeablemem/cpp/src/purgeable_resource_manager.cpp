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

#include "hitrace_meter.h"
#include "parameters.h"
#include "pm_log.h"
#include "purgeable_mem_base.h"
#include "purgeable_resource_manager.h"

namespace OHOS {
namespace PurgeableMem {
namespace {
/* System parameter name */
const std::string THREAD_POOL_TASK_NUMBER_SYS_NAME = "persist.commonlibrary.purgeable.threadpooltasknum";
const std::string LRU_CACHE_CAPACITY_SYS_NAME = "persist.commonlibrary.purgeable.lrucachecapacity";

/* Threadpool task number and lrucache capacity */
constexpr int32_t THREAD_POOL_TASK_NUMBER = 4;
constexpr int32_t MIN_THREAD_POOL_TASK_NUMBER = 1;
constexpr int32_t MAX_THREAD_POOL_TASK_NUMBER = 20;
constexpr int32_t LRU_CACHE_CAPACITY = 200;
constexpr int32_t MIN_LRU_CACHE_CAPACITY = 1;
constexpr int32_t MAX_LRU_CACHE_CAPACITY = 2000;
}

void PurgeableResourceManager::LruCache::Visited(std::shared_ptr<PurgeableMemBase> key)
{
    if (key == nullptr) {
        return;
    }

    auto resourcePtrIter = positionMap_.find(key);
    if (resourcePtrIter != positionMap_.end()) {
        resourcePtrList_.splice(resourcePtrList_.begin(), resourcePtrList_, resourcePtrIter->second);
        resourcePtrIter->second = resourcePtrList_.begin();
    }
}

void PurgeableResourceManager::LruCache::Insert(std::shared_ptr<PurgeableMemBase> key)
{
    if (key == nullptr) {
        return;
    }

    auto resourcePtrIter = positionMap_.find(key);
    if (resourcePtrIter != positionMap_.end()) {
        resourcePtrList_.splice(resourcePtrList_.begin(), resourcePtrList_, resourcePtrIter->second);
        resourcePtrIter->second = resourcePtrList_.begin();
        return;
    }

    resourcePtrList_.emplace_front(key);
    positionMap_.emplace(key, resourcePtrList_.begin());
    if (static_cast<int32_t>(resourcePtrList_.size()) > lruCacheCapacity_) {
        auto popResource = resourcePtrList_.back();
        if (popResource->GetPinStatus() == 0) {
            popResource->Pin();
        }
        positionMap_.erase(resourcePtrList_.back());
        resourcePtrList_.pop_back();
    }
}

void PurgeableResourceManager::LruCache::Erase(std::shared_ptr<PurgeableMemBase> key)
{
    if (key == nullptr) {
        return;
    }

    auto resourcePtrIter = positionMap_.find(key);
    if (resourcePtrIter == positionMap_.end()) {
        return;
    }

    resourcePtrList_.erase(resourcePtrIter->second);
    positionMap_.erase(key);
}

void PurgeableResourceManager::LruCache::SetCapacity(int32_t capacity)
{
    if (capacity < 0 || capacity > MAX_LRU_CACHE_CAPACITY) {
        PM_HILOG_DEBUG(LOG_CORE, "[PurgeableResourceManager] SetCapacity FAILED: capacity value is invalid!");
        return;
    }

    lruCacheCapacity_ = capacity;
    while (lruCacheCapacity_ < static_cast<int32_t>(Size())) {
        Erase(resourcePtrList_.back());
    }
}

void PurgeableResourceManager::LruCache::Clear()
{
    positionMap_.clear();
    resourcePtrList_.clear();
}

std::list<std::shared_ptr<PurgeableMemBase>> PurgeableResourceManager::LruCache::GetResourcePtrList() const
{
    return resourcePtrList_;
}

std::shared_ptr<PurgeableMemBase> PurgeableResourceManager::LruCache::GetLastResourcePtr() const
{
    return resourcePtrList_.back();
}

size_t PurgeableResourceManager::LruCache::Size() const
{
    return positionMap_.size();
}

PurgeableResourceManager::PurgeableResourceManager()
{
    int32_t lruCacheCapacity = GetLruCacheCapacityFromSysPara();
    if (lruCacheCapacity < MIN_LRU_CACHE_CAPACITY || lruCacheCapacity > MAX_LRU_CACHE_CAPACITY) {
        PM_HILOG_ERROR(LOG_CORE, "[PurgeableResourceManager] Get error lrucache capacity from system parameter.");
        lruCacheCapacity = LRU_CACHE_CAPACITY;
    }
    lruCache_.SetCapacity(lruCacheCapacity);
    isThreadPoolStarted_ = false;
    PM_HILOG_DEBUG(LOG_CORE, "PurgeableResourceManager init. lruCacheCapacity is: %{public}d", lruCacheCapacity);
}

PurgeableResourceManager::~PurgeableResourceManager()
{
    std::lock_guard<std::mutex> lock(lruCacheMutex_);
    if (isThreadPoolStarted_) {
        threadPool_.Stop();
    }
    lruCache_.Clear();
}

PurgeableResourceManager &PurgeableResourceManager::GetInstance()
{
    static PurgeableResourceManager instance;
    return instance;
}

void PurgeableResourceManager::BeginAccessPurgeableMem()
{
    StartTrace(HITRACE_TAG_ZIMAGE, "OHOS::PurgeableMem::PurgeableResourceManager::BeginAccessPurgeableMem");
    std::list<std::shared_ptr<PurgeableMemBase>> resourcePtrList = lruCache_.GetResourcePtrList();

    if (resourcePtrList.size() == 0) {
        FinishTrace(HITRACE_TAG_ZIMAGE);
        return;
    }

    std::lock_guard<std::mutex> lock(lruCacheMutex_);
    if (resourcePtrList.size() == 0) {
        FinishTrace(HITRACE_TAG_ZIMAGE);
        return;
    }

    if (!isThreadPoolStarted_) {
        StartThreadPool();
    }

    for (auto &resourcePtr : resourcePtrList) {
        if (resourcePtr == nullptr) {
            continue;
        }
        auto task = std::bind(&PurgeableMemBase::BeginReadWithDataLock, resourcePtr);
        threadPool_.AddTask(task);
    }

    FinishTrace(HITRACE_TAG_ZIMAGE);
    PM_HILOG_DEBUG(LOG_CORE, "[PurgeableResourceManager] BeginAccessPurgeableMem list size: %{public}zu",
        lruCache_.Size());
}

void PurgeableResourceManager::EndAccessPurgeableMem()
{
    StartTrace(HITRACE_TAG_ZIMAGE, "OHOS::PurgeableMem::PurgeableResourceManager::EndAccessPurgeableMem");
    std::list<std::shared_ptr<PurgeableMemBase>> resourcePtrList = lruCache_.GetResourcePtrList();

    if (resourcePtrList.size() == 0) {
        FinishTrace(HITRACE_TAG_ZIMAGE);
        return;
    }

    std::lock_guard<std::mutex> lock(lruCacheMutex_);
    if (resourcePtrList.size() == 0) {
        FinishTrace(HITRACE_TAG_ZIMAGE);
        return;
    }

    if (!isThreadPoolStarted_) {
        StartThreadPool();
    }

    for (auto &resourcePtr : resourcePtrList) {
        if (resourcePtr == nullptr) {
            continue;
        }
        auto task = std::bind(&PurgeableMemBase::EndReadWithDataLock, resourcePtr);
        threadPool_.AddTask(task);
    }

    FinishTrace(HITRACE_TAG_ZIMAGE);
    PM_HILOG_DEBUG(LOG_CORE, "[PurgeableResourceManager] EndAccessPurgeableMem list size: %{public}zu",
        lruCache_.Size());
}

void PurgeableResourceManager::ChangeDataValid(std::shared_ptr<PurgeableMemBase> resourcePtr, bool isVaild) const
{
    StartTrace(HITRACE_TAG_ZIMAGE, "OHOS::PurgeableMem::PurgeableResourceManager::ChangeDataValid");
    std::lock_guard<std::mutex> dataLock(resourcePtr->dataLock_);
    resourcePtr->SetDataValid(isVaild);
    if (!isVaild && resourcePtr->GetPinStatus() == 0) {
        resourcePtr->Pin();
    }
    FinishTrace(HITRACE_TAG_ZIMAGE);
}

void PurgeableResourceManager::AddResource(std::shared_ptr<PurgeableMemBase> resourcePtr)
{
    auto task = [this, resourcePtr] () {
        if (resourcePtr == nullptr) {
            return;
        }
        AddResourceInner(resourcePtr);
    };
    AddTaskToThreadPool(task);
}

void PurgeableResourceManager::AddResourceInner(std::shared_ptr<PurgeableMemBase> resourcePtr)
{
    StartTrace(HITRACE_TAG_ZIMAGE, "OHOS::PurgeableMem::PurgeableResourceManager::AddResource");
    std::lock_guard<std::mutex> lock(lruCacheMutex_);
    if (resourcePtr == nullptr) {
        FinishTrace(HITRACE_TAG_ZIMAGE);
        return;
    }

    lruCache_.Insert(resourcePtr);
    FinishTrace(HITRACE_TAG_ZIMAGE);
    PM_HILOG_DEBUG(LOG_CORE, "[PurgeableResourceManager] AddResource resourcePtr: 0x%{public}lx, "
        "list size: %{public}zu", (long)resourcePtr.get(), lruCache_.Size());
}

void PurgeableResourceManager::RemoveResource(std::shared_ptr<PurgeableMemBase> resourcePtr)
{
    ChangeDataValid(resourcePtr, false);
    auto task = [this, resourcePtr] () {
        if (resourcePtr == nullptr) {
            return;
        }
        RemoveResourceInner(resourcePtr);
    };
    AddTaskToThreadPool(task);
}

void PurgeableResourceManager::RemoveResourceInner(std::shared_ptr<PurgeableMemBase> resourcePtr)
{
    StartTrace(HITRACE_TAG_ZIMAGE, "OHOS::PurgeableMem::PurgeableResourceManager::RemoveResource");
    std::lock_guard<std::mutex> lock(lruCacheMutex_);
    if (resourcePtr == nullptr) {
        FinishTrace(HITRACE_TAG_ZIMAGE);
        return;
    }

    lruCache_.Erase(resourcePtr);
    FinishTrace(HITRACE_TAG_ZIMAGE);
    PM_HILOG_DEBUG(LOG_CORE, "[PurgeableResourceManager] RemoveResource resourcePtr: 0x%{public}lx, "
        "list size: %{public}zu", (long)resourcePtr.get(), lruCache_.Size());
}

void PurgeableResourceManager::SetRecentUsedResource(std::shared_ptr<PurgeableMemBase> resourcePtr)
{
    std::lock_guard<std::mutex> lock(lruCacheMutex_);
    if (resourcePtr == nullptr) {
        return;
    }

    lruCache_.Visited(resourcePtr);
}

void PurgeableResourceManager::SetLruCacheCapacity(int32_t capacity)
{
    std::lock_guard<std::mutex> lock(lruCacheMutex_);
    lruCache_.SetCapacity(capacity);
}

void PurgeableResourceManager::Clear()
{
    std::lock_guard<std::mutex> lock(lruCacheMutex_);
    lruCache_.Clear();
}

void PurgeableResourceManager::RemoveLastResource()
{
    std::lock_guard<std::mutex> lock(lruCacheMutex_);
    StartTrace(HITRACE_TAG_ZIMAGE, "OHOS::PurgeableMem::PurgeableResourceManager::RemoveLastResource");
    if (lruCache_.Size() == 0) {
        FinishTrace(HITRACE_TAG_ZIMAGE);
        return;
    }

    std::shared_ptr<PurgeableMemBase> resourcePtr = lruCache_.GetLastResourcePtr();
    if (resourcePtr == nullptr) {
        FinishTrace(HITRACE_TAG_ZIMAGE);
        return;
    }

    lruCache_.Erase(resourcePtr);
    FinishTrace(HITRACE_TAG_ZIMAGE);
    PM_HILOG_DEBUG(LOG_CORE, "[PurgeableResourceManager] RemoveLastResource resourcePtr: 0x%{public}lx, "
        "list size: %{public}zu", (long)resourcePtr.get(), lruCache_.Size());
}

void PurgeableResourceManager::ShowLruCache() const
{
    std::lock_guard<std::mutex> lock(lruCacheMutex_);
    std::list<std::shared_ptr<PurgeableMemBase>> resourcePtrList = lruCache_.GetResourcePtrList();
    int cnt = 0;
    for (auto &resourcePtr : resourcePtrList) {
        cnt++;
        PM_HILOG_DEBUG(LOG_CORE, "[PurgeableResourceManager] ShowLruCache (resourcePtr: 0x%{public}lx, "
            "%{public}d th, list size: %{public}zu)", (long)resourcePtr.get(), cnt, lruCache_.Size());
    }
}

int32_t PurgeableResourceManager::GetThreadPoolTaskNumFromSysPara() const
{
    return system::GetIntParameter<int32_t>(THREAD_POOL_TASK_NUMBER_SYS_NAME, THREAD_POOL_TASK_NUMBER);
}

int32_t PurgeableResourceManager::GetLruCacheCapacityFromSysPara() const
{
    return system::GetIntParameter<int32_t>(LRU_CACHE_CAPACITY_SYS_NAME, LRU_CACHE_CAPACITY);
}

void PurgeableResourceManager::StartThreadPool()
{
    std::lock_guard<std::mutex> lock(threadPoolMutex_);
    if (isThreadPoolStarted_) {
        return;
    }

    int32_t threadPoolTaskNum = GetThreadPoolTaskNumFromSysPara();
    if (threadPoolTaskNum < MIN_THREAD_POOL_TASK_NUMBER || threadPoolTaskNum > MAX_THREAD_POOL_TASK_NUMBER) {
        PM_HILOG_ERROR(LOG_CORE, "[PurgeableResourceManager] Get error threadpool task number from system parameter.");
        threadPoolTaskNum = THREAD_POOL_TASK_NUMBER;
    }

    threadPool_.Start(threadPoolTaskNum);
    isThreadPoolStarted_ = true;
    PM_HILOG_DEBUG(LOG_CORE, "StartThreadPool finish.");
}

void PurgeableResourceManager::AddTaskToThreadPool(const std::function<void()> &f)
{
    if (!isThreadPoolStarted_) {
        StartThreadPool();
    }

    threadPool_.AddTask(f);
}
} /* namespace PurgeableMem */
} /* namespace OHOS */
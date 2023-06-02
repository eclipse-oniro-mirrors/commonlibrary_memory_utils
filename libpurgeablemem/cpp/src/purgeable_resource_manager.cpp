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
#include "purgeable_resource_manager.h"

namespace OHOS {
namespace PurgeableMem {
void LruCache::Visited(std::shared_ptr<PurgeableMemBase> key)
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

void LruCache::Insert(std::shared_ptr<PurgeableMemBase> key)
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

void LruCache::Erase(std::shared_ptr<PurgeableMemBase> key)
{
    if (key == nullptr) {
        return;
    }

    auto resourcePtrIter = positionMap_.find(key);
    if (resourcePtrIter == positionMap_.end()) {
        return;
    }

    if (key->GetPinStatus() == 0) {
        key->Pin();
    }

    resourcePtrList_.erase(resourcePtrIter->second);
    positionMap_.erase(key);
}

void LruCache::SetCapacity(int32_t capacity)
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

void LruCache::Clear()
{
    positionMap_.clear();
    resourcePtrList_.clear();
}

std::list<std::shared_ptr<PurgeableMemBase>> LruCache::GetResourcePtrList() const
{
    return resourcePtrList_;
}

std::shared_ptr<PurgeableMemBase> LruCache::GetLastResourcePtr() const
{
    return resourcePtrList_.back();
}

size_t LruCache::Size() const
{
    return positionMap_.size();
}

PurgeableResourceManager::PurgeableResourceManager()
{
    GetParaFromConfiguration();
}

PurgeableResourceManager::~PurgeableResourceManager()
{
    std::lock_guard<std::mutex> lock(mutex_);
    threadPool_.Stop();
    lruCache_.Clear();
}

PurgeableResourceManager &PurgeableResourceManager::GetInstance()
{
    static PurgeableResourceManager instance;
    return instance;
}

void PurgeableResourceManager::BeginAccessPurgeableMem()
{
    std::lock_guard<std::mutex> lock(mutex_);
    StartTrace(HITRACE_TAG_COMMONLIBRARY, "OHOS::PurgeableMem::PurgeableResourceManager::BeginAccessPurgeableMem");
    std::list<std::shared_ptr<PurgeableMemBase>> resourcePtrList = lruCache_.GetResourcePtrList();
    for (auto &resourcePtr : resourcePtrList) {
        if (resourcePtr == nullptr) {
            continue;
        }

        auto task = std::bind(&PurgeableMemBase::BeginRead, resourcePtr);
        threadPool_.AddTask(task);
    }

    FinishTrace(HITRACE_TAG_COMMONLIBRARY);
    PM_HILOG_DEBUG(LOG_CORE, "[PurgeableResourceManager] BeginAccessPurgeableMem list size: %{public}zu",
        lruCache_.Size());
}

void PurgeableResourceManager::EndAccessPurgeableMem()
{
    std::lock_guard<std::mutex> lock(mutex_);
    StartTrace(HITRACE_TAG_COMMONLIBRARY, "OHOS::PurgeableMem::PurgeableResourceManager::EndAccessPurgeableMem");
    std::list<std::shared_ptr<PurgeableMemBase>> resourcePtrList = lruCache_.GetResourcePtrList();
    for (auto &resourcePtr : resourcePtrList) {
        if (resourcePtr == nullptr) {
            continue;
        }

        auto task = std::bind(&PurgeableMemBase::EndRead, resourcePtr);
        threadPool_.AddTask(task);
    }

    FinishTrace(HITRACE_TAG_COMMONLIBRARY);
    PM_HILOG_DEBUG(LOG_CORE, "[PurgeableResourceManager] EndAccessPurgeableMem list size: %{public}zu",
        lruCache_.Size());
}

void PurgeableResourceManager::AddResource(std::shared_ptr<PurgeableMemBase> resourcePtr)
{
    std::lock_guard<std::mutex> lock(mutex_);
    StartTrace(HITRACE_TAG_COMMONLIBRARY, "OHOS::PurgeableMem::PurgeableResourceManager::AddResource");
    if (resourcePtr == nullptr) {
        FinishTrace(HITRACE_TAG_COMMONLIBRARY);
        return;
    }

    lruCache_.Insert(resourcePtr);
    FinishTrace(HITRACE_TAG_COMMONLIBRARY);
    PM_HILOG_DEBUG(LOG_CORE, "[PurgeableResourceManager] AddResource resourcePtr: 0x%{public}lx, "
        "list size: %{public}zu", (long)resourcePtr.get(), lruCache_.Size());
}

void PurgeableResourceManager::RemoveResource(std::shared_ptr<PurgeableMemBase> resourcePtr)
{
    std::lock_guard<std::mutex> lock(mutex_);
    StartTrace(HITRACE_TAG_COMMONLIBRARY, "OHOS::PurgeableMem::PurgeableResourceManager::RemoveResource");
    if (resourcePtr == nullptr) {
        FinishTrace(HITRACE_TAG_COMMONLIBRARY);
        return;
    }

    lruCache_.Erase(resourcePtr);
    FinishTrace(HITRACE_TAG_COMMONLIBRARY);
    PM_HILOG_DEBUG(LOG_CORE, "[PurgeableResourceManager] RemoveResource resourcePtr: 0x%{public}lx, "
        "list size: %{public}zu", (long)resourcePtr.get(), lruCache_.Size());
}

void PurgeableResourceManager::SetRecentUsedResource(std::shared_ptr<PurgeableMemBase> resourcePtr)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (resourcePtr == nullptr) {
        return;
    }

    lruCache_.Visited(resourcePtr);
}

void PurgeableResourceManager::SetLruCacheCapacity(int32_t capacity)
{
    std::lock_guard<std::mutex> lock(mutex_);
    lruCache_.SetCapacity(capacity);
}

void PurgeableResourceManager::Clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    lruCache_.Clear();
}

void PurgeableResourceManager::RemoveLastResource()
{
    std::lock_guard<std::mutex> lock(mutex_);
    StartTrace(HITRACE_TAG_COMMONLIBRARY, "OHOS::PurgeableMem::PurgeableResourceManager::RemoveLastResource");
    if (lruCache_.Size() == 0) {
        FinishTrace(HITRACE_TAG_COMMONLIBRARY);
        return;
    }

    std::shared_ptr<PurgeableMemBase> resourcePtr = lruCache_.GetLastResourcePtr();
    if (resourcePtr == nullptr) {
        FinishTrace(HITRACE_TAG_COMMONLIBRARY);
        return;
    }

    lruCache_.Erase(resourcePtr);
    FinishTrace(HITRACE_TAG_COMMONLIBRARY);
    PM_HILOG_DEBUG(LOG_CORE, "[PurgeableResourceManager] RemoveLastResource resourcePtr: 0x%{public}lx, "
        "list size: %{public}zu", (long)resourcePtr.get(), lruCache_.Size());
}

void PurgeableResourceManager::ShowLruCache() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::list<std::shared_ptr<PurgeableMemBase>> resourcePtrList = lruCache_.GetResourcePtrList();
    int cnt = 0;
    for (auto &resourcePtr : resourcePtrList) {
        cnt++;
        PM_HILOG_DEBUG(LOG_CORE, "[PurgeableResourceManager] ShowLruCache (resourcePtr: 0x%{public}lx, "
            "%{public}d th, list size: %{public}zu)", (long)resourcePtr.get(), cnt, lruCache_.Size());
    }
}

int32_t PurgeableResourceManager::GetThreadPoolTaskNumFromSysPara()
{
    return system::GetIntParameter<int32_t>(THREAD_POOL_TASK_NUMBER_SYS_NAME, THREAD_POOL_TASK_NUMBER);
}

int32_t PurgeableResourceManager::GetLruCacheCapacityFromSysPara()
{
    return system::GetIntParameter<int32_t>(LRU_CACHE_CAPACITY_SYS_NAME, LRU_CACHE_CAPACITY);
}

void PurgeableResourceManager::GetParaFromConfiguration()
{
    int32_t threadPoolTaskNum = GetThreadPoolTaskNumFromSysPara();
    int32_t lruCacheCapacity = GetLruCacheCapacityFromSysPara();
    if (threadPoolTaskNum < MIN_THREAD_POOL_TASK_NUMBER || threadPoolTaskNum > MAX_THREAD_POOL_TASK_NUMBER) {
        PM_HILOG_ERROR(LOG_CORE, "[PurgeableResourceManager] Get error threadpool task number from system parameter.");
        return;
    }

    if (lruCacheCapacity < MIN_LRU_CACHE_CAPACITY || lruCacheCapacity > MAX_LRU_CACHE_CAPACITY) {
        PM_HILOG_ERROR(LOG_CORE, "[PurgeableResourceManager] Get error lrucache capacity from system parameter.");
        return;
    }

    lruCache_.SetCapacity(lruCacheCapacity);
    if (threadPool_.GetThreadsNum() == 0) {
        threadPool_.Start(threadPoolTaskNum);
    }

    PM_HILOG_DEBUG(LOG_CORE, "[PurgeableResourceManager] lruCacheCapacity is: %{public}d, "
        "threadPool threadsNum is: %{public}zu", lruCacheCapacity, threadPool_.GetThreadsNum());
}
} /* namespace PurgeableMem */
} /* namespace OHOS */
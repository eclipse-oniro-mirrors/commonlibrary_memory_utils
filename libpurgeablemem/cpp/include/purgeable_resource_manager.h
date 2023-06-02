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

#ifndef OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PURGEABLE_RESOURCE_MANAGER_H
#define OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PURGEABLE_RESOURCE_MANAGER_H

#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

#include "purgeable_mem_base.h"
#include "thread_pool.h"

namespace OHOS {
namespace PurgeableMem {
/* System parameter name */
const std::string THREAD_POOL_TASK_NUMBER_SYS_NAME = "persist.commonlibrary.purgeable.threadpooltasknum";
const std::string LRU_CACHE_CAPACITY_SYS_NAME = "persist.commonlibrary.purgeable.lrucachecapacity";
/* Threadpool task number and lrucache capacity */
constexpr int32_t THREAD_POOL_TASK_NUMBER = 8;
constexpr int32_t MIN_THREAD_POOL_TASK_NUMBER = 1;
constexpr int32_t MAX_THREAD_POOL_TASK_NUMBER = 20;
constexpr int32_t LRU_CACHE_CAPACITY = 100;
constexpr int32_t MIN_LRU_CACHE_CAPACITY = 1;
constexpr int32_t MAX_LRU_CACHE_CAPACITY = 200;

class LruCache {
public:
    /*
     * Visited: visit the cache entry with the given key.
     * If the entry is found, it will be move to the most-recent position in the cache.
    */
    void Visited(std::shared_ptr<PurgeableMemBase> key);

    /*
     * Insert: insert the PurgeableMemBase key in the lrucache.
     * Input: @key: ptr of PurgeableMemBase.
    */
    void Insert(std::shared_ptr<PurgeableMemBase> key);

    /*
     * Erase: erase the PurgeableMemBase key in the lrucache.
     * Input: @key: ptr of PurgeableMemBase.
    */
    void Erase(std::shared_ptr<PurgeableMemBase> key);

    /*
     * SetCapacity: set the capacity of the lrucache.
     * Input: the capacity of lrucache.
    */
    void SetCapacity(int32_t capacity);

    /*
     * Clear: clear the resourcePtrList and positionMap of the lrucache.
    */
    void Clear();

    using ListSharedPtrIterator = std::list<std::shared_ptr<PurgeableMemBase>>::iterator;
    std::list<std::shared_ptr<PurgeableMemBase>> GetResourcePtrList() const;
    std::shared_ptr<PurgeableMemBase> GetLastResourcePtr() const;
    size_t Size() const;

private:
    int32_t lruCacheCapacity_;
    std::list<std::shared_ptr<PurgeableMemBase>> resourcePtrList_;
    std::unordered_map<std::shared_ptr<PurgeableMemBase>, ListSharedPtrIterator> positionMap_;
};

class PurgeableResourceManager {
public:
    PurgeableResourceManager(const PurgeableResourceManager&) = delete;
    PurgeableResourceManager& operator=(const PurgeableResourceManager&) = delete;
    ~PurgeableResourceManager();

    static PurgeableResourceManager &GetInstance();
    void BeginAccessPurgeableMem();
    void EndAccessPurgeableMem();
    void AddResource(std::shared_ptr<PurgeableMemBase> resourcePtr);
    void RemoveResource(std::shared_ptr<PurgeableMemBase> resourcePtr);
    void SetRecentUsedResource(std::shared_ptr<PurgeableMemBase> resourcePtr);
    void SetLruCacheCapacity(int32_t capacity);
    void Clear();
    void RemoveLastResource();
    void ShowLruCache() const;

private:
    PurgeableResourceManager();
    int32_t GetThreadPoolTaskNumFromSysPara();
    int32_t GetLruCacheCapacityFromSysPara();
    void GetParaFromConfiguration();

    mutable std::mutex mutex_;
    LruCache lruCache_;
    ThreadPool threadPool_;
};
} /* namespace PurgeableMem */
} /* namespace OHOS */
#endif /* OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PURGEABLE_RESOURCE_MANAGER_H */
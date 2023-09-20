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

#ifndef OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PURGEABLE_MEM_BASE_H
#define OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PURGEABLE_MEM_BASE_H

#ifndef OHOS_MAXIMUM_PURGEABLE_MEMORY
#define OHOS_MAXIMUM_PURGEABLE_MEMORY ((1024) * (1024) * (1024)) /* 1G */
#endif /* OHOS_MAXIMUM_PURGEABLE_MEMORY */

#include <memory> /* unique_ptr */
#include <shared_mutex> /* shared_mutex */
#include <string>

#include "purgeable_mem_builder.h"
#include "purgeable_resource_manager.h"
#include "ux_page_table.h"
#include "ffrt.h"

namespace OHOS {
namespace PurgeableMem {
class PurgeableMemBase {
public:
    /*
     * BeginRead: begin read the PurgeableMem obj.
     * Return:  return true if the obj's content is present.
     *          If content is purged(no present), system will recover its data,
     *          return false if content is purged and recover failed.
     *          While return true if content recover success.
     * OS cannot reclaim the memory of the obj's content when this
     * function return true, until EndRead() is called.
     */
    bool BeginRead();

    /*
     * EndRead: end read the PurgeableMem obj.
     * OS may reclaim the memory of its content
     * at a later time when this function returns.
     */
    void EndRead();

    /*
     * BeginRead: begin read the PurgeableMem obj.
     * Return:  return true if the obj's content is present.
     *          If content is purged(no present), system will recover its data,
     *          return false if content is purged and recover failed.
     *          While return true if content recover success.
     * OS cannot reclaim the memory of the obj's content when this
     * function return true, until EndRead() is called.
     */

    bool BeginWrite();

    /*
     * EndWrite: end write the PurgeableMem obj.
     * OS may reclaim the memory of its content
     * at a later time when this function returns.
     */
    void EndWrite();

    /*
     * ModifyContentByBuilder: append a PurgeableMemBuilder obj to the PurgeableMem obj.
     * Input:   @modifier: unique_ptr of PurgeableMemBuilder, it will modify content of this obj.
     * Return:  modify result, true is success, while false is fail.
     * This function should be protected by BeginWrite()/EndWrite().
     */
    bool ModifyContentByBuilder(std::unique_ptr<PurgeableMemBuilder> modifier);

    /*
     * GetContent: get content ptr of the PurgeableMem obj.
     * Return:  return the content ptr, which is start address of the obj's content.
     * This function should be protected by BeginRead()/EndRead()
     * or BeginWrite()/EndWrite().
     */
    void *GetContent();

    /*
     * GetContentSize: get content size of the PurgeableMem obj.
     * Return:  return content size of the obj's content.
     */
    size_t GetContentSize();

    /*
     * ResizeData: resize size of the PurgeableMem obj.
     */
    virtual void ResizeData(size_t newSize);
    void SetRebuildSuccessCallback(std::function<void()> &callback);
    bool IsDataValid();
    void SetDataValid(bool target);
    bool BeginReadWithDataLock();
    void EndReadWithDataLock();

    PurgeableMemBase();
    virtual ~PurgeableMemBase();
    PurgeableMemBase(const PurgeableMemBase&) = delete;
    PurgeableMemBase& operator = (PurgeableMemBase&) = delete;
    PurgeableMemBase(PurgeableMemBase&&) noexcept = delete;
    PurgeableMemBase& operator = (PurgeableMemBase&&) noexcept = delete;

protected:
    void *dataPtr_ = nullptr;
    ffrt::mutex dataLock_;
    bool isDataValid_ {true};
    size_t dataSizeInput_ = 0;
    std::unique_ptr<PurgeableMemBuilder> builder_ = nullptr;
    std::shared_mutex rwlock_;
    unsigned int buildDataCount_ = 0;
    bool BuildContent();
    bool IfNeedRebuild();
    virtual bool Pin();
    virtual bool Unpin();
    virtual bool IsPurged();
    virtual int GetPinStatus() const;
    virtual void AfterRebuildSucc();
    virtual std::string ToString() const;
    friend class PurgeableResourceManager;
};
} /* namespace PurgeableMem */
} /* namespace OHOS */
#endif /* OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PURGEABLE_MEM_BASE_H */

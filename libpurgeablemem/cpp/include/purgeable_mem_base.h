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

#include <memory> /* unique_ptr */
#include <shared_mutex> /* shared_mutex */
#include <string>

#include "purgeable_mem_builder.h"
#include "ux_page_table.h"

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


    PurgeableMemBase();
    virtual ~PurgeableMemBase();
    PurgeableMemBase(const PurgeableMemBase&) = delete;
    PurgeableMemBase& operator = (PurgeableMemBase&) = delete;
    PurgeableMemBase(PurgeableMemBase&&) noexcept = delete;
    PurgeableMemBase& operator = (PurgeableMemBase&&) noexcept = delete;

protected:
    void *dataPtr_ = nullptr;
    size_t dataSizeInput_;
    std::unique_ptr<PurgeableMemBuilder> builder_ = nullptr;
    std::shared_mutex rwlock_;
    unsigned int buildDataCount_;
    bool BuildContent_();
    bool IfNeedRebuild_();
    virtual bool Pin_();
    virtual bool Unpin_();
    virtual bool IsPurged_();
    virtual bool CreatePurgeableData_();
    virtual void AfterRebuildSucc_();
    virtual std::string ToString_() const;
};
} /* namespace PurgeableMem */
} /* namespace OHOS */
#endif /* OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PURGEABLE_MEM_BASE_H */

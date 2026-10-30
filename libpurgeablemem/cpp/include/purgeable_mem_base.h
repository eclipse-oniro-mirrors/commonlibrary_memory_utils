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
#include "ux_page_table.h"

namespace OHOS {
namespace PurgeableMem {

/* Purgeable memory backend type */
enum PurgMemType {
    PURG_MEM_TYPE_ANONYMOUS = 0,  /* User-space UX page table */
    PURG_MEM_TYPE_ASHMEM = 1,     /* Kernel ashmem purgeable */
    PURG_MEM_TYPE_DMA_BUF = 2,    /* Kernel dma-buf purgeable */
};

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
     *
     * Attension: the return value must be recevied and handled,
     *            since the visiting of this object with the failure result
     *            will cause unsuspected result.
     * For example:
     *               if (BeginRead()) {
     *                   // visit this object
     *                   EndRead();
     *               }
     */
    bool BeginRead();

    /*
     * EndRead: end read the PurgeableMem obj.
     * Return:  true if the Unpin succeeded (reference count decremented);
     *          false means the purgeable mechanism is permanently disabled
     *          and the content will never be reclaimed by the kernel.
     */
    bool EndRead();

    /*
     * BeginRead: begin write the PurgeableMem obj.
     * Return:  return true if the obj's content is present.
     *          If content is purged(no present), system will recover its data,
     *          return false if content is purged and recover failed.
     *          While return true if content recover success.
     * OS cannot reclaim the memory of the obj's content when this
     * function return true, until EndWrite() is called.
     *
     * Attension: the return value must be recevied and handled,
     *            since the visiting of this object with the failure result
     *            will cause unsuspected result.
     * For example:
     *               if (BeginWrite()) {
     *                   // visit this object
     *                   EndWrite();
     *               }
     */

    bool BeginWrite();

    /*
     * EndWrite: end write the PurgeableMem obj.
     * Return:  true if the Unpin succeeded (reference count decremented);
     *          false means the purgeable mechanism is permanently disabled
     *          and the content will never be reclaimed by the kernel.
     */
    bool EndWrite();

    /*
     * DisablePurgeable: ensure the content is present (rebuild it if purged),
     * then permanently disable the purgeable feature of this object.
     * Return:  true if disabled successfully; false if the content rebuild
     *          failed, in which case the object is still purgeable.
     */
    bool DisablePurgeable();

    /*
     * DisablePurgeableKernel: best-effort kernel-side disable hook.
     * Called by DisablePurgeable() and the disable-fallback paths after
     * the userspace disabled_ flag is set. Failure is logged and ignored
     * (soft degradation) — the userspace flag alone guarantees safety.
     * Return:  0 on success, negative on failure.
     */
    virtual int DisablePurgeableKernel();

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

    PurgeableMemBase();
    virtual ~PurgeableMemBase();
    PurgeableMemBase(const PurgeableMemBase&) = delete;
    PurgeableMemBase& operator = (PurgeableMemBase&) = delete;
    PurgeableMemBase(PurgeableMemBase&&) noexcept = delete;
    PurgeableMemBase& operator = (PurgeableMemBase&&) noexcept = delete;
    virtual int GetPinStatus() const;
    virtual bool Pin();
    virtual PurgMemType GetPurgMemType() const;
    virtual int GetFd() const;
    virtual unsigned long GetIno() const;

protected:
    void *dataPtr_ = nullptr;
    std::mutex dataLock_;
    bool isDataValid_ {true};
    /*
     * isSupport_: operation guard — when false, all purgeable operations
     * (Pin/Unpin/IsPurged/GetPinStatus) become no-ops on this object.
     * Written by the kernel probe in CreatePurgeableData/ChangeData
     * (only when !disabled_), and forced to false by DisablePurgeable().
     */
    bool isSupport_ = false;
    /*
     * disabled_: write guard for isSupport_ — when true, CreatePurgeableData/
     * ChangeData skip the purgeable marking entirely, so isSupport_ stays
     * false and can never be set back to true. One-way: set only by
     * DisablePurgeable(), no restore path.
     */
    bool disabled_ = false;
    size_t dataSizeInput_ = 0;
    std::unique_ptr<PurgeableMemBuilder> builder_ = nullptr;
    unsigned int buildDataCount_ = 0;
    bool BuildContent();
    bool IfNeedRebuild();
    /*
     * BeginReadUnlocked: the pin and rebuild body of BeginRead, the caller
     * must hold dataLock_. The content stays pinned on success.
     */
    bool BeginReadUnlocked();
    virtual bool Unpin();
    virtual bool IsPurged();
    virtual void AfterRebuildSucc();
    virtual std::string ToString() const;
};
} /* namespace PurgeableMem */
} /* namespace OHOS */
#endif /* OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PURGEABLE_MEM_BASE_H */

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

#ifndef OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PURGEABLE_ASHMEM_H
#define OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PURGEABLE_ASHMEM_H

#include <memory>
#include <shared_mutex>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/ashmem.h>

#include "ashmem.h"
#include "purgeable_mem_builder.h"
#include "purgeable_mem_base.h"
#include "ux_page_table.h"

#ifndef ASHMEM_SET_PURGEABLE
#define ASHMEM_SET_PURGEABLE                   _IO(__ASHMEMIOC, 11)
#endif
#ifndef ASHMEM_GET_PURGEABLE
#define ASHMEM_GET_PURGEABLE                   _IO(__ASHMEMIOC, 12)
#endif
#ifndef PURGEABLE_ASHMEM_IS_PURGED
#define PURGEABLE_ASHMEM_IS_PURGED             _IO(__ASHMEMIOC, 13)
#endif
#ifndef PURGEABLE_ASHMEM_REBUILD_SUCCESS
#define PURGEABLE_ASHMEM_REBUILD_SUCCESS       _IO(__ASHMEMIOC, 14)
#endif

namespace OHOS {
namespace PurgeableMem {
class PurgeableAshMem : public PurgeableMemBase {
public:
    PurgeableAshMem(size_t dataSize, std::unique_ptr<PurgeableMemBuilder> builder);
    PurgeableAshMem(std::unique_ptr<PurgeableMemBuilder> builder);
    ~PurgeableAshMem() override;
    int GetAshmemFd();
    /*
     * Resize the self-allocated ashmem region; the content is rebuilt by the
     * builder during the resize, before the new region is marked purgeable.
     * Regions taken over by ChangeAshmemData() cannot be resized.
     */
    void ResizeData(size_t newSize) override;
    /*
     * Allocate a new ashmem region. The content is built by the builder
     * BEFORE the region is marked purgeable, so the kernel SET_PURGEABLE
     * records its baseline checksum over the real content.
     * After Unpin() the region ends at kernel refcount 0: a standard
     * purgeable object whose content is complete and reclaimable.
     * The first BeginRead/BeginWrite hits the content directly without
     * triggering a rebuild — unless the kernel has reclaimed it in between.
     * Rejected if the object already holds an external region taken over
     * via ChangeAshmemData (rebind is forbidden).
     */
    bool CreatePurgeableData();
    /*
     * Take over an existing ashmem region that already contains data,
     * converting it to purgeable.
     * Increments buildDataCount_ — assumes data is already present.
     * Does NOT call Unpin — buffer already has data, keep it pinned.
     * One takeover per object: rejected if the object already holds an
     * external region (rebind is forbidden — use ReplacePurgeableMem instead).
     */
    bool ChangeAshmemData(size_t size, int fd, void *data);

    /*
     * Replace the current ashmem region with a new one, transferring the
     * kernel pin count. The caller must be inside a BeginRead/BeginWrite
     * section (old region pinned at least once). The content in the new
     * region must already be built by the caller (with @newBuilder).
     * The old fd is NOT closed — the caller retains ownership.
     * Symmetric to PurgeableDmaBuf::ReplacePurgeableMem.
     * @return true on success, false on failure (object state unchanged).
     */
    bool ReplacePurgeableMem(size_t size, int newFd, void *newData,
                             std::unique_ptr<PurgeableMemBuilder> newBuilder) override;

    PurgMemType GetPurgMemType() const override;
    int GetFd() const override;

private:
    /*
     * CreatePurgeableDataUnlocked: the lock-free body of CreatePurgeableData.
     * The caller must hold dataLock_.
     */
    bool CreatePurgeableDataUnlocked();
    /*
     * ValidateCreatePurgeableData: state validation for
     * CreatePurgeableDataUnlocked. Return: true if the state is valid.
     */
    bool ValidateCreatePurgeableData();
    /*
     * BuildAndMarkPurgeable: build the content by builder BEFORE marking
     * the region purgeable. Return: true on success; on failure the caller
     * must release the region.
     * The caller must hold dataLock_.
     */
    bool BuildAndMarkPurgeable();

    /*
     * ValidateReplacePurgeableMem: parameter and state validation for
     * ReplacePurgeableMem. Return: true if all parameters and state are valid.
     */
    bool ValidateReplacePurgeableMem(size_t size, int newFd, void *newData,
                                     const std::unique_ptr<PurgeableMemBuilder> &newBuilder);

    /*
     * AlignPinCount: pin the new fd until its kernel pin count matches
     * @targetCount. The new region starts at count 1 after SET_PURGEABLE.
     * Return: true if aligned, false on ioctl failure.
     */
    bool AlignPinCount(int newFd, int targetCount);

protected:
    int ashmemFd_;
    bool isChange_;
    ashmem_pin pin_ = { static_cast<uint32_t>(0), static_cast<uint32_t>(0) };
    bool Pin() override;
    bool Unpin() override;
    bool IsPurged() override;
    int GetPinStatus() const override;
    void AfterRebuildSucc() override;
    std::string ToString() const override;
};
} /* namespace PurgeableMem */
} /* namespace OHOS */
#endif /* OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PURGEABLE_ASHMEM_H */

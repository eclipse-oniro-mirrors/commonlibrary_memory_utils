/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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

#ifndef OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PURGEABLE_DMABUF_H
#define OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PURGEABLE_DMABUF_H

#include <cstdint> /* uint32_t */
#include <memory>
#include <string>

#include "purgeable_dmabuf_c.h" /* ioctl macros, struct DmaBufPurgeablePin, PurgDmaBuf* bridge */
#include "purgeable_mem_builder.h"
#include "purgeable_mem_base.h"

namespace OHOS {
namespace PurgeableMem {
/*
 * PurgeableDmaBuf: Purgeable memory backed by a DMA buffer.
 *
 * Allocates its own DMA buffer via libdmabufheap in CreatePurgeableData(),
 * or takes over an existing DMA buffer via ChangeDmaBufData().
 *
 * Uses DMA buf purgeable ioctls (DMA_BUF_SET_PURGEABLE, PIN/UNPIN, etc.)
 * directly on the dma-buf fd, same pattern as PurgeableAshMem.
 */
class PurgeableDmaBuf : public PurgeableMemBase {
public:
    /*
     * Construct with builder only. Call CreatePurgeableData() to allocate
     * a new DMA buffer, or ChangeDmaBufData() to take over an existing one.
     */
    explicit PurgeableDmaBuf(std::unique_ptr<PurgeableMemBuilder> builder);

    ~PurgeableDmaBuf() override;

    /*
     * Get the DMA buffer fd.
     */
    int GetDmaBufFd() const;

    /*
     * Allocate a new DMA buffer via libdmabufheap and mark it as purgeable.
     * SET_PURGEABLE sets the kernel default pin count to 1 (pinned).
     * Calls Unpin() so the empty buffer can be reclaimed by the kernel
     * until the first BeginRead/BeginWrite triggers BuildContent via builder.
     * Does NOT increment buildDataCount_.
     *
     * @param size     Data size of the DMA buffer
     * @param heapName Name of the dma-heap (e.g. "system")
     * @return true on success, false on failure
     */
    bool CreatePurgeableData(size_t size, const char *heapName = "system");

    /*
     * Take over an existing DMA buffer that already contains data,
     * converting it to purgeable.
     * Increments buildDataCount_ — assumes data is already present,
     * same as PurgeableAshMem::ChangeAshmemData.
     * Does NOT call Unpin — buffer already has data, keep it pinned.
     *
     * @param size   Data size of the DMA buffer
     * @param fd     File descriptor of the DMA buffer (dma-buf fd)
     * @param data   Mmaped data pointer of the DMA buffer
     * @return true on success, false on failure
     */
    bool ChangeDmaBufData(size_t size, int fd, void *data);

    /*
     * Resize the self-allocated DMA buffer, the content will be rebuilt by
     * builder at the next access. Buffers taken over by ChangeDmaBufData()
     * cannot be resized.
     */
    void ResizeData(size_t newSize) override;

    PurgMemType GetPurgMemType() const override;
    int GetFd() const override;
    unsigned long GetIno() const override;
    int GetPinStatus() const override;

protected:
    int dmaBufFd_ = -1;
    bool isChange_ = false;
    unsigned long ino_ = 0;  /* cached dmabuf inode, queried once at create/change */
    struct DmaBufPurgeablePin pin_ = { static_cast<uint32_t>(0), static_cast<uint32_t>(0) };

    /* ReleaseSelfAllocatedBuffer: release the previous self-allocated buffer, if any. */
    void ReleaseSelfAllocatedBuffer();

    bool Pin() override;
    bool Unpin() override;
    bool IsPurged() override;
    void AfterRebuildSucc() override;
    std::string ToString() const override;
};
} /* namespace PurgeableMem */
} /* namespace OHOS */
#endif /* OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PURGEABLE_DMABUF_H */

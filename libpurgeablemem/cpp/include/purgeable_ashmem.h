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
    void ResizeData(size_t newSize) override;
    bool ChangeAshmemData(size_t size, int fd, void *data);

protected:
    int ashmemFd_;
    int isSupport_;
    bool isChange_;
    ashmem_pin pin_ = { static_cast<uint32_t>(0), static_cast<uint32_t>(0) };
    bool Pin() override;
    bool Unpin() override;
    bool IsPurged() override;
    int GetPinStatus() const override;
    bool CreatePurgeableData_();
    void AfterRebuildSucc() override;
    std::string ToString() const override;
};
} /* namespace PurgeableMem */
} /* namespace OHOS */
#endif /* OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PURGEABLE_ASHMEM_H */

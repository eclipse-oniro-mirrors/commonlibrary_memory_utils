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

#ifndef OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PURGEABLE_MEM_H
#define OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PURGEABLE_MEM_H

#include <memory> /* unique_ptr */
#include <shared_mutex> /* shared_mutex */
#include <string>

#include "purgeable_mem_builder.h"
#include "purgeable_mem_base.h"
#include "ux_page_table.h"

namespace OHOS {
namespace PurgeableMem {
class PurgeableMem : public PurgeableMemBase {
public:
    PurgeableMem(size_t dataSize, std::unique_ptr<PurgeableMemBuilder> builder);
    ~PurgeableMem();
    void ResizeData(size_t newSize) override;


protected:
    std::unique_ptr<UxPageTable> pageTable_ = nullptr;
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
#endif /* OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PURGEABLE_MEM_H */

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

#include "pm_smartptr_util.h"
#include "purgeable_mem_builder.h"

namespace OHOS {
namespace PurgeableMem {
#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "PurgeableMem: Builder"

PurgeableMemBuilder::~PurgeableMemBuilder()
{
    if (nextBuilder_) {
        nextBuilder_.reset();
    }
}

void PurgeableMemBuilder::AppendBuilder(std::unique_ptr<PurgeableMemBuilder> builder)
{
    IF_NULL_LOG_ACTION(builder, "input builder is nullptr", return);
    if (nextBuilder_) {
        nextBuilder_->AppendBuilder(std::move(builder));
    } else {
        nextBuilder_ = std::move(builder);
    }
}

bool PurgeableMemBuilder::BuildAll(void *data, size_t size)
{
    if (!Build(data, size)) {
        HILOG_ERROR(LOG_CORE, "%{public}s: build(0x%{public}llx, %{public}zu) fail",
            __func__, (unsigned long long)data, size);
        return false;
    }
    if (!nextBuilder_) {
        return true;
    }
    return nextBuilder_->BuildAll(data, size);
}
} /* namespace PurgeableMem */
} /* namespace OHOS */

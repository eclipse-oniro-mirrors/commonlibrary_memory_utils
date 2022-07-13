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

#include <cstdlib> /* malloc */

#include "hilog/log_c.h"
#include "ux_page_table.h"

namespace OHOS {
namespace PurgeableMem {
#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "PurgeableMem: UPT"

UxPageTable::UxPageTable(uint64_t addr, size_t len)
{
    uxpt_ = (UxPageTableStruct *)malloc(UxPageTableSize());
    if (!uxpt_) {
        HILOG_ERROR(LOG_CORE, "%{public}s: malloc UxPageTableStruct fail", __func__);
    }
    PMState err = InitUxPageTable(uxpt_, addr, len); /* dataPtr is aligned */
    if (err != PM_OK) {
        HILOG_ERROR(LOG_CORE, "%{public}s: InitUxPageTable fail, %{public}s", __func__, GetPMStateName(err));
        free(uxpt_);
        uxpt_ = nullptr;
    }
}

UxPageTable::~UxPageTable()
{
    /* unmap uxpt */
    if (uxpt_) {
        PMState err = DeinitUxPageTable(uxpt_);
        if (err != PM_OK) {
            HILOG_ERROR(LOG_CORE, "%{public}s: deinit upt fail, %{public}s", __func__, GetPMStateName(err));
        } else {
            free(uxpt_);
            uxpt_ = nullptr;
        }
    }
}

void UxPageTable::GetUxpte(uint64_t addr, size_t len)
{
    UxpteGet(uxpt_, addr, len);
}

void UxPageTable::PutUxpte(uint64_t addr, size_t len)
{
    UxptePut(uxpt_, addr, len);
}


bool UxPageTable::CheckPresent(uint64_t addr, size_t len)
{
    return UxpteIsPresent(uxpt_, addr, len);
}

std::string UxPageTable::ToString() const
{
    return "uxptAddr: " + std::to_string((unsigned long long)uxpt_);
}
} /* namespace PurgeableMem */
} /* namespace OHOS */

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

#ifndef OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PM_SMARTPTR_UTIL_H
#define OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PM_SMARTPTR_UTIL_H

#include "pm_ptr_util.h"

namespace OHOS {
namespace PurgeableMem {
#define MAKE_UNIQUE(ptrSelf, classType, errLog, errAction, ...)    \
    do {                                                           \
        ptrSelf = nullptr;                                         \
        try {                                                      \
            ptrSelf = std::make_unique<classType>(__VA_ARGS__);    \
        } catch (...) {                                            \
            HILOG_ERROR(LOG_CORE, errLog);                         \
        }                                                          \
        if (ptrSelf == nullptr) {                                  \
            errAction;                                             \
        }                                                          \
    } while (0)
} /* namespace PurgeableMem */
} /* namespace OHOS */
#endif /* OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PM_SMARTPTR_UTIL_H */

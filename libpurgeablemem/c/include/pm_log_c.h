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

#ifndef OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PM_LOG_C_H
#define OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_CPP_INCLUDE_PM_LOG_C_H

#include "hilog/log.h"

#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0xD001799

#ifdef PM_HILOG_ERROR_C
#undef PM_HILOG_ERROR_C
#endif

#ifdef PM_HILOG_INFO_C
#undef PM_HILOG_INFO_C
#endif

#ifdef PM_HILOG_DEBUG_C
#undef PM_HILOG_DEBUG_C
#endif

#define PM_FILENAME "purgeable"

#define PM_HILOG_ERROR_C(logCore, fmt, ...)            \
    (void)HILOG_ERROR(             \
        LOG_CORE, "[%{public}s(%{public}s:%{public}d)]" fmt, PM_FILENAME, __FUNCTION__, __LINE__, ##__VA_ARGS__)

#define PM_HILOG_INFO_C(logCore, fmt, ...)             \
    (void)HILOG_INFO(              \
        LOG_CORE, "[%{public}s(%{public}s:%{public}d)]" fmt, PM_FILENAME, __FUNCTION__, __LINE__, ##__VA_ARGS__)

#define PM_HILOG_DEBUG_C(logCore, fmt, ...)            \
    (void)HILOG_DEBUG(             \
        LOG_CORE, "[%{public}s(%{public}s:%{public}d)]" fmt, PM_FILENAME, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#endif
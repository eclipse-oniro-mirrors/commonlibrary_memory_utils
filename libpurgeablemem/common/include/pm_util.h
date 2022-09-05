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

#ifndef OHOS_UTILS_MEMOEY_LIBPURGEABLEMEM_COMMON_INCLUDE_PM_UTIL_H
#define OHOS_UTILS_MEMOEY_LIBPURGEABLEMEM_COMMON_INCLUDE_PM_UTIL_H

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* End of #if __cplusplus */
#endif /* End of #ifdef __cplusplus */

/*
 * USE_UXPT is true means using uxpt using uxpt in libpurgeable,
 * while false means not using uxpt, false will be used in the following cases:
 * case 1: if there is no purgeable mem module in kernel.
 * case 2: if you want close libpurgeable, meanwhile doesn't affect user programs.
 */
#define USE_UXPT true

#define MAP_PURGEABLE 0x04
#define MAP_USEREXPTE 0x08

#define PAGE_SHIFT 12
#ifdef PAGE_SIZE
#undef PAGE_SIZE
#endif
#define PAGE_SIZE (1 << PAGE_SHIFT)

/*
 * When UXPT is not used, In order not to affect the normal function
 * of user programs, this lib will provide normal anon memory. So
 * MAP_PURGEABLE is set to 0x0.
 */
#if (USE_UXPT == false)
#undef MAP_PURGEABLE
#define MAP_PURGEABLE 0x0
#undef MAP_USEREXPTE
#define MAP_USEREXPTE 0x0
#endif

#ifdef __cplusplus
#if __cplusplus
}
#endif /* End of #if __cplusplus */
#endif /* End of #ifdef __cplusplus */

#endif /* OHOS_UTILS_MEMOEY_LIBPURGEABLEMEM_COMMON_INCLUDE_PM_UTIL_H */

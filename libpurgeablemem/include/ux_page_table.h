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

#ifndef LIB_PURGEABLE_MEM_UX_PAGE_TABLE_H
#define LIB_PURGEABLE_MEM_UX_PAGE_TABLE_H

#include <stdint.h> /* uint64_t */
#include <stdbool.h> /* bool */
#include <sys/types.h> /* size_t */
#include "pm_state.h"

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
#define USE_UXPT false

#if defined(USE_UXPT) && (USE_UXPT == true)
#define MAP_USEREXPTE 0x80
#else
#define MAP_USEREXPTE 0x0
#endif

/*
 * using uint64_t as uxpte_t to avoid avoid confusion on 32-bit and 64 bit systems.
 * Type uxpte_t may be modified to uint32_t in the future, so typedef is used.
 */
typedef uint64_t uxpte_t;

/* user extend page table */
struct UxPageTable;

bool UxpteIsEnabled(void);
size_t UxPageTableSize(void);

PMState InitUxPageTable(struct UxPageTable *upt, void *addr, size_t len);
PMState DeinitUxPageTable(struct UxPageTable *upt);

void UxpteGet(struct UxPageTable *upt, void *addr, size_t len);
void UxptePut(struct UxPageTable *upt, void *addr, size_t len);
bool UxpteIsPresent(struct UxPageTable *upt, void *addr, size_t len);


#ifdef __cplusplus
#if __cplusplus
}
#endif /* End of #if __cplusplus */
#endif /* End of #ifdef __cplusplus */

#endif /* LIB_PURGEABLE_MEM_UX_PAGE_TABLE_H */

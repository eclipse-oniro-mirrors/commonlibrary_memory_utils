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

#ifndef OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_COMMON_INCLUDE_UX_PAGE_TABLE_C_H
#define OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_COMMON_INCLUDE_UX_PAGE_TABLE_C_H

#include <stdint.h> /* uint64_t */
#include <stdbool.h> /* bool */
#include <stddef.h> /* size_t */
#include "pm_state_c.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* End of #if __cplusplus */
#endif /* End of #ifdef __cplusplus */

/* user extend page table */
typedef struct UserExtendPageTable UxPageTableStruct;

bool UxpteIsEnabled(void);
size_t UxPageTableSize(void);

PMState InitUxPageTable(UxPageTableStruct *upt, uint64_t addr, size_t len);
PMState DeinitUxPageTable(UxPageTableStruct *upt);

void UxpteGet(UxPageTableStruct *upt, uint64_t addr, size_t len);
void UxptePut(UxPageTableStruct *upt, uint64_t addr, size_t len);
void UxpteClear(UxPageTableStruct *upt, uint64_t addr, size_t len);
bool UxpteIsPresent(UxPageTableStruct *upt, uint64_t addr, size_t len);

#ifdef __cplusplus
#if __cplusplus
}
#endif /* End of #if __cplusplus */
#endif /* End of #ifdef __cplusplus */
#endif /* OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_COMMON_INCLUDE_UX_PAGE_TABLE_C_H */

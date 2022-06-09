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

#ifndef OHOS_UTILS_MEMOEY_LIBPURGEABLEMEM_C_INCLUDE_PURGEABLE_MEM_BUILDER_C_H
#define OHOS_UTILS_MEMOEY_LIBPURGEABLEMEM_C_INCLUDE_PURGEABLE_MEM_BUILDER_C_H

#include <stdbool.h>

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* End of #if __cplusplus */
#endif /* End of #ifdef __cplusplus */

/* purgeable mem builder */
struct PurgMemBuilder;

typedef bool (*PurgMemBuilderFunc)(void *, size_t, void *);


struct PurgMemBuilder *PurgMemBuilderCreate(PurgMemBuilderFunc func, void *param, const char *name);

/* If return true, @builder will be set to NULL to avoid Use-After-Free */
bool PurgMemBuilderDestroy(struct PurgMemBuilder *builder);

bool PurgMemBuilderAppendFunc(struct PurgMemBuilder *builder, PurgMemBuilderFunc func, void *param,
                              const char *name);

bool PurgMemBuilderAppendBuilder(struct PurgMemBuilder *builder, struct PurgMemBuilder *newcomer);

/* build @data content from @builder */
bool PurgMemBuilderBuildAll(struct PurgMemBuilder *builder, void *data, size_t size);

#ifdef __cplusplus
#if __cplusplus
}
#endif /* End of #if __cplusplus */
#endif /* End of #ifdef __cplusplus */

#endif /* OHOS_UTILS_MEMOEY_LIBPURGEABLEMEM_C_INCLUDE_PURGEABLE_MEM_BUILDER_C_H */

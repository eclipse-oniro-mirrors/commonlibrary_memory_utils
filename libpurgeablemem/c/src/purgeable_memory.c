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
#include "pthread.h"

#include "purgeable_mem_builder_c.h"
#include "purgeable_mem_c.h"
#include "ux_page_table_c.h"
#include "purgeable_memory.h"

#undef LOG_TAG
#define LOG_TAG "PurgeableMemNDK"

typedef bool (*OH_PurgeableMemory_ModifyFunc)(void *, size_t, void *);
typedef struct PurgMem OH_PurgeableMemory;
typedef struct PurgMem PurgMem;

OH_PurgeableMemory *OH_PurgeableMemory_Create(
    size_t size, OH_PurgeableMemory_ModifyFunc func, void *funcPara)
{
    return (OH_PurgeableMemory *)PurgMemCreate(size, func, funcPara);
}

bool OH_PurgeableMemory_Destroy(OH_PurgeableMemory *purgObj)
{
    return PurgMemDestroy((PurgMem *)purgObj);
}

bool OH_PurgeableMemory_BeginRead(OH_PurgeableMemory *purgObj)
{
    return PurgMemBeginRead((PurgMem *)purgObj);
}

void OH_PurgeableMemory_EndRead(OH_PurgeableMemory *purgObj)
{
    PurgMemEndRead((PurgMem *)purgObj);
}

bool OH_PurgeableMemory_BeginWrite(OH_PurgeableMemory *purgObj)
{
    return PurgMemBeginWrite((PurgMem *)purgObj);
}

void OH_PurgeableMemory_EndWrite(OH_PurgeableMemory *purgObj)
{
    PurgMemEndWrite((PurgMem *)purgObj);
}

void *OH_PurgeableMemory_GetContent(OH_PurgeableMemory *purgObj)
{
    return PurgMemGetContent((PurgMem *)purgObj);
}

size_t OH_PurgeableMemory_ContentSize(OH_PurgeableMemory *purgObj)
{
    return PurgMemGetContentSize((PurgMem *)purgObj);
}

bool OH_PurgeableMemory_AppendModify(OH_PurgeableMemory *purgObj,
                                     OH_PurgeableMemory_ModifyFunc func, void *funcPara)
{
    return PurgMemAppendModify((PurgMem *)purgObj, func, funcPara);
}
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

#ifndef OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_C_INCLUDE_PURGEABLE_MEM_C_H
#define OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_C_INCLUDE_PURGEABLE_MEM_C_H

#include <stdbool.h> /* bool */
#include <stddef.h> /* size_t */

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* End of #if __cplusplus */
#endif /* End of #ifdef __cplusplus */

/* Purgeable mem struct */
struct PurgMem;

/*
 * Function pointer, it points to a function which build content of a PurgMem obj.
 * Input:   void *: data ptr, points to start address of a PurgMem obj's content.
 * Input:   size_t: data size of the content.
 * Input:   void *: other private parameters.
 * Return:  build content result, true means success, while false is fail.
 */
typedef bool (*PurgMemModifyFunc)(void *, size_t, void *);

/*
 * PurgMemCreate: create a PurgMem obj.
 * Input:   @size: data size of a PurgMem obj's content.
 * Input:   @func: function pointer, it recover data when the PurgMem obj's content is purged.
 * Input:   @funcPara: parameters used by @func.
 * Return:  a PurgMem obj.
 */
struct PurgMem *PurgMemCreate(size_t size, PurgMemModifyFunc func, void *funcPara);

/*
 * PurgMemDestroy: destroy a PurgMem obj.
 * Input:   @purgObj: a PurgMem obj to be destroyed.
 * Return:  true is success, while false is fail. return true if @purgObj is NULL.
 * If return true, @purgObj will be set to NULL to avoid Use-After-Free.
 */
bool PurgMemDestroy(struct PurgMem *purgObj);

/*
 * PurgMemBeginRead: begin read a PurgMem obj.
 * Input:   @purgObj: a PurgMem obj.
 * Return:  return true if @purgObj's content is present.
 *          If content is purged(no present), system will recover its data,
 *          return false if content is purged and recover failed.
 *          While return true if content recover success.
 * OS cannot reclaim the memory of @purgObj's content when this
 * function return true, until PurgMemEndRead() is called.
 */
bool PurgMemBeginRead(struct PurgMem *purgObj);

/*
 * PurgMemEndRead: end read a PurgMem obj.
 * Input:   @purgObj: a PurgMem obj.
 * OS may reclaim the memory of @purgObj's content
 * at a later time when this function returns.
 */
void PurgMemEndRead(struct PurgMem *purgObj);

/*
 * PurgMemBeginWrite: begin write a PurgMem obj.
 * Input:   @purgObj: a PurgMem obj.
 * Return:  return true if @purgObj's content is present.
 *          if content is purged(no present), system will recover its data,
 *          return false if content is purged and recover failed.
 *          While return true if content recover success.
 * OS cannot reclaim the memory of @purgObj's content when this
 * function return true, until PurgMemEndWrite() is called.
 */
bool PurgMemBeginWrite(struct PurgMem *purgObj);

/*
 * PurgMemEndWrite: end write a PurgMem obj.
 * Input:   @purgObj: a PurgMem obj.
 * OS may reclaim the memory of @purgObj's content
 * at a later time when this function returns.
 */
void PurgMemEndWrite(struct PurgMem *purgObj);

/*
 * PurgMemGetContent: get content ptr of a PurgMem obj.
 * Input:   @purgObj: a PurgMem obj.
 * Return:  return start address of a PurgMem obj's content.
 *          Return NULL if @purgObj is NULL.
 * This function should be protect by PurgMemBeginRead()/PurgMemEndRead()
 * or PurgMemBeginWrite()/PurgMemEndWrite()
 */
void *PurgMemGetContent(struct PurgMem *purgObj);

/*
 * PurgMemGetContentSize: get content size of a PurgMem obj.
 * Input:   @purgObj: a PurgMem obj.
 * Return:  return content size of @purgObj.
 *          Return 0 if @purgObj is NULL.
 */
size_t PurgMemGetContentSize(struct PurgMem *purgObj);

/*
 * PurgMemAppendModify: append a modify to a PurgMem obj.
 * Input:   @purgObj: a PurgMem obj.
 * Input:   @size: data size of a PurgMem obj's content.
 * Input:   @func: function pointer, it will modify content of @PurgMem.
 * Input:   @funcPara: parameters used by @func.
 * Return:  append result, true is success, while false is fail.
 */
bool PurgMemAppendModify(struct PurgMem *purgObj, PurgMemModifyFunc func, void *funcPara);

#ifdef __cplusplus
#if __cplusplus
}
#endif /* End of #if __cplusplus */
#endif /* End of #ifdef __cplusplus */

#endif /* OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_C_INCLUDE_PURGEABLE_MEM_C_H */

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

#include <stdlib.h> /* malloc */
#include <sys/mman.h> /* mmap */
#include <pthread.h>
#include <stdio.h> /* FILE */

#include "securec.h"
#include "pm_ptr_util.h"
#include "pm_util.h"
#include "pm_state_c.h"
#include "ux_page_table_c.h"
#include "purgeable_mem_builder_c.h"
#include "pm_log_c.h"
#include "purgeable_mem_c.h"

#undef LOG_TAG
#define LOG_TAG "PurgeableMemC"

struct PurgMem {
    void *dataPtr;
    size_t dataSizeInput;
    struct PurgMemBuilder *builder;
    UxPageTableStruct *uxPageTable;
    pthread_rwlock_t rwlock;
    unsigned int buildDataCount;
};

static inline void LogPurgMemInfo_(struct PurgMem *obj)
{
    PM_HILOG_INFO_C(LOG_CORE, "purgMemObj(%{public}lx) dataPtr(%{public}lx) dataSizeInput(%{public}zu)"
        " builderPtr(%{public}lx) uxpt(%{public}lx)",
        (unsigned long)obj, (unsigned long)(obj->dataPtr), obj->dataSizeInput,
        (unsigned long)(obj->builder), (unsigned long)(obj->uxPageTable));
}

static inline size_t RoundUp(size_t val, size_t align)
{
    if (align == 0) {
        return val;
    }
    return ((val + align - 1) / align) * align;
}

static bool IsPurgMemPtrValid_(struct PurgMem *purgObj);
static bool IsPurged(struct PurgMem *purgObj);

static struct PurgMem *PurgMemCreate_(size_t len, struct PurgMemBuilder *builder)
{
    /* PurgMemObj allow no builder temporaily */
    struct PurgMem *pugObj = NULL;
    pugObj = (struct PurgMem *)malloc(sizeof(struct PurgMem));
    if (!pugObj) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: malloc struct PurgMem fail", __func__);
        return NULL;
    }
    size_t size = RoundUp(len, PAGE_SIZE);
    int type = MAP_ANONYMOUS;
    type |= (UxpteIsEnabled() ? MAP_PURGEABLE : MAP_PRIVATE);
    pugObj->dataPtr = mmap(NULL, size, PROT_READ | PROT_WRITE, type, -1, 0);
    if (pugObj->dataPtr == MAP_FAILED) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: mmap dataPtr fail", __func__);
        pugObj->dataPtr = NULL;
        goto free_pug_obj;
    }

    pugObj->uxPageTable = (UxPageTableStruct *)malloc(UxPageTableSize());
    if (!(pugObj->uxPageTable)) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: malloc UxPageTableStruct fail", __func__);
        goto unmap_data;
    }
    PMState err = InitUxPageTable(pugObj->uxPageTable, (uint64_t)(pugObj->dataPtr), size); /* dataPtr is aligned */
    if (err != PM_OK) {
        PM_HILOG_ERROR_C(LOG_CORE,
            "%{public}s: InitUxPageTable fail, %{public}s", __func__, GetPMStateName(err));
        goto free_uxpt;
    }
    int lockInitRet = pthread_rwlock_init(&(pugObj->rwlock), NULL);
    if (lockInitRet != 0) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: pthread_rwlock_init fail, %{public}d", __func__, lockInitRet);
        goto deinit_upt;
    }
    pugObj->builder = builder;
    pugObj->dataSizeInput = len;
    pugObj->buildDataCount = 0;

    PM_HILOG_INFO_C(LOG_CORE, "%{public}s: LogPurgMemInfo_:", __func__);
    LogPurgMemInfo_(pugObj);
    return pugObj;

deinit_upt:
    DeinitUxPageTable(pugObj->uxPageTable);
free_uxpt:
    free(pugObj->uxPageTable);
    pugObj->uxPageTable = NULL;
unmap_data:
    munmap(pugObj->dataPtr, size);
    pugObj->dataPtr = NULL;
free_pug_obj:
    free(pugObj);
    pugObj = NULL;

    return NULL;
}

struct PurgMem *PurgMemCreate(size_t len, PurgMemModifyFunc func, void *funcPara)
{
    if (len == 0) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: input len 0", __func__);
        return NULL;
    }
    /* a PurgMemObj must have builder */
    IF_NULL_LOG_ACTION(func, "%{public}s: input func is NULL", return NULL);
    struct PurgMem *purgMemObj = PurgMemCreate_(len, NULL);
    /* create fail */
    if (!purgMemObj) {
        return purgMemObj;
    }

    if (PurgMemAppendModify(purgMemObj, func, funcPara)) {
        return purgMemObj;
    }

    /* append func fail meas create builder failed */
    PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: append mod func fail", __func__);
    if (!PurgMemDestroy(purgMemObj)) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: destroy PurgMem fail after append modFunc fail", __func__);
    }
    return NULL;
}

bool PurgMemDestroy(struct PurgMem *purgObj)
{
    IF_NULL_LOG_ACTION(purgObj, "input is NULL", return true);
    PM_HILOG_INFO_C(LOG_CORE, "%{public}s: LogPurgMemInfo_:", __func__);
    LogPurgMemInfo_(purgObj);

    PMState err = PM_OK;
    /* destroy rwlock */
    int ret = pthread_rwlock_destroy(&(purgObj->rwlock));
    if (ret != 0) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: pthread_rwlock_destroy fail, %{public}d", __func__, ret);
    }
    /* destroy builder */
    if (purgObj->builder) {
        if (!PurgMemBuilderDestroy(purgObj->builder)) {
            PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: PurgMemBuilderDestroy fail", __func__);
            err = PMB_DESTORY_FAIL;
        } else {
            purgObj->builder = NULL;
        }
    }
    /* unmap purgeable mem region */
    if (purgObj->dataPtr) {
        size_t size = RoundUp(purgObj->dataSizeInput, PAGE_SIZE);
        if (munmap(purgObj->dataPtr, size) != 0) {
            PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: munmap dataPtr fail", __func__);
            err = PM_UNMAP_PURG_FAIL;
        } else {
            /* double check munmap result: if uxpte is set to no_present */
            if (UxpteIsEnabled() && !IsPurged(purgObj)) {
                PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: munmap dataPtr succ, but uxpte present", __func__);
                err = PM_UXPT_PRESENT_DATA_PURGED;
            }
            purgObj->dataPtr = NULL;
        }
    }
    /* unmap uxpt */
    if (purgObj->uxPageTable) {
        PMState deinitRet = DeinitUxPageTable(purgObj->uxPageTable);
        if (deinitRet != PM_OK) {
            PM_HILOG_ERROR_C(LOG_CORE,
                "%{public}s: deinit upt fail, %{public}s", __func__, GetPMStateName(deinitRet));
            err = deinitRet;
        } else {
            free(purgObj->uxPageTable);
            purgObj->uxPageTable = NULL;
        }
    }

    if (err == PM_OK) {
        free(purgObj);
        purgObj = NULL; /* set input para NULL to avoid UAF */
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: succ", __func__);
        return true;
    }
    PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: fail, %{public}s", __func__, GetPMStateName(err));
    return false;
}

static bool IsPurgMemPtrValid_(struct PurgMem *purgObj)
{
    IF_NULL_LOG_ACTION(purgObj, "obj is NULL", return false);
    IF_NULL_LOG_ACTION(purgObj->dataPtr, "dataPtr is NULL", return false);
    IF_NULL_LOG_ACTION(purgObj->uxPageTable, "pageTable is NULL", return false);
    IF_NULL_LOG_ACTION(purgObj->builder, "builder is NULL", return false);

    return true;
}

static inline bool PurgMemBuildData_(struct PurgMem *purgObj)
{
    bool succ = false;
    /* clear content before rebuild */
    if (memset_s(purgObj->dataPtr, RoundUp(purgObj->dataSizeInput, PAGE_SIZE), 0, purgObj->dataSizeInput) != EOK) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s, clear content fail", __func__);
        return succ;
    }
    /* @purgObj->builder is not NULL since it is checked by IsPurgMemPtrValid_() before */
    succ = PurgMemBuilderBuildAll(purgObj->builder, purgObj->dataPtr, purgObj->dataSizeInput);
    if (succ) {
        purgObj->buildDataCount++;
    }
    return succ;
}

static PMState TryBeginRead_(struct PurgMem *purgObj)
{
    int rwlockRet = pthread_rwlock_rdlock(&(purgObj->rwlock));
    if (rwlockRet != 0) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: rdlock fail. %{public}d", __func__, rwlockRet);
        return PM_LOCK_READ_FAIL;
    }

    if (!IsPurged(purgObj)) {
        PM_HILOG_INFO_C(LOG_CORE,
            "%{public}s: not purged, return true. MAP_PUG=0x%{public}x", __func__, MAP_PURGEABLE);
        return PM_DATA_NO_PURGED;
    }

    rwlockRet = pthread_rwlock_unlock(&(purgObj->rwlock));
    if (rwlockRet != 0) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: rd unlock fail. %{public}d", __func__, rwlockRet);
        return PM_UNLOCK_READ_FAIL;
    }

    return PM_DATA_PURGED;
}

static PMState BeginReadBuildData_(struct PurgMem *purgObj)
{
    bool rebuildRet = false;
    int rwlockRet = pthread_rwlock_wrlock(&(purgObj->rwlock));
    if (rwlockRet) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: wrlock fail. %{public}d", __func__, rwlockRet);
        return PM_LOCK_WRITE_FAIL;
    }

    if (IsPurged(purgObj)) {
        rebuildRet = PurgMemBuildData_(purgObj);
        PM_HILOG_ERROR_C(LOG_CORE,
            "%{public}s: purged, after built %{public}s", __func__, rebuildRet ? "succ" : "fail");
    }

    rwlockRet = pthread_rwlock_unlock(&(purgObj->rwlock));
    if (rwlockRet != 0) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: wr unlock fail. %{public}d", __func__, rwlockRet);
        return PM_UNLOCK_WRITE_FAIL;
    }

    if (!rebuildRet) {
        return PMB_BUILD_ALL_FAIL;
    }

    return PMB_BUILD_ALL_SUCC;
}

bool PurgMemBeginRead(struct PurgMem *purgObj)
{
    if (!IsPurgMemPtrValid_(purgObj)) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: para is invalid", __func__);
        return false;
    }
    PM_HILOG_INFO_C(LOG_CORE, "%{public}s: LogPurgMemInfo_:", __func__);
    LogPurgMemInfo_(purgObj);
    bool ret = false;
    PMState err = PM_OK;
    UxpteGet(purgObj->uxPageTable, (uint64_t)(purgObj->dataPtr), purgObj->dataSizeInput);
    while (true) {
        err = TryBeginRead_(purgObj);
        if (err == PM_DATA_NO_PURGED) {
            ret = true;
            break;
        } else if (err != PM_DATA_PURGED) {
            break;
        }

        err = BeginReadBuildData_(purgObj);
        if (err != PMB_BUILD_ALL_SUCC) {
            ret = false;
            break;
        }
    }

    if (!ret) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: %{public}s, UxptePut.", __func__, GetPMStateName(err));
        UxptePut(purgObj->uxPageTable, (uint64_t)(purgObj->dataPtr), purgObj->dataSizeInput);
    }
    return ret;
}

bool PurgMemBeginWrite(struct PurgMem *purgObj)
{
    if (!IsPurgMemPtrValid_(purgObj)) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: para is invalid", __func__);
        return false;
    }
    PM_HILOG_INFO_C(LOG_CORE, "%{public}s: LogPurgMemInfo_:", __func__);
    LogPurgMemInfo_(purgObj);
    int rwlockRet = 0;
    bool rebuildRet = false;
    PMState err = PM_OK;

    UxpteGet(purgObj->uxPageTable, (uint64_t)(purgObj->dataPtr), purgObj->dataSizeInput);

    rwlockRet = pthread_rwlock_wrlock(&(purgObj->rwlock));
    if (rwlockRet != 0) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: wrlock fail. %{public}d", __func__, rwlockRet);
        err = PM_LOCK_WRITE_FAIL;
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: %{public}s, return false, UxptePut.", __func__, GetPMStateName(err));
        UxptePut(purgObj->uxPageTable, (uint64_t)(purgObj->dataPtr), purgObj->dataSizeInput);
        return false;
    }

    if (!IsPurged(purgObj)) {
        return true;
    }

    /* data is purged */
    rebuildRet = PurgMemBuildData_(purgObj);
    PM_HILOG_INFO_C(LOG_CORE, "%{public}s: purged, built %{public}s", __func__, rebuildRet ? "succ" : "fail");
    if (rebuildRet) {
        return true;
    }
    /* data is purged and rebuild failed. return false */
    err = PMB_BUILD_ALL_FAIL;
    rwlockRet = pthread_rwlock_unlock(&(purgObj->rwlock));
    if (rwlockRet != 0) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: wr unlock fail. %{public}d", __func__, rwlockRet);
    }

    PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: %{public}s, return false, UxptePut.", __func__, GetPMStateName(err));
    UxptePut(purgObj->uxPageTable, (uint64_t)(purgObj->dataPtr), purgObj->dataSizeInput);
    return false;
}

static inline void EndAccessPurgMem_(struct PurgMem *purgObj)
{
    if (!IsPurgMemPtrValid_(purgObj)) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: para is invalid", __func__);
        return;
    }
    int rwlockRet = 0;
    rwlockRet = pthread_rwlock_unlock(&(purgObj->rwlock));
    if (rwlockRet != 0) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: unlock fail. %{public}d", __func__, rwlockRet);
    }
    UxptePut(purgObj->uxPageTable, (uint64_t)(purgObj->dataPtr), purgObj->dataSizeInput);
}

void PurgMemEndRead(struct PurgMem *purgObj)
{
    EndAccessPurgMem_(purgObj);
}

void PurgMemEndWrite(struct PurgMem *purgObj)
{
    EndAccessPurgMem_(purgObj);
}

void *PurgMemGetContent(struct PurgMem *purgObj)
{
    if (!IsPurgMemPtrValid_(purgObj)) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: para is invalid", __func__);
        return NULL;
    }
    return purgObj->dataPtr;
}

size_t PurgMemGetContentSize(struct PurgMem *purgObj)
{
    if (!IsPurgMemPtrValid_(purgObj)) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: para is invalid", __func__);
        return 0;
    }
    return purgObj->dataSizeInput;
}

bool PurgMemAppendModify(struct PurgMem *purgObj, PurgMemModifyFunc func, void *funcPara)
{
    IF_NULL_LOG_ACTION(func, "input func is NULL", return true);
    IF_NULL_LOG_ACTION(purgObj, "input purgObj is NULL", return false);
    /* apply modify */
    bool succ = func(purgObj->dataPtr, purgObj->dataSizeInput, funcPara);
    if (!succ) {
        return false;
    }
    struct PurgMemBuilder *builder = PurgMemBuilderCreate(func, funcPara, NULL);
    IF_NULL_LOG_ACTION(builder, "PurgMemBuilderCreate fail", return false);

    if (purgObj->builder == NULL) { /* PurgMemObj has no builder previous */
        purgObj->builder = builder;
        return true;
    }
    return PurgMemBuilderAppendBuilder(purgObj->builder, builder);
}

static bool IsPurged(struct PurgMem *purgObj)
{
    /* first access, return true means purged */
    if (purgObj->buildDataCount == 0) {
        PM_HILOG_INFO_C(LOG_CORE, "%{public}s, has never built, return true", __func__);
        return true;
    }
    return !UxpteIsPresent(purgObj->uxPageTable, (uint64_t)(purgObj->dataPtr), purgObj->dataSizeInput);
}

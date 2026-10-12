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
#include "purgeable_dmabuf_c.h"

#undef LOG_TAG
#define LOG_TAG "PurgeableMemC"

/* Internal memory type */
enum PurgMemType {
    PURG_MEM_TYPE_ANONYMOUS = 0,
    PURG_MEM_TYPE_DMA_BUF = 1,
};

struct PurgMem {
    void *dataPtr;
    size_t dataSizeInput;
    struct PurgMemBuilder *builder;
    UxPageTableStruct *uxPageTable;
    pthread_rwlock_t rwlock;
    unsigned int buildDataCount;
    /* DMA buffer specific fields */
    enum PurgMemType type;
    int dmaBufFd;
    bool isSupport;
    bool isChange;
};

static void LogPurgMemInfo(struct PurgMem *obj)
{
    if (obj == NULL) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: obj is NULL!", __func__);
        return;
    }
    if (obj->type == PURG_MEM_TYPE_DMA_BUF) {
        PM_HILOG_INFO_C(LOG_CORE, "purgMemObj(%{public}lx) type=DMA dataPtr(%{public}lx) dataSizeInput(%{public}zu)"
            " dmaBufFd(%{public}d) isSupport(%{public}d)",
            (unsigned long)obj, (unsigned long)(obj->dataPtr), obj->dataSizeInput,
            obj->dmaBufFd, obj->isSupport);
    } else {
        PM_HILOG_INFO_C(LOG_CORE, "purgMemObj(%{public}lx) type=ANON dataPtr(%{public}lx) dataSizeInput(%{public}zu)"
            " builderPtr(%{public}lx) uxpt(%{public}lx)",
            (unsigned long)obj, (unsigned long)(obj->dataPtr), obj->dataSizeInput,
            (unsigned long)(obj->builder), (unsigned long)(obj->uxPageTable));
    }
}

static inline size_t RoundUp(size_t val, size_t align)
{
    if (val + align < val || val + align < align) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: Addition overflow!", __func__);
        return val;
    }
    if (align == 0) {
        return val;
    }
    return ((val + align - 1) / align) * align;
}

static bool IsPurgMemPtrValid(struct PurgMem *purgObj);
static bool IsPurged(struct PurgMem *purgObj);
static int TypeCast(void);
static bool PurgMemDmaBufPinInternal(struct PurgMem *purgObj);
static bool PurgMemDmaBufUnpinInternal(struct PurgMem *purgObj);
static bool PurgMemBeginAccessDmaBuf(struct PurgMem *purgObj);

/*
 * PurgMemInitAnonData: map the anonymous memory and init its ux page table.
 * All resources are released on failure.
 */
static bool PurgMemInitAnonData(struct PurgMem *pugObj, size_t len)
{
    size_t size = RoundUp(len, PAGE_SIZE);
    pugObj->dataPtr = mmap(NULL, size, PROT_READ | PROT_WRITE, TypeCast(), -1, 0);
    if (pugObj->dataPtr == MAP_FAILED) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: mmap dataPtr fail", __func__);
        pugObj->dataPtr = NULL;
        return false;
    }

    pugObj->uxPageTable = (UxPageTableStruct *)malloc(UxPageTableSize());
    if (pugObj->uxPageTable == NULL) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: malloc UxPageTableStruct fail", __func__);
        goto unmap_data;
    }
    PMState err = InitUxPageTable(pugObj->uxPageTable, (uint64_t)(pugObj->dataPtr), size); /* dataPtr is aligned */
    if (err != PM_OK) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: InitUxPageTable fail, %{public}s", __func__, GetPMStateName(err));
        goto free_uxpt;
    }
    return true;

free_uxpt:
    free(pugObj->uxPageTable);
    pugObj->uxPageTable = NULL;
unmap_data:
    munmap(pugObj->dataPtr, size);
    pugObj->dataPtr = NULL;
    return false;
}

/* PurgMemDeinitAnonData: release the ux page table and the anonymous memory. */
static void PurgMemDeinitAnonData(struct PurgMem *pugObj, size_t size)
{
    DeinitUxPageTable(pugObj->uxPageTable);
    free(pugObj->uxPageTable);
    pugObj->uxPageTable = NULL;
    munmap(pugObj->dataPtr, size);
    pugObj->dataPtr = NULL;
}

/*
 * PurgMemCreate_: create an anonymous-memory PurgMem obj.
 * A NULL builder is allowed temporarily.
 */
static struct PurgMem *PurgMemCreate_(size_t len, struct PurgMemBuilder *builder)
{
    struct PurgMem *pugObj = (struct PurgMem *)malloc(sizeof(struct PurgMem));
    if (pugObj == NULL) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: malloc struct PurgMem fail", __func__);
        return NULL;
    }

    if (!PurgMemInitAnonData(pugObj, len)) {
        free(pugObj);
        return NULL;
    }

    int lockInitRet = pthread_rwlock_init(&(pugObj->rwlock), NULL);
    if (lockInitRet != 0) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: pthread_rwlock_init fail, %{public}d", __func__, lockInitRet);
        PurgMemDeinitAnonData(pugObj, RoundUp(len, PAGE_SIZE));
        free(pugObj);
        return NULL;
    }

    pugObj->builder = builder;
    pugObj->dataSizeInput = len;
    pugObj->buildDataCount = 0;
    pugObj->type = PURG_MEM_TYPE_ANONYMOUS;
    pugObj->dmaBufFd = -1;
    pugObj->isSupport = false;
    pugObj->isChange = false;

    PM_HILOG_INFO_C(LOG_CORE, "%{public}s: LogPurgMemInfo:", __func__);
    LogPurgMemInfo(pugObj);
    return pugObj;
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

/*
 * UnmapAnonData: munmap the anonymous memory of @purgObj and clear dataPtr.
 * Returns PM_OK on success, PM_UNMAP_PURG_FAIL on munmap failure. Warns if
 * the uxpte is still present after a successful munmap.
 */
static PMState UnmapAnonData(struct PurgMem *purgObj)
{
    size_t size = RoundUp(purgObj->dataSizeInput, PAGE_SIZE);
    if (munmap(purgObj->dataPtr, size) != 0) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: munmap dataPtr fail", __func__);
        return PM_UNMAP_PURG_FAIL;
    }
    /* double check munmap result: if uxpte is set to no_present */
    if (UxpteIsEnabled() && !IsPurged(purgObj)) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: munmap dataPtr succ, but uxpte present", __func__);
    }
    purgObj->dataPtr = NULL;
    return PM_OK;
}

/* DestroyPurgMemBuilder: destroy the builder of @purgObj, return the error state. */
static PMState DestroyPurgMemBuilder(struct PurgMem *purgObj)
{
    if (purgObj->builder == NULL) {
        return PM_OK;
    }
    if (!PurgMemBuilderDestroy(purgObj->builder)) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: PurgMemBuilderDestroy fail", __func__);
        return PMB_DESTORY_FAIL;
    }
    purgObj->builder = NULL;
    return PM_OK;
}

/* FreePurgMemUxpt: deinit and free the ux page table of @purgObj, return the error state. */
static PMState FreePurgMemUxpt(struct PurgMem *purgObj)
{
    if (purgObj->uxPageTable == NULL) {
        return PM_OK;
    }
    PMState deinitRet = DeinitUxPageTable(purgObj->uxPageTable);
    if (deinitRet != PM_OK) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: deinit upt fail, %{public}s", __func__, GetPMStateName(deinitRet));
        return deinitRet;
    }
    free(purgObj->uxPageTable);
    purgObj->uxPageTable = NULL;
    return PM_OK;
}

bool PurgMemDestroy(struct PurgMem *purgObj)
{
    IF_NULL_LOG_ACTION(purgObj, "input is NULL", return true);
    int rwlockRet = pthread_rwlock_wrlock(&(purgObj->rwlock));
    if (rwlockRet) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: wrlock fail. %{public}d", __func__, rwlockRet);
        return false;
    }
    PM_HILOG_INFO_C(LOG_CORE, "%{public}s: LogPurgMemInfo:", __func__);
    LogPurgMemInfo(purgObj);

    /* destroy builder */
    PMState err = DestroyPurgMemBuilder(purgObj);
    /* unmap purgeable mem region */
    if (purgObj->dataPtr) {
        if (purgObj->type == PURG_MEM_TYPE_DMA_BUF) {
            /* DMA buffer is owned externally — do NOT munmap/close.
             * Reset fd to invalid so any post-destroy access (e.g. on the
             * error path where the object is not freed) short-circuits in
             * PurgMemDmaBufPinInternal/UnpinInternal (dmaBufFd > 0 check). */
            purgObj->dataPtr = NULL;
            purgObj->dmaBufFd = -1;
        } else {
            PMState unmapRet = UnmapAnonData(purgObj);
            if (unmapRet != PM_OK) {
                err = unmapRet;
            }
        }
    }
    /* unmap uxpt */
    PMState uxptRet = FreePurgMemUxpt(purgObj);
    if (uxptRet != PM_OK) {
        err = uxptRet;
    }
    pthread_rwlock_unlock(&(purgObj->rwlock));

    if (err == PM_OK) {
        /* destroy rwlock */
        rwlockRet = pthread_rwlock_destroy(&(purgObj->rwlock));
        if (rwlockRet != 0) {
            PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: pthread_rwlock_destroy fail, %{public}d", __func__, rwlockRet);
        }
        free(purgObj);
        purgObj = NULL; /* set input para NULL to avoid UAF */
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: succ", __func__);
        return true;
    }
    PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: fail, %{public}s", __func__, GetPMStateName(err));
    return false;
}

static bool IsPurgMemPtrValid(struct PurgMem *purgObj)
{
    IF_NULL_LOG_ACTION(purgObj, "obj is NULL", return false);
    IF_NULL_LOG_ACTION(purgObj->dataPtr, "dataPtr is NULL", return false);
    IF_NULL_LOG_ACTION(purgObj->builder, "builder is NULL", return false);
    switch (purgObj->type) {
        case PURG_MEM_TYPE_DMA_BUF:
            if (purgObj->dmaBufFd < 0) {
                PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: dmaBufFd is invalid", __func__);
                return false;
            }
            return true;
        case PURG_MEM_TYPE_ANONYMOUS:
            IF_NULL_LOG_ACTION(purgObj->uxPageTable, "pageTable is NULL", return false);
            return true;
        default:
            PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: unknown type %{public}d", __func__, purgObj->type);
            return false;
    }
}

static inline bool PurgMemBuildData(struct PurgMem *purgObj)
{
    bool succ = false;
    /* clear content before rebuild */
    if (memset_s(purgObj->dataPtr, RoundUp(purgObj->dataSizeInput, PAGE_SIZE), 0, purgObj->dataSizeInput) != EOK) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s, clear content fail", __func__);
        return succ;
    }
    /* @purgObj->builder is not NULL since it is checked by IsPurgMemPtrValid() before */
    succ = PurgMemBuilderBuildAll(purgObj->builder, purgObj->dataPtr, purgObj->dataSizeInput);
    if (succ) {
        purgObj->buildDataCount++;
    }
    return succ;
}

static PMState TryBeginRead(struct PurgMem *purgObj)
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

static PMState BeginReadBuildData(struct PurgMem *purgObj)
{
    bool rebuildRet = false;
    int rwlockRet = pthread_rwlock_wrlock(&(purgObj->rwlock));
    if (rwlockRet) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: wrlock fail. %{public}d", __func__, rwlockRet);
        return PM_LOCK_WRITE_FAIL;
    }

    if (IsPurged(purgObj)) {
        rebuildRet = PurgMemBuildData(purgObj);
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
    if (!IsPurgMemPtrValid(purgObj)) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: para is invalid", __func__);
        return false;
    }
    PM_HILOG_INFO_C(LOG_CORE, "%{public}s: LogPurgMemInfo:", __func__);
    LogPurgMemInfo(purgObj);

    if (purgObj->type == PURG_MEM_TYPE_DMA_BUF) {
        return PurgMemBeginAccessDmaBuf(purgObj);
    }

    bool ret = false;
    PMState err = PM_OK;
    UxpteGet(purgObj->uxPageTable, (uint64_t)(purgObj->dataPtr), purgObj->dataSizeInput);
    while (true) {
        err = TryBeginRead(purgObj);
        if (err == PM_DATA_NO_PURGED) {
            ret = true;
            break;
        } else if (err != PM_DATA_PURGED) {
            break;
        }

        err = BeginReadBuildData(purgObj);
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
    if (!IsPurgMemPtrValid(purgObj)) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: para is invalid", __func__);
        return false;
    }
    PM_HILOG_INFO_C(LOG_CORE, "%{public}s: LogPurgMemInfo:", __func__);
    LogPurgMemInfo(purgObj);

    if (purgObj->type == PURG_MEM_TYPE_DMA_BUF) {
        return PurgMemBeginAccessDmaBuf(purgObj);
    }

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
    rebuildRet = PurgMemBuildData(purgObj);
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

static void EndAccessPurgMem(struct PurgMem *purgObj)
{
    if (!IsPurgMemPtrValid(purgObj)) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: para is invalid", __func__);
        return;
    }
    if (purgObj->type == PURG_MEM_TYPE_DMA_BUF) {
        PurgMemDmaBufUnpinInternal(purgObj);
        pthread_rwlock_unlock(&(purgObj->rwlock));
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
    EndAccessPurgMem(purgObj);
}

void PurgMemEndWrite(struct PurgMem *purgObj)
{
    EndAccessPurgMem(purgObj);
}

void *PurgMemGetContent(struct PurgMem *purgObj)
{
    if (!IsPurgMemPtrValid(purgObj)) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: para is invalid", __func__);
        return NULL;
    }
    return purgObj->dataPtr;
}

size_t PurgMemGetContentSize(struct PurgMem *purgObj)
{
    if (!IsPurgMemPtrValid(purgObj)) {
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
    if (purgObj->type == PURG_MEM_TYPE_DMA_BUF) {
        if (!purgObj->isSupport || purgObj->dmaBufFd < 0) {
            return false;
        }
        return PurgDmaBufIsPurged(purgObj->dmaBufFd) > 0;
    }
    return !UxpteIsPresent(purgObj->uxPageTable, (uint64_t)(purgObj->dataPtr), purgObj->dataSizeInput);
}

static int TypeCast(void)
{
    unsigned int utype = MAP_ANONYMOUS;
    utype |= (UxpteIsEnabled() ? MAP_PURGEABLE : MAP_PRIVATE);
    int type = (int) utype;
    return type;
}

/*
 * DMA buffer Pin via ioctl.
 */
static bool PurgMemDmaBufPinInternal(struct PurgMem *purgObj)
{
    if (purgObj == NULL || purgObj->type != PURG_MEM_TYPE_DMA_BUF) {
        return false;
    }
    if (!purgObj->isSupport) {
        return true;
    }
    if (purgObj->dmaBufFd >= 0) {
        struct DmaBufPurgeablePin pin = {0, 0};
        int ret = PurgDmaBufPin(purgObj->dmaBufFd, &pin);
        if (ret < 0) {
            PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: Pin fail, fd:%{public}d ret:%{public}d",
                __func__, purgObj->dmaBufFd, ret);
            return false;
        }
        if (ret > 0) {
            /* the buffer was purged before this pin, needs rebuild */
            PM_HILOG_INFO_C(LOG_CORE, "%{public}s: Pin succ (was purged), fd:%{public}d",
                __func__, purgObj->dmaBufFd);
        } else {
            PM_HILOG_INFO_C(LOG_CORE, "%{public}s: Pin succ, fd:%{public}d pinStatus:%{public}d",
                __func__, purgObj->dmaBufFd, PurgDmaBufGetPinStatus(purgObj->dmaBufFd));
        }
    }
    return true;
}

/*
 * DMA buffer Unpin via ioctl.
 */
static bool PurgMemDmaBufUnpinInternal(struct PurgMem *purgObj)
{
    if (purgObj == NULL || purgObj->type != PURG_MEM_TYPE_DMA_BUF) {
        return false;
    }
    if (!purgObj->isSupport) {
        return true;
    }
    if (purgObj->dmaBufFd >= 0) {
        struct DmaBufPurgeablePin pin = {0, 0};
        int ret = PurgDmaBufUnpin(purgObj->dmaBufFd, &pin);
        if (ret < 0) {
            PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: Unpin fail, fd:%{public}d ret:%{public}d",
                __func__, purgObj->dmaBufFd, ret);
            return false;
        }
        PM_HILOG_INFO_C(LOG_CORE, "%{public}s: Unpin succ, fd:%{public}d pinStatus:%{public}d",
            __func__, purgObj->dmaBufFd, PurgDmaBufGetPinStatus(purgObj->dmaBufFd));
    }
    return true;
}

/*
 * RebuildDmaBufIfPurged: rebuild the DMA buffer content of @purgObj if it has
 * never been built or has been purged. Unpins the buffer on rebuild failure.
 * Returns true when the content is present, false on rebuild failure.
 */
static bool RebuildDmaBufIfPurged(struct PurgMem *purgObj)
{
    if (purgObj->buildDataCount != 0 && !IsPurged(purgObj)) {
        return true;
    }
    if (!PurgMemBuildData(purgObj)) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: rebuild fail, Unpin", __func__);
        PurgMemDmaBufUnpinInternal(purgObj);
        return false;
    }
    PurgDmaBufRebuildSuccess(purgObj->dmaBufFd);
    return true;
}

/*
 * PurgMemBeginAccessDmaBuf: dma-buf path of PurgMemBeginRead/PurgMemBeginWrite.
 * Holds the rwlock through the entire access section (until the paired
 * PurgMemEndRead/PurgMemEndWrite), so that concurrent access on the same
 * object is serialized, matching the anonymous-memory path.
 * Returns true on success (caller may access dataPtr, lock held), false on
 * failure (lock released).
 */
static bool PurgMemBeginAccessDmaBuf(struct PurgMem *purgObj)
{
    int rwlockRet = pthread_rwlock_wrlock(&(purgObj->rwlock));
    if (rwlockRet != 0) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: wrlock fail. %{public}d", __func__, rwlockRet);
        return false;
    }

    if (!PurgMemDmaBufPinInternal(purgObj)) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: Pin fail", __func__);
        goto unlock_fail;
    }
    if (!RebuildDmaBufIfPurged(purgObj)) {
        goto unlock_fail;
    }
    return true; /* lock held through the access section */

unlock_fail:
    rwlockRet = pthread_rwlock_unlock(&(purgObj->rwlock));
    if (rwlockRet != 0) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: unlock fail. %{public}d", __func__, rwlockRet);
    }
    return false;
}

/*
 * PurgMemCreateDmaBuf: create a DMA buffer PurgMem obj (empty shell, no mmap).
 * Use PurgMemChangeDmaBufData() to attach an external DMA buffer.
 */
struct PurgMem *PurgMemCreateDmaBuf(PurgMemModifyFunc func, void *funcPara)
{
    IF_NULL_LOG_ACTION(func, "%{public}s: input func is NULL", return NULL);

    struct PurgMem *pugObj = NULL;
    pugObj = (struct PurgMem *)malloc(sizeof(struct PurgMem));
    if (!pugObj) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: malloc struct PurgMem fail", __func__);
        return NULL;
    }

    int lockInitRet = pthread_rwlock_init(&(pugObj->rwlock), NULL);
    if (lockInitRet != 0) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: pthread_rwlock_init fail, %{public}d", __func__, lockInitRet);
        free(pugObj);
        return NULL;
    }

    pugObj->dataPtr = NULL;
    pugObj->dataSizeInput = 0;
    pugObj->buildDataCount = 0;
    pugObj->type = PURG_MEM_TYPE_DMA_BUF;
    pugObj->uxPageTable = NULL;
    pugObj->dmaBufFd = -1;
    pugObj->isSupport = false;
    pugObj->isChange = false;

    /* Create builder only — do NOT apply modify now: dataPtr is NULL for the
     * empty DMA shell. The modify will be applied via builder->BuildAll()
     * during the first BeginRead/BeginWrite after ChangeDmaBufData attaches
     * a real DMA buffer. Same deferred-build semantics as the open-source
     * implementation. */
    pugObj->builder = PurgMemBuilderCreate(func, funcPara, NULL);
    if (pugObj->builder == NULL) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: PurgMemBuilderCreate fail", __func__);
        PurgMemDestroy(pugObj);
        return NULL;
    }

    PM_HILOG_INFO_C(LOG_CORE, "%{public}s: LogPurgMemInfo:", __func__);
    LogPurgMemInfo(pugObj);
    return pugObj;
}

/*
 * PurgMemChangeDmaBufData: take over an existing DMA buffer.
 * The DMA buffer is owned externally — PurgMem will not munmap/close it on destroy.
 * Marks the DMA buffer as purgeable and unpins it, same as PurgeableAshMem::ChangeAshmemData.
 */
bool PurgMemChangeDmaBufData(struct PurgMem *purgObj, size_t size, int fd, void *data)
{
    IF_NULL_LOG_ACTION(purgObj, "input purgObj is NULL", return false);
    if (purgObj->type != PURG_MEM_TYPE_DMA_BUF) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: not DMA type", __func__);
        return false;
    }
    if (size == 0) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: invalid size 0", __func__);
        return false;
    }
    if (fd < 0) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: invalid fd", __func__);
        return false;
    }
    if (data == NULL) {
        PM_HILOG_ERROR_C(LOG_CORE, "%{public}s: data is NULL", __func__);
        return false;
    }

    purgObj->dataSizeInput = size;
    purgObj->dmaBufFd = fd;
    purgObj->dataPtr = data;
    purgObj->buildDataCount++;
    purgObj->isChange = true;

    /* Mark DMA buffer as purgeable. Kernel default pin count is 1 after SET_PURGEABLE.
     * Does NOT call Unpin — buffer already has data, keep it pinned.
     * Same as PurgeableDmaBuf::ChangeDmaBufData. */
    PurgDmaBufSetPurgeable(purgObj->dmaBufFd);
    purgObj->isSupport = (PurgDmaBufGetPurgeable(purgObj->dmaBufFd) == PURGEABLE_DMA_BUF_PURGEABLE);

    PM_HILOG_INFO_C(LOG_CORE, "%{public}s: succ, fd:%{public}d size:%{public}zu isSupport:%{public}d",
        __func__, purgObj->dmaBufFd, purgObj->dataSizeInput, purgObj->isSupport);
    return true;
}

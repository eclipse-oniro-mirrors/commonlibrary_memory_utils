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

#include <stddef.h> /* NULL */
#include <sys/mman.h> /* mmap */
#include <sched.h> /* sched_yield() */

#include "hilog/log.h"
#include "ux_page_table.h"

#undef LOG_TAG
#define LOG_TAG "libpurgeablemem: uxpt"

#if (USE_UXPT == true)  /* (USE_UXPT == true) means using uxpt */

struct UxPageTable {
    uint64_t dataAddr;
    size_t dataSize;
    uxpte_t *uxpte;
};

/*
 * -------------------------------------------------------------------------
 * |         virtual page number                |                           |
 * |--------------------------------------------| vaddr offset in virt page |
 * | uxpte page number |  offset in uxpte page  |                           |
 * --------------------------------------------------------------------------
 * |                   |  UXPTE_PER_PAGE_SHIFT  |        PAGE_SHIFT         |
 */
static const size_t PAGE_SHIFT = 12;
static const size_t PAGE_SIZE = 1 << PAGE_SHIFT;
static const size_t UXPTE_SIZE_SHIFT = 3;
static const size_t UXPTE_PER_PAGE_SHIFT = PAGE_SHIFT - UXPTE_SIZE_SHIFT;
static const size_t UXPTE_PER_PAGE = 1 << UXPTE_PER_PAGE_SHIFT;

/* get virtual page number from virtual address */
static inline uint64_t VirtPageNo_(uint64_t vaddr)
{
    return vaddr >> PAGE_SHIFT;
}

/* page number in user page table of uxpte for virtual address */
static inline uint64_t UxptePageNo_(uint64_t vaddr)
{
    return VirtPageNo_(vaddr) >> UXPTE_PER_PAGE_SHIFT;
}

/* uxpte offset in uxpte page for virtual address */
static inline uint64_t UxpteOffset_(uint64_t vaddr)
{
    return VirtPageNo_(vaddr) & (UXPTE_PER_PAGE - 1);
}

static const size_t UXPTE_PRESENT_BIT = 1;
static const size_t UXPTE_PRESENT_MASK = (1 << UXPTE_PRESENT_BIT) - 1;
static const size_t UXPTE_REFCNT_ONE = 1 << UXPTE_PRESENT_BIT;
static uxpte_t UXPTE_UNDER_RECLAIM = (uxpte_t)(-UXPTE_REFCNT_ONE);

static inline bool IsUxptePresent_(uxpte_t pte)
{
    return pte & (uxpte_t)UXPTE_PRESENT_MASK;
}

static inline bool IsUxpteUnderReclaim_(uxpte_t pte)
{
    return pte == UXPTE_UNDER_RECLAIM;
}

static inline size_t GetUxPageSize_(uint64_t dataAddr, size_t dataSize)
{
    return (UxptePageNo_(dataAddr + dataSize - 1) - UxptePageNo_(dataAddr) + 1) * PAGE_SIZE;
}

static inline uint64_t RoundUp_(uint64_t val, size_t align)
{
    if (align == 0) {
        return val;
    }
    return ((val + align - 1) / align) * align;
}

static inline uint64_t RoundDown_(uint64_t val, size_t align)
{
    if (align == 0) {
        return val;
    }
    return val & (~(align - 1));
}

enum UxpteOp {
    UPT_GET = 0,
    UPT_PUT = 1,
    UPT_IS_PRESENT = 2,
};

static void UxpteAdd_(uxpte_t *pte, size_t incNum);
static void UxpteSub_(uxpte_t *pte, size_t decNum);

static void GetUxpteAt_(struct UxPageTable *upt, uint64_t addr);
static void PutUxpteAt_(struct UxPageTable *upt, uint64_t addr);
static bool IsPresentAt_(struct UxPageTable *upt, uint64_t addr);
static PMState UxpteOps_(struct UxPageTable *upt, uint64_t addr, size_t len, enum UxpteOp op);

static uxpte_t *MapUxptePages_(uint64_t dataAddr, size_t dataSize);
static int UnmapUxptePages_(uxpte_t *ptes, size_t size);

bool UxpteIsEnabled(void)
{
    return true;
}

size_t UxPageTableSize(void)
{
    return sizeof(struct UxPageTable);
}

PMState InitUxPageTable(struct UxPageTable *upt, void *addr, size_t len)
{
    upt->dataAddr = (uint64_t) addr;
    upt->dataSize = len;
    upt->uxpte = MapUxptePages_(upt->dataAddr, upt->dataSize);
    if (!(upt->uxpte)) {
        return PM_MMAP_UXPT_FAIL;
    }
    return PM_OK;
}

PMState DeinitUxPageTable(struct UxPageTable *upt)
{
    size_t size = GetUxPageSize_(upt->dataAddr, upt->dataSize);
    int unmapRet = 0;
    if (upt->uxpte) {
        unmapRet = UnmapUxptePages_(upt->uxpte, size);
        if (unmapRet) {
            HILOG_ERROR(LOG_CORE, "%{public}s: unmap uxpt fail", __func__);
            return PM_UNMAP_UXPT_FAIL;
        }
        upt->uxpte = NULL;
    }
    upt->dataAddr = 0;
    upt->dataSize = 0;
    return PM_OK;
}

void UxpteGet(struct UxPageTable *upt, void *addr, size_t len)
{
    UxpteOps_(upt, (uint64_t)addr, len, UPT_GET);
}

void UxptePut(struct UxPageTable *upt, void *addr, size_t len)
{
    UxpteOps_(upt, (uint64_t)addr, len, UPT_PUT);
}

bool UxpteIsPresent(struct UxPageTable *upt, void *addr, size_t len)
{
    PMState ret = UxpteOps_(upt, (uint64_t)addr, len, UPT_IS_PRESENT);
    return ret == PM_OK;
}

static void UxpteAdd_(uxpte_t *pte, size_t incNum)
{
    uxpte_t old = 0, ret = 0;

    while (true) {
        old = *pte;
        if (IsUxpteUnderReclaim_(old)) {
            sched_yield();
            continue;
        }

        ret = __sync_val_compare_and_swap(pte, old, old + incNum);
        if (ret == old)
            break;
    }
}

static void UxpteSub_(uxpte_t *pte, size_t decNum)
{
    (void)__sync_fetch_and_sub(pte, decNum);
}

static inline size_t GetIndexInUxpte_(uint64_t startAddr, uint64_t currAddr)
{
    return UxpteOffset_(startAddr) + (VirtPageNo_(currAddr) - VirtPageNo_(startAddr));
}

static void GetUxpteAt_(struct UxPageTable *upt, uint64_t addr)
{
    size_t index = GetIndexInUxpte_(upt->dataAddr, addr);
    UxpteAdd_(&(upt->uxpte[index]), UXPTE_REFCNT_ONE);

    HILOG_DEBUG(LOG_CORE, "%{public}s: addr(0x%{public}llx) upte=0x%{public}llx",
        __func__, (unsigned long long)addr, (unsigned long long)(upt->uxpte[index]));
}

static void PutUxpteAt_(struct UxPageTable *upt, uint64_t addr)
{
    size_t index = GetIndexInUxpte_(upt->dataAddr, addr);
    UxpteSub_(&(upt->uxpte[index]), UXPTE_REFCNT_ONE);

    HILOG_DEBUG(LOG_CORE, "%{public}s: addr(0x%{public}llx) upte=0x%{public}llx",
        __func__, (unsigned long long)addr, (unsigned long long)(upt->uxpte[index]));
}

static bool IsPresentAt_(struct UxPageTable *upt, uint64_t addr)
{
    size_t index = GetIndexInUxpte_(upt->dataAddr, addr);

    HILOG_DEBUG(LOG_CORE, "%{public}s: addr(0x%{public}llx) upte=0x%{public}llx PRESENT_MASK=0x%{public}zx",
        __func__, (unsigned long long)addr, (unsigned long long)(upt->uxpte[index]), UXPTE_PRESENT_MASK);

    return IsUxptePresent_(upt->uxpte[index]);
}

static PMState UxpteOps_(struct UxPageTable *upt, uint64_t addr, size_t len, enum UxpteOp op)
{
    uint64_t start =  RoundDown_(addr, PAGE_SIZE);
    uint64_t end = RoundUp_(addr + len, PAGE_SIZE);
    if (start < upt->dataAddr || end > (upt->dataAddr + upt->dataSize)) {
        HILOG_ERROR(LOG_CORE, "%{public}s: addr(0x%{public}llx) start(0x%{public}llx) < dataAddr(0x%{public}llx)"
            " || end(0x%{public}llx) > dataAddr+dataSize(0x%{public}llx) out of bound",
            __func__, (unsigned long long)addr, (unsigned long long)start, (unsigned long long)(upt->dataAddr),
            (unsigned long long)end, (unsigned long long)(upt->dataAddr + upt->dataSize));

        return PM_UXPT_OUT_RANGE;
    }

    for (uint64_t off = start; off < end; off += PAGE_SIZE) {
        switch (op) {
            case UPT_GET: {
                GetUxpteAt_(upt, off);
                break;
            }
            case UPT_PUT: {
                PutUxpteAt_(upt, off);
                break;
            }
            case UPT_IS_PRESENT: {
                if (!IsPresentAt_(upt, off)) {
                    HILOG_ERROR(LOG_CORE, "%{public}s: addr(0x%{public}llx) not present", __func__,
                        (unsigned long long)addr);
                    return PM_UXPT_NO_PRESENT;
                }
                break;
            }
            default:
                break;
        }
    }

    return PM_OK;
}

static uxpte_t *MapUxptePages_(uint64_t dataAddr, size_t dataSize)
{
    int prot = PROT_READ | PROT_WRITE;
    int type = MAP_PRIVATE | MAP_ANONYMOUS | MAP_USEREXPTE;
    size_t size = GetUxPageSize_(dataAddr, dataSize);
    uxpte_t *ptes = (uxpte_t*)mmap(NULL, size, prot, type, -1, UxptePageNo_(dataAddr) * PAGE_SIZE);
    if (ptes == (void *)-1) {
        HILOG_ERROR(LOG_CORE, "%{public}s: fail, return NULL", __func__);
        ptes = NULL;
    }

    return ptes;
}

static int UnmapUxptePages_(uxpte_t *ptes, size_t size)
{
    return munmap(ptes, size);
}

#else /* (USE_UXPT == false), it means does not using uxpt */

bool UxpteIsEnabled(void)
{
    return false;
}

size_t UxPageTableSize(void)
{
    return 0;
}

PMState InitUxPageTable(struct UxPageTable *upt, void *addr, size_t len)
{
    return PM_OK;
}

PMState DeinitUxPageTable(struct UxPageTable *upt)
{
    return PM_OK;
}

void UxpteGet(struct UxPageTable *upt, void *addr, size_t len) {}

void UxptePut(struct UxPageTable *upt, void *addr, size_t len) {}

bool UxpteIsPresent(struct UxPageTable *upt, void *addr, size_t len)
{
    return true;
}

#endif /* USE_UXPT == true */

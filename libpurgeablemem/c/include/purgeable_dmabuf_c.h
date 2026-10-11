/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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

#ifndef OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_C_INCLUDE_PURGEABLE_DMABUF_C_H
#define OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_C_INCLUDE_PURGEABLE_DMABUF_C_H

#include <sys/ioctl.h> /* _IO, _IOW */
#include <linux/dma-buf.h> /* DMA_BUF_BASE, __u32 */

#ifndef DMA_BUF_BASE
#define DMA_BUF_BASE 'b'
#endif

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* End of #if __cplusplus */
#endif /* End of #ifdef __cplusplus */

/*
 * Purgeable extension ioctls of dma-buf, all of them work on a dma-buf fd.
 * Note: the kernel UAPI names these structs dma_buf_purgeable_pin and
 * dma_buf_purgeable_ino; they are renamed to CamelCase for the code style
 * rule and the layouts are identical. Remove the definitions below once a
 * future kernel UAPI header provides them.
 *
 * offset/len are reserved for future range-based pin/unpin. The current kernel
 * implementation (linux-6.6-lts) ignores them — DMA_BUF_PURGEABLE_PIN/UNPIN
 * always operate on the whole buffer regardless of these values. Pass {0, 0}.
 */
struct DmaBufPurgeablePin {
    __u32 offset; /* reserved: offset of the range to be pinned/unpinned */
    __u32 len; /* reserved: len of the range to be pinned/unpinned, 0 means to the end */
};

/* set the dma-buf purgeable */
#ifndef DMA_BUF_SET_PURGEABLE
#define DMA_BUF_SET_PURGEABLE _IO(DMA_BUF_BASE, 9)
#endif

/* return 1 if the dma-buf has been set purgeable */
#ifndef DMA_BUF_GET_PURGEABLE
#define DMA_BUF_GET_PURGEABLE _IO(DMA_BUF_BASE, 10)
#endif

/* pin the dma-buf, return 1 if the dma-buf was purged before this pin */
#ifndef DMA_BUF_PURGEABLE_PIN
#define DMA_BUF_PURGEABLE_PIN _IOW(DMA_BUF_BASE, 11, struct DmaBufPurgeablePin)
#endif

/* unpin the dma-buf */
#ifndef DMA_BUF_PURGEABLE_UNPIN
#define DMA_BUF_PURGEABLE_UNPIN _IOW(DMA_BUF_BASE, 12, struct DmaBufPurgeablePin)
#endif

/* get pin status of the dma-buf */
#ifndef DMA_BUF_GET_PIN_STATUS
#define DMA_BUF_GET_PIN_STATUS _IO(DMA_BUF_BASE, 13)
#endif

/* return value > 0 means the dma-buf has been purged */
#ifndef DMA_BUF_PURGEABLE_IS_PURGED
#define DMA_BUF_PURGEABLE_IS_PURGED _IO(DMA_BUF_BASE, 14)
#endif

/* tell kernel the purged dma-buf has been rebuilt */
#ifndef DMA_BUF_PURGEABLE_REBUILD_SUCCESS
#define DMA_BUF_PURGEABLE_REBUILD_SUCCESS _IO(DMA_BUF_BASE, 15)
#endif

/* get dmabuf internal inode number */
#ifndef DMA_BUF_PURGEABLE_GET_INO
struct DmaBufPurgeableIno {
    __u64 ino;
};
#define DMA_BUF_PURGEABLE_GET_INO _IOR(DMA_BUF_BASE, 16, struct DmaBufPurgeableIno)
#endif

/* return value of DMA_BUF_GET_PURGEABLE: the dma-buf is purgeable */
#define PURGEABLE_DMA_BUF_PURGEABLE 1

/*
 * Heap flag of the purgeable DMA buffer allocation: must be set so that the
 * kernel records UVA info (record_buffer_uva_info) during mmap, enabling
 * try_to_unmap_uva to zap pages during purge. Without this flag,
 * buffer_info_get returns NULL and purge fails with -EINVAL (-22).
 * Real apps (sceneboard/PixelMap) allocate via system_heap_third which
 * auto-sets this flag.
 */
#ifndef DMA_HEAP_FLAG_DIRECT_RECLAIM
#define DMA_HEAP_FLAG_DIRECT_RECLAIM (1ULL << 6)
#endif

/*
 * Bridge of the purgeable extension ioctls of dma-buf.
 * These functions are implemented in C++, but exported as "C" functions,
 * so both C and C++ code of libpurgeablemem can call them.
 * All of them return the raw ioctl return value.
 */

/*
 * PurgDmaBufSetPurgeable: set the dma-buf @dmabufFd purgeable.
 * Input:   @dmabufFd: fd of the dma-buf.
 * Return:  the raw ioctl return value, 0 means success.
 */
int PurgDmaBufSetPurgeable(int dmabufFd);

/*
 * PurgDmaBufGetPurgeable: get purgeable state of the dma-buf @dmabufFd.
 * Input:   @dmabufFd: fd of the dma-buf.
 * Return:  the raw ioctl return value, 1 means the dma-buf is purgeable.
 */
int PurgDmaBufGetPurgeable(int dmabufFd);

/*
 * PurgDmaBufPin: pin the dma-buf @dmabufFd, kernel cannot reclaim it while pinned.
 * Input:   @dmabufFd: fd of the dma-buf.
 * Input:   @pin: pin range, {0, 0} for the whole buffer (current kernel always pins the whole buffer).
 * Return:  the raw ioctl return value, 1 means the dma-buf was purged before this pin.
 */
int PurgDmaBufPin(int dmabufFd, struct DmaBufPurgeablePin *pin);

/*
 * PurgDmaBufUnpin: unpin the dma-buf @dmabufFd, kernel may reclaim it later.
 * Input:   @dmabufFd: fd of the dma-buf.
 * Input:   @pin: unpin range, {0, 0} for the whole buffer (current kernel always unpins the whole buffer).
 * Return:  the raw ioctl return value, 0 means success.
 */
int PurgDmaBufUnpin(int dmabufFd, struct DmaBufPurgeablePin *pin);

/*
 * PurgDmaBufGetPinStatus: get pin status of the dma-buf @dmabufFd.
 * Input:   @dmabufFd: fd of the dma-buf.
 * Return:  the raw ioctl return value.
 */
int PurgDmaBufGetPinStatus(int dmabufFd);

/*
 * PurgDmaBufIsPurged: check whether the dma-buf @dmabufFd has been purged.
 * Input:   @dmabufFd: fd of the dma-buf.
 * Return:  the raw ioctl return value, value > 0 means purged.
 */
int PurgDmaBufIsPurged(int dmabufFd);

/*
 * PurgDmaBufRebuildSuccess: tell kernel the purged dma-buf @dmabufFd has been rebuilt.
 * Input:   @dmabufFd: fd of the dma-buf.
 * Return:  the raw ioctl return value, 0 means success.
 */
int PurgDmaBufRebuildSuccess(int dmabufFd);

#ifdef __cplusplus
#if __cplusplus
}
#endif /* End of #if __cplusplus */
#endif /* End of #ifdef __cplusplus */

#endif /* OHOS_UTILS_MEMORY_LIBPURGEABLEMEM_C_INCLUDE_PURGEABLE_DMABUF_C_H */

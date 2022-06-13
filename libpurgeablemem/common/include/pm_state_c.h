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

#ifndef OHOS_UTILS_MEMOEY_LIBPURGEABLEMEM_COMMON_INCLUDE_PM_STATE_C_H
#define OHOS_UTILS_MEMOEY_LIBPURGEABLEMEM_COMMON_INCLUDE_PM_STATE_C_H

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* End of #if __cplusplus */
#endif /* End of #ifdef __cplusplus */

/*
 * PMState: Inner state.
 * It may be used in C or CPP code, so using "typedef enum" for common cases.
 */
typedef enum {
    PM_OK = 0,
    PM_BUILDER_NULL,
    PM_MMAP_PURG_FAIL,
    PM_MMAP_UXPT_FAIL,
    PM_UNMAP_PURG_SUCC,
    PM_UNMAP_PURG_FAIL,
    PM_UNMAP_UXPT_FAIL,
    PM_UXPT_OUT_RANGE,
    PM_UXPT_PRESENT_DATA_PURGED,
    PM_UXPT_NO_PRESENT,
    PM_LOCK_INIT_FAIL,
    PM_LOCK_READ_SUCC,
    PM_LOCK_READ_FAIL,
    PM_LOCK_WRITE_FAIL,
    PM_UNLOCK_READ_FAIL,
    PM_UNLOCK_WRITE_FAIL,
    PM_DATA_PURGED,
    PM_DATA_NO_PURGED,
    PMB_BUILD_ALL_SUCC,
    PMB_BUILD_ALL_FAIL,
    PMB_DESTORY_FAIL, /* builder destory failed */
    PM_ERR_TYPES,
} PMState; /* purgeable mem errno */

/*
 * PMStateName: Inner func, get name of PMState @state.
 * Input:   @state: PMState.
 * Return:  the name of PMState @state.
 */
const char *GetPMStateName(PMState state);

#ifdef __cplusplus
#if __cplusplus
}
#endif /* End of #if __cplusplus */
#endif /* End of #ifdef __cplusplus */

#endif /* OHOS_UTILS_MEMOEY_LIBPURGEABLEMEM_COMMON_INCLUDE_PM_STATE_C_H */

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

#include "pm_state_c.h"

#undef LOG_TAG
#define LOG_TAG "PurgeableMemC: state"

static const char * const PMStateNames[PM_ERR_TYPES] = {
    "OK",
    "BuilderPtrNULL",
    "MmapPurgMemFail",
    "MmapUxptFail",
    "UnmapPurgMemSucc",
    "UnmapPurgMemFail",
    "UnmapUxptFail",
    "UxpteOutOfRange",
    "UxptePresentDataPurged",
    "UxpteNoPresent",
    "LockInitFail",
    "ReadLockSucc",
    "ReadLockFail",
    "WriteLockFail",
    "ReadUnlockFail",
    "WriteUnlockFail",
    "DataPurged",
    "DataNoPurged",
    "BuilderBuildAllSucc",
    "BuilderBuildAllFail",
    "BuilderDestoryFailed",
};

const char *GetPMStateName(PMState state)
{
    if (state < 0 || state >= PM_ERR_TYPES) {
        return "NONE";
    }
    return PMStateNames[state];
}

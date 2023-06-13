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

#ifndef LIB_MEM_INFO_H
#define LIB_MEM_INFO_H

#include <memory>
#include <string>

namespace OHOS {
namespace MemInfo {
// get rss refer to memmgr
uint64_t GetRssByPid(const int pid);

// get pss from smaps_rollup, include graphics memory
uint64_t GetPssByPid(const int pid);

// get graphics memory from hdi
bool GetGraphicsMemory(const int pid, uint64_t &gl, uint64_t &graph);
} /* namespace MemInfo */
} /* namespace OHOS */
#endif /* LIB_MEM_INFO_H */
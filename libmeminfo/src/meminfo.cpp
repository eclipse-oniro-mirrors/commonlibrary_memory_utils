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

#include <fstream>
#include <sstream>
#include <v1_0/imemory_tracker_interface.h>

#include "file_ex.h" // LoadStringFromFile
#include "hilog/log.h"
#include "meminfo.h"

namespace OHOS {
namespace MemInfo {
using namespace OHOS::HDI::Memorytracker::V1_0;
constexpr HiviewDFX::HiLogLabel LABEL = { LOG_CORE, 0xD001799, "MemInfo" };
constexpr int PAGE_TO_KB = 4;
constexpr int BYTE_PER_KB = 1024;

// get rss refer to memmgr
uint64_t GetRssByPid(const int pid)
{
    uint64_t size = 0;
    std::string stat;
    std::string statm;
    std::string statPid;
    std::string vss;
    std::string rss;
    std::string name;
    std::string status;
    std::string statPath = "/proc/" + std::to_string(pid) + "/stat";
    // format like:
    // 1 (init) S 0 0 0 0 -1 4210944 1 ...
    if (!OHOS::LoadStringFromFile(statPath, stat)) {
        HiviewDFX::HiLog::Error(LABEL, "stat file error!");
        return size;
    }
    std::istringstream isStat(stat);
    isStat >> statPid >> name >> status;

    if (statPid != std::to_string(pid)) {
        HiviewDFX::HiLog::Error(LABEL, "pid error!");
        return size;
    }

    std::string statmPath = "/proc/" + std::to_string(pid) + "/statm";
    // format like:
    // 640 472 369 38 0 115 0
    if (!OHOS::LoadStringFromFile(statmPath, statm)) {
        HiviewDFX::HiLog::Error(LABEL, "statm file error!");
        return size;
    }
    std::istringstream isStatm(statm);
    isStatm >> vss >> rss; // pages

    size = static_cast<uint64_t>(atoi(rss.c_str()) * PAGE_TO_KB);
    return size;
}

// get pss from smaps_rollup, include graphics memory
uint64_t GetPssByPid(const int pid)
{
    uint64_t size = 0;
    std::string filename = "/proc/" + std::to_string(pid) + "/smaps_rollup";
    std::ifstream in(filename);
    if (!in) {
        HiviewDFX::HiLog::Error(LABEL, "File %{public}s not found.\n", filename.c_str());
        return size;
    }

    std::string content;
    while (getline(in, content)) {
        std::string::size_type typePos = content.find(":");
        if (typePos != content.npos) {
            std::string type = content.substr(0, typePos);
            if (type == "Pss") {
                std::string valueStr = content.substr(typePos + 1);
                const int base = 10;
                size = strtoull(valueStr.c_str(), nullptr, base);
                break;
            }
        }
    }
    in.close();

    uint64_t gl = 0;
    uint64_t graph = 0;
    bool ret = GetGraphicsMemory(pid, gl, graph);
    if (ret) {
        size += gl;
        size += graph;
    }
    return size;
}

// get graphics memory from hdi
bool GetGraphicsMemory(const int pid, uint64_t &gl, uint64_t &graph)
{
    bool ret = false;
    sptr<IMemoryTrackerInterface> memtrack = IMemoryTrackerInterface::Get(true);
    if (memtrack == nullptr) {
        HiviewDFX::HiLog::Error(LABEL, "memtrack service is null");
        return ret;
    }
    const std::vector<std::pair<MemoryTrackerType, std::string>> MEMORY_TRACKER_TYPES = {
        {MEMORY_TRACKER_TYPE_GL, "GL"}, {MEMORY_TRACKER_TYPE_GRAPH, "Graph"},
        {MEMORY_TRACKER_TYPE_OTHER, "Other"}
    };

    for (const auto &memTrackerType : MEMORY_TRACKER_TYPES) {
        std::vector<MemoryRecord> records;
        if (memtrack->GetDevMem(pid, memTrackerType.first, records) != HDF_SUCCESS) {
            continue;
        }
        uint64_t value = 0;
        for (const auto &record : records) {
            if ((static_cast<uint32_t>(record.flags) & FLAG_UNMAPPED) == FLAG_UNMAPPED) {
                value = static_cast<uint64_t>(record.size / BYTE_PER_KB);
                break;
            }
        }
        if (memTrackerType.first == MEMORY_TRACKER_TYPE_GL) {
            gl = value;
            ret = true;
        } else if (memTrackerType.first == MEMORY_TRACKER_TYPE_GRAPH) {
            graph = value;
            ret = true;
        }
    }
    return ret;
}
} /* namespace MemInfo */
} /* namespace OHOS */

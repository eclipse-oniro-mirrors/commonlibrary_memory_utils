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

#include "meminfo.h"

#include <fstream>
#include <sstream>
#include <v1_0/imemory_tracker_interface.h>

#include "file_ex.h" // LoadStringFromFile
#include "hilog/log.h"

#undef LOG_TAG
#define LOG_TAG "MemInfo"

#undef LOG_DOMAIN
#define LOG_DOMAIN 0xD001799


namespace OHOS {
namespace MemInfo {
using namespace OHOS::HDI::Memorytracker::V1_0;
constexpr int PAGE_TO_KB = 4;
constexpr int BYTE_PER_KB = 1024;

// get Rss from statm
uint64_t GetRssByPid(const int pid)
{
    uint64_t size = 0;
    std::string statm;
    std::string vss;
    std::string rss;

    std::string statmPath = "/proc/" + std::to_string(pid) + "/statm";
    // format like:
    // 640 472 369 38 0 115 0
    if (!OHOS::LoadStringFromFile(statmPath, statm)) {
        HILOG_ERROR(LOG_CORE, "statm file error!");
        return size;
    }
    std::istringstream isStatm(statm);
    isStatm >> vss >> rss; // pages

    size = static_cast<uint64_t>(atoi(rss.c_str()) * PAGE_TO_KB);
    return size;
}

// get Pss from smaps_rollup
uint64_t GetPssByPid(const int pid)
{
    uint64_t size = 0;
    std::string filename = "/proc/" + std::to_string(pid) + "/smaps_rollup";
    std::ifstream in(filename);
    if (!in) {
        HILOG_ERROR(LOG_CORE, "File %{public}s not found.\n", filename.c_str());
        return size;
    }

    std::string content;
    while (in.good() && getline(in, content)) {
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
    return size;
}

// get SwapPss from smaps_rollup
uint64_t GetSwapPssByPid(const int pid)
{
    uint64_t size = 0;
    std::string filename = "/proc/" + std::to_string(pid) + "/smaps_rollup";
    std::ifstream in(filename);
    if (!in) {
        HILOG_ERROR(LOG_CORE, "File %{public}s not found.\n", filename.c_str());
        return size;
    }

    std::string content;
    while (in.good() && getline(in, content)) {
        std::string::size_type typePos = content.find(":");
        if (typePos != content.npos) {
            std::string type = content.substr(0, typePos);
            if (type == "SwapPss") {
                std::string valueStr = content.substr(typePos + 1);
                const int base = 10;
                size = strtoull(valueStr.c_str(), nullptr, base);
                break;
            }
        }
    }
    in.close();
    return size;
}

// get graphics memory from hdi
bool GetGraphicsMemory(const int pid, uint64_t &gl, uint64_t &graph)
{
    bool ret = false;
    sptr<IMemoryTrackerInterface> memtrack = IMemoryTrackerInterface::Get(true);
    if (memtrack == nullptr) {
        HILOG_ERROR(LOG_CORE, "memtrack service is null");
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

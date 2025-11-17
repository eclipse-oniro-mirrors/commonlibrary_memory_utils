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
#include <vector>
#include <iostream>

namespace OHOS {
namespace MemInfo {
constexpr int MAX_STRING_LEN = 256;
struct DmaNodeInfo {
    char process[MAX_STRING_LEN];
    int process_size;
    int pid;
    int fd;
    int64_t size_bytes;
    int64_t ino;
    int exp_pid;
    char exp_task_comm[MAX_STRING_LEN];
    int exp_task_comm_size;
    char buf_name[MAX_STRING_LEN];
    int buf_name_size;
    char exp_name[MAX_STRING_LEN];
    int exp_name_size;
    bool can_reclaim;
    bool is_reclaim;
    char buf_type[MAX_STRING_LEN];
    int buf_type_size;
    char reclaim_info[MAX_STRING_LEN];
    int reclaim_info_size;
    char leak_type[MAX_STRING_LEN];
    int leak_type_size;
};

struct DmaNodeInfoWrapper {
    std::string process;
    int pid;
    int fd;
    int64_t size_bytes;
    int64_t ino;
    int exp_pid;
    std::string exp_task_comm;
    std::string buf_name;
    std::string exp_name;
    bool can_reclaim;
    bool is_reclaim;
    std::string buf_type;
    std::string reclaim_info;
    std::string leak_type;

    void print() const
    {
        std::cout << "process=" << process
              << ", pid=" << pid
              << ", fd=" << fd
              << ", size_bytes=" << size_bytes
              << ", ino=" << ino
              << ", exp_pid=" << exp_pid
              << ", exp_task_comm=" << exp_task_comm
              << ", buf_name=" << buf_name
              << ", exp_name=" << exp_name
              << ", can_reclaim=" << can_reclaim
              << ", is_reclaim=" << is_reclaim
              << ", buf_type=" << buf_type
              << ", reclaim_info=" << reclaim_info
              << ", leak_type=" << leak_type
              << std::endl;
    }
};

// get deduplicated DMA information
std::vector<DmaNodeInfoWrapper> GetDmaInfo(int pid);

// get Rss from statm
uint64_t GetRssByPid(const int pid);

// get Pss from smaps_rollup
uint64_t GetPssByPid(const int pid);

// get SwapPss from smaps_rollup
uint64_t GetSwapPssByPid(const int pid);

// get graphics memory from hdi
bool GetGraphicsMemory(const int pid, uint64_t &gl, uint64_t &graph);
} /* namespace MemInfo */
} /* namespace OHOS */
#endif /* LIB_MEM_INFO_H */
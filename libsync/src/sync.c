/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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
#include "sync.h"
#include <errno.h>
#include <poll.h>
#include <stdio.h>

int SyncWait(int fileDescriptor, int timeout)
{
    if (fileDescriptor < 0) {
        errno = EINVAL;
        return -1;
    }

    struct pollfd pfd = { .fd = fileDescriptor, .events = POLLIN };
    int pollResult;
    
    while (1) {
        pollResult = poll(&pfd, 1, timeout);

        if (pollResult > 0) {
            if (pfd.revents & (POLLERR | POLLNVAL)) {
                errno = EINVAL;
                return -1;
            }
            return 0;
        } else if (pollResult == 0) {
            errno = ETIME;
            return -1;
        } else if (pollResult != -1 || (errno != EINTR && errno != EAGAIN)) {
            break;
        }
    }
    return pollResult;
}

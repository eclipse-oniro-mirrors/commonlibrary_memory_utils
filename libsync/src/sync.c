/*
 * Copyright 2012 Google, Inc
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

int SyncWait(int num, int time)
{
    struct pollfd work;
    int result;

    if (num < 0) {
        errno = EINVAL;
        return -1;
    }

    work.fd = num;
    work.events = POLLIN;

    do {
        result = poll(&work, 1, time);
        if (result > 0) {
            if (work.revents & (POLLERR | POLLNVAL)) {
                errno = EINVAL;
                return -1;
            }
            return 0;
        } else if (result == 0) {
            errno = ETIME;
            return -1;
        }
    } while (result == -1 && (errno == EINTR || errno == EAGAIN));

    return result;
}

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

#include <stdbool.h> /* bool */
#include <stddef.h> /* NULL */
#include <stdlib.h> /* malloc */

#include "hilog/log.h"
#include "pm_ptr_util.h"
#include "pm_builder.h"

#undef LOG_TAG
#define LOG_TAG "libpurgeablemem: builder"

/* purgeable mem builder */
struct PurgMemBuilder {
    struct PurgMemBuilder *nextBuilder;
    PurgMemBuilderFunc Build;
    void *param;
    const char *name;
};

/* append a guest builder @newcomer to @head */
static void AppendBuilder_(struct PurgMemBuilder *head, struct PurgMemBuilder *newcomer);

struct PurgMemBuilder *PurgMemBuilderCreate(PurgMemBuilderFunc func, void *param, const char *name)
{
    IF_NULL_LOG_ACTION(func, "func is NULL", return NULL);

    struct PurgMemBuilder *builder = NULL;
    builder = (struct PurgMemBuilder *)malloc(sizeof(struct PurgMemBuilder));
    if (!builder) {
        HILOG_ERROR(LOG_CORE, "%{public}s: malloc struct PurgMemBuilder failed", __func__);
        return NULL;
    }
    builder->Build = func;
    builder->nextBuilder = NULL;
    builder->param = param;
    builder->name = name;
    return builder;
}

bool PurgMemBuilderDestroy(struct PurgMemBuilder *builder)
{
    IF_NULL_LOG_ACTION(builder, "builder is NULL", return true);

    struct PurgMemBuilder *curr = builder;
    struct PurgMemBuilder *next = NULL;
    while (curr) {
        next = curr->nextBuilder;
        free(curr);
        curr = next;
    }
    /* set input para NULL to avoid UAF */
    builder = NULL;
    return true;
}

bool PurgMemBuilderAppendFunc(struct PurgMemBuilder *builder, PurgMemBuilderFunc func, void *param,
                              const char *name)
{
    IF_NULL_LOG_ACTION(builder, "input builder is NULL", return false);
    IF_NULL_LOG_ACTION(func, "input func is NULL", return false);

    struct PurgMemBuilder *newcomer = PurgMemBuilderCreate(func, param, name);
    IF_NULL_LOG_ACTION(newcomer, "create new builder failed", return false);

    AppendBuilder_(builder, newcomer);
    return true;
}

/* build @data content from @builder */
bool PurgMemBuilderBuildAll(struct PurgMemBuilder *builder, void *data, size_t size)
{
    if (!builder->Build) {
        HILOG_ERROR(LOG_CORE, "builder has no Build(), %{public}s", builder->name);
        return true;
    }
    if (!builder->Build(data, size, builder->param)) {
        HILOG_ERROR(LOG_CORE, "build data failed, name %{public}s", builder->name ?: "NULL");
        return false;
    }
    if (!(builder->nextBuilder)) {
        return true;
    }
    return PurgMemBuilderBuildAll(builder->nextBuilder, data, size);
}

bool PurgMemBuilderAppendBuilder(struct PurgMemBuilder *builder, struct PurgMemBuilder *newcomer)
{
    IF_NULL_LOG_ACTION(builder, "input builder is NULL", return false);
    IF_NULL_LOG_ACTION(newcomer, "input newcomer is NULL", return false);

    AppendBuilder_(builder, newcomer);
    return true;
}

/* append a guest builder @newcomer to @head */
static void AppendBuilder_(struct PurgMemBuilder *head, struct PurgMemBuilder *newcomer)
{
    if (!head->nextBuilder) {
        head->nextBuilder = newcomer;
        return;
    }
    return AppendBuilder_(head->nextBuilder, newcomer);
}

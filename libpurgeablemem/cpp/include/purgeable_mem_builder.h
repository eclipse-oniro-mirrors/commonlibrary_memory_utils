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

#ifndef OHOS_UTILS_MEMOEY_LIBPURGEABLEMEM_CPP_INCLUDE_PURGEABLE_MEM_BUILDER_H
#define OHOS_UTILS_MEMOEY_LIBPURGEABLEMEM_CPP_INCLUDE_PURGEABLE_MEM_BUILDER_H

#include <memory> /* unique_ptr */

namespace OHOS {
namespace PurgeableMem {
/*
 * Class PurgeableMemBuilder is a base class of user's builder.
 * PurgeableMem users can define their builders by inheriting this class.
 * In its member func Build(), user should define how to build the content of a PurgeableMem obj.
 */
class PurgeableMemBuilder {
public:
    virtual ~PurgeableMemBuilder();

    /*
     * User should define how to build the content of a PurgeableMem obj in this func.
     * Input:   data: data ptr, ponits to start address of a PurgeableMem obj's content.
     * Input:   size: data size of the content.
     * Return:  build content result, true means success, while false is fail.
     */
    virtual bool Build(void *data, size_t size) = 0;

private:
    std::unique_ptr<PurgeableMemBuilder> nextBuilder_ = nullptr;

    /* Only called by its friend */
    void AppendBuilder(std::unique_ptr<PurgeableMemBuilder> builder);
    bool BuildAll(void *data, size_t size);
    friend class PurgeableMem;
};
} /* namespace PurgeableMem */
} /* namespace OHOS */
#endif /* OHOS_UTILS_MEMOEY_LIBPURGEABLEMEM_CPP_INCLUDE_PURGEABLE_MEM_BUILDER_H */

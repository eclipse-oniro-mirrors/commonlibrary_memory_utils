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
#ifndef OHOS_MEMORY_UTILS_PURGEABLE_PIXELMAP_BUILDER_H
#define OHOS_MEMORY_UTILS_PURGEABLE_PIXELMAP_BUILDER_H

#include "image_source.h"
#include "image_type.h"
#include "pixel_map.h"

#include "purgeable_ashmem.h"
#include "purgeable_mem_builder.h"
#include "purgeable_resource_manager.h"

#include "memory.h"

namespace OHOS {
namespace PurgeableBuilder {
using namespace OHOS::Media;

class PurgeablePixelMapBuilder : public PurgeableMem::PurgeableMemBuilder {
public:
    PurgeablePixelMapBuilder(uint32_t index, std::unique_ptr<ImageSource> &imageSource,
        DecodeOptions opts, PixelMap *pixelMap);

    bool Build(void *data, size_t size) override;

    ~PurgeablePixelMapBuilder() {}

private:
    uint32_t index_;
    DecodeOptions opts_;
    PixelMap *pixelMap_;
    std::unique_ptr<ImageSource> imageSource_;
}; // class PurgeablePixelMapBuilder

bool GetSysForPurgeable();
void SetBuilderToBePurgeable(std::unique_ptr<PixelMap> &pixelMap,
                             std::unique_ptr<PurgeableMem::PurgeableMemBuilder> &builder);
void RemoveFromPurgeableResourceMgr(std::shared_ptr<PixelMap> &pixelMap);
void AddToPurgeableResourceMgr(std::unique_ptr<PixelMap> &pixelMap);
bool MakePixelMapToBePurgeable(std::unique_ptr<PixelMap> &pixelMap,
    std::unique_ptr<ImageSource> &backupImgSrc4Rebuild, DecodeOptions &decodeOpts);
bool IfCanBePurgeable(DecodeOptions &decodeOpts);
} // namespace PurgeableBuilder
} // namespace OHOS
#endif /* OHOS_MEMORY_UTILS_PURGEABLE_PIXELMAP_BUILDER_H */
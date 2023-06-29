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

#include "purgeable_pixelmap_builder.h"

#include "hitrace_meter.h"
#include "hilog/log.h"
#include "parameters.h"
#include "purgeable_ashmem.h"
#include "purgeable_mem_base.h"
#include "purgeable_mem_builder.h"
#include "purgeable_resource_manager.h"

#ifndef _WIN32
#include "securec.h"
#else
#include "memory.h"
#endif

namespace OHOS {
namespace PurgeableBuilder {
constexpr HiviewDFX::HiLogLabel LABEL = { LOG_CORE, 0xD001799, "PurgeablePixelMapBuilder" };
constexpr int THRESHOLD_HEIGHT = 256;
constexpr int THRESHOLD_WIDGHT = 256;
const std::string SYSTEM_PARAM_PURGEABLE_ENABLE = "persist.resourceschedule.memmgr.purgeable.enable";
const std::string SYSTEM_PARAM_PIXELMAP_THRESHOLD_HEIGHT = "persist.memmgr.purgeable.pixelmap.threshold.height";
const std::string SYSTEM_PARAM_PIXELMAP_THRESHOLD_WIDGHT = "persist.memmgr.purgeable.pixelmap.threshold.widght";

PurgeablePixelMapBuilder::PurgeablePixelMapBuilder(uint32_t index, std::unique_ptr<ImageSource> &imageSource,
    DecodeOptions opts, PixelMap *pixelMap)
    : index_(index), opts_(opts), pixelMap_(pixelMap), imageSource_(move(imageSource)) {}

bool PurgeablePixelMapBuilder::Build(void *data, size_t size)
{
    HiviewDFX::HiLog::Debug(LABEL, "purgeableMem build in.");
    uint32_t errorCode;
    if (imageSource_ == nullptr) {
        return false;
    }

    StartTrace(HITRACE_TAG_ZIMAGE, "OHOS::PurgeableBuilder::PixelMapPurgeableMemBuilder::Build");
    std::unique_ptr<PixelMap> pixelMap = imageSource_->CreatePixelMap(index_, opts_, errorCode);
    if (pixelMap == nullptr || pixelMap_ == nullptr) {
        FinishTrace(HITRACE_TAG_ZIMAGE);
        return false;
    }

    StartTrace(HITRACE_TAG_ZIMAGE, ("OHOS::PurgeableBuilder::PixelMapPurgeableMemBuilder::CopyData " +
                                    std::to_string(size)));
    if (memcpy_s((char *)pixelMap_->GetPixels(), size, (char *)pixelMap->GetPixels(), size)) {
        FinishTrace(HITRACE_TAG_ZIMAGE);
        return false;
    }

    DoRebuildSuccessCallback();

    FinishTrace(HITRACE_TAG_ZIMAGE); // memcpy_s trace
    FinishTrace(HITRACE_TAG_ZIMAGE); // PixelMapPurgeableMemBuilder::Build trace

    return true;
}

bool GetSysForPurgeable()
{
    return system::GetBoolParameter(SYSTEM_PARAM_PURGEABLE_ENABLE, false);
}

void SetBuilderToBePurgeable(std::unique_ptr<PixelMap> &pixelMap,
                             std::unique_ptr<PurgeableMem::PurgeableMemBuilder> &builder)
{
    HiviewDFX::HiLog::Debug(LABEL, "set builder for purgeable pixelmap. allocatorType = %{public}d.",
                            pixelMap->GetAllocatorType());
    StartTrace(HITRACE_TAG_ZIMAGE, "OHOS::PurgeableBuilder::SetBuilderToBePurgeable");
    if (builder == nullptr) {
        FinishTrace(HITRACE_TAG_ZIMAGE);
        return;
    }

    if (pixelMap->GetAllocatorType() == AllocatorType::SHARE_MEM_ALLOC) {
        std::shared_ptr<OHOS::PurgeableMem::PurgeableAshMem> tmpPtr =
            std::make_shared<OHOS::PurgeableMem::PurgeableAshMem>(std::move(builder));
        bool isChanged = tmpPtr->ChangeAshmemData(pixelMap->GetCapacity(),
            *(static_cast<int *>(pixelMap->GetFd())), pixelMap->GetWritablePixels());
        if (isChanged) {
            pixelMap->SetPurgeableMemPtr(tmpPtr);
            pixelMap->GetPurgeableMemPtr()->BeginRead();
        } else {
            HiviewDFX::HiLog::Error(LABEL, "ChangeAshmemData fail.");
        }
    }

    FinishTrace(HITRACE_TAG_ZIMAGE);
}

void RemoveFromPurgeableResourceMgr(std::shared_ptr<PixelMap> &pixelMap)
{
    StartTrace(HITRACE_TAG_ZIMAGE, "OHOS::PurgeableBuilder::RemoveFromPurgeableResourceMgr");
    HiviewDFX::HiLog::Debug(LABEL, "remove pixelmap from PurgeableResourceMgr.");

    if (pixelMap->IsPurgeable()) {
        PurgeableMem::PurgeableResourceManager::GetInstance().RemoveResource(pixelMap->GetPurgeableMemPtr());
    }

    FinishTrace(HITRACE_TAG_ZIMAGE);
}

void AddToPurgeableResourceMgr(std::unique_ptr<PixelMap> &pixelMap)
{
    StartTrace(HITRACE_TAG_ZIMAGE, "OHOS::PurgeableBuilder::AddToPurgeableResourceMgr");
    HiviewDFX::HiLog::Debug(LABEL, "add pixelmap purgeablemem ptr to PurgeableResourceMgr");

    if (pixelMap->IsPurgeable()) {
        PurgeableMem::PurgeableResourceManager::GetInstance().AddResource(pixelMap->GetPurgeableMemPtr());
    }

    FinishTrace(HITRACE_TAG_ZIMAGE);
}

bool IfCanBePurgeable(DecodeOptions &decodeOpts)
{
    int thresholdHeight = system::GetIntParameter(SYSTEM_PARAM_PIXELMAP_THRESHOLD_HEIGHT, THRESHOLD_HEIGHT);
    int thresholdWidght = system::GetIntParameter(SYSTEM_PARAM_PIXELMAP_THRESHOLD_WIDGHT, THRESHOLD_WIDGHT);
    Size size = decodeOpts.desiredSize;

    if (size.height > thresholdHeight || size.width > thresholdWidght) {
        return false;
    }
    return true;
}

bool MakePixelMapToBePurgeable(std::unique_ptr<PixelMap> &pixelMap, std::unique_ptr<ImageSource> &backupImgSrc4Rebuild,
    DecodeOptions &decodeOpts)
{
    StartTrace(HITRACE_TAG_ZIMAGE, "OHOS::PurgeableBuilder::MakePixelMapToBePurgeable");
    HiviewDFX::HiLog::Debug(LABEL, "MakePixelMapToBePurgeable in.");

    if (!GetSysForPurgeable()) {
        FinishTrace(HITRACE_TAG_ZIMAGE);
        return false;
    }

    if (!IfCanBePurgeable(decodeOpts)) {
        FinishTrace(HITRACE_TAG_ZIMAGE);
        return false;
    }

    if (pixelMap == nullptr || backupImgSrc4Rebuild == nullptr) {
        HiviewDFX::HiLog::Error(LABEL, "PixelMap or backupImgSrc4Rebuild is null.");
        FinishTrace(HITRACE_TAG_ZIMAGE);
        return false;
    }

    if (pixelMap->IsPurgeable()) {
        HiviewDFX::HiLog::Error(LABEL, "PixelMap is already purgeable.");
        FinishTrace(HITRACE_TAG_ZIMAGE);
        return false;
    }

    std::unique_ptr<PurgeableMem::PurgeableMemBuilder> purgeableMemBuilder =
        std::make_unique<PurgeablePixelMapBuilder>(0, backupImgSrc4Rebuild, decodeOpts, pixelMap.get());
    SetBuilderToBePurgeable(pixelMap, purgeableMemBuilder);

    if (pixelMap->IsPurgeable()) {
        AddToPurgeableResourceMgr(pixelMap);
    }

    FinishTrace(HITRACE_TAG_ZIMAGE);
    return true;
}
} // namespace PurgeableBuilder
} // namespace OHOS
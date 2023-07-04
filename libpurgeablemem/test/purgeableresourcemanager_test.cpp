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

#include "gtest/gtest.h"

#define private public
#define protected public
#include "purgeable_resource_manager.h"
#undef private
#undef protected

namespace OHOS {
namespace PurgeableMem {
using namespace testing;
using namespace testing::ext;

class PurgeableResourceManagerTest : public testing::Test {
public:
    static void SetUpTestCase();
    static void TearDownTestCase();
    void SetUp();
    void TearDown();
};

void PurgeableResourceManagerTest::SetUpTestCase()
{
}

void PurgeableResourceManagerTest::TearDownTestCase()
{
}

void PurgeableResourceManagerTest::SetUp()
{
}

void PurgeableResourceManagerTest::TearDown()
{
}

HWTEST_F(PurgeableResourceManagerTest, VisitedTest, TestSize.Level1)
{
    std::shared_ptr<PurgeableMemBase> key = std::make_shared<PurgeableMemBase>();
    int32_t capacity = 1;
    LruCache lrucache;
    lrucache.SetCapacity(capacity);
    lrucache.Visited(nullptr);
    lrucache.Visited(key);
    lrucache.Insert(key);
    EXPECT_EQ(lrucache.Size(), 1);
    lrucache.Visited(key);
    lrucache.Clear();
    EXPECT_EQ(lrucache.Size(), 0);
}

HWTEST_F(PurgeableResourceManagerTest, InsertTest, TestSize.Level1)
{
    std::shared_ptr<PurgeableMemBase> key = std::make_shared<PurgeableMemBase>();
    std::shared_ptr<PurgeableMemBase> key1 = std::make_shared<PurgeableMemBase>();
    int32_t capacity = 1;
    LruCache lrucache;
    lrucache.SetCapacity(capacity);
    lrucache.Insert(nullptr);
    lrucache.Insert(key);
    lrucache.Insert(key);
    lrucache.Insert(key1);
    EXPECT_EQ(lrucache.Size(), 1);
    lrucache.Visited(key);
    lrucache.Clear();
    EXPECT_EQ(lrucache.Size(), 0);
}

HWTEST_F(PurgeableResourceManagerTest, EraseTest, TestSize.Level1)
{
    std::shared_ptr<PurgeableMemBase> key = std::make_shared<PurgeableMemBase>();
    int32_t capacity = 1;
    LruCache lrucache;
    lrucache.SetCapacity(capacity);
    lrucache.Clear();
    lrucache.Erase(nullptr);
    lrucache.Erase(key);
    EXPECT_EQ(lrucache.Size(), 0);
    lrucache.Insert(key);
    EXPECT_EQ(lrucache.Size(), 1);
    lrucache.Erase(key);
    EXPECT_EQ(lrucache.Size(), 0);
}

HWTEST_F(PurgeableResourceManagerTest, SetCapacityTest, TestSize.Level1)
{
    std::shared_ptr<PurgeableMemBase> key = std::make_shared<PurgeableMemBase>();
    std::shared_ptr<PurgeableMemBase> key1 = std::make_shared<PurgeableMemBase>();
    int32_t capacity = -1;
    LruCache lrucache;
    lrucache.SetCapacity(capacity);
    capacity = MAX_LRU_CACHE_CAPACITY + 1;
    lrucache.SetCapacity(capacity);
    capacity = 2;
    lrucache.SetCapacity(capacity);
    lrucache.Erase(key);
    lrucache.Erase(key1);
    capacity = 1;
    lrucache.SetCapacity(capacity);
    lrucache.Clear();
    EXPECT_EQ(lrucache.Size(), 0);
}

HWTEST_F(PurgeableResourceManagerTest, BeginAccessPurgeableMemTest, TestSize.Level1)
{
    std::shared_ptr<PurgeableMemBase> key = std::make_shared<PurgeableMemBase>();
    PurgeableResourceManager::GetInstance().BeginAccessPurgeableMem();
    PurgeableResourceManager::GetInstance().lruCache_.Insert(key);
    PurgeableResourceManager::GetInstance().BeginAccessPurgeableMem();
    PurgeableResourceManager::GetInstance().isThreadPoolStarted_ = true;
    PurgeableResourceManager::GetInstance().BeginAccessPurgeableMem();
    EXPECT_NE(PurgeableResourceManager::GetInstance().lruCache_.Size(), 0);
    PurgeableResourceManager::GetInstance().Clear();
}

HWTEST_F(PurgeableResourceManagerTest, EndAccessPurgeableMemTest, TestSize.Level1)
{
    std::shared_ptr<PurgeableMemBase> key = std::make_shared<PurgeableMemBase>();
    PurgeableResourceManager::GetInstance().EndAccessPurgeableMem();
    PurgeableResourceManager::GetInstance().lruCache_.Insert(key);
    PurgeableResourceManager::GetInstance().EndAccessPurgeableMem();
    PurgeableResourceManager::GetInstance().isThreadPoolStarted_ = true;
    PurgeableResourceManager::GetInstance().EndAccessPurgeableMem();
    EXPECT_EQ(PurgeableResourceManager::GetInstance().lruCache_.Size(), 1);
    PurgeableResourceManager::GetInstance().Clear();
}

HWTEST_F(PurgeableResourceManagerTest, AddResourceTest, TestSize.Level1)
{
    std::shared_ptr<PurgeableMemBase> key = std::make_shared<PurgeableMemBase>();
    PurgeableResourceManager::GetInstance().AddResource(nullptr);
    PurgeableResourceManager::GetInstance().AddResource(key);
    EXPECT_EQ(PurgeableResourceManager::GetInstance().lruCache_.Size(), 1);
    PurgeableResourceManager::GetInstance().Clear();
}

HWTEST_F(PurgeableResourceManagerTest, RemoveResourceTest, TestSize.Level1)
{
    std::shared_ptr<PurgeableMemBase> key = std::make_shared<PurgeableMemBase>();
    PurgeableResourceManager::GetInstance().RemoveResource(nullptr);
    PurgeableResourceManager::GetInstance().AddResource(key);
    EXPECT_EQ(PurgeableResourceManager::GetInstance().lruCache_.Size(), 1);
    PurgeableResourceManager::GetInstance().RemoveResource(key);
    EXPECT_EQ(PurgeableResourceManager::GetInstance().lruCache_.Size(), 0);
    PurgeableResourceManager::GetInstance().Clear();
}

HWTEST_F(PurgeableResourceManagerTest, SetRecentUsedResourceTest, TestSize.Level1)
{
    std::shared_ptr<PurgeableMemBase> key = std::make_shared<PurgeableMemBase>();
    PurgeableResourceManager::GetInstance().SetRecentUsedResource(nullptr);
    PurgeableResourceManager::GetInstance().SetRecentUsedResource(key);
    PurgeableResourceManager::GetInstance().Clear();
    EXPECT_EQ(PurgeableResourceManager::GetInstance().lruCache_.Size(), 0);
}

HWTEST_F(PurgeableResourceManagerTest, SetLruCacheCapacityTest, TestSize.Level1)
{
    std::shared_ptr<PurgeableMemBase> key = std::make_shared<PurgeableMemBase>();
    int32_t capacity = 1;
    PurgeableResourceManager::GetInstance().SetLruCacheCapacity(capacity);
    PurgeableResourceManager::GetInstance().SetRecentUsedResource(key);
    EXPECT_EQ(PurgeableResourceManager::GetInstance().lruCache_.lruCacheCapacity_, capacity);
}

HWTEST_F(PurgeableResourceManagerTest, RemoveLastResourceTest, TestSize.Level1)
{
    std::shared_ptr<PurgeableMemBase> key = std::make_shared<PurgeableMemBase>();
    PurgeableResourceManager::GetInstance().Clear();
    PurgeableResourceManager::GetInstance().RemoveLastResource();
    PurgeableResourceManager::GetInstance().ShowLruCache();
    EXPECT_EQ(PurgeableResourceManager::GetInstance().lruCache_.Size(), 0);
    PurgeableResourceManager::GetInstance().AddResource(key);
    PurgeableResourceManager::GetInstance().RemoveLastResource();
    PurgeableResourceManager::GetInstance().ShowLruCache();
    EXPECT_EQ(PurgeableResourceManager::GetInstance().lruCache_.Size(), 0);
    PurgeableResourceManager::GetInstance().StartThreadPool();
    PurgeableResourceManager::GetInstance().StartThreadPool();
}
}
}

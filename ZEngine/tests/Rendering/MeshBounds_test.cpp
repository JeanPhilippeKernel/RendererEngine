#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Managers/AssetManager.h>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <initializer_list>
#include <latch>
#include <limits>
#include <thread>

using namespace ZEngine::Core::Maths;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Importers;
using ZEngine::Managers::AssetManager;

class MeshBoundsTest : public testing::Test
{
protected:
    MemoryManager Memory = {};
    uuids::uuid   ID     = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    uuids::uuid   HierID = *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222");

    void          SetUp() override
    {
        ASSERT_EQ(AssetManager::Instance(), nullptr);
        Memory.Initialize(ZMega(512), {});
        AssetManager::Initialize(&Memory.MainArena, nullptr, "/project");
    }
    void TearDown() override
    {
        AssetManager::Shutdown();
    }
    AssetMesh Mesh(std::initializer_list<Vec3f> points)
    {
        AssetMesh mesh = {};
        mesh.MeshUUID  = ID;
        mesh.Vertices.init(&Memory.MainArena, points.size() * 8);
        mesh.Indices.init(&Memory.MainArena, points.size());
        mesh.SubMeshes.init(&Memory.MainArena, 2);
        for (const auto& point : points)
        {
            mesh.Vertices.push(point.x);
            mesh.Vertices.push(point.y);
            mesh.Vertices.push(point.z);
            for (uint32_t i = 0; i < 5; ++i)
                mesh.Vertices.push(0.f);
            mesh.Indices.push(uint32_t(mesh.Indices.size()));
        }
        if (points.size())
            mesh.SubMeshes.push({.VertexCount = uint32_t(points.size()), .IndexCount = uint32_t(points.size()), .VertexUnitStreamSize = 32, .IndexUnitStreamSize = 4, .TotalByteSize = uint32_t(points.size() * 36)});
        return mesh;
    }
    void Ingest(AssetMesh& mesh)
    {
        AssetNodeHierarchy hierarchy = {};
        hierarchy.NodeHierarchyUUID  = HierID;
        hierarchy.MeshUUID           = ID;
        AssetManager::IngestMesh(std::move(mesh), std::move(hierarchy));
    }
    void Ingest(AssetMesh&& mesh)
    {
        Ingest(mesh);
    }
    void ExpectBox(const Vec3f& minimum, const Vec3f& maximum)
    {
        Vec3f actual_min = {}, actual_max = {};
        ASSERT_TRUE(AssetManager::Instance()->TryGetMeshAABB(ID, actual_min, actual_max));
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            EXPECT_FLOAT_EQ(actual_min[axis], minimum[axis]);
            EXPECT_FLOAT_EQ(actual_max[axis], maximum[axis]);
        }
    }
    void ExpectUnavailable()
    {
        Vec3f minimum(1.f, 2.f, 3.f), maximum(4.f, 5.f, 6.f), center(7.f, 8.f, 9.f);
        float radius = 10.f;
        EXPECT_FALSE(AssetManager::Instance()->TryGetMeshAABB(ID, minimum, maximum));
        EXPECT_FALSE(AssetManager::Instance()->TryGetMeshBounds(ID, center, radius));
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            EXPECT_FLOAT_EQ(minimum[axis], float(axis + 1));
            EXPECT_FLOAT_EQ(maximum[axis], float(axis + 4));
            EXPECT_FLOAT_EQ(center[axis], float(axis + 7));
        }
        EXPECT_FLOAT_EQ(radius, 10.f);
    }
};

TEST(MeshBoundsValueTest, RuntimeBoundsAreDefaultInitialized)
{
    const AssetMesh mesh = {};
    EXPECT_FALSE(mesh.BoundsValid);
    EXPECT_FLOAT_EQ(mesh.BoundsRadius, 0.f);
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        EXPECT_FLOAT_EQ(mesh.BoundsCenter[axis], 0.f);
        EXPECT_FLOAT_EQ(mesh.BoundsMin[axis], 0.f);
        EXPECT_FLOAT_EQ(mesh.BoundsMax[axis], 0.f);
    }
}

TEST_F(MeshBoundsTest, ComputesKnownBoxAcrossAllSubmeshesWithoutBorrowingSourceStorage)
{
    auto mesh                     = Mesh({
        {-4.f,  2.f,  7.f},
        { 6.f, -3.f,  9.f},
        { 1.f,  5.f, -2.f},
        {-2.f,  4.f,  8.f},
        { 3.f, -1.f,  1.f},
        { 0.f,  1.f,  4.f}
    });
    mesh.SubMeshes[0].VertexCount = mesh.SubMeshes[0].IndexCount = 3;
    mesh.SubMeshes.push({.VertexCount = 3, .IndexCount = 3, .VertexOffset = 3, .IndexOffset = 3});
    // Source runtime fields must never substitute for scanning the owned vertices.
    mesh.BoundsMin = mesh.BoundsMax = {100.f, 100.f, 100.f};
    mesh.BoundsValid                = true;
    Ingest(mesh);
    ExpectBox({-4.f, -3.f, -2.f}, {6.f, 5.f, 9.f});
    EXPECT_EQ(AssetManager::Instance()->GetMeshAsset(ID)->SubMeshes.size(), 2u);
    mesh.Vertices[0] = -1000.f;
    ExpectBox({-4.f, -3.f, -2.f}, {6.f, 5.f, 9.f});

    Vec3f center = {};
    float radius = 0.f;
    ASSERT_TRUE(AssetManager::Instance()->TryGetMeshBounds(ID, center, radius));
    EXPECT_FLOAT_EQ(center.x, 4.f / 6.f);
    EXPECT_FLOAT_EQ(center.y, 8.f / 6.f);
    EXPECT_FLOAT_EQ(center.z, 27.f / 6.f);
    const auto* owned = AssetManager::Instance()->GetMeshAsset(ID);
    for (size_t i = 0; i < owned->Vertices.size(); i += 8)
    {
        const double x = double(owned->Vertices[i]) - center.x;
        const double y = double(owned->Vertices[i + 1]) - center.y;
        const double z = double(owned->Vertices[i + 2]) - center.z;
        EXPECT_GE(double(radius) * radius, x * x + y * y + z * z);
    }
}

TEST_F(MeshBoundsTest, ReloadRecomputesBoundsAndPreservesMeshAndHierarchyHandles)
{
    Ingest(Mesh({
        {-4.f, -3.f, -2.f},
        { 6.f,  5.f,  9.f}
    }));
    const auto mesh_handle = AssetManager::Instance()->Registry->FindByUUID(ID)->SlotHandle;
    const auto hier_handle = AssetManager::Instance()->Registry->FindByUUID(HierID)->SlotHandle;
    Ingest(Mesh({
        {20.f, 30.f, 40.f},
        {21.f, 32.f, 43.f}
    }));
    ExpectBox({20.f, 30.f, 40.f}, {21.f, 32.f, 43.f});
    EXPECT_EQ(AssetManager::Instance()->Meshes.size(), 1u);
    EXPECT_EQ(AssetManager::Instance()->NodeHierarchies.size(), 1u);
    EXPECT_EQ(AssetManager::Instance()->Registry->FindByUUID(ID)->SlotHandle, mesh_handle);
    EXPECT_EQ(AssetManager::Instance()->Registry->FindByUUID(HierID)->SlotHandle, hier_handle);
}

TEST_F(MeshBoundsTest, EmptyReloadClearsPreviousSphereAndBox)
{
    Ingest(Mesh({
        {-4.f, -3.f, -2.f},
        { 6.f,  5.f,  9.f}
    }));
    Ingest(Mesh({}));
    ExpectUnavailable();
    const auto* mesh = AssetManager::Instance()->GetMeshAsset(ID);
    ASSERT_NE(mesh, nullptr);
    EXPECT_FALSE(mesh->BoundsValid);
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        EXPECT_EQ(mesh->BoundsMin[axis], 0.f);
        EXPECT_EQ(mesh->BoundsMax[axis], 0.f);
        EXPECT_EQ(mesh->BoundsCenter[axis], 0.f);
    }
    EXPECT_EQ(mesh->BoundsRadius, 0.f);
}

TEST_F(MeshBoundsTest, NonfinitePositionAndPartialVertexReloadsCannotPublishPartialBounds)
{
    for (const float value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            Ingest(Mesh({
                {-4.f, -3.f, -2.f},
                { 6.f,  5.f,  9.f}
            }));
            auto mesh               = Mesh({
                {1.f, 2.f, 3.f},
                {4.f, 5.f, 6.f}
            });
            mesh.Vertices[8 + axis] = value;
            Ingest(mesh);
            ExpectUnavailable();
        }
    Ingest(Mesh({
        {-4.f, -3.f, -2.f},
        { 6.f,  5.f,  9.f}
    }));
    auto partial = Mesh({
        {1.f, 2.f, 3.f},
        {4.f, 5.f, 6.f}
    });
    partial.Vertices.push(7.f);
    Ingest(partial);
    ExpectUnavailable();
}

TEST_F(MeshBoundsTest, PointPlanarAndNegativeOnlyBoxesAreValid)
{
    Ingest(Mesh({
        {3.f, -4.f, 5.f}
    }));
    ExpectBox({3.f, -4.f, 5.f}, {3.f, -4.f, 5.f});
    EXPECT_EQ(AssetManager::Instance()->GetMeshAsset(ID)->BoundsRadius, 0.f);
    Ingest(Mesh({
        {-7.f, -6.f, -5.f},
        {-2.f, -3.f, -5.f},
        {-4.f, -8.f, -5.f}
    }));
    ExpectBox({-7.f, -8.f, -5.f}, {-2.f, -3.f, -5.f});
}

TEST_F(MeshBoundsTest, HugeFiniteVerticesAvoidCentroidAndDistanceOverflow)
{
    const float huge = std::numeric_limits<float>::max();
    Ingest(Mesh({
        {huge, huge, huge},
        {huge, huge, huge}
    }));
    ExpectBox({huge, huge, huge}, {huge, huge, huge});
    const auto* mesh = AssetManager::Instance()->GetMeshAsset(ID);
    EXPECT_EQ(mesh->BoundsCenter.x, huge);
    EXPECT_EQ(mesh->BoundsCenter.y, huge);
    EXPECT_EQ(mesh->BoundsCenter.z, huge);
    EXPECT_EQ(mesh->BoundsRadius, 0.f);
    Ingest(Mesh({
        {-huge, -huge, -huge},
        { huge,  huge,  huge}
    }));
    ExpectBox({-huge, -huge, -huge}, {huge, huge, huge});
    EXPECT_EQ(mesh->BoundsCenter.x, 0.f);
    EXPECT_EQ(mesh->BoundsCenter.y, 0.f);
    EXPECT_EQ(mesh->BoundsCenter.z, 0.f);
    // No representable float sphere: keep the renderer's always-visible sentinel.
    EXPECT_EQ(mesh->BoundsRadius, 0.f);
    Vec3f center = {};
    float radius = 123.f;
    EXPECT_FALSE(AssetManager::Instance()->TryGetMeshBounds(ID, center, radius));
    EXPECT_EQ(radius, 123.f);
}

TEST_F(MeshBoundsTest, UnknownScannerOnlyMismatchedAndCorruptBoundsPreserveOutputs)
{
    ExpectUnavailable();
    AssetManager::RegisterAsset(ZEngine::Managers::AssetType::MESH, ID, 0);
    ExpectUnavailable();
    Ingest(Mesh({
        {-4.f, -3.f, -2.f},
        { 6.f,  5.f,  9.f}
    }));
    auto* mesh = AssetManager::Instance()->GetMeshAsset(ID);
    ASSERT_NE(mesh, nullptr);
    mesh->MeshUUID = HierID;
    ExpectUnavailable();
    mesh->MeshUUID     = ID;
    mesh->BoundsMin.x  = 7.f;
    mesh->BoundsRadius = std::numeric_limits<float>::infinity();
    ExpectUnavailable();
    mesh->BoundsMin.x    = -4.f;
    mesh->BoundsMax.z    = std::numeric_limits<float>::quiet_NaN();
    mesh->BoundsRadius   = 1.f;
    mesh->BoundsCenter.x = std::numeric_limits<float>::quiet_NaN();
    ExpectUnavailable();
}

TEST_F(MeshBoundsTest, RepeatedCopiedQueriesAllocateNothingAndRetainNoBorrowedPointers)
{
    Ingest(Mesh({
        {-4.f, -3.f, -2.f},
        { 6.f,  5.f,  9.f}
    }));
    const auto offset = Memory.MainArena.m_current_offset;
    for (uint32_t i = 0; i < 10000; ++i)
    {
        Vec3f minimum = {}, maximum = {};
        ASSERT_TRUE(AssetManager::Instance()->TryGetMeshAABB(ID, minimum, maximum));
        EXPECT_EQ(minimum.x, -4.f);
        EXPECT_EQ(maximum.z, 9.f);
    }
    EXPECT_EQ(Memory.MainArena.m_current_offset, offset);
}

TEST_F(MeshBoundsTest, ConcurrentReloadAndQueriesNeverMixBoxRevisions)
{
    Ingest(Mesh({
        {-4.f, -3.f, -2.f},
        { 6.f,  5.f,  9.f}
    }));
    auto              first  = Mesh({
        {-4.f, -3.f, -2.f},
        { 6.f,  5.f,  9.f}
    });
    auto              second = Mesh({
        {20.f, 30.f, 40.f},
        {21.f, 32.f, 43.f}
    });
    std::atomic<bool> done   = false;
    std::atomic<bool> good   = true;
    std::latch        ready(1);
    std::thread       reader([&] {
        bool first_query = true;
        do
        {
            Vec3f minimum = {}, maximum = {};
            if (AssetManager::Instance()->TryGetMeshAABB(ID, minimum, maximum) && !((minimum.x == -4.f && minimum.y == -3.f && minimum.z == -2.f && maximum.x == 6.f && maximum.y == 5.f && maximum.z == 9.f) || (minimum.x == 20.f && minimum.y == 30.f && minimum.z == 40.f && maximum.x == 21.f && maximum.y == 32.f && maximum.z == 43.f)))
                good.store(false);
            if (first_query)
            {
                ready.count_down();
                first_query = false;
            }
        } while (!done.load());
    });
    ready.wait();
    for (uint32_t i = 0; i < 200; ++i)
        Ingest(i % 2 ? first : second);
    done.store(true);
    reader.join();
    EXPECT_TRUE(good.load());
}

TEST_F(MeshBoundsTest, BusyIngestionOmitsOptionalBoundsWithoutWaitingOrChangingOutputs)
{
    Ingest(Mesh({
        {-4.f, -3.f, -2.f},
        { 6.f,  5.f,  9.f}
    }));
    // Keep the test bounded even if the accessor regresses to a blocking lock.
    // The main thread releases ingestion after the deadline, then joins before
    // inspecting the worker's outputs.
    std::unique_lock        ingest(AssetManager::Instance()->IngestMutex);
    std::mutex              completion_mutex = {};
    std::condition_variable completed        = {};
    bool                    finished = false, available = false;
    Vec3f                   minimum(1.f, 2.f, 3.f), maximum(4.f, 5.f, 6.f);
    std::thread             worker([&] {
        available = AssetManager::Instance()->TryGetMeshAABB(ID, minimum, maximum);
        {
            std::lock_guard lock(completion_mutex);
            finished = true;
        }
        completed.notify_one();
    });
    {
        std::unique_lock lock(completion_mutex);
        EXPECT_TRUE(completed.wait_for(lock, std::chrono::seconds(2), [&] { return finished; }));
    }
    ingest.unlock();
    worker.join();
    EXPECT_FALSE(available);
    EXPECT_EQ(minimum.x, 1.f);
    EXPECT_EQ(maximum.z, 6.f);
    ExpectBox({-4.f, -3.f, -2.f}, {6.f, 5.f, 9.f});
}

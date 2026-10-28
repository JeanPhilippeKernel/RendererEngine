#include <Tetragrama/Editor.h>
#include <Tetragrama/Gizmo/GizmoConstants.h>
#include <Tetragrama/Gizmo/GizmoViewport.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <ZEngine/UI/ZUIInput.h>
#include <ZEngine/UI/ZUIInteraction.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <sstream>
#include <thread>

using namespace Tetragrama;
using namespace Tetragrama::Gizmo;
using namespace ZEngine::Core::Maths;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Rendering::Cameras;
using namespace ZEngine::Rendering::Renderers;
using namespace ZEngine::UI;

namespace
{
    std::filesystem::path VisualCaptureDirectory()
    {
        const char* value = std::getenv("ZENGINE_GIZMO_CAPTURE_DIR");
        return value && *value ? std::filesystem::path(value) : std::filesystem::path{};
    }

    bool WriteVisualCapture(const std::filesystem::path& directory, const char* name, const GizmoFrameData& frame, const CameraFrameData& camera, const float rect[4])
    {
        if (directory.empty())
        {
            return true;
        }
        std::error_code error = {};
        std::filesystem::create_directories(directory, error);
        if (error)
        {
            return false;
        }
        Geometry::ProjectionFrame projection = {};
        if (!Geometry::MakeProjectionFrame(camera.View, camera.Projection, camera.Position, camera.ProjectionType == CameraType::ORTHOGRAPHIC ? Geometry::ProjectionKind::Orthographic : Geometry::ProjectionKind::Perspective, camera.UsesReverseZ, &projection))
        {
            return false;
        }
        std::ofstream output(directory / (std::string(name) + ".svg"));
        if (!output)
        {
            return false;
        }
        const auto color = [](uint32_t packed) {
            std::ostringstream value;
            value << "#" << std::hex << std::setfill('0') << std::setw(2) << (packed & 0xffu) << std::setw(2) << ((packed >> 8) & 0xffu) << std::setw(2) << ((packed >> 16) & 0xffu);
            return value.str();
        };
        const auto alpha   = [](uint32_t packed) { return float((packed >> 24) & 0xffu) / 255.f; };
        const auto project = [&](const Vec3f& point, Vec2f& result) {
            Geometry::ProjectedPoint projected = {};
            if (!Geometry::Project(
                    point,
                    projection,
                    {
                    {          rect[0],           rect[1]},
                    {rect[2] - rect[0], rect[3] - rect[1]}
            },
                    &projected))
            {
                return false;
            }
            result = projected.Screen - Vec2f{rect[0], rect[1]};
            return true;
        };
        output << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"800\" height=\"600\" viewBox=\"0 0 800 600\"><rect width=\"800\" height=\"600\" fill=\"#15171d\"/>";
        for (uint32_t index = 0; index < frame.TriangleCount; ++index)
        {
            const auto& triangle = frame.Triangles[index];
            Vec2f       a = {}, b = {}, c = {};
            if (project(triangle.A, a) && project(triangle.B, b) && project(triangle.C, c))
            {
                output << "<polygon points=\"" << a.x << ',' << a.y << ' ' << b.x << ',' << b.y << ' ' << c.x << ',' << c.y << "\" fill=\"" << color(triangle.Color) << "\" fill-opacity=\"" << alpha(triangle.Color) << "\"/>";
            }
        }
        for (uint32_t index = 0; index < frame.LineCount; ++index)
        {
            const auto& line = frame.Lines[index];
            Vec2f       a = {}, b = {};
            if (project(line.A, a) && project(line.B, b))
            {
                output << "<line x1=\"" << a.x << "\" y1=\"" << a.y << "\" x2=\"" << b.x << "\" y2=\"" << b.y << "\" stroke=\"" << color(line.Color) << "\" stroke-opacity=\"" << alpha(line.Color) << "\" stroke-width=\"" << line.Width << "\"";
                if (line.DashOn > 0.f && line.DashOff > 0.f)
                {
                    output << " stroke-dasharray=\"" << line.DashOn << ' ' << line.DashOff << "\" stroke-dashoffset=\"" << line.DashPhase << "\"";
                }
                output << "/>";
            }
        }
        for (uint32_t index = 0; index < frame.SpriteCount; ++index)
        {
            const auto& sprite = frame.Sprites[index];
            Vec2f       point  = {};
            if (!project(sprite.Position, point))
            {
                continue;
            }
            const float radius = sprite.Size * .5f;
            if (sprite.Round)
            {
                output << "<circle cx=\"" << point.x << "\" cy=\"" << point.y << "\" r=\"" << radius << "\" fill=\"" << color(sprite.Fill) << "\" fill-opacity=\"" << alpha(sprite.Fill) << "\" stroke=\"" << color(sprite.Outline) << "\" stroke-opacity=\"" << alpha(sprite.Outline) << "\" stroke-width=\"" << sprite.OutlineWidth << "\"/>";
            }
            else
            {
                output << "<rect x=\"" << point.x - radius << "\" y=\"" << point.y - radius << "\" width=\"" << sprite.Size << "\" height=\"" << sprite.Size << "\" fill=\"" << color(sprite.Fill) << "\" fill-opacity=\"" << alpha(sprite.Fill) << "\"/>";
            }
        }
        output << "</svg>";
        return bool(output);
    }

    class GizmoViewportTest : public testing::Test
    {
    protected:
        ZEngine::Core::Memory::MemoryManager         memory   = {};
        Scene                                        scene    = {};
        EditorSession                                session  = {};
        ZUIContext                                   ctx      = {};
        std::unique_ptr<Tetragrama::Gizmo::Viewport> viewport = {};
        FlyCamera                                    camera{4.f / 3.f, {}};
        EntityID                                     entity         = {};
        const float                                  rect[4]        = {37.f, 61.f, 837.f, 661.f};
        Vec3f                                        drag_grab      = {};
        RotationSnapshot                             rotation_start = {};
        Vec3f                                        rotation_grab  = {};
        ScaleSnapshot                                scale_start    = {};
        CameraSnapshot                               camera_start   = {};
        Vec2f                                        scale_press    = {};

        virtual EditorSessionConfig                  SessionConfig() const
        {
            return {.MaxTargets = 8, .HistoryEntries = 8, .HistoryBytes = 4096};
        }

        void SetUp() override
        {
            memory.Initialize(ZMega(64), {});
            scene.Initialize(&memory.MainArena);
            entity = scene.CreateEntity();
            scene.AddComponent<UUIDComponent>(entity, {uuids::uuid::from_string("a27629dd-11bd-442b-b54b-2252ec544f2e").value()});
            scene.AddComponent<TransformComponent>(entity, {});
            ASSERT_TRUE(session.Initialize(&memory.MainArena, SessionConfig()));
            ASSERT_TRUE(session.BeginMutationPhase());
            ASSERT_TRUE(session.BindScene(&scene));
            ASSERT_TRUE(session.Select(entity));
            ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Translate}));
            ASSERT_TRUE(session.ProcessRequests());
            session.EndMutationPhase();
            viewport = std::make_unique<Tetragrama::Gizmo::Viewport>();
            camera.SetPosition({0.f, 0.f, 10.f});
            camera.SetOrientation(0.f, 0.f);
            ZUIContextInit(&ctx, &memory.MainArena, ZKilo(128), ZKilo(64), 64, 32);
            BuildUI();
        }

        void TearDown() override
        {
            session.Shutdown();
            ZUIContextDestroy(&ctx);
            viewport.reset();
            scene.Shutdown();
            memory.Shutdown();
        }

        ZUIBox* Box(uint64_t key, float x, float y, float width, float height, ZUIBoxFlags flags = ZUI_Clickable)
        {
            ctx.Current       = ctx.Root;
            auto* box         = ZUIPushBox(&ctx, "", 0, flags);
            box->Key          = key;
            box->ScreenMin[0] = x;
            box->ScreenMin[1] = y;
            box->ScreenMax[0] = x + width;
            box->ScreenMax[1] = y + height;
            return box;
        }

        void BuildUI()
        {
            ZUIBeginFrame(&ctx, 1.f / 60.f);
            Box(1, 0.f, 0.f, 1024.f, 768.f, ZUI_None);
            Box(42, rect[0], rect[1], 800.f, 600.f, ZUI_Clickable | ZUI_NoKeyboardFocus);
            auto* hit              = Box(99, rect[0], rect[1], 800.f, 600.f, ZUI_Clickable | ZUI_NoKeyboardFocus);
            hit->CustomHitTest     = Tetragrama::Gizmo::Viewport::Filter;
            hit->CustomHitTestData = viewport.get();
            ctx.ViewportInputKey   = 42;
            ctx.MousePos[0]        = 437.f;
            ctx.MousePos[1]        = 361.f;
            viewport->Bind(&ctx, rect, 42, 99);
            viewport->BuildHitFrame(session, camera.CaptureFrameData(), &ctx, true);
        }

        void CaptureVisual(const char* name)
        {
            GizmoFrameData frame           = {};
            const auto     captured_camera = camera.CaptureFrameData();
            viewport->BuildRender(session, captured_camera, &ctx, true, 800, 600, frame);
            ASSERT_TRUE(frame.ValidCounts());
            ASSERT_FALSE(frame.Empty());
            ASSERT_TRUE(WriteVisualCapture(VisualCaptureDirectory(), name, frame, captured_camera, rect));
        }

        void Update(bool focused = true)
        {
            ctx.InteractionCapture = {session.Active(), session.Active()};
            viewport->UpdateHover(session, camera.CaptureFrameData(), &ctx, focused);
        }

        bool Hover(Handle handle)
        {
            viewport->BuildHitFrame(session, camera.CaptureFrameData(), &ctx, true);
            for (const auto& shape : viewport->Hits().View())
            {
                if (shape.Handle != uint32_t(handle))
                {
                    continue;
                }
                Vec2f point = (shape.Points[0] + shape.Points[1]) * .5f;
                if (shape.Geometry == HitGeometry::Triangle)
                {
                    point = (shape.Points[0] + shape.Points[1] + shape.Points[2]) / 3.f;
                }
                if (viewport->Hits().Pick(point).Handle != uint32_t(handle))
                {
                    continue;
                }
                ZUIFeedMousePos(&ctx, point.x, point.y);
                Update();
                return viewport->Hovered() == handle;
            }
            return false;
        }

        void Press(uint32_t button, float x, float y)
        {
            ctx.MousePos[0] = ctx.MousePressPos[button][0] = x;
            ctx.MousePos[1] = ctx.MousePressPos[button][1] = y;
            ctx.MouseDown[button] = ctx.MousePressed[button] = true;
            ctx.MousePressAlt[button]                        = ctx.AltDown;
            ++ctx.MousePressSerial[button];
        }

        Vec2f Screen(const Vec3f& point)
        {
            const auto                camera_frame = camera.CaptureFrameData();
            Geometry::ProjectionFrame projection   = {};
            EXPECT_TRUE(Geometry::MakeProjectionFrame(camera_frame.View, camera_frame.Projection, camera_frame.Position, Geometry::ProjectionKind::Perspective, false, &projection));
            Geometry::ProjectedPoint projected = {};
            EXPECT_TRUE(
                Geometry::Project(
                    point,
                    projection,
                    {
                    {rect[0], rect[1]},
                    {  800.f,   600.f}
            },
                    &projected));
            return projected.Screen;
        }

        // Test-only session entry point. Production physical press routing is
        // deliberately still gated and is not exercised by these checks.
        bool BeginTranslation(uint64_t serial = 1)
        {
            if (!session.BeginMutationPhase())
            {
                return false;
            }
            TranslationBegin input = {
                .Bounds = {{rect[0], rect[1]}, {800.f, 600.f}},
                  .Key = 99, .PointerSerial = serial, .Constraint = Handle::TranslateX
            };
            const auto camera_frame = camera.CaptureFrameData();
            bool       valid        = session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Translation, .Enabled = false}) && session.ProcessRequests();
            scene.SynchronizeHierarchy();
            valid = valid && ResolveTargetFrame(session, input.Expected) && Geometry::MakeProjectionFrame(camera_frame.View, camera_frame.Projection, camera_frame.Position, Geometry::ProjectionKind::Perspective, false, &input.Projection);
            if (valid)
            {
                drag_grab     = input.Expected.Pivot + Vec3f(.7f, 0.f, 0.f);
                input.Pointer = Screen(drag_grab);
                valid         = session.BeginTranslation(input);
            }
            session.EndMutationPhase();
            return valid;
        }

        bool Preview(float delta)
        {
            if (!session.BeginMutationPhase())
            {
                return false;
            }
            const bool updated = session.UpdateTranslation(Screen(drag_grab + Vec3f(delta, 0.f, 0.f)), true);
            session.EndMutationPhase();
            return updated;
        }

        void Route(bool focused = true, bool allow_begin = true)
        {
            ASSERT_TRUE(session.BeginMutationPhase());
            viewport->UpdateTransformInput(session, camera.CaptureFrameData(), &ctx, focused, allow_begin);
            session.EndMutationPhase();
        }

        void Move(const Vec3f& point)
        {
            const auto screen = Screen(point);
            ZUIFeedMousePos(&ctx, screen.x, screen.y);
        }

        void PhysicalPress()
        {
            Move({.7f, 0.f, 0.f});
            ZUIFeedMouseButton(&ctx, 0, true);
        }

        void ReleaseBeforeLaterInput()
        {
            ZUIFeedMouseButton(&ctx, 0, false);
            ctx.ShiftDown = !ctx.ShiftDown;
            ctx.CtrlDown  = !ctx.CtrlDown;
            ZUIFeedMousePos(&ctx, 100.f, 100.f);
            ZUIFeedBeginFrame(&ctx);
            Route();
        }

        EntityID ExtraEntity(uint32_t identity, const Vec3f& position = {}, bool transform = true)
        {
            ZEngine::Core::Containers::FixedArray<uint8_t, 16> bytes = {};
            for (uint32_t i = 0; i < 4; ++i)
            {
                bytes[15 - i] = uint8_t(identity >> (8 * i));
            }
            const auto result = scene.CreateEntity();
            scene.AddComponent<UUIDComponent>(result, {uuids::uuid(bytes.begin(), bytes.end())});
            if (transform)
            {
                scene.AddComponent<TransformComponent>(result, {.Position = position});
            }
            return result;
        }

        void ChooseGroup(std::initializer_list<EntityID> ids)
        {
            ASSERT_TRUE(session.BeginMutationPhase());
            ZEngine::Core::Containers::FixedArray<EntityTarget, 8> targets = {};
            ASSERT_LE(ids.size(), targets.size());
            uint32_t count = 0;
            for (const auto id : ids)
            {
                targets[count++] = session.TargetFor(id);
            }
            ASSERT_TRUE(session.SetSelection({targets.data(), count}, session.TargetFor(entity).UUID));
            session.EndMutationPhase();
            scene.SynchronizeHierarchy();
            BuildUI();
        }

        void RotationMode(bool oblique = true)
        {
            ASSERT_TRUE(session.BeginMutationPhase());
            ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Rotate}));
            ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Rotation, .Enabled = false}));
            ASSERT_TRUE(session.ProcessRequests());
            session.EndMutationPhase();
            if (oblique)
            {
                camera.SetPosition({7.f, 5.f, 10.f});
                camera.SetOrientation(25.f, -35.f);
            }
            BuildUI();
        }

        void ScaleMode(bool oblique = true)
        {
            ASSERT_TRUE(session.BeginMutationPhase());
            ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Scale}));
            ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Scale, .Enabled = false}));
            ASSERT_TRUE(session.ProcessRequests());
            session.EndMutationPhase();
            if (oblique)
            {
                camera.SetPosition({7.f, 5.f, 10.f});
                camera.SetOrientation(25.f, -35.f);
            }
            BuildUI();
        }

        bool ScalePress(Handle handle = Handle::ScaleUniform, bool route = true)
        {
            viewport->BuildHitFrame(session, camera.CaptureFrameData(), &ctx, true);
            bool found = false;
            for (const auto& shape : viewport->Hits().View())
            {
                if (shape.Handle == uint32_t(handle) && shape.Geometry == HitGeometry::Rectangle)
                {
                    scale_press = (shape.Points[0] + shape.Points[1]) * .5f;
                    found       = viewport->Hits().Pick(scale_press).Handle == uint32_t(handle);
                    break;
                }
            }
            if (!found)
            {
                return false;
            }
            ZUIFeedMousePos(&ctx, scale_press.x, scale_press.y);
            ZUIFeedMouseButton(&ctx, 0, true);
            if (!route)
            {
                return true;
            }
            Route();
            return session.ReadScaleSnapshot(scale_start);
        }

        void ScaleMove(float ratio)
        {
            Vec2f point = {};
            if (scale_start.Constraint == Handle::ScaleUniform)
            {
                point = scale_press + Vec2f(float(std::log(double(ratio)) * std::numbers::sqrt2 * Constants::UniformScalePixelsPerEfold), 0.f);
            }
            else
            {
                const auto   axis   = scale_start.Axes[uint32_t(scale_start.Constraint) - uint32_t(Handle::ScaleX)];
                const double length = std::hypot(double(axis.x), double(axis.y), double(axis.z));
                point               = Screen(scale_start.Pivot + axis * float(scale_start.HandleLength * ratio / length));
            }
            ZUIFeedMousePos(&ctx, point.x, point.y);
        }

        void LightMode()
        {
            ASSERT_TRUE(session.BeginMutationPhase());
            scene.AddComponent<LightComponent>(entity, {.LightType = LightComponent::Type::Point, .Range = 2.f});
            ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Select}));
            ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Translation, .Enabled = false}));
            ASSERT_TRUE(session.ProcessRequests());
            scene.SynchronizeHierarchy();
            session.EndMutationPhase();
            BuildUI();
        }

        void LightPress()
        {
            Move({2.f, 0.f, 0.f});
            ZUIFeedMouseButton(&ctx, 0, true);
        }

        void CameraMode()
        {
            ASSERT_TRUE(session.BeginMutationPhase());
            scene.AddComponent<CameraComponent>(entity, {.Near = 1.f, .Far = 4.f, .AspectRatio = 1.5f});
            ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Select}));
            ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Translation, .Enabled = false}));
            ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Rotation, .Enabled = false}));
            ASSERT_TRUE(session.ProcessRequests());
            scene.SynchronizeHierarchy();
            session.EndMutationPhase();
            camera.SetPosition({7.f, 5.f, 10.f});
            camera.SetOrientation(25.f, -35.f);
            BuildUI();
        }

        bool CameraPress(Handle handle = Handle::CameraFarLowerLeft, bool route = true)
        {
            viewport->BuildHitFrame(session, camera.CaptureFrameData(), &ctx, true);
            for (const auto& shape : viewport->Hits().View())
            {
                if (shape.Handle != uint32_t(handle))
                {
                    continue;
                }
                const auto point = (shape.Points[0] + shape.Points[1]) * .5f;
                if (viewport->Hits().Pick(point).Handle != shape.Handle)
                {
                    continue;
                }
                ZUIFeedMousePos(&ctx, point.x, point.y);
                ZUIFeedMouseButton(&ctx, 0, true);
                if (!route)
                {
                    return true;
                }
                Route();
                return session.ReadCameraSnapshot(camera_start);
            }
            return false;
        }

        void CameraMoveValue(float value)
        {
            CameraHandle handle = {};
            ASSERT_TRUE(ResolveCameraHandle(camera_start.Frame, camera_start.Constraint, handle));
            const double delta = handle.Field == AuthoredFloatField::CameraFovY ? handle.Factor * std::tan(double(value) * std::numbers::pi / 360.0) - handle.Parameter : (double(value) - (handle.Field == AuthoredFloatField::CameraNear ? camera_start.Frame.Camera.Near : camera_start.Frame.Camera.Far)) * handle.Factor;
            Move(handle.Position + handle.Direction * float(delta));
        }

        Geometry::ProjectionFrame RotationProjection()
        {
            const auto                frame  = camera.CaptureFrameData();
            Geometry::ProjectionFrame result = {};
            EXPECT_TRUE(Geometry::MakeProjectionFrame(frame.View, frame.Projection, frame.Position, frame.ProjectionType == CameraType::ORTHOGRAPHIC ? Geometry::ProjectionKind::Orthographic : Geometry::ProjectionKind::Perspective, frame.UsesReverseZ, &result));
            return result;
        }

        bool RotationPress(Handle handle = Handle::RotateView, bool route = true)
        {
            viewport->BuildHitFrame(session, camera.CaptureFrameData(), &ctx, true);
            Vec2f point = {};
            bool  found = false;
            for (const auto& shape : viewport->Hits().View())
            {
                if (shape.Handle == uint32_t(handle))
                {
                    const auto candidate = (shape.Points[0] + shape.Points[1]) * .5f;
                    if (viewport->Hits().Pick(candidate).Handle == uint32_t(handle))
                    {
                        point = candidate;
                        found = true;
                        break;
                    }
                }
            }
            if (!found || !viewport->Hits().PickRingPoint(handle, point, RotationProjection(), rotation_grab))
            {
                return false;
            }
            ZUIFeedMousePos(&ctx, point.x, point.y);
            ZUIFeedMouseButton(&ctx, 0, true);
            if (!route)
            {
                return true;
            }
            Route();
            return session.ReadRotationSnapshot(rotation_start);
        }

        void RotationMove(double angle)
        {
            const float radius  = (rotation_grab - rotation_start.Pivot).magnitude();
            const auto  radial  = rotation_start.StartDirection;
            const auto  tangent = cross3d(rotation_start.Axis, radial);
            const auto  point   = rotation_start.Pivot + (radial * float(std::cos(angle)) + tangent * float(std::sin(angle))) * radius;
            const auto  screen  = Screen(point);
            ZUIFeedMousePos(&ctx, screen.x, screen.y);
        }
    };

    class GizmoViewportMeshTest : public GizmoViewportTest
    {
    protected:
        ZEngine::Core::Memory::MemoryManager asset_memory = {};
        uuids::uuid                          mesh_id      = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        uuids::uuid                          hierarchy_id = *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222");

        void                                 SetUp() override
        {
            GizmoViewportTest::SetUp();
            ASSERT_EQ(ZEngine::Managers::AssetManager::Instance(), nullptr);
            asset_memory.Initialize(ZMega(512), {});
            ZEngine::Managers::AssetManager::Initialize(&asset_memory.MainArena, nullptr, "/project");
            scene.AddComponent<MeshComponent>(entity, {.MeshUUID = mesh_id});
        }
        void TearDown() override
        {
            ZEngine::Managers::AssetManager::Shutdown();
            asset_memory.Shutdown();
            GizmoViewportTest::TearDown();
        }
        void Ingest(const Vec3f& minimum, const Vec3f& maximum)
        {
            ZEngine::Importers::AssetMesh mesh = {};
            mesh.MeshUUID                      = mesh_id;
            mesh.Vertices.init(&asset_memory.MainArena, 16);
            mesh.Indices.init(&asset_memory.MainArena, 2);
            mesh.SubMeshes.init(&asset_memory.MainArena, 1);
            for (const auto& point : {minimum, maximum})
            {
                mesh.Vertices.push(point.x);
                mesh.Vertices.push(point.y);
                mesh.Vertices.push(point.z);
                for (uint32_t i = 0; i < 5; ++i)
                {
                    mesh.Vertices.push(0.f);
                }
                mesh.Indices.push(uint32_t(mesh.Indices.size()));
            }
            mesh.SubMeshes.push({.VertexCount = 2, .IndexCount = 2, .VertexUnitStreamSize = 32, .IndexUnitStreamSize = 4, .TotalByteSize = 72});
            ZEngine::Importers::AssetNodeHierarchy hierarchy = {};
            hierarchy.NodeHierarchyUUID                      = hierarchy_id;
            hierarchy.MeshUUID                               = mesh_id;
            ZEngine::Managers::AssetManager::IngestMesh(std::move(mesh), std::move(hierarchy));
        }
    };

    class GizmoViewportLargeGroupTest : public GizmoViewportMeshTest
    {
    protected:
        EditorSessionConfig SessionConfig() const override
        {
            return {.MaxTargets = 4096, .HistoryEntries = 8, .HistoryBytes = ZMega(2)};
        }
    };

    void ExpectRootBox(const GizmoFrameData& output, uint32_t index, const Vec3f& minimum, const Vec3f& maximum, const Mat4f& start, const Mat4f& world, const uuids::uuid& mesh)
    {
        ASSERT_LT(index, output.MeshLineGroupCount);
        const auto& group = output.MeshLineGroups[index];
        ASSERT_EQ(group.Count, 24u);
        ASSERT_LE(group.First + group.Count, output.LineCount);
        EXPECT_EQ(group.MeshUUID, mesh);
        Geometry::BoxCorners ghost = {}, current = {};
        ASSERT_TRUE(Geometry::TransformBox(minimum, maximum, start, &ghost));
        ASSERT_TRUE(Geometry::TransformBox(minimum, maximum, world, &current));
        uint32_t edge = 0;
        for (uint32_t corner = 0; corner < 8; ++corner)
        {
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                if (!(corner & (1u << axis)))
                {
                    for (uint32_t row = 0; row < 3; ++row)
                    {
                        EXPECT_FLOAT_EQ(output.Lines[group.First + edge].A[row], current[corner][row]);
                        EXPECT_FLOAT_EQ(output.Lines[group.First + edge].B[row], current[corner | (1u << axis)][row]);
                        EXPECT_FLOAT_EQ(output.Lines[group.First + 12 + edge].A[row], ghost[corner][row]);
                        EXPECT_FLOAT_EQ(output.Lines[group.First + 12 + edge].B[row], ghost[corner | (1u << axis)][row]);
                    }
                    ++edge;
                }
            }
        }
    }
} // namespace

TEST_F(GizmoViewportTest, PureEcsSelectionCapturesHoverBeforeAnyNewOverlay)
{
    ctx.HotKey       = ctx.ViewportInputKey;
    const auto frame = ctx.FrameIndex;
    const auto bytes = ctx.FrameArena.m_current_offset;
    for (uint32_t update = 0; update < 20; ++update)
    {
        Update();
        EXPECT_EQ(viewport->Hovered(), Handle::TranslateView);
        EXPECT_EQ(ctx.HotKey, 99u);
        EXPECT_TRUE(ZUIGetInputCapture(&ctx).Pointer);
        EXPECT_FALSE(ZUIGetInputCapture(&ctx).Keyboard);
    }
    EXPECT_EQ(ctx.FrameIndex, frame);
    EXPECT_EQ(ctx.FrameArena.m_current_offset, bytes);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
    EXPECT_FALSE(session.Active()); // authored drag remains gated
}

TEST_F(GizmoViewportTest, PhysicalScaleRoutesEveryCubeWithReflectionsThroughOneUndoableBatch)
{
    scene.GetComponent<TransformComponent>(entity)->Scale = {-1.2f, .8f, 1.5f};
    scene.SynchronizeHierarchy();
    ScaleMode();
    for (bool ortho : {false, true})
    {
        for (auto handle : {Handle::ScaleX, Handle::ScaleY, Handle::ScaleZ, Handle::ScaleUniform})
        {
            SCOPED_TRACE(testing::Message() << "ortho=" << ortho << " handle=" << uint32_t(handle));
            camera.SetProjectionType(ortho ? CameraType::ORTHOGRAPHIC : CameraType::PERSPECTIVE);
            const auto before = CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
            ASSERT_TRUE(ScalePress(handle));
            ASSERT_TRUE(session.ScaleActive());
            Update(); // native mutation-before-hover ordering
            EXPECT_EQ(ctx.ActiveKey, viewport->HitKey());
            EXPECT_TRUE(ctx.InteractionCapture.Keyboard);
            ScaleMove(1.5f);
            Route();
            ScaleSnapshot preview = {};
            ASSERT_TRUE(session.ReadScaleSnapshot(preview));
            EXPECT_NEAR(preview.Ratio, 1.5f, .0003f);
            GizmoFrameData output = {};
            viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 1600, 1200, output);
            EXPECT_EQ(output.SpriteCount, 4u);
            EXPECT_EQ(output.TriangleCount, 0u);
            ScaleMove(1.75f);
            ZUIFeedMouseButton(&ctx, 0, false);
            ZUIFeedMousePos(&ctx, 50.f, 70.f); // only the matching release position matters
            Route();
            EXPECT_FALSE(session.Active());
            EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
            EXPECT_EQ(ctx.ActiveKey, 0u);
            ASSERT_EQ(session.History().UndoCount(), 1u);
            const auto after = CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                EXPECT_NEAR(after.Scale[axis], before.Scale[axis] * (handle == Handle::ScaleUniform || uint32_t(handle) - uint32_t(Handle::ScaleX) == axis ? 1.75f : 1.f), .0005f);
            }
            ASSERT_TRUE(session.BeginMutationPhase());
            ASSERT_TRUE(session.Undo());
            EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity)), before));
            ASSERT_TRUE(session.Redo());
            EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity)), after));
            ASSERT_TRUE(session.Undo());
            session.EndMutationPhase();
        }
    }
}

TEST_F(GizmoViewportTest, CollapsedScaleAxesUseParentedLocalOrientationAndInvalidateIdenticalWorldMatrices)
{
    const auto         parent       = scene.CreateEntity();
    TransformComponent parent_value = {};
    parent_value.Rotation           = {.2f, -.3f, .4f};
    parent_value.Scale              = {-2.f, 3.f, .5f};
    scene.AddComponent<TransformComponent>(parent, parent_value);
    const auto group = scene.CreateEntity();
    ASSERT_TRUE(scene.SetParent(group, parent));
    ASSERT_TRUE(scene.SetParent(entity, group));
    scene.GetComponent<TransformComponent>(entity)->Scale = {0.f, -0.f, 0.f};
    scene.SynchronizeHierarchy();
    ScaleMode();
    TargetFrame before = {};
    ASSERT_TRUE(ResolveTargetFrame(session, before));
    const auto& parent_world = scene.GetComponent<TransformComponent>(parent)->WorldTransform;
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        for (uint32_t row = 0; row < 3; ++row)
        {
            EXPECT_FLOAT_EQ(before.Axes[axis][row], parent_world(row, axis) * (axis == 1 ? -1.f : 1.f));
        }
    }
    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    EXPECT_EQ(output.SpriteCount, 4u);
    EXPECT_EQ(viewport->Hits().Count, 1u); // uniform remains available, all axes disabled
    auto* transform     = scene.GetComponent<TransformComponent>(entity);
    transform->Rotation = {.3f, .2f, -.4f};
    scene.SynchronizeHierarchy();
    TargetFrame after = {};
    ASSERT_TRUE(ResolveTargetFrame(session, after));
    EXPECT_EQ(before.WorldRevision, after.WorldRevision);
    EXPECT_FALSE(SameTargetFrame(before, after));
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    EXPECT_GT(output.SpriteCount, 1u);
    ASSERT_TRUE(ScalePress());
    ScaleMove(1.25f);
    Route();
    EXPECT_EQ(scene.GetComponent<TransformComponent>(entity)->Scale.x, 0.f);
    EXPECT_TRUE(std::signbit(scene.GetComponent<TransformComponent>(entity)->Scale.y));
    ctx.EscapePressed = true;
    Route();
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoViewportTest, ActiveScaleKeepsCapturedAxisEditableAfterPreviewCrossesNearZeroThreshold)
{
    scene.GetComponent<TransformComponent>(entity)->Scale = {Constants::MinimumEditableScale, 1.f, 1.f};
    scene.SynchronizeHierarchy();
    ScaleMode(false);
    ASSERT_TRUE(ScalePress(Handle::ScaleX));
    ScaleMove(.001f);
    Route();
    ASSERT_TRUE(session.ScaleActive());
    EXPECT_LT(scene.GetComponent<TransformComponent>(entity)->Scale.x, Constants::MinimumEditableScale);
    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    bool active_cube = false;
    for (const auto& shape : viewport->Hits().View())
    {
        active_cube |= shape.Handle == uint32_t(Handle::ScaleX) && shape.Priority == HitPriority::ScaleCube;
    }
    EXPECT_TRUE(active_cube);
    ScaleMove(2.f);
    Route();
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Scale.x, 2.f * Constants::MinimumEditableScale, 1e-7f);
    ctx.EscapePressed = true;
    Route();
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Scale.x, Constants::MinimumEditableScale);
}

TEST_F(GizmoViewportTest, ScaleGateConsumesOldPressesAndOwnedReleaseSurvivesOverlayBackpressure)
{
    ScaleMode();
    ASSERT_TRUE(ScalePress(Handle::ScaleUniform, false));
    Route(true, false);
    Route(true, true);
    EXPECT_FALSE(session.Active());
    ZUIFeedMouseButton(&ctx, 0, false);
    ASSERT_TRUE(ScalePress(Handle::ScaleUniform, false));
    Update();
    EXPECT_EQ(viewport->Hovered(), Handle::ScaleUniform);
    ctx.MousePressed[0] = false;
    Route();
    ASSERT_TRUE(session.ReadScaleSnapshot(scale_start));
    ScaleMove(1.5f);
    Route();
    ScaleMove(1.75f);
    ZUIFeedMouseButton(&ctx, 0, false);
    ctx.MouseReleased[0] = false;
    Route(true, false);
    EXPECT_FALSE(session.Active());
    EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Scale.x, 1.75f, .0001f);
}

TEST_F(GizmoViewportTest, ScaleCaptureLossRestoresExactlyAndNeverStealsAForeignOwner)
{
    ScaleMode();
    for (uint32_t reason = 0; reason < 7; ++reason)
    {
        const auto before = CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
        ASSERT_TRUE(ScalePress());
        ScaleMove(1.5f);
        Route();
        if (reason == 1)
        {
            camera.SetPosition({7.1f, 5.f, 10.f});
        }
        else if (reason == 2)
        {
            ctx.PendingPopupKey = 1234;
        }
        else if (reason == 3)
        {
            ctx.ActiveKey = 777;
        }
        else if (reason == 4)
        {
            ctx.InputScale[0] = .5f;
            viewport->Bind(&ctx, rect, 42, 99);
        }
        else if (reason == 5)
        {
            ctx.EscapePressed = true;
        }
        else if (reason == 6)
        {
            const float moved[4] = {38.f, 61.f, 838.f, 661.f};
            viewport->Bind(&ctx, moved, 42, 99);
        }
        Route(reason != 0);
        EXPECT_FALSE(session.Active());
        EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity)), before));
        EXPECT_FALSE(session.Dirty());
        EXPECT_EQ(session.History().UndoCount(), 0u);
        if (reason == 3)
        {
            EXPECT_EQ(ctx.ActiveKey, 777u);
        }
        ctx.ActiveKey = ctx.PendingPopupKey = 0;
        ctx.InputScale[0]                   = 1.f;
        ZUIFeedMouseButton(&ctx, 0, false);
        viewport->Bind(&ctx, rect, 42, 99);
        Update();
    }
}

TEST_F(GizmoViewportTest, InvalidScaleReleaseCommitsOnlyLastAcceptedPreview)
{
    ScaleMode();
    ASSERT_TRUE(ScalePress());
    ScaleMove(1.5f);
    Route();
    ZUIFeedMouseButton(&ctx, 0, false);
    ctx.MouseReleasePos[0][0] = std::numeric_limits<float>::quiet_NaN();
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 1u);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Scale.x, 1.5f, .0001f);
}

TEST_F(GizmoViewportTest, RepeatedPhysicalScaleCancelAndRenderReuseSessionAndUiArenas)
{
    ScaleMode();
    const auto     bytes    = memory.MainArena.m_current_offset;
    const auto     ui_bytes = ctx.FrameArena.m_current_offset;
    GizmoFrameData output   = {};
    for (uint32_t i = 0; i < 1000; ++i)
    {
        ASSERT_TRUE(ScalePress());
        ScaleMove(1.5f);
        Route();
        viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
        ASSERT_FALSE(output.Empty());
        ctx.EscapePressed = true;
        Route();
        ASSERT_FALSE(session.Active());
        ZUIFeedMouseButton(&ctx, 0, false);
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, bytes);
    EXPECT_EQ(ctx.FrameArena.m_current_offset, ui_bytes);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoViewportTest, PhysicalRotationRoutesEveryRingUnderBothProjectionsIntoOneUndoableBatch)
{
    RotationMode();
    for (bool ortho : {false, true})
    {
        for (const auto handle : {Handle::RotateX, Handle::RotateY, Handle::RotateZ, Handle::RotateView})
        {
            SCOPED_TRACE(testing::Message() << "ortho=" << ortho << " handle=" << uint32_t(handle));
            camera.SetProjectionType(ortho ? CameraType::ORTHOGRAPHIC : CameraType::PERSPECTIVE);
            const auto before = CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
            ASSERT_TRUE(RotationPress(handle));
            ASSERT_TRUE(session.RotationActive());
            Update(); // native mutation-before-hover ordering
            EXPECT_EQ(ctx.ActiveKey, viewport->HitKey());
            EXPECT_TRUE(ctx.InteractionCapture.Keyboard);
            RotationMove(.4);
            Route();
            RotationSnapshot preview = {};
            ASSERT_TRUE(session.ReadRotationSnapshot(preview));
            EXPECT_NEAR(preview.Angle, .4, .0002);
            GizmoFrameData output = {};
            viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
            EXPECT_GT(output.TriangleCount, 0u);
            EXPECT_EQ(output.SpriteCount, 1u);
            RotationMove(.7);
            ZUIFeedMouseButton(&ctx, 0, false);
            // A newer uncaptured cursor must not replace the matching release edge.
            ZUIFeedMousePos(&ctx, 50.f, 70.f);
            Route();
            EXPECT_FALSE(session.Active());
            EXPECT_EQ(ctx.ActiveKey, 0u);
            EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
            EXPECT_EQ(session.History().UndoCount(), 1u);
            const auto after = CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
            ASSERT_TRUE(session.BeginMutationPhase());
            ASSERT_TRUE(session.Undo());
            EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity)), before));
            ASSERT_TRUE(session.Redo());
            EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity)), after));
            ASSERT_TRUE(session.Undo());
            session.EndMutationPhase();
        }
    }
}

TEST_F(GizmoViewportTest, EdgeOnPhysicalRotationUsesTheCapturedSignedTangent)
{
    RotationMode(false);
    ASSERT_TRUE(RotationPress(Handle::RotateX));
    Geometry::RotationReference mapping = {};
    ASSERT_TRUE(
        Geometry::CaptureRotation(
            {
            .Projection = RotationProjection(), .Bounds = {           {37.f, 61.f},          {800.f, 600.f}},
                 .Pointer = {ctx.MousePressPos[0][0], ctx.MousePressPos[0][1]},
                 .Axis = rotation_start.Axis, .RingPoint = rotation_grab, .RadiusPx = Constants::RingRadiusPx, .TangentFallbackCosine = Constants::RotateTangentFallbackCosine, .PlaneMinCosine = Constants::PlaneParallelCosine
    },
            &mapping));
    ASSERT_TRUE(mapping.Tangent);
    const auto point = mapping.Pointer + mapping.ScreenTangent * 50.f;
    ZUIFeedMousePos(&ctx, point.x, point.y);
    Route();
    RotationSnapshot preview = {};
    ASSERT_TRUE(session.ReadRotationSnapshot(preview));
    EXPECT_NEAR(preview.Angle, .5, .00001);
    ctx.EscapePressed = true;
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoViewportTest, RotationGateConsumesOldPressesButStillReleasesAnOwnedDragWhenDisabled)
{
    RotationMode();
    ASSERT_TRUE(RotationPress(Handle::RotateView, false));
    Route(true, false);
    EXPECT_FALSE(session.Active());
    Route(true, true);
    EXPECT_FALSE(session.Active());
    ZUIFeedMouseButton(&ctx, 0, false);
    ASSERT_TRUE(RotationPress());
    RotationMove(.4);
    ZUIFeedMouseButton(&ctx, 0, false);
    Route(true, false);
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 1u);
}

TEST_F(GizmoViewportTest, RotationHoverAndDurableEdgesSurviveOverlayBackpressure)
{
    RotationMode();
    ASSERT_TRUE(RotationPress(Handle::RotateView, false));
    Update();
    EXPECT_EQ(viewport->Hovered(), Handle::RotateView);
    EXPECT_TRUE(ctx.InteractionCapture.Pointer);
    ctx.MousePressed[0] = false;
    Route();
    ASSERT_TRUE(session.ReadRotationSnapshot(rotation_start));
    RotationMove(.4);
    Route();
    RotationMove(.7);
    ZUIFeedMouseButton(&ctx, 0, false);
    ctx.MouseReleased[0] = false;
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 1u);
    EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
}

TEST_F(GizmoViewportTest, RotationClickBetweenUpdatesIsANoopAndCannotReplay)
{
    RotationMode();
    ASSERT_TRUE(RotationPress(Handle::RotateView, false));
    ZUIFeedMouseButton(&ctx, 0, false);
    ctx.MousePressed[0] = ctx.MouseReleased[0] = false;
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
    Route();
    EXPECT_FALSE(session.Active());
}

TEST_F(GizmoViewportTest, RotationLosesCaptureOnFocusCameraPopupOrForeignOwnerAndRestoresExactly)
{
    RotationMode();
    for (uint32_t reason = 0; reason < 4; ++reason)
    {
        const auto before = CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
        ASSERT_TRUE(RotationPress());
        RotationMove(.4);
        Route();
        if (reason == 1)
        {
            camera.SetPosition({7.1f, 5.f, 10.f});
        }
        else if (reason == 2)
        {
            ctx.PendingPopupKey = 1234;
        }
        else if (reason == 3)
        {
            ctx.ActiveKey = 777;
        }
        Route(reason != 0);
        EXPECT_FALSE(session.Active());
        EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity)), before));
        EXPECT_EQ(session.History().UndoCount(), 0u);
        EXPECT_FALSE(session.Dirty());
        if (reason == 3)
        {
            EXPECT_EQ(ctx.ActiveKey, 777u);
        }
        ctx.ActiveKey = ctx.PendingPopupKey = 0;
        ZUIFeedMouseButton(&ctx, 0, false);
        Update();
    }
}

TEST_F(GizmoViewportTest, RepeatedPhysicalRotationCancelReusesSessionAndUiArenas)
{
    RotationMode();
    const auto bytes    = memory.MainArena.m_current_offset;
    const auto ui_bytes = ctx.FrameArena.m_current_offset;
    for (uint32_t i = 0; i < 1000; ++i)
    {
        ASSERT_TRUE(RotationPress());
        RotationMove(.4);
        Route();
        ctx.EscapePressed = true;
        Route();
        ZUIFeedMouseButton(&ctx, 0, false);
        Route();
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, bytes);
    EXPECT_EQ(ctx.FrameArena.m_current_offset, ui_bytes);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoViewportTest, MovingOffHandleRetiresOnlyItsOwnStaleHotState)
{
    Update();
    ASSERT_EQ(ctx.HotKey, 99u);
    ctx.MousePos[0] = 100.f;
    ctx.MousePos[1] = 100.f;
    Update();
    EXPECT_EQ(viewport->Hovered(), Handle::None);
    EXPECT_EQ(ctx.HotKey, 42u);
    EXPECT_FALSE(ZUIGetInputCapture(&ctx).Pointer);
    ctx.HotKey = 123;
    Update();
    EXPECT_EQ(ctx.HotKey, 123u);
}

TEST_F(GizmoViewportTest, DefaultUnboundControllerNeverClaimsAnotherWidgetsHotState)
{
    auto unbound    = std::make_unique<Tetragrama::Gizmo::Viewport>();
    ctx.HotKey      = 0;
    ctx.MousePos[0] = ctx.MousePos[1] = 100.f;
    unbound->UpdateHover(session, camera.CaptureFrameData(), &ctx, true);
    EXPECT_EQ(ctx.HotKey, 0u);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_EQ(unbound->Hovered(), Handle::None);
    EXPECT_FALSE(ctx.InteractionCapture.Pointer);
}

TEST_F(GizmoViewportTest, HiddenGizmoAddsNoRetainedTreeTraversalEvenOnANewPress)
{
    struct Probe
    {
        mutable uint32_t Calls = 0;
    } probe                    = {};
    auto* foreign              = Box(123, 427.f, 351.f, 20.f, 20.f);
    foreign->CustomHitTestData = &probe;
    foreign->CustomHitTest     = [](const ZUIBox*, const float[2], const void* data) {
        ++static_cast<const Probe*>(data)->Calls;
        return true;
    };
    ctx.HotKey = 123;
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Select}));
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.ProcessRequests());
    session.EndMutationPhase();
    for (uint32_t update = 0; update < 20; ++update)
    {
        Update();
    }
    Press(1, 437.f, 361.f);
    Update();
    EXPECT_EQ(probe.Calls, 0u);
    EXPECT_EQ(ctx.HotKey, 123u);
    EXPECT_EQ(viewport->Hovered(), Handle::None);
}

TEST_F(GizmoViewportTest, AltLeftPressOverHandleNeverLeaksToCameraOrHistory)
{
    ctx.AltDown = true;
    Press(0, 437.f, 361.f);
    Update();
    EXPECT_TRUE(ZUIGetInputCapture(&ctx).Pointer);
    EXPECT_FALSE(ZUIGetInputCapture(&ctx).Keyboard);
    EXPECT_EQ(ctx.HotKey, 99u);
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoViewportTest, RightAndMiddlePressOverHandleCannotStartNavigation)
{
    for (uint32_t button : {1u, 2u})
    {
        Press(button, 437.f, 361.f);
        Update();
        EXPECT_EQ(viewport->Hovered(), Handle::TranslateView);
        EXPECT_TRUE(ZUIGetInputCapture(&ctx).Pointer);
        ctx.MouseDown[button] = false;
        Update();
    }
}

TEST_F(GizmoViewportTest, CameraGestureStartedElsewhereIsNotStolenByLaterHover)
{
    for (uint32_t button : {0u, 1u, 2u})
    {
        Press(button, 100.f, 100.f);
        Update();
        EXPECT_FALSE(ZUIGetInputCapture(&ctx).Pointer);
        ctx.MousePos[0] = 437.f;
        ctx.MousePos[1] = 361.f;
        for (uint32_t update = 0; update < 3; ++update)
        {
            Update();
            EXPECT_EQ(viewport->Hovered(), Handle::None);
            EXPECT_FALSE(ZUIGetInputCapture(&ctx).Pointer);
        }
        ctx.MouseDown[button] = false;
        Update();
        EXPECT_EQ(viewport->Hovered(), Handle::TranslateView);
        EXPECT_TRUE(ZUIGetInputCapture(&ctx).Pointer);
    }
}

TEST_F(GizmoViewportTest, PressStartedOnHandleCannotBecomeCameraNavigationWhenPointerLeaves)
{
    ctx.AltDown = true;
    for (uint32_t button : {0u, 1u, 2u})
    {
        Press(button, 437.f, 361.f);
        Update();
        ASSERT_TRUE(ctx.InteractionCapture.Pointer);
        ctx.MousePos[0] = ctx.MousePos[1] = 100.f;
        Update();
        EXPECT_EQ(viewport->Hovered(), Handle::None);
        EXPECT_TRUE(ctx.InteractionCapture.Pointer);
        EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
        EXPECT_FALSE(session.Active());
        ctx.MouseDown[button] = false;
        Update();
        EXPECT_FALSE(ctx.InteractionCapture.Pointer);
    }
}

TEST_F(GizmoViewportTest, ReleaseAndNewPressBetweenUpdatesReevaluateGestureOwnership)
{
    for (uint32_t button : {0u, 1u, 2u})
    {
        ZUIFeedMousePos(&ctx, 100.f, 100.f);
        ZUIFeedMouseButton(&ctx, button, true);
        Update();
        ASSERT_FALSE(ctx.InteractionCapture.Pointer);
        ZUIFeedMouseButton(&ctx, button, false);
        ZUIFeedMousePos(&ctx, 437.f, 361.f);
        ZUIFeedMouseButton(&ctx, button, true);
        Update();
        EXPECT_EQ(viewport->Hovered(), Handle::TranslateView);
        EXPECT_TRUE(ctx.InteractionCapture.Pointer);
        if (button == 0)
        {
            ctx.ActiveKey = 99; // previous overlay's captured press
        }
        ZUIFeedMouseButton(&ctx, button, false);
        ZUIFeedMousePos(&ctx, 100.f, 100.f);
        ZUIFeedMouseButton(&ctx, button, true);
        Update();
        EXPECT_EQ(ctx.ActiveKey, 0u);
        EXPECT_EQ(viewport->Hovered(), Handle::None);
        EXPECT_FALSE(ZUIGetInputCapture(&ctx).Pointer);
        ZUIFeedMouseButton(&ctx, button, false);
        Update();
    }
}

TEST_F(GizmoViewportTest, LaterToolbarOwnsOverlappingHandleAndCannotBeStolen)
{
    Box(123, 427.f, 351.f, 20.f, 20.f);
    Update();
    EXPECT_EQ(viewport->Hovered(), Handle::None);
    ZUIInteractionPass(&ctx);
    EXPECT_EQ(ctx.HotKey, 123u);
    EXPECT_TRUE(ZUIGetInputCapture(&ctx).Pointer);
}

TEST_F(GizmoViewportTest, ModalPopupTextAndForeignActiveOwnersBlockHandleHover)
{
    for (uint32_t owner = 0; owner < 5; ++owner)
    {
        ctx.PopupStackSize  = owner == 0 ? 1 : 0;
        ctx.ActiveModalKey  = owner == 1 ? 123 : 0;
        ctx.TextInputActive = owner == 2;
        ctx.ActiveKey       = owner == 3 ? 123 : 0;
        ctx.DragSourceKey   = owner == 4 ? 123 : 0;
        Update();
        EXPECT_EQ(viewport->Hovered(), Handle::None) << owner;
    }
    ctx.PopupStackSize = 0;
    ctx.ActiveModalKey = ctx.ActiveKey = ctx.DragSourceKey = 0;
    ctx.TextInputActive                                    = false;
    Update();
    EXPECT_EQ(viewport->Hovered(), Handle::TranslateView);
}

TEST_F(GizmoViewportTest, PendingPopupSuppressesRetainedHandleHoverUntilClosedWithoutRebuildingTheOverlay)
{
    Update();
    ASSERT_EQ(viewport->Hovered(), Handle::TranslateView);
    const auto main      = memory.MainArena.m_current_offset;
    const auto frame     = ctx.FrameArena.m_current_offset;
    const auto states    = ctx.PersistentArena.m_current_offset;
    const auto ui        = ctx.FrameIndex;
    ZUIBox     probe     = {};
    probe.Key            = viewport->HitKey();
    const float point[2] = {437.f, 361.f};
    ZUIOpenPopup(&ctx, "PendingViewportPopup", 30.f, 40.f);
    const auto key = ctx.PendingPopupKey;
    ASSERT_NE(key, 0u);
    for (uint32_t update = 0; update < 32; ++update)
    {
        Update();
        EXPECT_EQ(viewport->Hovered(), Handle::None);
        EXPECT_FALSE(Tetragrama::Gizmo::Viewport::Filter(&probe, point, viewport.get()));
        EXPECT_NE(ctx.HotKey, probe.Key);
        EXPECT_TRUE(ZUIGetInputCapture(&ctx).Pointer);
        EXPECT_TRUE(ZUIGetInputCapture(&ctx).Keyboard);
        EXPECT_EQ(ctx.PendingPopupKey, key);
        EXPECT_EQ(ctx.PopupStackSize, 0u);
        EXPECT_EQ(ctx.FrameIndex, ui);
        EXPECT_EQ(memory.MainArena.m_current_offset, main);
        EXPECT_EQ(ctx.FrameArena.m_current_offset, frame);
        EXPECT_EQ(ctx.PersistentArena.m_current_offset, states);
    }
    EXPECT_FALSE(session.Dirty());
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 0u);
    ZUIClosePopup(&ctx);
    Update();
    EXPECT_EQ(viewport->Hovered(), Handle::TranslateView);
    EXPECT_TRUE(Tetragrama::Gizmo::Viewport::Filter(&probe, point, viewport.get()));
    EXPECT_FALSE(ZUIGetInputCapture(&ctx).Keyboard);
}

TEST_F(GizmoViewportTest, FocusLossClearsOwnedCaptureWithoutWaitingForOverlay)
{
    Press(0, 437.f, 361.f);
    Update();
    ctx.ActiveKey = 99;
    Update(false);
    EXPECT_EQ(viewport->Hovered(), Handle::None);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_FALSE(ctx.InteractionCapture.Pointer);
    EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
}

TEST_F(GizmoViewportTest, HiddenOrStaleViewportCannotCaptureAndRebindRestoresIt)
{
    Update();
    ++ctx.FrameIndex;
    Update();
    EXPECT_EQ(viewport->Hits().Count, 0u);
    EXPECT_FALSE(ctx.InteractionCapture.Pointer);
    viewport->Bind(&ctx, rect, 42, 99);
    Update();
    EXPECT_TRUE(ctx.InteractionCapture.Pointer);
    ctx.ViewportInputKey = 0;
    Update();
    EXPECT_EQ(viewport->Hovered(), Handle::None);
    EXPECT_EQ(viewport->Hits().Count, 0u);
}

TEST_F(GizmoViewportTest, ViewportExtentAndInputScaleChangesInvalidateRetainedCache)
{
    Update();
    ctx.InputScale[0] = 0.5f;
    Update();
    EXPECT_EQ(viewport->Hits().Count, 0u);
    viewport->Bind(&ctx, rect, 42, 99);
    Update();
    EXPECT_GT(viewport->Hits().Count, 0u);
    viewport->Unbind();
    Update();
    EXPECT_EQ(viewport->Hits().Count, 0u);
    EXPECT_FALSE(ctx.InteractionCapture.Pointer);
}

TEST_F(GizmoViewportTest, SelectionClearEpochReplacementAndTargetLossClearGeometry)
{
    for (uint32_t action = 0; action < 3; ++action)
    {
        ASSERT_TRUE(session.BeginMutationPhase());
        ASSERT_TRUE(session.Select(entity));
        session.EndMutationPhase();
        Update();
        ASSERT_GT(viewport->Hits().Count, 0u);
        ASSERT_TRUE(session.BeginMutationPhase());
        if (action == 0)
        {
            session.ClearSelection();
        }
        else if (action == 1)
        {
            ASSERT_TRUE(session.BindScene(&scene));
        }
        else
        {
            scene.RemoveComponent<TransformComponent>(entity);
        }
        session.EndMutationPhase();
        Update();
        EXPECT_EQ(viewport->Hits().Count, 0u);
        EXPECT_FALSE(ctx.InteractionCapture.Pointer);
    }
}

TEST_F(GizmoViewportTest, UnselectedDirectionalLightRendersAndQueuesSelectionFromItsPassiveMarker)
{
    const EntityID directional = ExtraEntity(1, {1.f, 0.f, 0.f});
    scene.AddComponent<LightComponent>(directional, {.LightType = LightComponent::Type::Directional});
    scene.SynchronizeHierarchy();

    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    ASSERT_TRUE(output.ValidCounts());
    ASSERT_GT(output.SpriteCount, 0u);
    EXPECT_EQ(output.Sprites[output.SpriteCount - 1].Position, Vec3f(1.f, 0.f, 0.f));
    ASSERT_GE(output.LineCount, 9u);
    bool arrow_found = false;
    for (uint32_t line = 0; line < output.LineCount; ++line)
    {
        arrow_found |= output.Lines[line].A == Vec3f(1.f, 0.f, 0.f) && output.Lines[line].B == Vec3f(1.f, 0.f, 1.f);
    }
    EXPECT_TRUE(arrow_found);
    EXPECT_GT(output.TriangleCount, 0u);

    ASSERT_TRUE(session.BeginMutationPhase());
    session.ClearSelection();
    session.EndMutationPhase();
    BuildUI();

    const Vec2f marker = Screen({1.f, 0.f, 0.f});
    ZUIFeedMousePos(&ctx, marker.x, marker.y);
    ZUIFeedMouseButton(&ctx, 0, true);
    Route();
    EXPECT_TRUE(ctx.InteractionCapture.Pointer);

    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.ProcessRequests());
    session.EndMutationPhase();
    EXPECT_EQ(session.Selection().ActiveUUID(), session.TargetFor(directional).UUID);
}

TEST_F(GizmoViewportTest, UnselectedPointSpotAndCameraRenderAndQueueSelectionFromPassiveMarkers)
{
    const EntityID point = ExtraEntity(1, {1.f, 0.f, 0.f});
    scene.AddComponent<LightComponent>(point, {.LightType = LightComponent::Type::Point});
    const EntityID spot = ExtraEntity(2, {-1.f, 0.f, 0.f});
    scene.AddComponent<LightComponent>(spot, {.LightType = LightComponent::Type::Spot});
    const EntityID camera_entity = ExtraEntity(3, {0.f, 1.f, 0.f});
    scene.AddComponent<CameraComponent>(camera_entity, {.Near = .1f, .Far = 10.f, .AspectRatio = 1.f});
    scene.SynchronizeHierarchy();

    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    ASSERT_TRUE(output.ValidCounts());
    EXPECT_GE(output.SpriteCount, 2u);
    // The camera icon is a filled solid; point and spot retain their 23 lines.
    EXPECT_GE(output.LineCount, 23u);
    EXPECT_GE(output.TriangleCount, 22u);

    const auto select = [&](EntityID target, const Vec3f& origin) {
        ASSERT_TRUE(session.BeginMutationPhase());
        session.ClearSelection();
        session.EndMutationPhase();
        BuildUI();
        const Vec2f marker = Screen(origin);
        ZUIFeedMousePos(&ctx, marker.x, marker.y);
        ZUIFeedMouseButton(&ctx, 0, true);
        Route();
        EXPECT_TRUE(ctx.InteractionCapture.Pointer);
        ASSERT_TRUE(session.BeginMutationPhase());
        ASSERT_TRUE(session.ProcessRequests());
        session.EndMutationPhase();
        EXPECT_EQ(session.Selection().ActiveUUID(), session.TargetFor(target).UUID);
        ZUIFeedMouseButton(&ctx, 0, false);
    };
    select(point, {1.f, 0.f, 0.f});
    select(spot, {-1.f, 0.f, 0.f});
    select(camera_entity, {0.f, 1.f, 0.f});
}

TEST_F(GizmoViewportTest, CapturedTargetContainsNoPointersAndTracksParentWorldChanges)
{
    TargetFrame before = {};
    ASSERT_TRUE(ResolveTargetFrame(session, before));
    EXPECT_EQ(before.Target.UUID, session.Selection().ActiveUUID());
    EXPECT_EQ(before.Target.CachedEntity, entity);
    const auto         parent    = scene.CreateEntity();
    TransformComponent transform = {};
    transform.Position           = {1.f, 2.f, 3.f};
    transform.Scale              = {2.f, 3.f, 4.f};
    scene.AddComponent<TransformComponent>(parent, transform);
    ASSERT_TRUE(scene.SetParent(entity, parent));
    scene.SynchronizeHierarchy();
    TargetFrame after = {};
    ASSERT_TRUE(ResolveTargetFrame(session, after));
    EXPECT_FALSE(SameTargetFrame(before, after));
    EXPECT_FLOAT_EQ(after.Pivot.x, 1.f);
    EXPECT_FLOAT_EQ(after.Pivot.y, 2.f);
    EXPECT_FLOAT_EQ(after.Pivot.z, 3.f);
    const auto previous                                        = after;
    scene.GetComponent<TransformComponent>(parent)->Position.x = 5.f;
    scene.SynchronizeHierarchy();
    ASSERT_TRUE(ResolveTargetFrame(session, after));
    EXPECT_NE(after.WorldRevision, previous.WorldRevision);
    EXPECT_FALSE(SameTargetFrame(previous, after));
    EXPECT_FLOAT_EQ(previous.Pivot.x, 1.f); // immutable copy
    EXPECT_FLOAT_EQ(after.Pivot.x, 5.f);
}

TEST_F(GizmoViewportTest, LocalHandlesSupportNonuniformParentScaleAndRotatedChild)
{
    const auto         parent    = scene.CreateEntity();
    TransformComponent transform = {};
    transform.Scale              = {2.f, 3.f, 4.f};
    transform.Rotation           = {0.2f, 0.1f, 0.3f};
    scene.AddComponent<TransformComponent>(parent, transform);
    scene.GetComponent<TransformComponent>(entity)->Rotation = {0.4f, 0.6f, 0.1f};
    ASSERT_TRUE(scene.SetParent(entity, parent));
    scene.SynchronizeHierarchy();
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Space, .Space = TransformSpacePolicy::Local}));
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.ProcessRequests());
    session.EndMutationPhase();
    Update();
    EXPECT_GT(viewport->Hits().Count, 0u);
    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 1600, 1200, output);
    EXPECT_FALSE(output.Empty());
    EXPECT_EQ(output.SpriteCount, 1u);
}

TEST_F(GizmoViewportTest, ReadonlyAndInvalidHierarchyTargetsNeverRenderOrCapture)
{
    ASSERT_TRUE(session.BeginMutationPhase(false));
    session.EndMutationPhase();
    Update();
    EXPECT_EQ(viewport->Hits().Count, 0u);
    ASSERT_TRUE(session.BeginMutationPhase());
    session.EndMutationPhase();
    scene.AddComponent<ParentComponent>(entity, {.Parent = entity});
    scene.SynchronizeHierarchy();
    Update();
    EXPECT_EQ(viewport->Hits().Count, 0u);
    EXPECT_FALSE(ctx.InteractionCapture.Pointer);
}

TEST_F(GizmoViewportTest, RenderRebuildsWithExactPublishedCameraInsteadOfCachedUiCamera)
{
    Update();
    ASSERT_EQ(viewport->Hovered(), Handle::TranslateView);
    camera.SetPosition({1.f, 0.f, 20.f});
    const auto     captured = camera.CaptureFrameData();
    GizmoFrameData output   = {};
    viewport->BuildRender(session, captured, &ctx, true, 1600, 1200, output);
    ASSERT_EQ(output.SpriteCount, 1u);
    EXPECT_EQ(output.Sprites[0].Size, Constants::CenterSizePx * 2.f);
    Geometry::ProjectionFrame projection = {};
    ASSERT_TRUE(Geometry::MakeProjectionFrame(captured.View, captured.Projection, captured.Position, Geometry::ProjectionKind::Perspective, false, &projection));
    Geometry::ProjectedPoint center = {};
    ASSERT_TRUE(
        Geometry::Project(
            output.Sprites[0].Position,
            projection,
            {
            { 37.f,  61.f},
            {800.f, 600.f}
    },
            &center));
    EXPECT_NE(center.Screen.x, 437.f);
    EXPECT_EQ(viewport->Hits().Pick(center.Screen).Handle, uint32_t(Handle::TranslateView));
}

TEST_F(GizmoViewportTest, CameraVisibilityChangesRetireOldHitsWithoutAnOverlayAndHeldPressCannotRearm)
{
    const auto                                            ui_frame         = ctx.FrameIndex;
    const auto                                            arena_bytes      = memory.MainArena.m_current_offset;
    const auto                                            ui_bytes         = ctx.FrameArena.m_current_offset;
    const ZEngine::Core::Containers::FixedArray<Vec3f, 5> hidden_positions = {
        {{0.f, 0.f, .15f}, {}, {0.f, 0.f, -2.f}, {0.f, 0.f, 20000.f}, {1000.f, 0.f, 10.f}}
    };
    GizmoFrameData output = {};
    for (const auto kind : {CameraType::PERSPECTIVE, CameraType::ORTHOGRAPHIC})
    {
        for (bool reverse : {false, true})
        {
            for (const auto mode : {Operation::Translate, Operation::Rotate, Operation::Scale})
            {
                SCOPED_TRACE(testing::Message() << "projection " << uint32_t(kind) << " reverse " << reverse << " mode " << uint32_t(mode));
                ASSERT_TRUE(session.BeginMutationPhase());
                ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = mode}));
                ASSERT_TRUE(session.ProcessRequests());
                session.EndMutationPhase();
                camera.SetProjectionType(kind);
                const auto captured = [&] {
                    auto frame = camera.CaptureFrameData();
                    if (reverse)
                    {
                        for (uint32_t column = 0; column < 4; ++column)
                        {
                            frame.Projection(2, column) = frame.Projection(3, column) - frame.Projection(2, column);
                        }
                        frame.UsesReverseZ = true;
                    }
                    return frame;
                };
                const auto route = [&] {
                    ctx.InteractionCapture = {session.Active(), session.Active()};
                    ASSERT_TRUE(session.BeginMutationPhase());
                    viewport->UpdateTransformInput(session, captured(), &ctx, true, true);
                    session.EndMutationPhase();
                    viewport->UpdateHover(session, captured(), &ctx, true);
                };
                for (uint32_t position = 0; position < hidden_positions.size(); ++position)
                {
                    SCOPED_TRACE(position);
                    camera.SetPosition({0.f, 0.f, 10.f});
                    viewport->BuildRender(session, captured(), &ctx, true, 1600, 1200, output);
                    ASSERT_FALSE(output.Empty());
                    const auto wanted = mode == Operation::Rotate ? Handle::RotateView : mode == Operation::Scale ? Handle::ScaleUniform : Handle::TranslateView;
                    Vec2f      point  = {};
                    bool       found  = false;
                    for (const auto& shape : viewport->Hits().View())
                    {
                        if (shape.Handle != uint32_t(wanted))
                        {
                            continue;
                        }
                        point = (shape.Points[0] + shape.Points[1]) * .5f;
                        if (viewport->Hits().Pick(point).Handle == uint32_t(wanted))
                        {
                            found = true;
                            break;
                        }
                    }
                    ASSERT_TRUE(found);
                    ZUIFeedMousePos(&ctx, point.x, point.y);
                    viewport->UpdateHover(session, captured(), &ctx, true);
                    ASSERT_EQ(viewport->Hovered(), wanted);
                    ASSERT_TRUE(ctx.InteractionCapture.Pointer);

                    camera.SetPosition(hidden_positions[position]);
                    ZUIFeedMouseButton(&ctx, 0, true);
                    route(); // Revalidate at the press, before any new overlay/render.
                    EXPECT_FALSE(session.Active());
                    viewport->UpdateHover(session, captured(), &ctx, true);
                    EXPECT_EQ(viewport->Hovered(), Handle::None);
                    EXPECT_FALSE(ctx.InteractionCapture.Pointer);
                    EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
                    EXPECT_EQ(viewport->Hits().Pick(point).Handle, 0u);
                    viewport->BuildRender(session, captured(), &ctx, true, 1600, 1200, output);
                    if (position + 1 < hidden_positions.size())
                    {
                        EXPECT_TRUE(output.Empty());
                        EXPECT_EQ(viewport->Hits().Count, 0u);
                    }
                    else
                    {
                        // Offscreen geometry stays unclamped. Viewport/scissor
                        // clipping, not clamping, excludes its hit padding.
                        EXPECT_FALSE(output.Empty());
                        ASSERT_GT(viewport->Hits().Count, 0u);
                        for (const auto& shape : viewport->Hits().View())
                        {
                            EXPECT_LT(shape.Points[0].x, rect[0]);
                        }
                    }

                    camera.SetPosition({0.f, 0.f, 10.f});
                    route(); // Same retained press cannot start after recovery.
                    EXPECT_FALSE(session.Active());
                    viewport->BuildRender(session, captured(), &ctx, true, 1600, 1200, output);
                    ASSERT_FALSE(output.Empty());
                    EXPECT_EQ(viewport->Hits().Pick(point).Handle, uint32_t(wanted));
                    ZUIFeedMouseButton(&ctx, 0, false);
                    route();
                    ZUIFeedMouseButton(&ctx, 0, true);
                    route();
                    ASSERT_TRUE(session.Active()); // Only a fresh physical press.
                    EXPECT_TRUE(viewport->OwnsPointerCapture(session, &ctx));
                    ctx.EscapePressed = true;
                    route();
                    ZUIFeedMouseButton(&ctx, 0, false);
                    ZUIFeedMousePos(&ctx, 100.f, 100.f);
                    route();
                    EXPECT_FALSE(session.Active());
                    EXPECT_FALSE(ctx.InteractionCapture.Pointer);
                    EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
                    EXPECT_EQ(session.History().UndoCount(), 0u);
                    EXPECT_EQ(session.History().RedoCount(), 0u);
                    const auto* authored = scene.GetComponent<TransformComponent>(entity);
                    ASSERT_NE(authored, nullptr);
                    for (uint32_t axis = 0; axis < 3; ++axis)
                    {
                        EXPECT_FLOAT_EQ(authored->Position[axis], 0.f);
                        EXPECT_FLOAT_EQ(authored->Rotation[axis], 0.f);
                        EXPECT_FLOAT_EQ(authored->Scale[axis], 1.f);
                    }
                }
            }
        }
    }
    EXPECT_EQ(ctx.FrameIndex, ui_frame);
    EXPECT_EQ(memory.MainArena.m_current_offset, arena_bytes);
    EXPECT_EQ(ctx.FrameArena.m_current_offset, ui_bytes);
}

TEST_F(GizmoViewportTest, RenderSnapshotDoesNotFollowLaterAuthoredChanges)
{
    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    const GizmoFrameData retained                              = output;
    scene.GetComponent<TransformComponent>(entity)->Position.x = 1.f;
    scene.SynchronizeHierarchy();
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    ASSERT_EQ(output.SpriteCount, 1u);
    EXPECT_FLOAT_EQ(retained.Sprites[0].Position.x, 0.f);
    EXPECT_FLOAT_EQ(output.Sprites[0].Position.x, 1.f);
}

TEST_F(GizmoViewportTest, HighlightedRenderRemovesHandlesWhoseDimmedAlphaBecomesInvisible)
{
    constexpr float axis_dot = 0.9899f;
    camera.SetPosition({axis_dot * 10.f, 0.f, std::sqrt(1.f - axis_dot * axis_dot) * 10.f});
    camera.SetOrientation(0.f, -std::asin(axis_dot) * 180.f / 3.14159265358979323846f);
    Update();
    ASSERT_EQ(viewport->Hovered(), Handle::TranslateView);
    bool idle_x = false;
    for (const auto& hit : viewport->Hits().View())
    {
        idle_x |= hit.Handle == uint32_t(Handle::TranslateX);
    }
    ASSERT_TRUE(idle_x);
    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    ASSERT_FALSE(output.Empty());
    for (const auto& hit : viewport->Hits().View())
    {
        EXPECT_NE(hit.Handle, uint32_t(Handle::TranslateX));
    }
}

TEST_F(GizmoViewportTest, InvalidRenderExtentRetiresPreviousGeometryInsteadOfKeepingInvisibleHits)
{
    Update();
    ASSERT_GT(viewport->Hits().Count, 0u);
    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 0, 600, output);
    EXPECT_TRUE(output.Empty());
    EXPECT_EQ(viewport->Hits().Count, 0u);
    EXPECT_EQ(viewport->Hovered(), Handle::None);
}

TEST_F(GizmoViewportTest, ModeChangeClearsRetainedHotActiveAndPaintWithoutAuthoredMutation)
{
    Update();
    ctx.ActiveKey    = 99;
    ctx.MouseDown[0] = true;
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Select}));
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.ProcessRequests());
    session.EndMutationPhase();
    Update();
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_EQ(viewport->Hovered(), Handle::None);
    EXPECT_EQ(viewport->Hits().Count, 0u);
    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    EXPECT_TRUE(output.Empty());
    EXPECT_FALSE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoViewportTest, ActiveNonmeshPresentationUsesActualPreviewAndStableStartMarkerWithoutAllocating)
{
    ASSERT_TRUE(BeginTranslation());
    const auto     bytes       = memory.MainArena.m_current_offset;
    const auto     frame_bytes = ctx.FrameArena.m_current_offset;
    GizmoFrameData output      = {};
    for (uint32_t i = 0; i < 1000; ++i)
    {
        ASSERT_TRUE(Preview(i % 2 ? .5f : 1.f));
        TranslationSnapshot snapshot = {};
        ASSERT_TRUE(session.ReadTranslationSnapshot(snapshot));
        viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 1600, 1200, output);
        ASSERT_GT(output.LineCount, 5u);
        ASSERT_EQ(output.SpriteCount, 1u);
        EXPECT_EQ(output.MeshLineGroupCount, 0u);
        EXPECT_FALSE(output.DecorationsOmitted);
        EXPECT_FLOAT_EQ(output.Lines[0].A.x, 0.f);
        EXPECT_FLOAT_EQ(output.Lines[0].B.x, snapshot.Pivot.x);
        EXPECT_FLOAT_EQ(output.Sprites[0].Position.x, snapshot.Pivot.x);
        EXPECT_FLOAT_EQ(output.Lines[0].Width, Constants::GuideWidthPx * 2.f);
        EXPECT_FLOAT_EQ(output.Lines[1].DashOn, Constants::GhostDashOnPx * 2.f);
        const Vec3f marker_center = (output.Lines[1].A + output.Lines[3].A) * .5f;
        EXPECT_NEAR(marker_center.x, 0.f, .0001f);
        EXPECT_NEAR(marker_center.y, 0.f, .0001f);
        EXPECT_EQ(viewport->Hovered(), Handle::None);
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, bytes);
    EXPECT_EQ(ctx.FrameArena.m_current_offset, frame_bytes);
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoViewportTest, PendingOrInvalidPreviewCannotPublishStaleDragDecorations)
{
    ASSERT_TRUE(BeginTranslation());
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.UpdateTranslation(Screen(drag_grab + Vec3f(.5f, 0.f, 0.f)), true));
    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    EXPECT_TRUE(output.Empty());
    EXPECT_EQ(viewport->Hits().Count, 0u);
    session.EndMutationPhase();
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    ASSERT_EQ(output.SpriteCount, 1u);
    EXPECT_NEAR(output.Sprites[0].Position.x, .5f, .0002f);
    scene.GetComponent<TransformComponent>(entity)->Position.x = 7.f;
    scene.SynchronizeHierarchy();
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    EXPECT_TRUE(output.Empty());
    EXPECT_EQ(viewport->Hits().Count, 0u);
}

TEST_F(GizmoViewportMeshTest, CapturesOrientedStartBoxOnceAndUsesOnlyCopiedBoundsForLaterPreviews)
{
    const Vec3f minimum(-.4f, -.6f, -.2f), maximum(.5f, .3f, .7f);
    Ingest(minimum, maximum);
    auto* transform     = scene.GetComponent<TransformComponent>(entity);
    transform->Rotation = {.2f, -.3f, .4f};
    transform->Scale    = {-1.2f, .8f, 1.5f};
    ASSERT_TRUE(BeginTranslation());
    ASSERT_TRUE(Preview(.5f));
    TranslationSnapshot snapshot = {};
    ASSERT_TRUE(session.ReadTranslationSnapshot(snapshot));
    Geometry::BoxCorners start = {}, current = {};
    ASSERT_TRUE(Geometry::TransformBox(minimum, maximum, snapshot.StartWorld, &start));
    ASSERT_TRUE(Geometry::TransformBox(minimum, maximum, snapshot.World, &current));
    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    ASSERT_EQ(output.MeshLineGroupCount, 1u);
    const auto group = output.MeshLineGroups[0];
    EXPECT_EQ(group.MeshUUID, mesh_id);
    ASSERT_EQ(group.Count, 24u);
    const GizmoFrameData retained = output;
    uint32_t             edge     = 0;
    for (uint32_t corner = 0; corner < 8; ++corner)
    {
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (!(corner & (1u << axis)))
            {
                const auto& live  = output.Lines[group.First + edge];
                const auto& ghost = output.Lines[group.First + 12 + edge];
                for (uint32_t row = 0; row < 3; ++row)
                {
                    EXPECT_FLOAT_EQ(live.A[row], current[corner][row]);
                    EXPECT_FLOAT_EQ(live.B[row], current[corner | (1u << axis)][row]);
                    EXPECT_FLOAT_EQ(ghost.A[row], start[corner][row]);
                    EXPECT_FLOAT_EQ(ghost.B[row], start[corner | (1u << axis)][row]);
                }
                ++edge;
            }
        }
    }
    // A reload cannot mutate the captured start box or change its local bounds.
    Ingest({-2.f, -2.f, -2.f}, {2.f, 2.f, 2.f});
    ASSERT_TRUE(Preview(1.f));
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    ASSERT_EQ(output.MeshLineGroupCount, 1u);
    ASSERT_EQ(output.MeshLineGroups[0].Count, 24u);
    for (uint32_t i = 0; i < 24; ++i)
    {
        for (uint32_t row = 0; row < 3; ++row)
        {
            const auto& before = retained.Lines[group.First + i];
            const auto& after  = output.Lines[output.MeshLineGroups[0].First + i];
            EXPECT_NEAR(after.A[row] - before.A[row], i < 12 && row == 0 ? .5f : 0.f, .0002f);
            EXPECT_NEAR(after.B[row] - before.B[row], i < 12 && row == 0 ? .5f : 0.f, .0002f);
        }
    }
    const auto scene_bytes = memory.MainArena.m_current_offset;
    const auto asset_bytes = asset_memory.MainArena.m_current_offset;
    for (uint32_t i = 0; i < 1000; ++i)
    {
        viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, scene_bytes);
    EXPECT_EQ(asset_memory.MainArena.m_current_offset, asset_bytes);

    // Even without an idle presentation between drags, a new physical serial
    // must capture the reloaded box instead of reusing the previous ghost.
    ASSERT_TRUE(session.BeginMutationPhase());
    session.Cancel();
    session.EndMutationPhase();
    ASSERT_TRUE(BeginTranslation(2));
    ASSERT_TRUE(Preview(.5f));
    ASSERT_TRUE(session.ReadTranslationSnapshot(snapshot));
    ASSERT_TRUE(Geometry::TransformBox({-2.f, -2.f, -2.f}, {2.f, 2.f, 2.f}, snapshot.StartWorld, &start));
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    ASSERT_EQ(output.MeshLineGroupCount, 1u);
    ASSERT_EQ(output.MeshLineGroups[0].Count, 24u);
    for (uint32_t row = 0; row < 3; ++row)
    {
        EXPECT_FLOAT_EQ(output.Lines[output.MeshLineGroups[0].First + 12].A[row], start[0][row]);
    }
}

TEST_F(GizmoViewportMeshTest, ScaleRendersLiveAndCapturedMeshBoundsWithoutAccumulatedErrorOrArenaGrowth)
{
    const Vec3f minimum(-.4f, -.6f, -.2f), maximum(.5f, .3f, .7f);
    Ingest(minimum, maximum);
    scene.GetComponent<TransformComponent>(entity)->Rotation = {.2f, -.3f, .4f};
    scene.GetComponent<TransformComponent>(entity)->Scale    = {-1.2f, .8f, 1.5f};
    scene.SynchronizeHierarchy();
    ScaleMode();
    ASSERT_TRUE(ScalePress());
    ScaleMove(1.5f);
    Route();
    ScaleSnapshot snapshot = {};
    ASSERT_TRUE(session.ReadScaleSnapshot(snapshot));
    Geometry::BoxCorners start = {}, current = {};
    ASSERT_TRUE(Geometry::TransformBox(minimum, maximum, snapshot.StartWorld, &start));
    ASSERT_TRUE(Geometry::TransformBox(minimum, maximum, snapshot.World, &current));
    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    ASSERT_EQ(output.MeshLineGroupCount, 1u);
    const auto group = output.MeshLineGroups[0];
    EXPECT_EQ(group.MeshUUID, mesh_id);
    ASSERT_EQ(group.Count, 24u);
    uint32_t edge = 0;
    for (uint32_t corner = 0; corner < 8; ++corner)
    {
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (!(corner & (1u << axis)))
            {
                for (uint32_t row = 0; row < 3; ++row)
                {
                    EXPECT_FLOAT_EQ(output.Lines[group.First + edge].A[row], current[corner][row]);
                    EXPECT_FLOAT_EQ(output.Lines[group.First + edge].B[row], current[corner | (1u << axis)][row]);
                    EXPECT_FLOAT_EQ(output.Lines[group.First + 12 + edge].A[row], start[corner][row]);
                    EXPECT_FLOAT_EQ(output.Lines[group.First + 12 + edge].B[row], start[corner | (1u << axis)][row]);
                }
                ++edge;
            }
        }
    }
    Ingest({-2.f, -2.f, -2.f}, {2.f, 2.f, 2.f});
    ScaleMove(1.75f);
    Route();
    const auto bytes       = memory.MainArena.m_current_offset;
    const auto asset_bytes = asset_memory.MainArena.m_current_offset;
    for (uint32_t i = 0; i < 1000; ++i)
    {
        viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    }
    ASSERT_EQ(output.MeshLineGroupCount, 1u);
    for (uint32_t row = 0; row < 3; ++row)
    {
        EXPECT_FLOAT_EQ(output.Lines[output.MeshLineGroups[0].First + 12].A[row], start[0][row]);
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, bytes);
    EXPECT_EQ(asset_memory.MainArena.m_current_offset, asset_bytes);
    ctx.EscapePressed = true;
    Route();
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoViewportMeshTest, RetriesInitiallyUnavailableBoundsButNeverRetargetsToAnotherMesh)
{
    ASSERT_TRUE(BeginTranslation());
    ASSERT_TRUE(Preview(.5f));
    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    EXPECT_EQ(output.MeshLineGroupCount, 0u);
    Ingest({-.4f, -.6f, -.2f}, {.5f, .3f, .7f});
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    ASSERT_EQ(output.MeshLineGroupCount, 1u);
    const auto original = mesh_id;
    mesh_id             = *uuids::uuid::from_string("33333333-3333-4333-8333-333333333333");
    hierarchy_id        = *uuids::uuid::from_string("44444444-4444-4444-8444-444444444444");
    Ingest({-2.f, -2.f, -2.f}, {2.f, 2.f, 2.f});
    scene.GetComponent<MeshComponent>(entity)->MeshUUID = mesh_id;
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    EXPECT_EQ(output.MeshLineGroupCount, 0u);
    ASSERT_EQ(output.SpriteCount, 1u);
    scene.GetComponent<MeshComponent>(entity)->MeshUUID = original;
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    ASSERT_EQ(output.MeshLineGroupCount, 1u);
    EXPECT_EQ(output.MeshLineGroups[0].MeshUUID, original);
}

TEST_F(GizmoViewportMeshTest, GroupDecoratesOnlyAuthoredRootsAndUsesTheirOwnActualOrigins)
{
    const Vec3f minimum(-.2f, -.3f, -.1f), maximum(.3f, .2f, .4f);
    Ingest(minimum, maximum);
    const auto parent = ExtraEntity(2, {-1.f, 0.f, 0.f}), other = ExtraEntity(3, {1.f, 0.f, 0.f}), plain = ExtraEntity(4, {0.f, 1.f, 0.f});
    scene.GetComponent<TransformComponent>(entity)->Position = {.2f, .3f, 0.f};
    ASSERT_TRUE(scene.SetParent(entity, parent));
    scene.AddComponent<MeshComponent>(parent, {.MeshUUID = mesh_id});
    scene.AddComponent<MeshComponent>(other, {.MeshUUID = mesh_id});
    ChooseGroup({entity, parent, other, plain});
    ASSERT_TRUE(BeginTranslation());
    ASSERT_TRUE(Preview(.5f));
    RootFrameView frames = {};
    ASSERT_TRUE(session.ReadGizmoRootFrames(frames));
    ASSERT_EQ(frames.TotalRoots, 3u);
    ASSERT_EQ(frames.Frames.size(), 3u);
    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    ASSERT_EQ(output.MeshLineGroupCount, 2u); // the selected mesh child is pruned
    EXPECT_FALSE(output.DecorationsOmitted);
    EXPECT_EQ(session.Selection().ActiveUUID(), session.TargetFor(entity).UUID);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, .2f);
    for (uint32_t i = 0; i < 2; ++i)
    {
        const auto& root  = frames.Frames[i];
        const auto& world = scene.GetComponent<TransformComponent>(root.Target.CachedEntity)->WorldTransform;
        ExpectRootBox(output, i, minimum, maximum, root.StartWorld, world, mesh_id);
        const auto& guide = output.Lines[output.MeshLineGroups[i].First - 1];
        for (uint32_t row = 0; row < 3; ++row)
        {
            EXPECT_FLOAT_EQ(guide.A[row], root.StartWorld(row, 3));
            EXPECT_FLOAT_EQ(guide.B[row], world(row, 3));
        }
    }
    const auto& plain_guide = output.Lines[output.MeshLineGroups[1].First + 24];
    EXPECT_FLOAT_EQ(plain_guide.A.x, 0.f);
    EXPECT_NEAR(plain_guide.B.x, .5f, .0002f);
    EXPECT_FLOAT_EQ(plain_guide.A.y, 1.f);
}

TEST_F(GizmoViewportMeshTest, GroupTranslateRotateAndScaleBoxesMatchEachRootsCapturedAndCurrentWorld)
{
    const Vec3f minimum(-.2f, -.3f, -.1f), maximum(.3f, .2f, .4f);
    Ingest(minimum, maximum);
    const auto other = ExtraEntity(2, {1.2f, .3f, 0.f});
    scene.AddComponent<MeshComponent>(other, {.MeshUUID = mesh_id});
    scene.GetComponent<TransformComponent>(other)->Rotation = {.2f, -.3f, .4f};
    scene.GetComponent<TransformComponent>(other)->Scale    = {-1.2f, .8f, 1.5f};
    ChooseGroup({entity, other});
    GizmoFrameData output = {};
    for (auto mode : {Operation::Translate, Operation::Rotate, Operation::Scale})
    {
        SCOPED_TRACE(uint32_t(mode));
        if (mode == Operation::Translate)
        {
            ASSERT_TRUE(BeginTranslation());
            ASSERT_TRUE(Preview(.5f));
        }
        else if (mode == Operation::Rotate)
        {
            RotationMode(false);
            ASSERT_TRUE(RotationPress());
            RotationMove(.4);
            Route();
        }
        else
        {
            ScaleMode(false);
            ASSERT_TRUE(ScalePress());
            ScaleMove(1.5f);
            Route();
        }
        RootFrameView frames = {};
        ASSERT_TRUE(session.ReadGizmoRootFrames(frames));
        viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
        ASSERT_EQ(output.MeshLineGroupCount, 2u);
        EXPECT_FALSE(output.DecorationsOmitted);
        EXPECT_TRUE(output.ValidMeshLineGroups());
        for (uint32_t i = 0; i < 2; ++i)
        {
            const auto& root = frames.Frames[i];
            ExpectRootBox(output, i, minimum, maximum, root.StartWorld, scene.GetComponent<TransformComponent>(root.Target.CachedEntity)->WorldTransform, mesh_id);
        }
        ASSERT_TRUE(session.BeginMutationPhase());
        session.Cancel();
        session.EndMutationPhase();
        ZUIFeedMouseButton(&ctx, 0, false);
        Route();
        BuildUI();
    }
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoViewportMeshTest, GroupBoundsRetryThenFreezeAndANewCaptureRecachesWithoutAnIdleOverlay)
{
    const auto other = ExtraEntity(2, {1.2f, 0.f, 0.f});
    scene.AddComponent<MeshComponent>(other, {.MeshUUID = mesh_id});
    ChooseGroup({entity, other});
    ASSERT_TRUE(BeginTranslation());
    ASSERT_TRUE(Preview(.5f));
    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    EXPECT_EQ(output.MeshLineGroupCount, 0u);
    const Vec3f minimum(-.2f, -.3f, -.1f), maximum(.3f, .2f, .4f);
    Ingest(minimum, maximum);
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    ASSERT_EQ(output.MeshLineGroupCount, 2u);
    Ingest({-.5f, -.5f, -.5f}, {.5f, .5f, .5f});
    ASSERT_TRUE(Preview(1.f));
    RootFrameView frames = {};
    ASSERT_TRUE(session.ReadGizmoRootFrames(frames));
    const auto bytes = memory.MainArena.m_current_offset, asset_bytes = asset_memory.MainArena.m_current_offset, ui_bytes = ctx.FrameArena.m_current_offset;
    for (uint32_t i = 0; i < 100; ++i)
    {
        viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    }
    for (uint32_t i = 0; i < 2; ++i)
    {
        const auto& root = frames.Frames[i];
        ExpectRootBox(output, i, minimum, maximum, root.StartWorld, scene.GetComponent<TransformComponent>(root.Target.CachedEntity)->WorldTransform, mesh_id);
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, bytes);
    EXPECT_EQ(asset_memory.MainArena.m_current_offset, asset_bytes);
    EXPECT_EQ(ctx.FrameArena.m_current_offset, ui_bytes);
    const auto generation = frames.Generation;
    ASSERT_TRUE(session.BeginMutationPhase());
    session.Cancel();
    session.EndMutationPhase();
    ASSERT_TRUE(BeginTranslation()); // same physical test token, no idle render
    ASSERT_TRUE(session.ReadGizmoRootFrames(frames));
    EXPECT_NE(frames.Generation, generation);
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    ASSERT_EQ(output.MeshLineGroupCount, 2u);
    for (uint32_t i = 0; i < 2; ++i)
    {
        const auto& root = frames.Frames[i];
        ExpectRootBox(output, i, {-.5f, -.5f, -.5f}, {.5f, .5f, .5f}, root.StartWorld, scene.GetComponent<TransformComponent>(root.Target.CachedEntity)->WorldTransform, mesh_id);
    }
}

TEST_F(GizmoViewportMeshTest, ChangedMeshBeforeTheFirstGroupRenderCannotRetargetThePressTimeGhost)
{
    Ingest({-.2f, -.3f, -.1f}, {.3f, .2f, .4f});
    const auto original = mesh_id;
    const auto other    = ExtraEntity(2, {1.2f, 0.f, 0.f});
    scene.AddComponent<MeshComponent>(other, {.MeshUUID = original});
    ChooseGroup({entity, other});
    ASSERT_TRUE(BeginTranslation());
    ASSERT_TRUE(Preview(.5f));
    mesh_id      = *uuids::uuid::from_string("33333333-3333-4333-8333-333333333333");
    hierarchy_id = *uuids::uuid::from_string("44444444-4444-4444-8444-444444444444");
    Ingest({-.5f, -.5f, -.5f}, {.5f, .5f, .5f});
    scene.GetComponent<MeshComponent>(entity)->MeshUUID = mesh_id;
    GizmoFrameData output                               = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    ASSERT_EQ(output.MeshLineGroupCount, 1u);
    EXPECT_EQ(output.MeshLineGroups[0].MeshUUID, original);
    EXPECT_GT(output.MeshLineGroups[0].First, 4u); // first root uses a center marker
    scene.GetComponent<MeshComponent>(entity)->MeshUUID = original;
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    EXPECT_EQ(output.MeshLineGroupCount, 2u);
}

TEST_F(GizmoViewportMeshTest, GroupDecorationCacheNeverCrossesSessionOwnershipWithMatchingEpochAndGeneration)
{
    Ingest({-.2f, -.3f, -.1f}, {.3f, .2f, .4f});
    const auto other = ExtraEntity(2, {1.2f, 0.f, 0.f});
    scene.AddComponent<MeshComponent>(other, {.MeshUUID = mesh_id});
    ChooseGroup({entity, other});
    ASSERT_TRUE(BeginTranslation());
    RootFrameView first = {};
    ASSERT_TRUE(session.ReadGizmoRootFrames(first));
    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    ASSERT_EQ(output.MeshLineGroupCount, 2u);
    ASSERT_TRUE(session.BeginMutationPhase());
    session.Cancel();
    session.EndMutationPhase();
    Ingest({-.5f, -.5f, -.5f}, {.5f, .5f, .5f});
    EditorSession replacement = {};
    ASSERT_TRUE(replacement.Initialize(&memory.MainArena, SessionConfig()));
    ASSERT_TRUE(replacement.BeginMutationPhase());
    ASSERT_TRUE(replacement.BindScene(&scene));
    const EntityTarget targets[2] = {replacement.TargetFor(entity), replacement.TargetFor(other)};
    ASSERT_TRUE(replacement.SetSelection({targets, 2}, targets[0].UUID));
    ASSERT_TRUE(replacement.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Translate}));
    ASSERT_TRUE(replacement.ProcessRequests());
    TranslationBegin begin = {
        .Projection = RotationProjection(), .Bounds = {{rect[0], rect[1]}, {800.f, 600.f}},
             .Pointer = Screen({.7f, 0.f, 0.f}
             ), .Key = 99, .PointerSerial = 1, .Constraint = Handle::TranslateX
    };
    ASSERT_TRUE(ResolveTargetFrame(replacement, begin.Expected));
    ASSERT_TRUE(replacement.BeginTranslation(begin));
    replacement.EndMutationPhase();
    RootFrameView second = {};
    ASSERT_TRUE(replacement.ReadGizmoRootFrames(second));
    EXPECT_EQ(replacement.SceneEpoch(), session.SceneEpoch());
    EXPECT_EQ(first.Generation, second.Generation);
    viewport->BuildRender(replacement, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    ASSERT_EQ(output.MeshLineGroupCount, 2u);
    for (uint32_t i = 0; i < 2; ++i)
    {
        const auto& root = second.Frames[i];
        ExpectRootBox(output, i, {-.5f, -.5f, -.5f}, {.5f, .5f, .5f}, root.StartWorld, scene.GetComponent<TransformComponent>(root.Target.CachedEntity)->WorldTransform, mesh_id);
    }
    replacement.Shutdown();
}

TEST_F(GizmoViewportMeshTest, ForeignEditToANonactiveRootSuppressesTheEntireGroupPresentation)
{
    Ingest({-.2f, -.3f, -.1f}, {.3f, .2f, .4f});
    const auto other = ExtraEntity(2, {1.2f, 0.f, 0.f});
    scene.AddComponent<MeshComponent>(other, {.MeshUUID = mesh_id});
    ChooseGroup({entity, other});
    ASSERT_TRUE(BeginTranslation());
    ASSERT_TRUE(Preview(.5f));
    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    ASSERT_EQ(output.MeshLineGroupCount, 2u);
    scene.GetComponent<TransformComponent>(other)->Position.y = 1.f;
    scene.SynchronizeHierarchy();
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    EXPECT_TRUE(output.Empty());
    EXPECT_EQ(viewport->Hits().Count, 0u);
    ASSERT_TRUE(session.BeginMutationPhase());
    session.Cancel();
    session.EndMutationPhase();
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(other)->Position.y, 1.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoViewportLargeGroupTest, FourThousandRootsRemainAtomicWhenDecorationCapacityIsExceeded)
{
    constexpr uint32_t count = 4096;
    Ingest({-.2f, -.3f, -.1f}, {.3f, .2f, .4f});
    ZEngine::Core::Containers::FixedArray<EntityTarget, count> targets = {};
    targets[0]                                                         = session.TargetFor(entity);
    for (uint32_t i = 1; i < count; ++i)
    {
        const auto root = ExtraEntity(i + 1);
        scene.AddComponent<MeshComponent>(root, {.MeshUUID = mesh_id});
        targets[i] = session.TargetFor(root);
    }
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.SetSelection({targets.data(), count}, targets[count - 1].UUID));
    session.EndMutationPhase();
    scene.SynchronizeHierarchy();
    BuildUI();
    const auto bytes = memory.MainArena.m_current_offset, asset_bytes = asset_memory.MainArena.m_current_offset, ui_bytes = ctx.FrameArena.m_current_offset;
    ASSERT_TRUE(BeginTranslation());
    ASSERT_EQ(session.TransformCount(), count);
    ASSERT_TRUE(Preview(.5f));
    RootFrameView frames = {};
    ASSERT_TRUE(session.ReadGizmoRootFrames(frames));
    EXPECT_EQ(frames.TotalRoots, count);
    ASSERT_EQ(frames.Frames.size(), RootFrameView::Capacity);
    EXPECT_EQ(frames.Frames[0].Target.UUID, targets[count - 1].UUID);
    GizmoFrameData output = {};
    for (uint32_t i = 0; i < 8; ++i)
    {
        viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    }
    EXPECT_FALSE(output.Empty());
    EXPECT_TRUE(output.DecorationsOmitted);
    EXPECT_TRUE(output.ValidCounts());
    EXPECT_TRUE(output.ValidMeshLineGroups());
    ASSERT_GT(output.MeshLineGroupCount, 0u);
    EXPECT_LT(output.MeshLineGroupCount, RootFrameView::Capacity);
    EXPECT_GT(viewport->Hits().Count, 0u);
    EXPECT_EQ(output.SpriteCount, 1u);
    for (const auto& target : targets)
    {
        EXPECT_NEAR(scene.GetComponent<TransformComponent>(target.CachedEntity)->Position.x, .5f, .0002f);
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, bytes);
    EXPECT_EQ(asset_memory.MainArena.m_current_offset, asset_bytes);
    EXPECT_EQ(ctx.FrameArena.m_current_offset, ui_bytes);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.UpdateTranslation(Screen(drag_grab + Vec3f(.5f, 0.f, 0.f)), false));
    ASSERT_EQ(session.History().UndoCount(), 1u);
    ASSERT_TRUE(session.Undo());
    session.EndMutationPhase();
    for (const auto& target : targets)
    {
        EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(target.CachedEntity)->Position.x, 0.f);
    }
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoViewportTest, PhysicalPressUsesLatchedGrabAndReleaseUsesFinalEdgeWithoutAnOverlayBuild)
{
    PhysicalPress();
    const auto serial = ctx.MousePressSerial[0];
    Move({1.2f, 0.f, 0.f});  // Movement already arrived after pointer-down.
    ZUIFeedBeginFrame(&ctx); // No transient press edge remains.
    Route();
    ASSERT_TRUE(session.TranslationActive());
    EXPECT_EQ(session.TranslationPointerSerial(), serial);
    EXPECT_EQ(ctx.ActiveKey, 99u);
    EXPECT_TRUE(ZUIGetInputCapture(&ctx).Pointer);
    EXPECT_TRUE(ZUIGetInputCapture(&ctx).Keyboard);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, .5f, .0002f);
    Move({1.7f, 0.f, 0.f});
    ZUIFeedMouseButton(&ctx, 0, false);
    Move({8.f, 0.f, 0.f}); // Later uncaptured movement is not the final sample.
    ZUIFeedBeginFrame(&ctx);
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_FALSE(ctx.InteractionCapture.Pointer);
    EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, 1.f, .0002f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    for (uint32_t i = 0; i < 10; ++i)
    {
        Route();
    }
    EXPECT_EQ(session.History().UndoCount(), 1u);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    ASSERT_TRUE(session.Redo());
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, 1.f, .0002f);
    session.EndMutationPhase();
}

TEST_F(GizmoViewportTest, HoverAfterMutationPhasePreservesTheOwnedPressThroughCommitAndHistory)
{
    // Native Editor::OnUpdate routes the physical press before UpdateHover.
    // Eligibility then rejects new starts because the session is already active;
    // that must not reinterpret the captured press as camera navigation.
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Translation, .Enabled = false}));
    ASSERT_TRUE(session.ProcessRequests());
    session.EndMutationPhase();
    PhysicalPress();
    Route();
    ASSERT_TRUE(session.TranslationActive());
    const auto serial = ctx.MousePressSerial[0];
    Update();
    EXPECT_EQ(ctx.ActiveKey, 99u);
    EXPECT_EQ(session.TranslationPointerSerial(), serial);
    EXPECT_TRUE(ZUIGetInputCapture(&ctx).Pointer);
    EXPECT_TRUE(ZUIGetInputCapture(&ctx).Keyboard);
    for (uint32_t sample = 1; sample <= 4; ++sample)
    {
        Move({.7f + float(sample) * .25f, 0.f, 0.f});
        Route();
        ASSERT_TRUE(session.TranslationActive());
        Update();
        EXPECT_EQ(ctx.ActiveKey, 99u);
        EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, float(sample) * .25f, .0002f);
    }
    ZUIFeedMouseButton(&ctx, 0, false);
    Route();
    Update();
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, 1.f, .0002f);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    ASSERT_TRUE(session.Redo());
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, 1.f, .0002f);
    session.EndMutationPhase();
}

TEST_F(GizmoViewportTest, CompletePhysicalClickBetweenUpdatesCommitsOnlyOneChangedDrag)
{
    PhysicalPress();
    Move({1.7f, 0.f, 0.f});
    ZUIFeedMouseButton(&ctx, 0, false);
    ZUIFeedBeginFrame(&ctx);
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, 1.f, .0002f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
}

TEST_F(GizmoViewportTest, PhysicalMultiRootReleaseWorksWithoutRebuildingTheOverlayAndDoesNotDoubleMoveAnActiveChild)
{
    const auto parent = ExtraEntity(2), other = ExtraEntity(3, {2.f, 0.f, 0.f});
    ASSERT_TRUE(scene.SetParent(entity, parent));
    ChooseGroup({entity, parent, other});
    const auto frame = ctx.FrameIndex;
    PhysicalPress();
    Route();
    ASSERT_TRUE(session.TranslationActive());
    EXPECT_EQ(session.TransformCount(), 2u);
    EXPECT_TRUE(ctx.InteractionCapture.Keyboard);
    Move({1.2f, 0.f, 0.f});
    Route();
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(parent)->Position.x, .5f, .0002f);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(other)->Position.x, 2.5f, .0002f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->WorldTransform(0, 3), .5f, .0002f);
    ZUIFeedMouseButton(&ctx, 0, false);
    Move({4.f, 0.f, 0.f}); // Later motion cannot replace the physical release sample.
    Route();
    EXPECT_EQ(ctx.FrameIndex, frame);
    EXPECT_FALSE(session.Active());
    EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    EXPECT_EQ(session.Selection().ActiveUUID(), session.TargetFor(entity).UUID);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(other)->Position.x, 2.5f, .0002f);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.Undo());
    session.EndMutationPhase();
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(parent)->Position.x, 0.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(other)->Position.x, 2.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->WorldTransform(0, 3), 0.f);
}

TEST_F(GizmoViewportTest, PhysicalGroupScaleEscRestoresEveryRootAndRetiresCaptureWithoutAnOverlaySlot)
{
    const auto other = ExtraEntity(2, {2.f, 0.f, 0.f});
    ChooseGroup({entity, other});
    ScaleMode();
    const auto frame = ctx.FrameIndex;
    ASSERT_TRUE(ScalePress());
    ASSERT_EQ(session.TransformCount(), 2u);
    ScaleMove(1.5f);
    Route();
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Scale.x, 1.5f, .0002f);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(other)->Position.x, 3.f, .0002f);
    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    EXPECT_FALSE(output.Empty());
    ctx.EscapePressed = true;
    Route();
    EXPECT_EQ(ctx.FrameIndex, frame);
    EXPECT_FALSE(session.Active());
    EXPECT_FALSE(ctx.InteractionCapture.Pointer);
    EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Scale.x, 1.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(other)->Position.x, 2.f);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoViewportTest, ASelectedNonactiveTransformlessTargetSuppressesAllTransformHandles)
{
    const auto other = ExtraEntity(2, {}, false);
    ChooseGroup({entity, other});
    Update();
    EXPECT_EQ(viewport->Hits().Count, 0u);
    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    EXPECT_TRUE(output.Empty());
    PhysicalPress();
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.Selection().Entities().size(), 2u);
}

TEST_F(GizmoViewportTest, PhysicalNoopAddsNoHistoryAndCannotRearmWhileItsPressIsRetained)
{
    PhysicalPress();
    Route();
    ASSERT_TRUE(session.TranslationActive());
    ZUIFeedMouseButton(&ctx, 0, false);
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
    ctx.MousePressed[0] = true; // Retained stale UI edge, not a new physical serial.
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoViewportTest, BeginGateConsumesPhysicalPressWithoutMutatingOrReplayingWhenEnabled)
{
    PhysicalPress();
    Route(true, false);
    EXPECT_FALSE(session.Active());
    Move({1.2f, 0.f, 0.f});
    Route(true, true);
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    ZUIFeedMouseButton(&ctx, 0, false);
    PhysicalPress();
    Route();
    ASSERT_TRUE(session.TranslationActive());
    // Disabling new starts must not disable release of an already-owned drag.
    Move({1.7f, 0.f, 0.f});
    ZUIFeedMouseButton(&ctx, 0, false);
    Route(true, false);
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 1u);
}

TEST_F(GizmoViewportTest, PressOutsideAHandleCannotBeStolenAfterCursorCrossesIt)
{
    ZUIFeedMousePos(&ctx, 100.f, 100.f);
    ZUIFeedMouseButton(&ctx, 0, true);
    Move({.7f, 0.f, 0.f});
    Route();
    EXPECT_FALSE(session.Active());
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
}

TEST_F(GizmoViewportTest, NewLayoutNeverReplaysAPressThatPredatesItsBinding)
{
    PhysicalPress();
    viewport->Unbind();
    viewport->Bind(&ctx, rect, 42, 99);
    Route();
    EXPECT_FALSE(session.Active());
    ZUIFeedMouseButton(&ctx, 0, false);
    PhysicalPress();
    Route();
    EXPECT_TRUE(session.TranslationActive());
}

TEST_F(GizmoViewportTest, EscapeCancelsExactPreviewWithoutRearmingOrRetainingKeyboardCapture)
{
    PhysicalPress();
    Route();
    Move({1.2f, 0.f, 0.f});
    Route();
    ASSERT_NE(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    ctx.EscapePressed = true;
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_FALSE(ctx.EscapePressed);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_FALSE(ctx.InteractionCapture.Pointer);
    EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
    Route();
    EXPECT_FALSE(session.Active());
}

TEST_F(GizmoViewportTest, FocusLossAndMissingContextCancelAndDoNotReplayHeldPressOnReturn)
{
    for (bool missing : {false, true})
    {
        PhysicalPress();
        Route();
        ASSERT_TRUE(session.TranslationActive());
        Move({1.2f, 0.f, 0.f});
        Route();
        if (missing)
        {
            ASSERT_TRUE(session.BeginMutationPhase());
            viewport->UpdateTransformInput(session, camera.CaptureFrameData(), nullptr, true, true);
            session.EndMutationPhase();
        }
        else
        {
            Route(false);
        }
        EXPECT_FALSE(session.Active());
        EXPECT_EQ(session.History().UndoCount(), 0u);
        EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
        Route();
        EXPECT_FALSE(session.Active());
        EXPECT_EQ(ctx.ActiveKey, 0u);
        EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
        ZUIFeedMouseButton(&ctx, 0, false);
        ctx.ActiveKey = 0;
    }
}

TEST_F(GizmoViewportTest, MissingReleaseAndForeignCaptureCancelRatherThanCommitOrEraseAnotherOwner)
{
    for (bool foreign : {false, true})
    {
        PhysicalPress();
        Route();
        ASSERT_TRUE(session.TranslationActive());
        Move({1.2f, 0.f, 0.f});
        Route();
        if (foreign)
        {
            ctx.ActiveKey = 123;
        }
        else
        {
            ctx.MouseDown[0] = false; // No matching physical release was delivered.
        }
        Route();
        EXPECT_FALSE(session.Active());
        EXPECT_EQ(ctx.ActiveKey, foreign ? 123u : 0u);
        EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
        EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
        EXPECT_EQ(session.History().UndoCount(), 0u);
        ZUIFeedMouseButton(&ctx, 0, false);
        ctx.ActiveKey = 0;
    }
}

TEST_F(GizmoViewportTest, CameraChangeCancelsEvenWhenAMatchingReleaseIsWaiting)
{
    PhysicalPress();
    Route();
    Move({1.2f, 0.f, 0.f});
    Route();
    ZUIFeedMouseButton(&ctx, 0, false);
    camera.SetPosition({1.f, 0.f, 10.f});
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
}

TEST_F(GizmoViewportTest, RebindResizeAndDpiChangesCancelCapturedMappingInsteadOfUsingNewCoordinates)
{
    for (uint32_t change = 0; change < 4; ++change)
    {
        PhysicalPress();
        Route();
        ASSERT_TRUE(session.TranslationActive());
        Move({1.2f, 0.f, 0.f});
        Route();
        if (change == 0)
        {
            const float moved[4] = {38.f, 61.f, 838.f, 661.f};
            viewport->Bind(&ctx, moved, 42, 99);
        }
        else if (change == 1)
        {
            ++ctx.ScreenW;
        }
        else if (change == 2)
        {
            ctx.InputScale[0] = 2.f;
        }
        else
        {
            viewport->Unbind();
            viewport->Bind(&ctx, rect, 42, 99);
        }
        Route();
        EXPECT_FALSE(session.Active());
        EXPECT_EQ(session.History().UndoCount(), 0u);
        EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
        ZUIFeedMouseButton(&ctx, 0, false);
        ctx.ScreenW       = 1280;
        ctx.InputScale[0] = 1.f;
        viewport->Bind(&ctx, rect, 42, 99);
    }
}

TEST_F(GizmoViewportTest, PopupTextAndReservedNavigationChordsCannotBeginOrRearmTranslation)
{
    for (uint32_t blocker = 0; blocker < 8; ++blocker)
    {
        PhysicalPress();
        if (blocker == 0)
        {
            ctx.PopupStackSize = 1;
        }
        else if (blocker == 1)
        {
            ctx.PendingPopupKey = 123;
        }
        else if (blocker == 2)
        {
            ctx.TextInputActive = true;
        }
        else if (blocker == 3)
        {
            ctx.ActiveModalKey = 123;
        }
        else if (blocker == 4)
        {
            ctx.AltDown = true;
        }
        else if (blocker == 5)
        {
            ctx.MouseDown[1] = true;
        }
        else if (blocker == 6)
        {
            ctx.MouseDown[2] = true;
        }
        else
        {
            ctx.InteractionCapture.Keyboard = true;
        }
        Route();
        EXPECT_FALSE(session.Active());
        ctx.PopupStackSize  = 0;
        ctx.PendingPopupKey = ctx.ActiveModalKey = 0;
        ctx.TextInputActive = ctx.AltDown = ctx.MouseDown[1] = ctx.MouseDown[2] = false;
        ctx.InteractionCapture.Keyboard                                         = false;
        Route();
        EXPECT_FALSE(session.Active());
        ZUIFeedMouseButton(&ctx, 0, false);
    }
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoViewportTest, OverlappingUiWinsAtPressPositionEvenIfCursorAlreadyMovedOffIt)
{
    PhysicalPress();
    Box(123, ctx.MousePressPos[0][0] - 4.f, ctx.MousePressPos[0][1] - 4.f, 8.f, 8.f);
    Move({1.2f, 0.f, 0.f});
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoViewportTest, ReleaseThenNewPressCommitsOldSampleWithoutStealingNewWidgetsInput)
{
    PhysicalPress();
    Route();
    Move({1.2f, 0.f, 0.f});
    ZUIFeedMouseButton(&ctx, 0, false);
    ZUIFeedMousePos(&ctx, 100.f, 100.f);
    ZUIFeedMouseButton(&ctx, 0, true);
    ctx.ActiveKey = 123;
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, .5f, .0002f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    EXPECT_EQ(ctx.ActiveKey, 123u);
    EXPECT_TRUE(ctx.MousePressed[0]);
    Route();
    EXPECT_FALSE(session.Active());
}

TEST_F(GizmoViewportTest, SessionLifecycleCancellationRetiresUiOwnershipAndCannotRearmOldPress)
{
    PhysicalPress();
    Route();
    Move({1.2f, 0.f, 0.f});
    Route();
    ASSERT_TRUE(session.BeginMutationPhase());
    session.ClearSelection();
    viewport->UpdateTransformInput(session, camera.CaptureFrameData(), &ctx, true, true);
    ASSERT_TRUE(session.Select(entity));
    session.EndMutationPhase();
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoViewportTest, PhysicalRouterRejectsWrongPhaseAndThreadBeforeConsumingThePress)
{
    PhysicalPress();
    viewport->UpdateTransformInput(session, camera.CaptureFrameData(), &ctx, true, true);
    EXPECT_FALSE(session.Active());
    ASSERT_TRUE(session.BeginMutationPhase());
    const auto  captured = camera.CaptureFrameData();
    std::thread worker([&] { viewport->UpdateTransformInput(session, captured, &ctx, true, true); });
    worker.join();
    viewport->UpdateTransformInput(session, captured, &ctx, true, true);
    session.EndMutationPhase();
    EXPECT_TRUE(session.TranslationActive());
}

TEST_F(GizmoViewportTest, ReplacedPressWithoutMatchingReleaseCancelsAndConsumesTheReplacementSerial)
{
    PhysicalPress();
    Route();
    Move({1.2f, 0.f, 0.f});
    Route();
    ++ctx.MousePressSerial[0]; // Old release missing; never infer a commit.
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    Route();
    EXPECT_FALSE(session.Active());
}

TEST_F(GizmoViewportTest, FineAndShortcutModifiersAreSampledWithoutChangingCapturedPreferences)
{
    PhysicalPress();
    Route();
    const auto preferences = session.GizmoSettings();
    Move({1.43f, 0.f, 0.f});
    ctx.ShiftDown = true;
    Route();
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, .073f, .0002f);
    ctx.ShiftDown = false;
    ctx.CtrlDown  = true; // Invert the default enabled snapping.
    Route();
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, .73f, .0002f);
    ctx.CtrlDown = false;
    Route();
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, .7f, .0002f);
    EXPECT_EQ(session.GizmoSettings().Revision, preferences.Revision);
    EXPECT_EQ(session.GizmoSettings().TranslationSnap.Enabled, preferences.TranslationSnap.Enabled);
    EXPECT_EQ(session.GizmoSettings().TranslationSnap.Increment, preferences.TranslationSnap.Increment);
}

TEST_F(GizmoViewportTest, ReleaseUsesItsOwnFineAndSnapModifiersRatherThanLaterKeyEvents)
{
    const auto preferences = session.GizmoSettings();
    for (uint32_t modifiers = 0; modifiers < 4; ++modifiers)
    {
        SCOPED_TRACE(modifiers);
        PhysicalPress();
        Route();
        ASSERT_TRUE(session.TranslationActive());
        Move({1.43f, 0.f, 0.f});
        ctx.ShiftDown        = (modifiers & 1) != 0;
        ctx.CtrlDown         = (modifiers & 2) != 0;
        const float expected = ctx.ShiftDown ? .073f : ctx.CtrlDown ? .73f : .7f;
        ZUIFeedMouseButton(&ctx, 0, false);
        // Key events and uncaptured motion may arrive before the next update.
        ctx.ShiftDown = !ctx.ShiftDown;
        ctx.CtrlDown  = !ctx.CtrlDown;
        Move({5.f, 0.f, 0.f});
        ZUIFeedBeginFrame(&ctx);
        Route();
        EXPECT_FALSE(session.Active());
        EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, expected, .0002f);
        ASSERT_EQ(session.History().UndoCount(), 1u);
        EXPECT_EQ(ctx.ActiveKey, 0u);
        EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
        ASSERT_TRUE(session.BeginMutationPhase());
        ASSERT_TRUE(session.Undo());
        session.EndMutationPhase();
        EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
        EXPECT_FALSE(session.Dirty());
    }
    EXPECT_EQ(session.GizmoSettings().Revision, preferences.Revision);
}

TEST_F(GizmoViewportTest, FastFineReleaseCommitsWithoutAnIntermediatePreviewOrRearmingTheNextPress)
{
    PhysicalPress();
    Move({1.43f, 0.f, 0.f});
    ctx.ShiftDown = true;
    ZUIFeedMouseButton(&ctx, 0, false);
    ctx.ShiftDown = false;
    ZUIFeedBeginFrame(&ctx);
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, .073f, .0002f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.Undo());
    session.EndMutationPhase();

    PhysicalPress();
    Route();
    ASSERT_TRUE(session.TranslationActive());
    Move({1.43f, 0.f, 0.f});
    ctx.ShiftDown = true;
    ZUIFeedMouseButton(&ctx, 0, false);
    ctx.ShiftDown = false;
    ctx.CtrlDown  = true;
    ZUIFeedMousePos(&ctx, 100.f, 100.f);
    ZUIFeedMouseButton(&ctx, 0, true);
    ctx.ActiveKey = 123;
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, .073f, .0002f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    EXPECT_EQ(ctx.ActiveKey, 123u);
    EXPECT_TRUE(ctx.MousePressed[0]);
    Route();
    EXPECT_FALSE(session.Active());
}

TEST_F(GizmoViewportTest, ReleaseModifierSnapshotPreservesNoOpsAndDoesNotOverrideCancellation)
{
    PhysicalPress();
    Route();
    Move({.73f, 0.f, 0.f}); // Snaps back to the unchanged start at release.
    ReleaseBeforeLaterInput();
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
    for (const bool focus_loss : {false, true})
    {
        PhysicalPress();
        Route();
        Move({1.43f, 0.f, 0.f});
        ctx.ShiftDown = true;
        Route();
        ASSERT_NE(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
        ZUIFeedMouseButton(&ctx, 0, false);
        if (focus_loss)
        {
            ZUIFeedFocusLost(&ctx);
        }
        else
        {
            ctx.EscapePressed = true;
        }
        Route(!focus_loss);
        EXPECT_FALSE(session.Active());
        EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
        EXPECT_EQ(session.History().UndoCount(), 0u);
        EXPECT_FALSE(session.Dirty());
    }
}

TEST_F(GizmoViewportTest, RotationReleaseRetainsThePreviewForEveryFineAndSnapModifierCombination)
{
    RotationMode();
    const auto before = CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
    for (uint32_t modifiers = 0; modifiers < 4; ++modifiers)
    {
        SCOPED_TRACE(modifiers);
        ASSERT_TRUE(RotationPress());
        RotationMove(.73);
        ctx.ShiftDown = (modifiers & 1) != 0;
        ctx.CtrlDown  = (modifiers & 2) != 0;
        Route();
        const auto expected = CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
        ASSERT_FALSE(SameLocalTransform(expected, before));
        ReleaseBeforeLaterInput();
        EXPECT_FALSE(session.Active());
        EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity)), expected));
        ASSERT_EQ(session.History().UndoCount(), 1u);
        ASSERT_TRUE(session.BeginMutationPhase());
        ASSERT_TRUE(session.Undo());
        EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity)), before));
        ASSERT_TRUE(session.Redo());
        EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity)), expected));
        ASSERT_TRUE(session.Undo());
        session.EndMutationPhase();
    }
}

TEST_F(GizmoViewportTest, ScaleReleaseRetainsThePreviewForEveryFineAndSnapModifierCombination)
{
    ScaleMode();
    const auto before = CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
    for (uint32_t modifiers = 0; modifiers < 4; ++modifiers)
    {
        SCOPED_TRACE(modifiers);
        ASSERT_TRUE(ScalePress());
        ScaleMove(1.73f);
        ctx.ShiftDown = (modifiers & 1) != 0;
        ctx.CtrlDown  = (modifiers & 2) != 0;
        Route();
        const auto expected = CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
        ASSERT_FALSE(SameLocalTransform(expected, before));
        ReleaseBeforeLaterInput();
        EXPECT_FALSE(session.Active());
        EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity)), expected));
        ASSERT_EQ(session.History().UndoCount(), 1u);
        ASSERT_TRUE(session.BeginMutationPhase());
        ASSERT_TRUE(session.Undo());
        EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity)), before));
        session.EndMutationPhase();
    }
}

TEST_F(GizmoViewportTest, PivotReleaseRetainsFineAndSnapModifiersWithoutAuthoredChangesOrHistory)
{
    const auto before = CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
    for (uint32_t modifiers = 0; modifiers < 4; ++modifiers)
    {
        SCOPED_TRACE(modifiers);
        ASSERT_TRUE(session.BeginMutationPhase());
        if (modifiers)
        {
            ASSERT_TRUE(session.ClearCustomPivot());
        }
        session.EndMutationPhase();
        BuildUI();
        ctx.AltDown = true;
        Move({0.f, 0.f, 0.f});
        ZUIFeedMouseButton(&ctx, 0, true);
        Route();
        ASSERT_TRUE(session.PivotActive());
        ctx.AltDown = false;
        Move({.73f, .43f, 0.f});
        ctx.ShiftDown = (modifiers & 1) != 0;
        ctx.CtrlDown  = (modifiers & 2) != 0;
        Route();
        Vec3f expected = {};
        ASSERT_TRUE(session.ReadCustomPivot(expected));
        ReleaseBeforeLaterInput();
        EXPECT_FALSE(session.Active());
        Vec3f actual = {};
        ASSERT_TRUE(session.ReadCustomPivot(actual));
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            EXPECT_FLOAT_EQ(actual[axis], expected[axis]);
        }
        EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity)), before));
        EXPECT_EQ(session.History().UndoCount(), 0u);
        EXPECT_FALSE(session.Dirty());
    }
}

TEST_F(GizmoViewportTest, LightReleaseRetainsThePreviewForEveryFineAndSnapModifierCombination)
{
    LightMode();
    for (uint32_t modifiers = 0; modifiers < 4; ++modifiers)
    {
        SCOPED_TRACE(modifiers);
        LightPress();
        Route();
        ASSERT_TRUE(session.LightActive());
        Move({2.73f, 0.f, 0.f});
        ctx.ShiftDown = (modifiers & 1) != 0;
        ctx.CtrlDown  = (modifiers & 2) != 0;
        Route();
        const float expected = scene.GetComponent<LightComponent>(entity)->Range;
        ASSERT_NE(expected, 2.f);
        ReleaseBeforeLaterInput();
        EXPECT_FALSE(session.Active());
        EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, expected);
        ASSERT_EQ(session.History().UndoCount(), 1u);
        ASSERT_TRUE(session.BeginMutationPhase());
        ASSERT_TRUE(session.Undo());
        session.EndMutationPhase();
        EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 2.f);
    }
}

TEST_F(GizmoViewportTest, CameraReleaseRetainsFarAndFovPreviewsForEveryFineAndSnapModifierCombination)
{
    CameraMode();
    for (const auto handle : {Handle::CameraFarLowerLeft, Handle::CameraFovUpperRight})
    {
        for (uint32_t modifiers = 0; modifiers < 4; ++modifiers)
        {
            SCOPED_TRACE(uint32_t(handle));
            SCOPED_TRACE(modifiers);
            ASSERT_TRUE(CameraPress(handle));
            CameraMoveValue(handle == Handle::CameraFarLowerLeft ? 5.73f : 78.3f);
            ctx.ShiftDown = (modifiers & 1) != 0;
            ctx.CtrlDown  = (modifiers & 2) != 0;
            Route();
            const auto expected = *scene.GetComponent<CameraComponent>(entity);
            ASSERT_TRUE(expected.Far != 4.f || expected.FovY != 60.f);
            ReleaseBeforeLaterInput();
            EXPECT_FALSE(session.Active());
            EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(entity)->Far, expected.Far);
            EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(entity)->FovY, expected.FovY);
            ASSERT_EQ(session.History().UndoCount(), 1u);
            ASSERT_TRUE(session.BeginMutationPhase());
            ASSERT_TRUE(session.Undo());
            session.EndMutationPhase();
            EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(entity)->Far, 4.f);
            EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(entity)->FovY, 60.f);
        }
    }
}

TEST_F(GizmoViewportTest, ModalOrTextOwnershipAppearingMidDragCancelsBeforeAMatchingRelease)
{
    for (uint32_t blocker = 0; blocker < 3; ++blocker)
    {
        PhysicalPress();
        Route();
        ASSERT_TRUE(session.Active());
        Move({1.2f, 0.f, 0.f});
        Route();
        ZUIFeedMouseButton(&ctx, 0, false);
        if (blocker == 0)
        {
            ctx.TextInputActive = true;
        }
        else if (blocker == 1)
        {
            ctx.ActiveModalKey = 123;
        }
        else
        {
            ctx.PendingPopupKey = 123;
        }
        Route();
        EXPECT_FALSE(session.Active());
        EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
        EXPECT_EQ(session.History().UndoCount(), 0u);
        ctx.TextInputActive = false;
        ctx.ActiveModalKey = ctx.PendingPopupKey = 0;
    }
}

TEST_F(GizmoViewportTest, EveryTranslateHandleStartsThroughPaintOrderPickingInBothCameraKinds)
{
    camera.SetOrientation(25.f, 35.f);
    camera.SetPosition(camera.GetForward() * -10.f);
    for (const auto kind : {CameraType::PERSPECTIVE, CameraType::ORTHOGRAPHIC})
    {
        camera.SetProjectionType(kind);
        for (const auto handle : {Handle::TranslateX, Handle::TranslateY, Handle::TranslateZ, Handle::TranslateYZ, Handle::TranslateZX, Handle::TranslateXY, Handle::TranslateView})
        {
            viewport->BuildHitFrame(session, camera.CaptureFrameData(), &ctx, true);
            Vec2f point = {};
            bool  found = false;
            for (const auto& shape : viewport->Hits().View())
            {
                if (shape.Handle != uint32_t(handle))
                {
                    continue;
                }
                const uint32_t vertices = shape.Geometry == HitGeometry::Triangle ? 3 : shape.Geometry == HitGeometry::ConvexQuad ? 4 : 2;
                point                   = {};
                for (uint32_t i = 0; i < vertices; ++i)
                {
                    point = point + shape.Points[i] / float(vertices);
                }
                if (viewport->Hits().Pick(point).Handle == uint32_t(handle))
                {
                    found = true;
                    break;
                }
            }
            ASSERT_TRUE(found) << uint32_t(handle) << " camera " << uint32_t(kind);
            ZUIFeedMousePos(&ctx, point.x, point.y);
            ZUIFeedMouseButton(&ctx, 0, true);
            Route();
            ASSERT_TRUE(session.TranslationActive());
            TranslationSnapshot snapshot = {};
            ASSERT_TRUE(session.ReadTranslationSnapshot(snapshot));
            EXPECT_EQ(snapshot.Constraint, handle);
            ZUIFeedMouseButton(&ctx, 0, false);
            Route();
            EXPECT_FALSE(session.Active());
            EXPECT_EQ(session.History().UndoCount(), 0u);
        }
    }
}

TEST_F(GizmoViewportTest, OrdinaryOverlayRebuildPreservesActiveCaptureButRightAndMiddleCannotStealIt)
{
    PhysicalPress();
    Route();
    ASSERT_TRUE(session.TranslationActive());
    BuildUI();
    Move({1.2f, 0.f, 0.f});
    ZUIFeedMouseButton(&ctx, 1, true);
    ZUIFeedMouseButton(&ctx, 2, true);
    ctx.ScrollDelta = 5.f;
    Route();
    EXPECT_TRUE(session.TranslationActive());
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, .5f, .0002f);
    EXPECT_TRUE(ctx.InteractionCapture.Pointer);
    EXPECT_TRUE(ctx.InteractionCapture.Keyboard);
    ZUIFeedMouseButton(&ctx, 0, false);
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 1u);
}

TEST_F(GizmoViewportTest, RepeatedPhysicalDragsReuseRoutingSessionAndUiStorage)
{
    const auto scene_bytes = memory.MainArena.m_current_offset;
    const auto ui_bytes    = ctx.FrameArena.m_current_offset;
    for (uint32_t i = 0; i < 1000; ++i)
    {
        PhysicalPress();
        Route();
        ASSERT_TRUE(session.TranslationActive());
        Move({1.2f, 0.f, 0.f});
        Route();
        ctx.EscapePressed = true;
        Route();
        ASSERT_FALSE(session.Active());
        ZUIFeedMouseButton(&ctx, 0, false);
        EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, scene_bytes);
    EXPECT_EQ(ctx.FrameArena.m_current_offset, ui_bytes);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoViewportTest, AltCentreEditsOnlyThePivotInEveryTransformModeAndReleasesAtTheDurableEdge)
{
    const auto before   = CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
    const auto revision = session.DocumentRevision();
    for (const auto mode : {Operation::Translate, Operation::Rotate, Operation::Scale})
    {
        ASSERT_TRUE(session.BeginMutationPhase());
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = mode}));
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Translation, .Enabled = false}));
        ASSERT_TRUE(session.ProcessRequests());
        session.EndMutationPhase();
        BuildUI();
        ctx.AltDown = true;
        Move({0.f, 0.f, 0.f});
        Update();
        EXPECT_TRUE(ctx.InteractionCapture.Pointer);
        EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
        ZUIFeedMouseButton(&ctx, 0, true);
        Route();
        ASSERT_TRUE(session.PivotActive()) << uint32_t(mode);
        Update();
        EXPECT_TRUE(ctx.InteractionCapture.Pointer);
        EXPECT_TRUE(ctx.InteractionCapture.Keyboard);
        // Releasing Alt does not change the captured interaction's intent.
        ctx.AltDown = false;
        Move({1.f, 2.f, 0.f});
        ZUIFeedMouseButton(&ctx, 0, false);
        Move({4.f, 5.f, 0.f});
        ZUIFeedBeginFrame(&ctx);
        Route();
        EXPECT_FALSE(session.Active());
        Vec3f pivot = {};
        ASSERT_TRUE(session.ReadCustomPivot(pivot));
        EXPECT_NEAR(pivot.x, 1.f, .0001f);
        EXPECT_NEAR(pivot.y, 2.f, .0001f);
        EXPECT_EQ(ctx.ActiveKey, 0u);
        EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
        EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity)), before));
        EXPECT_EQ(session.DocumentRevision(), revision);
        EXPECT_EQ(session.History().UndoCount(), 0u);
        EXPECT_FALSE(session.Dirty());
    }
}

TEST_F(GizmoViewportTest, AltOutsideCentreLeavesCameraOrbitUncapturedAndCannotStealItWhenCrossingTheCentre)
{
    for (const auto mode : {Operation::Translate, Operation::Rotate, Operation::Scale})
    {
        ASSERT_TRUE(session.BeginMutationPhase());
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = mode}));
        ASSERT_TRUE(session.ProcessRequests());
        session.EndMutationPhase();
        BuildUI();
        ctx.AltDown = true;
        // This is an axis shaft for Translate/Scale and otherwise empty space.
        Move({.7f, 0.f, 0.f});
        ZUIFeedMouseButton(&ctx, 0, true);
        Route();
        Update();
        EXPECT_FALSE(session.Active());
        EXPECT_EQ(viewport->Hovered(), Handle::None);
        EXPECT_FALSE(ctx.InteractionCapture.Pointer);
        EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
        const auto query = ZUIQueryPointer(&ctx, ctx.MousePos);
        ASSERT_NE(query.HotBox, nullptr);
        EXPECT_EQ(query.HotBox->Key, 42u);
        Move({0.f, 0.f, 0.f});
        Route();
        Update();
        EXPECT_FALSE(session.Active());
        EXPECT_FALSE(ctx.InteractionCapture.Pointer);
        ZUIFeedMouseButton(&ctx, 0, false);
        Update();
    }
}

TEST_F(GizmoViewportTest, FastAltReleaseCannotReclassifyAPivotPressAsAnAuthoredTransform)
{
    for (const auto mode : {Operation::Translate, Operation::Rotate, Operation::Scale})
    {
        ASSERT_TRUE(session.BeginMutationPhase());
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = mode}));
        ASSERT_TRUE(session.ProcessRequests());
        session.EndMutationPhase();
        BuildUI();
        ctx.AltDown = true;
        Move({0.f, 0.f, 0.f});
        ZUIFeedMouseButton(&ctx, 0, true);
        Move({1.f, 2.f, 0.f});
        ZUIFeedMouseButton(&ctx, 0, false);
        ctx.AltDown = false;
        ZUIFeedBeginFrame(&ctx);
        Route();
        EXPECT_FALSE(session.Active());
        Vec3f point = {};
        ASSERT_TRUE(session.ReadCustomPivot(point)) << uint32_t(mode);
        EXPECT_NEAR(point.x, 1.f, .0001f);
        EXPECT_NEAR(point.y, 2.f, .0001f);
        EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
        EXPECT_EQ(session.History().UndoCount(), 0u);
    }
}

TEST_F(GizmoViewportTest, AltPressedAfterAPhysicalPressCannotBeginOrConvertItToPivotEditing)
{
    Move({0.f, 0.f, 0.f});
    ZUIFeedMouseButton(&ctx, 0, true);
    ctx.AltDown = true;
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_FALSE(session.PivotActive());
    Vec3f point = {};
    EXPECT_FALSE(session.ReadCustomPivot(point));
    ctx.AltDown = false;
    Route();
    EXPECT_FALSE(session.Active());
}

TEST_F(GizmoViewportTest, PivotStartsRemainGatedAndAnObservedHeldPressCannotRearm)
{
    ctx.AltDown = true;
    Move({0.f, 0.f, 0.f});
    ZUIFeedMouseButton(&ctx, 0, true);
    Route(true, false);
    EXPECT_FALSE(session.Active());
    Route(true, true);
    EXPECT_FALSE(session.Active());
    ZUIFeedMouseButton(&ctx, 0, false);
    Route();
    ZUIFeedMouseButton(&ctx, 0, true);
    Route();
    EXPECT_TRUE(session.PivotActive());
}

TEST_F(GizmoViewportTest, PivotCaptureCancellationRestoresThePreviousPointAcrossAllOwnerAndMappingChanges)
{
    for (uint32_t reason = 0; reason < 9; ++reason)
    {
        SCOPED_TRACE(reason);
        BuildUI();
        ctx.AltDown = true;
        Move({0.f, 0.f, 0.f});
        ZUIFeedMouseButton(&ctx, 0, true);
        Route();
        ASSERT_TRUE(session.PivotActive());
        Move({1.f, 2.f, 0.f});
        Route();
        Vec3f point = {};
        ASSERT_TRUE(session.ReadCustomPivot(point));
        const auto captured_camera = camera.CaptureFrameData();
        if (reason == 0)
        {
            Route(false);
        }
        else if (reason == 1)
        {
            ctx.EscapePressed = true;
            Route();
            EXPECT_FALSE(ctx.EscapePressed);
        }
        else if (reason == 2)
        {
            ctx.PendingPopupKey = 123;
            Route();
        }
        else if (reason == 3)
        {
            ctx.TextInputActive = true;
            Route();
        }
        else if (reason == 4)
        {
            ctx.ActiveKey = 123;
            Route();
            EXPECT_EQ(ctx.ActiveKey, 123u);
        }
        else if (reason == 5)
        {
            camera.SetPosition({0.f, 0.f, 11.f});
            Route();
            camera.SetPosition(captured_camera.Position);
        }
        else if (reason == 6)
        {
            ctx.InputScale[0] = 2.f;
            Route();
        }
        else if (reason == 7)
        {
            const float moved[4] = {38.f, 61.f, 838.f, 661.f};
            viewport->Bind(&ctx, moved, 42, 99);
            Route();
        }
        else
        {
            ASSERT_TRUE(session.BeginMutationPhase());
            viewport->UpdateTransformInput(session, captured_camera, nullptr, true, true);
            session.EndMutationPhase();
            Route(); // retire the retained owner token when context returns
        }
        EXPECT_FALSE(session.Active());
        EXPECT_FALSE(session.ReadCustomPivot(point));
        EXPECT_FALSE(session.Dirty());
        EXPECT_EQ(session.History().UndoCount(), 0u);
        ctx.ActiveKey = ctx.PendingPopupKey = 0;
        ctx.InputScale[0]                   = 1.f;
        ctx.TextInputActive                 = false;
        ZUIFeedMouseButton(&ctx, 0, false);
        Route();
    }
}

TEST_F(GizmoViewportTest, LightHoverInSelectModeCapturesCameraWithoutStartingOrChangingSelection)
{
    LightMode();
    Move({2.f, 0.f, 0.f});
    Update();
    EXPECT_EQ(viewport->Hovered(), Handle::LightRangePositiveX);
    EXPECT_TRUE(ctx.InteractionCapture.Pointer);
    EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.Selection().ActiveUUID(), scene.GetComponent<UUIDComponent>(entity)->Value);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoViewportTest, PhysicalLightReleaseUsesDurableEdgeAndSharedHistoryWithoutAnOverlayFrame)
{
    LightMode();
    LightPress();
    Route();
    ASSERT_TRUE(session.LightActive());
    Update();
    EXPECT_TRUE(ctx.InteractionCapture.Pointer);
    EXPECT_TRUE(ctx.InteractionCapture.Keyboard);
    Move({2.5f, 0.f, 0.f});
    Route();
    EXPECT_NEAR(scene.GetComponent<LightComponent>(entity)->Range, 2.5f, .0001f);
    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 1600, 1200, output);
    EXPECT_FALSE(output.Empty());
    EXPECT_EQ(output.Sprites[0].Size, 24.f);
    Move({3.f, 0.f, 0.f});
    ZUIFeedMouseButton(&ctx, 0, false);
    Move({7.f, 0.f, 0.f});
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_NEAR(scene.GetComponent<LightComponent>(entity)->Range, 3.f, .0001f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.Undo());
    session.EndMutationPhase();
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 2.f);
}

TEST_F(GizmoViewportTest, FastLightClickUsesLatchedPressAndReleaseWhileProductionGateConsumesOldPresses)
{
    LightMode();
    LightPress();
    Route(true, false);
    EXPECT_FALSE(session.Active());
    Route();
    EXPECT_FALSE(session.Active());
    ZUIFeedMouseButton(&ctx, 0, false);
    LightPress();
    Move({2.5f, 0.f, 0.f});
    ZUIFeedMouseButton(&ctx, 0, false);
    Move({100.f, 100.f, 0.f});
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_NEAR(scene.GetComponent<LightComponent>(entity)->Range, 2.5f, .0001f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
}

TEST_F(GizmoViewportTest, LightCancellationRestoresFieldAcrossOwnerAndMappingChanges)
{
    LightMode();
    for (uint32_t reason = 0; reason < 9; ++reason)
    {
        SCOPED_TRACE(reason);
        BuildUI();
        LightPress();
        Route();
        ASSERT_TRUE(session.LightActive());
        Move({2.5f, 0.f, 0.f});
        Route();
        const auto captured_camera = camera.CaptureFrameData();
        if (reason == 0)
        {
            Route(false);
        }
        else if (reason == 1)
        {
            ctx.EscapePressed = true;
            Route();
            EXPECT_FALSE(ctx.EscapePressed);
        }
        else if (reason == 2)
        {
            ctx.PendingPopupKey = 123;
            Route();
        }
        else if (reason == 3)
        {
            ctx.TextInputActive = true;
            Route();
        }
        else if (reason == 4)
        {
            ctx.ActiveKey = 123;
            Route();
            EXPECT_EQ(ctx.ActiveKey, 123u);
        }
        else if (reason == 5)
        {
            camera.SetPosition({0.f, 0.f, 11.f});
            Route();
            camera.SetPosition(captured_camera.Position);
        }
        else if (reason == 6)
        {
            ctx.InputScale[0] = 2.f;
            Route();
        }
        else if (reason == 7)
        {
            const float moved[4] = {38.f, 61.f, 838.f, 661.f};
            viewport->Bind(&ctx, moved, 42, 99);
            Route();
        }
        else
        {
            ASSERT_TRUE(session.BeginMutationPhase());
            viewport->UpdateTransformInput(session, captured_camera, nullptr, true, true);
            session.EndMutationPhase();
            Route();
        }
        EXPECT_FALSE(session.Active());
        EXPECT_FALSE(session.Dirty());
        EXPECT_EQ(session.History().UndoCount(), 0u);
        EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 2.f);
        Route();
        EXPECT_FALSE(session.Active());
        ctx.ActiveKey = ctx.PendingPopupKey = 0;
        ctx.InputScale[0]                   = 1.f;
        ctx.TextInputActive                 = false;
        ZUIFeedMouseButton(&ctx, 0, false);
        Route();
    }
}

TEST_F(GizmoViewportTest, ComponentCacheTracksActiveTargetAndLightFieldsInSelectMode)
{
    LightMode();
    Move({2.f, 0.f, 0.f});
    Update();
    ASSERT_EQ(viewport->Hovered(), Handle::LightRangePositiveX);
    scene.GetComponent<LightComponent>(entity)->Range = 3.f;
    Update();
    EXPECT_EQ(viewport->Hovered(), Handle::None);
    Move({3.f, 0.f, 0.f});
    Update();
    EXPECT_EQ(viewport->Hovered(), Handle::LightRangePositiveX);
    ASSERT_TRUE(session.BeginMutationPhase());
    const auto other = scene.CreateEntity();
    scene.AddComponent<UUIDComponent>(other, {*uuids::uuid::from_string("11111111-1111-4111-8111-111111111111")});
    scene.AddComponent<TransformComponent>(other, {});
    ASSERT_TRUE(session.Select(other, SelectionMode::Add));
    scene.SynchronizeHierarchy();
    session.EndMutationPhase();
    Update();
    EXPECT_EQ(viewport->Hits().Count, 0u);
}

TEST_F(GizmoViewportTest, AltOverComponentHandlesRemainsNavigationRatherThanAPivotOrFieldEdit)
{
    LightMode();
    ctx.AltDown = true;
    LightPress();
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 2.f);
    ctx.AltDown = false;
    Route();
    EXPECT_FALSE(session.Active());
}

TEST_F(GizmoViewportTest, CameraHoverUsesSelectModeAndCapturesOnlyThePointerUntilAPhysicalPress)
{
    CameraMode();
    const auto* shape = &viewport->Hits().Shapes[0];
    const auto  point = (shape->Points[0] + shape->Points[1]) * .5f;
    ZUIFeedMousePos(&ctx, point.x, point.y);
    Update();
    EXPECT_EQ(viewport->Hovered(), Handle::CameraFovUpperLeft);
    EXPECT_TRUE(ctx.InteractionCapture.Pointer);
    EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
    EXPECT_FALSE(session.Active());
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoViewportTest, PhysicalCameraFarUsesDurableReleaseAndSharedUndoWithoutAnOverlayBuild)
{
    CameraMode();
    ASSERT_TRUE(CameraPress());
    ASSERT_TRUE(session.CameraActive());
    Update();
    EXPECT_TRUE(ctx.InteractionCapture.Keyboard);
    CameraMoveValue(5.f);
    Route();
    EXPECT_NEAR(scene.GetComponent<CameraComponent>(entity)->Far, 5.f, .001f);
    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 1600, 1200, output);
    EXPECT_EQ(output.Sprites[2].Size, 24.f);
    CameraMoveValue(6.f);
    ZUIFeedMouseButton(&ctx, 0, false);
    ZUIFeedMousePos(&ctx, 100.f, 100.f);
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_NEAR(scene.GetComponent<CameraComponent>(entity)->Far, 6.f, .001f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.Undo());
    session.EndMutationPhase();
    EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(entity)->Far, 4.f);
}

TEST_F(GizmoViewportTest, PhysicalCameraFovEditsOnlyTheApertureAndReturnsToPressWithoutHistory)
{
    CameraMode();
    ASSERT_TRUE(CameraPress(Handle::CameraFovUpperRight));
    Update();
    CameraMoveValue(80.f);
    Route();
    EXPECT_NEAR(scene.GetComponent<CameraComponent>(entity)->FovY, 80.f, .001f);
    EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(entity)->Far, 4.f);
    CameraMoveValue(60.f);
    ZUIFeedMouseButton(&ctx, 0, false);
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(entity)->FovY, 60.f);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoViewportTest, PhysicalCameraNearUsesItsOwnHandleAndSharedUndo)
{
    CameraMode();
    ASSERT_TRUE(CameraPress(Handle::CameraNearRight));
    Update();
    CameraMoveValue(.5f);
    Route();
    EXPECT_NEAR(scene.GetComponent<CameraComponent>(entity)->Near, .5f, .001f);
    EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(entity)->Far, 4.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(entity)->FovY, 60.f);
    ZUIFeedMouseButton(&ctx, 0, false);
    Route();
    EXPECT_FALSE(session.Active());
    ASSERT_EQ(session.History().UndoCount(), 1u);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.Undo());
    session.EndMutationPhase();
    EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(entity)->Near, 1.f);
}

TEST_F(GizmoViewportTest, AcceptanceMatrixExportsDeterministicVisualCapturesAndExercisesPhysicalInput)
{
    const auto captures = VisualCaptureDirectory();
    if (!captures.empty())
    {
        std::cout << "Gizmo visual captures: " << captures << '\n';
    }

    CaptureVisual("translate-idle");
    Move({.7f, 0.f, 0.f});
    Update();
    ASSERT_EQ(viewport->Hovered(), Handle::TranslateX);
    CaptureVisual("translate-hover");
    PhysicalPress();
    Route();
    ASSERT_TRUE(session.TranslationActive());
    Move({1.43f, 0.f, 0.f});
    ctx.ShiftDown = true;
    Route();
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, .073f, .0002f);
    CaptureVisual("translate-shift-active");
    ctx.EscapePressed = true;
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    ZUIFeedMouseButton(&ctx, 0, false);
    ctx.ShiftDown = false;
    Route();

    ASSERT_TRUE(Hover(Handle::TranslateXY));
    CaptureVisual("translate-plane-hover");
    ZUIFeedMouseButton(&ctx, 0, true);
    Route();
    ASSERT_TRUE(session.TranslationActive());
    Move({.65f, .65f, 0.f});
    Route();
    CaptureVisual("translate-plane-active");
    ctx.EscapePressed = true;
    Route();
    EXPECT_FALSE(session.Active());
    ZUIFeedMouseButton(&ctx, 0, false);
    Route();

    RotationMode();
    CaptureVisual("rotate-idle");
    ASSERT_TRUE(RotationPress(Handle::RotateView, false));
    ZUIFeedMouseButton(&ctx, 0, false);
    Update();
    CaptureVisual("rotate-hover");
    ASSERT_TRUE(RotationPress());
    RotationMove(.5);
    Route();
    CaptureVisual("rotate-active");
    ctx.EscapePressed = true;
    Route();
    EXPECT_FALSE(session.Active());
    ZUIFeedMouseButton(&ctx, 0, false);
    Route();

    ASSERT_TRUE(session.BeginMutationPhase());
    scene.GetComponent<TransformComponent>(entity)->Rotation = {.2f, .4f, .1f};
    scene.SynchronizeHierarchy();
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Space, .Space = TransformSpacePolicy::Local}));
    ASSERT_TRUE(session.ProcessRequests());
    session.EndMutationPhase();
    BuildUI();
    CaptureVisual("rotate-local-idle");
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Space, .Space = TransformSpacePolicy::World}));
    ASSERT_TRUE(session.ProcessRequests());
    session.EndMutationPhase();
    BuildUI();

    ScaleMode();
    CaptureVisual("scale-idle");
    ASSERT_TRUE(Hover(Handle::ScaleUniform));
    CaptureVisual("scale-uniform-hover");
    ASSERT_TRUE(ScalePress(Handle::ScaleUniform, false));
    ZUIFeedMouseButton(&ctx, 0, false);
    Update();
    CaptureVisual("scale-hover");
    ASSERT_TRUE(ScalePress());
    ScaleMove(1.4f);
    Route();
    CaptureVisual("scale-active");
    ctx.EscapePressed = true;
    Route();
    EXPECT_FALSE(session.Active());
    ZUIFeedMouseButton(&ctx, 0, false);
    Route();

    LightMode();
    CaptureVisual("point-idle");
    auto* light      = scene.GetComponent<LightComponent>(entity);
    light->LightType = LightComponent::Type::Spot;
    light->Range     = 4.f;
    light->SpotAngle = 45.f;
    BuildUI();
    CaptureVisual("spot-idle");
    light->LightType = LightComponent::Type::Directional;
    BuildUI();
    CaptureVisual("directional-idle");

    ASSERT_TRUE(session.BeginMutationPhase());
    scene.RemoveComponent<LightComponent>(entity);
    scene.SynchronizeHierarchy();
    session.EndMutationPhase();
    CameraMode();
    CaptureVisual("camera-idle");
    ASSERT_TRUE(CameraPress(Handle::CameraNearRight));
    CameraMoveValue(.5f);
    Route();
    EXPECT_NEAR(scene.GetComponent<CameraComponent>(entity)->Near, .5f, .001f);
    CaptureVisual("camera-near-active");
    ctx.EscapePressed = true;
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(entity)->Near, 1.f);
    ZUIFeedMouseButton(&ctx, 0, false);
    Route();
    ASSERT_TRUE(CameraPress(Handle::CameraFovUpperLeft));
    CameraMoveValue(70.f);
    Route();
    EXPECT_NEAR(scene.GetComponent<CameraComponent>(entity)->FovY, 70.f, .001f);
    CaptureVisual("camera-fov-active");
    ctx.EscapePressed = true;
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(entity)->FovY, 60.f);
}

TEST_F(GizmoViewportTest, CameraPhysicalStartGateCannotRearmAnObservedHeldPress)
{
    CameraMode();
    ASSERT_TRUE(CameraPress(Handle::CameraFarLowerLeft, false));
    Route(true, false);
    EXPECT_FALSE(session.Active());
    Route();
    EXPECT_FALSE(session.Active());
    ZUIFeedMouseButton(&ctx, 0, false);
    ASSERT_TRUE(CameraPress());
    CameraMoveValue(5.f);
    ZUIFeedMouseButton(&ctx, 0, false);
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_NEAR(scene.GetComponent<CameraComponent>(entity)->Far, 5.f, .001f);
}

TEST_F(GizmoViewportTest, CameraCaptureCancelsForFocusEscapePopupTextOwnerViewDpiLayoutAndContextChanges)
{
    CameraMode();
    for (uint32_t reason = 0; reason < 9; ++reason)
    {
        SCOPED_TRACE(reason);
        BuildUI();
        ASSERT_TRUE(CameraPress());
        CameraMoveValue(5.f);
        Route();
        const auto captured_camera = camera.CaptureFrameData();
        if (reason == 0)
        {
            Route(false);
        }
        else if (reason == 1)
        {
            ctx.EscapePressed = true;
            Route();
            EXPECT_FALSE(ctx.EscapePressed);
        }
        else if (reason == 2)
        {
            ctx.PendingPopupKey = 123;
            Route();
        }
        else if (reason == 3)
        {
            ctx.TextInputActive = true;
            Route();
        }
        else if (reason == 4)
        {
            ctx.ActiveKey = 123;
            Route();
            EXPECT_EQ(ctx.ActiveKey, 123u);
        }
        else if (reason == 5)
        {
            camera.SetPosition({7.f, 5.f, 11.f});
            Route();
            camera.SetPosition(captured_camera.Position);
        }
        else if (reason == 6)
        {
            ctx.InputScale[0] = 2.f;
            Route();
        }
        else if (reason == 7)
        {
            const float moved[4] = {38.f, 61.f, 838.f, 661.f};
            viewport->Bind(&ctx, moved, 42, 99);
            Route();
        }
        else
        {
            ASSERT_TRUE(session.BeginMutationPhase());
            viewport->UpdateTransformInput(session, captured_camera, nullptr, true, true);
            session.EndMutationPhase();
            Route();
        }
        EXPECT_FALSE(session.Active());
        EXPECT_FALSE(session.Dirty());
        EXPECT_EQ(session.History().UndoCount(), 0u);
        EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(entity)->Far, 4.f);
        Route();
        EXPECT_FALSE(session.Active());
        ctx.ActiveKey = ctx.PendingPopupKey = 0;
        ctx.InputScale[0]                   = 1.f;
        ctx.TextInputActive                 = false;
        ZUIFeedMouseButton(&ctx, 0, false);
        Route();
    }
}

TEST_F(GizmoViewportTest, CameraGeometryCacheTracksAuthoredFieldsRatherThanOnlyTheWorldMatrix)
{
    CameraMode();
    auto point = (viewport->Hits().Shapes[0].Points[0] + viewport->Hits().Shapes[0].Points[1]) * .5f;
    ZUIFeedMousePos(&ctx, point.x, point.y);
    Update();
    ASSERT_EQ(viewport->Hovered(), Handle::CameraFovUpperLeft);
    scene.GetComponent<CameraComponent>(entity)->FovY = 90.f;
    Update();
    EXPECT_EQ(viewport->Hovered(), Handle::None);
    point = (viewport->Hits().Shapes[0].Points[0] + viewport->Hits().Shapes[0].Points[1]) * .5f;
    ZUIFeedMousePos(&ctx, point.x, point.y);
    Update();
    EXPECT_EQ(viewport->Hovered(), Handle::CameraFovUpperLeft);
}

TEST_F(GizmoViewportTest, PivotPreviewAndSavedMarkerUseOnlyProceduralDecorationWithoutGhostGeometry)
{
    ctx.AltDown = true;
    Move({0.f, 0.f, 0.f});
    ZUIFeedMouseButton(&ctx, 0, true);
    Route();
    ASSERT_TRUE(session.PivotActive());
    Move({1.f, 2.f, 0.f});
    Route();
    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    ASSERT_FALSE(output.Empty());
    uint32_t rings = 0, dashed = 0;
    for (uint32_t i = 0; i < output.SpriteCount; ++i)
    {
        if (output.Sprites[i].Size == Constants::PivotRingSizePx)
        {
            ++rings;
        }
    }
    for (uint32_t i = 0; i < output.LineCount; ++i)
    {
        dashed += output.Lines[i].DashOn != 0.f;
    }
    EXPECT_EQ(rings, 1u);
    EXPECT_EQ(dashed, 5u); // link and four origin-marker edges, no ghost box
    ZUIFeedMouseButton(&ctx, 0, false);
    Route();
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    ASSERT_FALSE(output.Empty());
    EXPECT_FALSE(output.DecorationsOmitted);
    EXPECT_EQ(output.MeshLineGroupCount, 0u);
}

namespace
{
    class GizmoViewportGridTest : public GizmoViewportTest
    {
    protected:
        SceneGridSettings grid       = {};
        TargetFrame       grid_start = {};

        void              SetUp() override
        {
            GizmoViewportTest::SetUp();
            grid.Origin      = {.3f, -.2f, .1f};
            grid.Orientation = fromEulerAngles(.2f, -.3f, .4f);
            ASSERT_TRUE(CanonicalizeSceneGridSettings(grid));
            ASSERT_TRUE(session.BeginMutationPhase());
            ASSERT_TRUE(session.SetSceneSettingsHooks({
                .Context = this,
                .ReadGrid =
                    [](void* context, const Scene& scene, EditorGridView& output) {
                        const auto& self = *static_cast<const GizmoViewportGridTest*>(context);
                        if (&scene != &self.scene)
                            return false;
                        output = {.Value = self.grid, .SceneUUID = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111"), .Editable = true};
                        return true;
                    },
                .ApplyGrid = [](void* context, Scene&, const SceneGridSettings& value) { static_cast<GizmoViewportGridTest*>(context)->grid = value; },
            }));
            ASSERT_TRUE(session.SelectScene(SceneTarget::Grid));
            ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Translation, .Enabled = false}));
            ASSERT_TRUE(session.ProcessRequests());
            session.EndMutationPhase();
            camera.SetPosition({7.f, 5.f, 10.f});
            camera.SetOrientation(25.f, -35.f);
            BuildUI();
        }

        bool GridPress(bool allow_begin = true)
        {
            if (!ResolveTargetFrame(session, grid_start))
            {
                return false;
            }
            drag_grab = grid_start.Pivot + grid_start.Axes[0] * .7f;
            Move(drag_grab);
            ZUIFeedMouseButton(&ctx, 0, true);
            Route(true, allow_begin);
            return session.TranslationActive();
        }

        void GridMove(float delta)
        {
            Move(drag_grab + grid_start.Axes[0] * delta);
        }
    };
} // namespace

TEST_F(GizmoViewportGridTest, PhysicalGridReleaseCommitsTheLatchedLocalPoseWithoutAnOverlayAndNeverEditsEntities)
{
    const auto before = grid;
    ASSERT_TRUE(GridPress());
    Update();
    EXPECT_TRUE(ctx.InteractionCapture.Keyboard);
    EXPECT_TRUE(ctx.InteractionCapture.Pointer);
    GridMove(.5f);
    ZUIFeedMouseButton(&ctx, 0, false);
    Move({5.f, 5.f, 0.f}); // Release must use the durable edge, not this later pointer.
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.TranslationKey(), 0u);
    ASSERT_EQ(session.History().UndoCount(), 1u);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        EXPECT_NEAR(grid.Origin[axis], before.Origin[axis] + grid_start.Axes[0][axis] * .5f, .0002f);
    }
    const auto after = grid;
    EXPECT_TRUE(session.Selection().Entities().empty());
    EXPECT_EQ(session.Selection().ActiveSceneTarget(), SceneTarget::Grid);
    EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity)), {}));
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.Undo());
    EXPECT_TRUE(EqualSceneGridSettings(grid, before));
    EXPECT_FALSE(session.Dirty());
    ASSERT_TRUE(session.Redo());
    EXPECT_TRUE(EqualSceneGridSettings(grid, after));
    session.EndMutationPhase();
}

TEST_F(GizmoViewportGridTest, GridTranslationAndRotationRetainReleaseModifiersAndOneHistoryEntry)
{
    const auto before = grid;
    for (const auto mode : {Operation::Translate, Operation::Rotate})
    {
        if (mode == Operation::Rotate)
        {
            RotationMode();
        }
        for (uint32_t modifiers = 0; modifiers < 4; ++modifiers)
        {
            SCOPED_TRACE(uint32_t(mode));
            SCOPED_TRACE(modifiers);
            if (mode == Operation::Translate)
            {
                ASSERT_TRUE(GridPress());
                GridMove(.73f);
            }
            else
            {
                ASSERT_TRUE(RotationPress());
                RotationMove(.73);
            }
            ctx.ShiftDown = (modifiers & 1) != 0;
            ctx.CtrlDown  = (modifiers & 2) != 0;
            Route();
            const auto expected = grid;
            ASSERT_FALSE(EqualSceneGridSettings(expected, before));
            ReleaseBeforeLaterInput();
            EXPECT_FALSE(session.Active());
            EXPECT_TRUE(EqualSceneGridSettings(grid, expected));
            ASSERT_EQ(session.History().UndoCount(), 1u);
            ASSERT_TRUE(session.BeginMutationPhase());
            ASSERT_TRUE(session.Undo());
            EXPECT_TRUE(EqualSceneGridSettings(grid, before));
            ASSERT_TRUE(session.Redo());
            EXPECT_TRUE(EqualSceneGridSettings(grid, expected));
            ASSERT_TRUE(session.Undo());
            session.EndMutationPhase();
            EXPECT_FALSE(session.Dirty());
        }
    }
}

TEST_F(GizmoViewportGridTest, GridRotationAndTranslationReuseTheRendererWithoutEntityMeshDecorations)
{
    ASSERT_TRUE(GridPress());
    GridMove(.5f);
    Route();
    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    EXPECT_FALSE(output.Empty());
    EXPECT_TRUE(output.ValidCounts());
    EXPECT_EQ(output.MeshLineGroupCount, 0u);
    EXPECT_FALSE(output.DecorationsOmitted);
    ctx.EscapePressed = true;
    Route();
    EXPECT_FALSE(session.Active());
    ZUIFeedMouseButton(&ctx, 0, false);
    Route();
    RotationMode();
    const auto before = grid;
    ASSERT_TRUE(RotationPress());
    RotationMove(.5);
    Route();
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    EXPECT_FALSE(output.Empty());
    EXPECT_EQ(output.MeshLineGroupCount, 0u);
    EXPECT_GT(output.TriangleCount, 0u);
    ZUIFeedMouseButton(&ctx, 0, false);
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_FALSE(EqualSceneGridSettings(grid, before));
    ASSERT_EQ(session.History().UndoCount(), 1u);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.Undo());
    session.EndMutationPhase();
    EXPECT_TRUE(EqualSceneGridSettings(grid, before));
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoViewportGridTest, GridFocusCaptureAndLayoutLossRestoreBeforeValuesAndRetireOnlyOurOwner)
{
    for (uint32_t reason = 0; reason < 4; ++reason)
    {
        const auto before = grid;
        ASSERT_TRUE(GridPress());
        GridMove(.5f);
        Route();
        if (reason == 1)
        {
            ctx.ActiveKey = 777;
        }
        else if (reason == 2)
        {
            const float moved[4] = {38.f, 61.f, 838.f, 661.f};
            viewport->Bind(&ctx, moved, 42, 99);
        }
        else if (reason == 3)
        {
            ctx.EscapePressed = true;
        }
        Route(reason != 0);
        EXPECT_FALSE(session.Active());
        EXPECT_EQ(session.TranslationKey(), 0u);
        EXPECT_TRUE(EqualSceneGridSettings(grid, before));
        EXPECT_FALSE(session.Dirty());
        EXPECT_EQ(session.History().UndoCount(), 0u);
        if (reason == 1)
        {
            EXPECT_EQ(ctx.ActiveKey, 777u);
        }
        ctx.ActiveKey = 0;
        ZUIFeedMouseButton(&ctx, 0, false);
        viewport->Bind(&ctx, rect, 42, 99);
        Update();
    }
}

TEST_F(GizmoViewportGridTest, M0StartGateStillAppliesToGridAndAltNeverBecomesACustomPivot)
{
    EXPECT_FALSE(GridPress(false));
    Route();
    EXPECT_FALSE(session.Active());
    ZUIFeedMouseButton(&ctx, 0, false);
    Route();
    ctx.AltDown = true;
    Move(grid.Origin);
    ZUIFeedMouseButton(&ctx, 0, true);
    Route();
    EXPECT_FALSE(session.Active());
    EXPECT_FALSE(session.Dirty());
    ctx.AltDown = false;
    Route();
    EXPECT_FALSE(session.Active());
    ZUIFeedMouseButton(&ctx, 0, false);
    Route();
    ASSERT_TRUE(GridPress());
}

TEST_F(GizmoViewportGridTest, GridNeverExposesScaleHandlesEvenWhenThePreviousEntityToolWasScale)
{
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.Select(entity));
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Scale}));
    ASSERT_TRUE(session.ProcessRequests());
    ASSERT_TRUE(session.SelectScene(SceneTarget::Grid));
    session.EndMutationPhase();
    BuildUI();
    GizmoFrameData output = {};
    viewport->BuildRender(session, camera.CaptureFrameData(), &ctx, true, 800, 600, output);
    EXPECT_TRUE(output.Empty());
    EXPECT_EQ(viewport->Hits().Count, 0u);
    EXPECT_FALSE(session.Active());
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoViewportGridTest, AcceptanceMatrixExportsArbitraryAxisGridVisualCaptures)
{
    CaptureVisual("grid-translate-idle");
    ASSERT_TRUE(GridPress());
    GridMove(.5f);
    Route();
    CaptureVisual("grid-translate-active");
    ctx.EscapePressed = true;
    Route();
    EXPECT_FALSE(session.Active());
    ZUIFeedMouseButton(&ctx, 0, false);
    Route();

    RotationMode();
    CaptureVisual("grid-rotate-idle");
    ASSERT_TRUE(RotationPress());
    RotationMove(.5);
    Route();
    CaptureVisual("grid-rotate-active");
    ctx.EscapePressed = true;
    Route();
    EXPECT_FALSE(session.Active());
}

TEST(GizmoViewportPickTest, ConvertsLogicalImageCoordinatesToBoundedRenderPixels)
{
    Viewport   viewport      = {};
    ZUIContext context       = {};
    context.ScreenW          = 1600;
    context.ScreenH          = 900;
    context.InputScale[0]    = 2.f;
    context.InputScale[1]    = 2.f;
    context.ViewportInputKey = 19;
    context.FrameIndex       = 7;
    const float rect[4]      = {100.f, 50.f, 900.f, 650.f};
    viewport.Bind(&context, rect, 19, 23);

    uint32_t    x = 99, y = 99;
    const float top_left[2] = {100.f, 50.f};
    ASSERT_TRUE(viewport.ToRenderPixel(top_left, 1600, 900, x, y));
    EXPECT_EQ(x, 0u);
    EXPECT_EQ(y, 0u);

    const float interior[2] = {500.f, 350.f};
    ASSERT_TRUE(viewport.ToRenderPixel(interior, 1600, 900, x, y));
    EXPECT_EQ(x, 800u);
    EXPECT_EQ(y, 450u);

    const float right_edge[2] = {900.f, 350.f};
    EXPECT_FALSE(viewport.ToRenderPixel(right_edge, 1600, 900, x, y));
    const float bottom_edge[2] = {500.f, 650.f};
    EXPECT_FALSE(viewport.ToRenderPixel(bottom_edge, 1600, 900, x, y));
    EXPECT_FALSE(viewport.ToRenderPixel(interior, 0, 900, x, y));
}

TEST(GizmoViewportPickTest, BindingRevisionRejectsPickCoordinatesFromReplacedViewport)
{
    Viewport   viewport      = {};
    ZUIContext context       = {};
    context.ScreenW          = 1280;
    context.ScreenH          = 720;
    context.InputScale[0]    = 1.f;
    context.InputScale[1]    = 1.f;
    context.ViewportInputKey = 41;
    context.FrameIndex       = 3;
    const float first[4]     = {10.f, 20.f, 410.f, 320.f};
    viewport.Bind(&context, first, 41, 47);
    const uint64_t first_revision = viewport.BindingRevision();
    ASSERT_NE(first_revision, 0u);

    const float replacement[4] = {20.f, 20.f, 420.f, 320.f};
    viewport.Bind(&context, replacement, 41, 47);
    EXPECT_GT(viewport.BindingRevision(), first_revision);
}

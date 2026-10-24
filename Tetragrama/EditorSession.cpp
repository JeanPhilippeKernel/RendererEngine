#include <Tetragrama/EditorSession.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Reflection/ComponentReflectionRegistry.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <memory>
#include <type_traits>

using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;

namespace Tetragrama
{
    namespace
    {
        bool ReadUntrackedProperty(const Scene& scene, EntityID entity, EditorUntrackedProperty property, EditorUntrackedValue& out)
        {
            if (!scene.IsAlive(entity))
            {
                return false;
            }
            switch (property)
            {
                case EditorUntrackedProperty::LightColor:
                    if (const auto* light = scene.GetComponent<LightComponent>(entity))
                    {
                        out.Color = {light->Color[0], light->Color[1], light->Color[2]};
                        return true;
                    }
                    break;
                case EditorUntrackedProperty::CameraMain:
                    if (const auto* camera = scene.GetComponent<CameraComponent>(entity))
                    {
                        out.MainCamera = camera->IsMain;
                        return true;
                    }
                    break;
                case EditorUntrackedProperty::RigidBodyMotion:
                    if (const auto* body = scene.GetComponent<RigidBodyComponent>(entity))
                    {
                        out.Motion = body->MotionKind;
                        return true;
                    }
                    break;
            }
            return false;
        }

        bool SameUntrackedProperty(EditorUntrackedProperty property, const EditorUntrackedValue& a, const EditorUntrackedValue& b)
        {
            switch (property)
            {
                case EditorUntrackedProperty::LightColor:
                    return a.Color == b.Color;
                case EditorUntrackedProperty::CameraMain:
                    return a.MainCamera == b.MainCamera;
                case EditorUntrackedProperty::RigidBodyMotion:
                    return a.Motion == b.Motion;
            }
            return false;
        }

        bool ValidUntrackedProperty(const Scene& scene, EntityID entity, EditorUntrackedProperty property, const EditorUntrackedValue& value)
        {
            EditorUntrackedValue current = {};
            if (!ReadUntrackedProperty(scene, entity, property, current))
            {
                return false;
            }
            switch (property)
            {
                case EditorUntrackedProperty::LightColor:
                {
                    auto candidate  = CaptureAuthoredLight(*scene.GetComponent<LightComponent>(entity));
                    candidate.Color = {value.Color[0], value.Color[1], value.Color[2]};
                    return ValidAuthoredLight(candidate);
                }
                case EditorUntrackedProperty::CameraMain:
                    return ValidAuthoredCamera(CaptureAuthoredCamera(*scene.GetComponent<CameraComponent>(entity)));
                case EditorUntrackedProperty::RigidBodyMotion:
                {
                    auto candidate       = CaptureAuthoredRigidBody(*scene.GetComponent<RigidBodyComponent>(entity));
                    candidate.MotionKind = value.Motion;
                    return ValidAuthoredRigidBody(candidate);
                }
            }
            return false;
        }

        void ApplyUntrackedProperty(Scene& scene, EntityID entity, EditorUntrackedProperty property, const EditorUntrackedValue& value)
        {
            switch (property)
            {
                case EditorUntrackedProperty::LightColor:
                {
                    auto& light = *scene.GetComponent<LightComponent>(entity);
                    for (size_t i = 0; i < value.Color.size(); ++i)
                    {
                        light.Color[i] = value.Color[i];
                    }
                    break;
                }
                case EditorUntrackedProperty::CameraMain:
                    scene.GetComponent<CameraComponent>(entity)->IsMain = value.MainCamera;
                    break;
                case EditorUntrackedProperty::RigidBodyMotion:
                    scene.GetComponent<RigidBodyComponent>(entity)->MotionKind = value.Motion;
                    break;
            }
        }

        uint64_t Mix(uint64_t value)
        {
            value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
            value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
            return value ^ (value >> 31);
        }

        struct SemanticDigest
        {
            std::array<uint64_t, 2> Value = {0x243f6a8885a308d3ull, 0x13198a2e03707344ull};

            void                    Add(uint64_t word)
            {
                Value[0] = Mix(Value[0] ^ word);
                Value[1] = Mix(Value[1] + word + 0x9e3779b97f4a7c15ull);
            }
            void Key(cstring key)
            {
                for (; key && *key; ++key)
                {
                    Add(static_cast<unsigned char>(*key));
                }
                Add(0u);
            }
            void Identity(const uuids::uuid& identity)
            {
                const auto bytes = identity.as_bytes();
                for (size_t i = 0; i < bytes.size(); i += 4)
                {
                    uint64_t word = 0;
                    for (size_t j = 0; j < 4; ++j)
                    {
                        word |= uint64_t(std::to_integer<uint8_t>(bytes[i + j])) << (j * 8);
                    }
                    Add(word);
                }
            }
            void Float(float value)
            {
                // Signed zeros are one semantic value; unused vector lanes never enter the digest.
                Add(value == 0.f ? 0u : std::bit_cast<uint32_t>(value));
            }
            void Vector(const ZEngine::Core::Maths::Vec3f& value)
            {
                Float(value.x);
                Float(value.y);
                Float(value.z);
            }
        };

        auto TransformDigest(const uuids::uuid& identity, const LocalTransformValue& value)
        {
            static const auto seed = [] {
                SemanticDigest value = {};
                value.Key("TransformComponent");
                return value;
            }();
            auto digest = seed;
            digest.Identity(identity);
            digest.Vector(value.Position);
            digest.Vector(value.Rotation);
            digest.Vector(value.Scale);
            return digest.Value;
        }

        auto FieldDigest(const uuids::uuid& identity, AuthoredFloatField field, float value)
        {
            static const auto seeds = [] {
                std::array<SemanticDigest, static_cast<size_t>(AuthoredFloatField::Count)> values = {};
                for (size_t i = 0; i < values.size(); ++i)
                {
                    const auto key = static_cast<AuthoredFloatField>(i);
                    values[i].Key(ComponentSchemaKey(key));
                    values[i].Key(ComponentFieldKey(key));
                }
                return values;
            }();
            auto digest = seeds[static_cast<size_t>(field)];
            digest.Identity(identity);
            digest.Float(value);
            return digest.Value;
        }

        auto GridDigest(const uuids::uuid& identity, const SceneGridBinary& value)
        {
            static const auto seed = [] {
                SemanticDigest value = {};
                value.Key("SceneGridSettings");
                return value;
            }();
            auto digest = seed;
            digest.Identity(identity);
            for (size_t i = 0; i < value.size(); i += 4)
            {
                uint32_t word = 0;
                for (uint32_t j = 0; j < 4; ++j)
                {
                    word |= uint32_t(std::to_integer<uint8_t>(value[i + j])) << (8 * j);
                }
                digest.Add(word);
            }
            return digest.Value;
        }
    } // namespace

    void EditorSession::AccountTransform(const uuids::uuid& identity, const LocalTransformValue& before, const LocalTransformValue& after)
    {
        if (SameLocalTransform(before, after))
        {
            return;
        }
        const auto old_digest = TransformDigest(identity, before);
        const auto new_digest = TransformDigest(identity, after);
        for (size_t i = 0; i < m_content_digest.size(); ++i)
        {
            m_content_digest[i] ^= old_digest[i] ^ new_digest[i];
        }
    }

    void EditorSession::AccountField(const uuids::uuid& identity, AuthoredFloatField field, float before, float after)
    {
        if (before == after)
        {
            return;
        }
        const auto old_digest = FieldDigest(identity, field, before);
        const auto new_digest = FieldDigest(identity, field, after);
        for (size_t i = 0; i < m_content_digest.size(); ++i)
        {
            m_content_digest[i] ^= old_digest[i] ^ new_digest[i];
        }
    }

    void EditorSession::AccountGrid(const uuids::uuid& identity, const SceneGridBinary& before, const SceneGridBinary& after)
    {
        if (before == after)
        {
            return;
        }
        const auto old_digest = GridDigest(identity, before), new_digest = GridDigest(identity, after);
        for (size_t i = 0; i < m_content_digest.size(); ++i)
        {
            m_content_digest[i] ^= old_digest[i] ^ new_digest[i];
        }
    }

    bool EditorSession::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, const EditorSessionConfig& config)
    {
        static_assert(std::is_trivially_copyable_v<TransformBatchHeader> && std::is_trivially_copyable_v<TransformChange> && std::is_trivially_copyable_v<FieldChange>);
        static_assert(EditorHistory::PayloadAlignment % alignof(TransformBatchHeader) == 0 && EditorHistory::PayloadAlignment % alignof(TransformChange) == 0 && EditorHistory::PayloadAlignment % alignof(FieldChange) == 0 && EditorHistory::PayloadAlignment % alignof(GridChange) == 0);
        if (!arena || m_initialized || !config.MaxTargets || config.MaxTargets > EntityRegistry::MAX_ENTITIES)
        {
            return false;
        }
        if (!m_history.Initialize(arena, config.HistoryEntries, config.HistoryBytes))
        {
            return false;
        }
        if (!m_selected_roots.Initialize(arena, config.MaxTargets))
        {
            return false;
        }
        m_selection_requests.init(arena, 0);
        if (!m_selection_requests.try_reserve(config.MaxTargets))
        {
            return false;
        }
        m_targets.Initialize(arena);
        m_selection.Initialize(arena, config.MaxTargets);
        m_unique.init(arena, config.MaxTargets * 2);
        m_runtime         = static_cast<TransformRuntime*>(arena->AllocateNoZero(sizeof(TransformRuntime) * config.MaxTargets, alignof(TransformRuntime)));
        m_transaction     = static_cast<std::byte*>(arena->AllocateNoZero(sizeof(TransformBatchHeader) + sizeof(TransformChange) * config.MaxTargets, alignof(TransformBatchHeader)));
        m_gizmo_roots     = static_cast<Gizmo::TransformRoot*>(arena->AllocateNoZero(sizeof(Gizmo::TransformRoot) * config.MaxTargets, alignof(Gizmo::TransformRoot)));
        m_gizmo_values    = static_cast<LocalTransformValue*>(arena->AllocateNoZero(sizeof(LocalTransformValue) * config.MaxTargets, alignof(LocalTransformValue)));
        m_gizmo_observers = static_cast<Gizmo::TransformObserver*>(arena->AllocateNoZero(sizeof(Gizmo::TransformObserver) * config.MaxTargets, alignof(Gizmo::TransformObserver)));
        if (!m_runtime || !m_transaction || !m_gizmo_roots || !m_gizmo_values || !m_gizmo_observers)
        {
            return false;
        }
        m_max_targets = config.MaxTargets;
        m_owner       = std::this_thread::get_id();
        m_initialized = true;
        return true;
    }

    bool EditorSession::Allowed() const
    {
        // Owner identity is stable for the initialized lifetime. Reject other
        // threads before reading the main-thread-only mutable phase flag.
        return m_owner == std::this_thread::get_id() && m_initialized && m_in_phase;
    }

    EntityTarget EditorSession::TargetFor(EntityID entity)
    {
        return m_owner == std::this_thread::get_id() && m_initialized ? m_targets.TargetFor(entity) : EntityTarget{};
    }

    EntityID EditorSession::Resolve(const EntityTarget& target)
    {
        return m_initialized && m_owner == std::this_thread::get_id() ? m_targets.Resolve(target) : INVALID_ENTITY;
    }

    EntityID EditorSession::Resolve(const uuids::uuid& identity)
    {
        return m_initialized && m_owner == std::this_thread::get_id() ? m_targets.Resolve(identity) : INVALID_ENTITY;
    }

    bool EditorSession::Reject(cstring message)
    {
        m_last_error = message;
        return false;
    }

    bool EditorSession::Enqueue(const PendingRequest& request)
    {
        if (!m_initialized || m_owner != std::this_thread::get_id())
        {
            return false;
        }
        if (!m_scene || m_request_count == MaxPendingRequests)
        {
            return Reject("The editor request queue is unavailable or full; no command was applied.");
        }
        m_requests[(m_request_first + m_request_count) % MaxPendingRequests] = request;
        ++m_request_count;
        return true;
    }

    bool EditorSession::Request(EditorCommand command)
    {
        if (command != EditorCommand::Undo && command != EditorCommand::Redo && command != EditorCommand::Cancel && command != EditorCommand::ClearSelection)
        {
            return false;
        }
        PendingRequest request = {};
        request.Epoch          = m_epoch;
        request.Command        = command;
        return Enqueue(request);
    }

    bool EditorSession::RequestGizmoChange(const Gizmo::Change& change)
    {
        if (!Gizmo::ValidChange(change))
        {
            return false;
        }
        PendingRequest request = {};
        request.Kind           = RequestKind::GizmoSettings;
        request.Epoch          = m_epoch;
        request.GizmoChange    = change;
        return Enqueue(request);
    }

    bool EditorSession::ApplyGizmoChange(const Gizmo::Change& change)
    {
        if (!Allowed() || !Gizmo::ValidChange(change) || (change.Kind == Gizmo::ChangeKind::Operation && change.Mode == Gizmo::Operation::Scale && m_selection.ActiveSceneTarget() == SceneTarget::Grid))
        {
            return false;
        }
        switch (change.Kind)
        {
            case Gizmo::ChangeKind::Operation:
                if (m_gizmo.Mode == change.Mode)
                {
                    return true;
                }
                Cancel();
                (void) ClearCustomPivot();
                (void) DiscardEditRequests(); // a retained numeric source cannot rearm after the transition
                m_gizmo.Mode = change.Mode;
                break;
            case Gizmo::ChangeKind::Space:
                if (m_gizmo.Space == change.Space)
                {
                    return true;
                }
                Cancel();
                (void) DiscardEditRequests();
                m_gizmo.Space = change.Space;
                break;
            case Gizmo::ChangeKind::SnapEnabled:
            case Gizmo::ChangeKind::SnapIncrement:
            {
                auto* setting = change.Target == Gizmo::SnapTarget::Translation ? &m_gizmo.TranslationSnap : change.Target == Gizmo::SnapTarget::Rotation ? &m_gizmo.RotationSnap : &m_gizmo.ScaleSnap;
                if (change.Kind == Gizmo::ChangeKind::SnapEnabled)
                {
                    if (setting->Enabled == change.Enabled)
                    {
                        return true;
                    }
                    setting->Enabled = change.Enabled;
                }
                else
                {
                    if (setting->Increment == change.Increment)
                    {
                        return true;
                    }
                    setting->Increment = change.Increment;
                }
                // A drag retains its captured snap settings, independent of these preferences.
                break;
            }
            case Gizmo::ChangeKind::GridVisibility:
                if (m_gizmo.ShowGrid == change.Enabled)
                {
                    return true;
                }
                m_gizmo.ShowGrid = change.Enabled;
                break;
        }
        ++m_gizmo.Revision;
        return true;
    }

    bool EditorSession::RequestSelection(EntityID entity, SelectionMode mode)
    {
        if (mode != SelectionMode::Replace && mode != SelectionMode::Add && mode != SelectionMode::Toggle)
        {
            return false;
        }
        PendingRequest request = {};
        request.Target         = TargetFor(entity);
        if (request.Target.UUID.is_nil())
        {
            return false;
        }
        request.Epoch = m_epoch;
        request.Kind  = RequestKind::EntitySelection;
        request.Mode  = mode;
        return Enqueue(request);
    }

    bool EditorSession::RequestSceneSelection(SceneTarget target)
    {
        if (target != SceneTarget::None && target != SceneTarget::Grid)
        {
            return false;
        }
        PendingRequest request = {};
        request.Epoch          = m_epoch;
        request.Kind           = RequestKind::SceneSelection;
        request.Scene          = target;
        return Enqueue(request);
    }

    bool EditorSession::RequestSelectionRange(ZEngine::Core::Containers::ArrayView<const EntityTarget> range, const uuids::uuid& active, bool additive)
    {
        if (!OnOwnerThread())
        {
            return false;
        }
        if (!m_scene || range.empty() || range.size() > m_max_targets - m_selection_requests.size() || m_request_count == MaxPendingRequests)
        {
            return Reject("The complete hierarchy range exceeds the bounded selection request budget.");
        }
        bool has_active = false;
        for (const auto& target : range)
        {
            const auto entity = m_targets.Resolve(target);
            if (!entity.IsValid() || entity != target.CachedEntity)
            {
                return Reject("The hierarchy range contains a stale or ambiguous target.");
            }
            has_active |= target.UUID == active;
        }
        if (!has_active)
        {
            return Reject("The range's active endpoint must belong to the complete range.");
        }
        PendingRequest request = {.Epoch = m_epoch, .Kind = RequestKind::EntitySelectionRange, .SelectionActive = active, .SelectionFirst = static_cast<uint32_t>(m_selection_requests.size()), .SelectionCount = static_cast<uint32_t>(range.size()), .SelectionAdditive = additive};
        if (!Enqueue(request))
        {
            return false;
        }
        for (const auto& target : range)
        {
            m_selection_requests.push(target);
        }
        return true;
    }

    bool EditorSession::RequestTransformValue(EntityID entity, const LocalTransformValue& value)
    {
        if (!ValidLocalTransform(value))
        {
            return false;
        }
        PendingRequest request = {};
        if (!CaptureEditRequest(request, entity, true, AuthoredFloatField::LightRange))
        {
            return false;
        }
        request.Kind      = RequestKind::TransformValue;
        request.Transform = value;
        return Enqueue(request);
    }

    bool EditorSession::RequestFieldValue(EntityID entity, AuthoredFloatField field, float value)
    {
        if (!ComponentSchemaKey(field) || !std::isfinite(value))
        {
            return false;
        }
        PendingRequest request = {};
        if (!CaptureEditRequest(request, entity, false, field))
        {
            return false;
        }
        request.Kind  = RequestKind::FieldValue;
        request.Field = field;
        request.Value = value;
        return Enqueue(request);
    }

    bool EditorSession::RequestControlDrag(EntityID entity, const EditorControlDrag& control)
    {
        if (!control.Key || !std::isfinite(control.StartX) || !std::isfinite(control.UnitsPerPixel) || control.UnitsPerPixel <= 0.f || (control.IsTransform && (control.Axis >= 3 || (control.Property != TransformProperty::Position && control.Property != TransformProperty::Rotation && control.Property != TransformProperty::Scale))) || (!control.IsTransform && !ComponentSchemaKey(control.Field)))
        {
            return false;
        }
        PendingRequest request = {};
        if (!CaptureEditRequest(request, entity, control.IsTransform, control.Field))
        {
            return false;
        }
        request.Kind    = RequestKind::ControlDrag;
        request.Control = control;
        return Enqueue(request);
    }

    bool EditorSession::RequestRename(const EntityTarget& target, std::string_view expected, std::string_view name)
    {
        PendingRequest request = {};
        static_assert(sizeof(NameComponent::Value) == request.Name.size());
        if (target.UUID.is_nil() || target.SceneEpoch != m_epoch || expected.size() >= request.ExpectedName.size() || name.empty() || name.size() >= request.Name.size() || name.find('\0') != std::string_view::npos || expected.find('\0') != std::string_view::npos)
        {
            return false;
        }
        request.Kind   = RequestKind::Rename;
        request.Epoch  = m_epoch;
        request.Target = target;
        if ((!expected.empty() && ZEngine::Helpers::secure_memcpy(request.ExpectedName.data(), request.ExpectedName.size(), expected.data(), expected.size()) != 0) || ZEngine::Helpers::secure_memcpy(request.Name.data(), request.Name.size(), name.data(), name.size()) != 0)
        {
            return false;
        }
        return Enqueue(request);
    }

    bool EditorSession::RequestDelete(EntityID entity)
    {
        PendingRequest request = {};
        request.Target         = TargetFor(entity);
        if (request.Target.UUID.is_nil())
        {
            return false;
        }
        request.Kind  = RequestKind::DeleteEntity;
        request.Epoch = m_epoch;
        return Enqueue(request);
    }

    bool EditorSession::RequestAddComponent(EntityID entity, std::string_view component)
    {
        PendingRequest request = {};
        if (component.empty() || component.size() >= request.Name.size() || component.find('\0') != std::string_view::npos)
        {
            return false;
        }
        request.Target = TargetFor(entity);
        if (request.Target.UUID.is_nil())
        {
            return false;
        }
        request.Kind  = RequestKind::AddComponent;
        request.Epoch = m_epoch;
        if (ZEngine::Helpers::secure_memcpy(request.Name.data(), request.Name.size(), component.data(), component.size()) != 0)
        {
            return false;
        }
        const auto* meta = ComponentReflectionRegistry::Get().LookupByName(request.Name.data());
        if (!meta || !meta->Add || meta->TypeID >= ARCHETYPE_MASK_CAPACITY || MaskHas(m_scene->GetMask(entity), meta->TypeID))
        {
            return false;
        }
        return Enqueue(request);
    }

    bool EditorSession::RequestUntrackedProperty(EntityID entity, EditorUntrackedProperty property, const EditorUntrackedValue& value)
    {
        PendingRequest request = {};
        request.Target         = TargetFor(entity);
        if (request.Target.UUID.is_nil() || !ReadUntrackedProperty(*m_scene, entity, property, request.ExpectedProperty) || !ValidUntrackedProperty(*m_scene, entity, property, value))
        {
            return false;
        }
        request.Kind          = RequestKind::UntrackedProperty;
        request.Epoch         = m_epoch;
        request.Property      = property;
        request.PropertyValue = value;
        return Enqueue(request);
    }

    bool EditorSession::RequestCollection(std::string_view name)
    {
        PendingRequest request = {};
        if (name.empty() || name.size() >= request.Name.size() || name.find('\0') != std::string_view::npos)
        {
            return false;
        }
        request.Kind  = RequestKind::CreateCollection;
        request.Epoch = m_epoch;
        if (ZEngine::Helpers::secure_memcpy(request.Name.data(), request.Name.size(), name.data(), name.size()) != 0)
        {
            return false;
        }
        return Enqueue(request);
    }

    bool EditorSession::CaptureEditRequest(PendingRequest& request, EntityID entity, bool transform, AuthoredFloatField field)
    {
        // UI reads happen outside the mutation phase. Publish our derived frames
        // at phase end; an owner collecting another intent inside the phase must
        // first flush any earlier preview/replay, not capture stale revisions.
        if (Allowed() && m_hierarchy_pending)
        {
            SynchronizeHierarchy();
        }
        request.Target = TargetFor(entity);
        if (request.Target.UUID.is_nil())
        {
            return false;
        }
        request.Epoch             = m_epoch;
        request.SelectionRevision = m_selection.Revision();
        request.HierarchyRevision = m_scene->Hierarchy().Revision();
        if (const auto* frame = m_scene->Hierarchy().Find(entity))
        {
            request.CapturedFrame       = true;
            request.ParentWorldRevision = m_scene->Hierarchy().ParentWorldRevision(entity);
            request.FrameRevision       = transform ? 0 : frame->WorldRevision;
        }
        if (!transform)
        {
            return ReadAuthoredFloatField(*m_scene, entity, field, request.ExpectedValue) && ValidAuthoredFloatField(*m_scene, entity, field, request.ExpectedValue);
        }
        const auto* component = m_scene->GetComponent<TransformComponent>(entity);
        if (!component)
        {
            return false;
        }
        request.ExpectedTransform = CaptureLocalTransform(*component);
        return ValidLocalTransform(request.ExpectedTransform);
    }

    bool EditorSession::MatchesEditRequest(const PendingRequest& request)
    {
        // An earlier queued transform/history replay can move this target's
        // frame during the same drain. Publish it before comparing the captured
        // revisions, not later inside BeginTransform/BeginField where accepting
        // the old intent would silently rebase it. History/toolbar bursts do not
        // need this scan; refresh only for a frame-sensitive edit when dirty.
        if (m_hierarchy_pending)
        {
            SynchronizeHierarchy();
        }
        const auto entity = m_targets.Resolve(request.Target);
        if (!entity.IsValid() || entity != request.Target.CachedEntity || request.SelectionRevision != m_selection.Revision() || request.HierarchyRevision != m_scene->Hierarchy().Revision())
        {
            return false;
        }
        const bool transform = request.Kind == RequestKind::TransformValue || (request.Kind == RequestKind::ControlDrag && request.Control.IsTransform);
        if (request.CapturedFrame)
        {
            const auto* frame = m_scene->Hierarchy().Find(entity);
            if (!frame || !frame->FrameValid || request.ParentWorldRevision != m_scene->Hierarchy().ParentWorldRevision(entity) || (!transform && request.FrameRevision != frame->WorldRevision))
            {
                return false;
            }
        }
        if (transform)
        {
            const auto* component = m_scene->GetComponent<TransformComponent>(entity);
            return component && SameLocalTransform(CaptureLocalTransform(*component), request.ExpectedTransform);
        }
        float      current = 0.f;
        const auto field   = request.Kind == RequestKind::ControlDrag ? request.Control.Field : request.Field;
        return ReadAuthoredFloatField(*m_scene, entity, field, current) && current == request.ExpectedValue;
    }

    bool EditorSession::HasPendingControl(uint64_t key) const
    {
        if (!key || !m_initialized || m_owner != std::this_thread::get_id())
        {
            return false;
        }
        for (uint32_t i = 0; i < m_request_count; ++i)
        {
            const auto& request = m_requests[(m_request_first + i) % MaxPendingRequests];
            if (request.Kind == RequestKind::ControlDrag && request.Control.Key == key)
            {
                return true;
            }
        }
        return false;
    }

    bool EditorSession::ApplyValueRequest(const PendingRequest& request)
    {
        Cancel();
        const bool transform = request.Kind == RequestKind::TransformValue;
        if (!(transform ? BeginTransform({&request.Target, 1}) : BeginField(request.Target, request.Field)))
        {
            return false;
        }
        if (!(transform ? PreviewTransforms({&request.Transform, 1}) : PreviewField(request.Value)))
        {
            Cancel();
            return false;
        }
        return Commit();
    }

    bool EditorSession::BeginControlRequest(const PendingRequest& request)
    {
        if (Active())
        {
            return m_control.Key == request.Control.Key && m_control.PointerSerial == request.Control.PointerSerial && (m_active == Interaction::Transform ? Batch().Count == 1 && Changes()[0].UUID == request.Target.UUID : m_field.UUID == request.Target.UUID) && m_control.IsTransform == request.Control.IsTransform && m_control.StartX == request.Control.StartX && m_control.UnitsPerPixel == request.Control.UnitsPerPixel &&
                   (m_control.IsTransform ? m_control.Property == request.Control.Property && m_control.Axis == request.Control.Axis : m_control.Field == request.Control.Field);
        }
        if (!(request.Control.IsTransform ? BeginTransform({&request.Target, 1}) : BeginField(request.Target, request.Control.Field)))
        {
            return false;
        }
        m_control = request.Control;
        return true;
    }

    bool EditorSession::UpdateControlDrag(float pointer_x, bool pointer_down, bool focused, bool cancel, bool capture_lost)
    {
        if (!Allowed())
        {
            return false;
        }
        if (!m_control.Key)
        {
            return true;
        }
        if (!focused || cancel || capture_lost)
        {
            Cancel();
            return true;
        }
        if (!ValidateActive())
        {
            Cancel();
            return Reject("The numeric interaction was cancelled because its target changed.");
        }
        // Every preview derives from immutable authored before-state, including
        // updates with no UI payload. Do not accumulate presentation-frame deltas.
        const float delta     = (pointer_x - m_control.StartX) * m_control.UnitsPerPixel;
        bool        previewed = false;
        if (std::isfinite(pointer_x) && std::isfinite(delta))
        {
            if (m_control.IsTransform)
            {
                const auto* before = TransformBefore(0);
                if (!before)
                {
                    Cancel();
                    return Reject("The numeric interaction has no captured transform.");
                }
                auto  value             = *before;
                auto& vector            = m_control.Property == TransformProperty::Position ? value.Position : m_control.Property == TransformProperty::Rotation ? value.Rotation : value.Scale;
                vector[m_control.Axis] += delta;
                previewed               = PreviewTransforms({&value, 1});
            }
            else
            {
                previewed = PreviewField(m_field.Before + delta);
            }
        }
        // An invalid candidate retains the last valid preview. Release still
        // terminates the interaction and commits only that valid authored state.
        if (!pointer_down && Active())
        {
            return Commit();
        }
        return previewed;
    }

    bool EditorSession::ProcessRequests()
    {
        if (!Allowed())
        {
            return false;
        }
        bool synchronized = false;
        bool succeeded    = true;
        while (m_request_count)
        {
            const auto request = m_requests[m_request_first];
            m_request_first    = (m_request_first + 1) % MaxPendingRequests;
            --m_request_count;
            bool applied = false;
            if (request.Epoch == m_epoch && m_scene)
            {
                // Editor-local toolbar changes need no scene-wide hierarchy
                // compatibility scan. Defer it until the first scene request.
                if (request.Kind != RequestKind::GizmoSettings && !synchronized)
                {
                    SynchronizeHierarchy();
                    synchronized = true;
                }
                if (request.Kind == RequestKind::GizmoSettings)
                {
                    applied = ApplyGizmoChange(request.GizmoChange);
                }
                else if (request.Kind == RequestKind::EntitySelection)
                {
                    const auto entity = m_targets.Resolve(request.Target);
                    // Unlike history replay, a delayed UI request must not select a replacement
                    // entity that acquired the same UUID after the original row disappeared.
                    if (entity.IsValid() && entity == request.Target.CachedEntity)
                    {
                        applied = Select(entity, request.Mode);
                    }
                }
                else if (request.Kind == RequestKind::SceneSelection)
                {
                    applied = SelectScene(request.Scene);
                }
                else if (request.Kind == RequestKind::EntitySelectionRange)
                {
                    if (request.SelectionFirst <= m_selection_requests.size() && request.SelectionCount <= m_selection_requests.size() - request.SelectionFirst)
                    {
                        const std::span<const EntityTarget> range = {m_selection_requests.data() + request.SelectionFirst, request.SelectionCount};
                        bool                                valid = !range.empty();
                        for (const auto& target : range)
                        {
                            const auto entity  = m_targets.Resolve(target);
                            valid             &= entity.IsValid() && entity == target.CachedEntity;
                        }
                        if (valid)
                        {
                            applied = ApplySelectionSet(range, request.SelectionActive, request.SelectionAdditive);
                        }
                    }
                }
                else if (request.Kind == RequestKind::ControlDrag || request.Kind == RequestKind::TransformValue || request.Kind == RequestKind::FieldValue)
                {
                    if (MatchesEditRequest(request))
                    {
                        applied = request.Kind == RequestKind::ControlDrag ? BeginControlRequest(request) : ApplyValueRequest(request);
                    }
                }
                else if (request.Kind == RequestKind::Reparent)
                {
                    const auto  child  = m_targets.Resolve(request.Target);
                    const auto  parent = request.ParentTarget.UUID.is_nil() ? INVALID_ENTITY : m_targets.Resolve(request.ParentTarget);
                    const auto* link   = m_scene->GetComponent<ParentComponent>(child);
                    if (child.IsValid() && child == request.Target.CachedEntity && parent == request.ParentTarget.CachedEntity && (request.ParentTarget.UUID.is_nil() || parent.IsValid()) && (link ? link->Parent : INVALID_ENTITY) == request.ExpectedParent)
                    {
                        applied = Reparent(child, parent);
                    }
                }
                else if (request.Kind == RequestKind::Rename && Editable())
                {
                    const auto entity = m_targets.Resolve(request.Target);
                    auto*      name   = m_scene->GetComponent<NameComponent>(entity);
                    if (entity.IsValid() && entity == request.Target.CachedEntity && name)
                    {
                        const auto end = std::find(std::begin(name->Value), std::end(name->Value), '\0');
                        if (end != std::end(name->Value) && std::string_view(name->Value, end - name->Value) == request.ExpectedName.data())
                        {
                            applied = true;
                            if (std::string_view(name->Value, end - name->Value) != request.Name.data())
                            {
                                // Rename history is a separate feature. Never replay
                                // old authored transactions across an untracked edit.
                                InvalidateForExternalMutation();
                                name    = m_scene->GetComponent<NameComponent>(entity);
                                applied = ZEngine::Helpers::secure_strcpy(name->Value, sizeof(name->Value), request.Name.data()) == 0;
                            }
                        }
                    }
                }
                else if (request.Kind == RequestKind::AddComponent && Editable())
                {
                    const auto  entity = m_targets.Resolve(request.Target);
                    const auto* meta   = ComponentReflectionRegistry::Get().LookupByName(request.Name.data());
                    if (entity.IsValid() && entity == request.Target.CachedEntity && meta && meta->Add && meta->TypeID < ARCHETYPE_MASK_CAPACITY && !MaskHas(m_scene->GetMask(entity), meta->TypeID))
                    {
                        InvalidateForExternalMutation();
                        // Invoke the registered typed initializer, not a component byte image.
                        meta->Add(*m_scene, entity);
                        applied = MaskHas(m_scene->GetMask(entity), meta->TypeID);
                    }
                }
                else if (request.Kind == RequestKind::UntrackedProperty && Editable())
                {
                    const auto           entity  = m_targets.Resolve(request.Target);
                    EditorUntrackedValue current = {};
                    if (entity.IsValid() && entity == request.Target.CachedEntity && ReadUntrackedProperty(*m_scene, entity, request.Property, current) && SameUntrackedProperty(request.Property, current, request.ExpectedProperty) && ValidUntrackedProperty(*m_scene, entity, request.Property, request.PropertyValue))
                    {
                        applied = true;
                        if (!SameUntrackedProperty(request.Property, current, request.PropertyValue))
                        {
                            InvalidateForExternalMutation();
                            ApplyUntrackedProperty(*m_scene, entity, request.Property, request.PropertyValue);
                        }
                    }
                }
                else if ((request.Kind == RequestKind::DeleteEntity || request.Kind == RequestKind::CreateCollection) && Editable() && m_scene_mutations.Validate && m_scene_mutations.Apply)
                {
                    const bool deleting = request.Kind == RequestKind::DeleteEntity;
                    const auto kind     = deleting ? EditorSceneMutation::DeleteEntity : EditorSceneMutation::CreateCollection;
                    const auto entity   = deleting ? m_targets.Resolve(request.Target) : INVALID_ENTITY;
                    if ((!deleting || (entity.IsValid() && entity == request.Target.CachedEntity)) && m_scene_mutations.Validate(m_scene_mutations.Context, *m_scene, kind, entity))
                    {
                        InvalidateForExternalMutation();
                        m_scene_mutations.Apply(m_scene_mutations.Context, *m_scene, kind, entity, request.Name.data());
                        if (m_selection.PruneInvalid(m_targets))
                        {
                            (void) ClearCustomPivot();
                        }
                        applied = true;
                    }
                }
                else if (request.Kind == RequestKind::Command)
                {
                    switch (request.Command)
                    {
                        case EditorCommand::Undo:
                            // A valid ordered burst can exhaust history before
                            // its final key-repeat. Empty history is a no-op,
                            // unlike failed validation or a read-only command.
                            applied = (Editable() && !Active() && !m_history.UndoCount()) || Undo();
                            break;
                        case EditorCommand::Redo:
                            applied = (Editable() && !Active() && !m_history.RedoCount()) || Redo();
                            break;
                        case EditorCommand::Cancel:
                            (void) DiscardEditRequests();
                            Cancel();
                            applied = true;
                            break;
                        case EditorCommand::ClearSelection:
                            ClearSelection();
                            applied = true;
                            break;
                    }
                }
            }
            if (!applied)
            {
                succeeded = false;
                Reject("An editor request was rejected because its target or command is no longer valid.");
            }
        }
        m_selection_requests.clear();
        return succeeded;
    }

    void EditorSession::DiscardRequests()
    {
        if (m_initialized && m_owner == std::this_thread::get_id())
        {
            m_request_first = m_request_count = 0;
            m_selection_requests.clear();
        }
    }

    bool EditorSession::DiscardEditRequests()
    {
        return DiscardEditRequests(true);
    }

    bool EditorSession::DiscardEditRequests(bool include_untracked)
    {
        if (!m_initialized || m_owner != std::this_thread::get_id())
        {
            return false;
        }
        uint32_t       kept  = 0;
        const uint32_t count = m_request_count;
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto& request = m_requests[(m_request_first + i) % MaxPendingRequests];
            if (request.Kind == RequestKind::TransformValue || request.Kind == RequestKind::FieldValue || request.Kind == RequestKind::ControlDrag || (include_untracked && request.Kind == RequestKind::UntrackedProperty))
            {
                continue;
            }
            if (kept != i)
            {
                m_requests[(m_request_first + kept) % MaxPendingRequests] = request;
            }
            ++kept;
        }
        m_request_count = kept;
        return kept != count;
    }

    void EditorSession::Shutdown()
    {
        if (!m_initialized || m_owner != std::this_thread::get_id())
        {
            return;
        }
        m_in_phase = true;
        Cancel();
        if (m_hierarchy_pending)
        {
            SynchronizeHierarchy();
        }
        m_history.Reset();
        m_selection.Clear();
        m_targets.Bind(nullptr, 0);
        m_selected_roots.Invalidate();
        m_selection_requests.clear();
        m_scene_mutations   = {};
        m_scene_settings    = {};
        m_grid              = {};
        m_custom_pivot      = {};
        m_pivot             = {};
        m_light             = {};
        m_camera_gizmo      = {};
        m_gizmo             = {};
        m_scene             = nullptr;
        m_hierarchy_pending = false;
        m_request_first = m_request_count = 0;
        m_in_phase                        = false;
        m_initialized                     = false;
    }

    bool EditorSession::BeginMutationPhase(bool editable)
    {
        if (!m_initialized || m_in_phase || m_owner != std::this_thread::get_id())
        {
            return false;
        }
        m_in_phase = true;
        m_editable = editable;
        if (m_scene && Active())
        {
            SynchronizeHierarchy();
        }
        if (Active() && (!editable || !ValidateActive()))
        {
            Cancel();
        }
        if (m_selection.PruneInvalid(m_targets))
        {
            Cancel();
            (void) ClearCustomPivot();
        }
        return true;
    }

    void EditorSession::EndMutationPhase()
    {
        if (Allowed())
        {
            if (m_hierarchy_pending)
            {
                SynchronizeHierarchy();
            }
            m_in_phase = false;
        }
    }

    void EditorSession::SynchronizeHierarchy()
    {
        if (m_scene)
        {
            m_scene->SynchronizeHierarchy();
        }
        m_hierarchy_pending = false;
    }

    bool EditorSession::BindScene(ZEngine::ECS::Scene* scene, bool saved)
    {
        if (!Allowed())
        {
            return false;
        }
        Cancel(); // original scene is still alive here
        (void) ClearCustomPivot();
        if (m_hierarchy_pending)
        {
            SynchronizeHierarchy();
        }
        m_history.Reset(saved);
        m_content_digest      = {};
        m_saved_digest        = {};
        m_saved_content_known = saved;
        m_selection.Clear();
        m_request_first = m_request_count = 0;
        m_selected_roots.Invalidate();
        m_selection_requests.clear();
        m_scene           = scene;
        m_scene_mutations = {};
        m_scene_settings  = {};
        m_grid            = {};
        SynchronizeHierarchy();
        ++m_epoch;
        m_targets.Bind(scene, m_epoch);
        ++m_document_revision;
        return true;
    }

    bool EditorSession::SetSceneMutationHooks(const EditorSceneMutationHooks& hooks)
    {
        if (!Allowed() || !m_scene || Active() || !hooks.Validate || !hooks.Apply)
        {
            return false;
        }
        // Queued requests carry an epoch, not callback ownership. Keep the
        // structural owner stable until BindScene retires that epoch/queue.
        if (m_scene_mutations.Validate)
        {
            return m_scene_mutations.Context == hooks.Context && m_scene_mutations.Validate == hooks.Validate && m_scene_mutations.Apply == hooks.Apply;
        }
        m_scene_mutations = hooks;
        return true;
    }

    bool EditorSession::ReadSelectedRoots(ZEngine::Core::Containers::ArrayView<const EntityTarget>& output)
    {
        return OnOwnerThread() && !m_hierarchy_pending && m_selected_roots.Read(*this, output);
    }

    bool EditorSession::Select(EntityID entity, SelectionMode mode)
    {
        if (!Allowed() || !m_selection.CanSelect(m_targets, entity, mode))
        {
            return false;
        }
        Cancel();
        const auto revision = m_selection.Revision();
        const bool selected = m_selection.Select(m_targets, entity, mode);
        if (m_selection.Revision() != revision)
        {
            (void) ClearCustomPivot();
        }
        return selected;
    }

    bool EditorSession::SetSelection(std::span<const EntityTarget> targets, const uuids::uuid& active)
    {
        return ApplySelectionSet(targets, active, false);
    }

    bool EditorSession::ApplySelectionSet(std::span<const EntityTarget> targets, const uuids::uuid& active, bool additive)
    {
        if (!Allowed() || !m_selection.Prepare(m_targets, targets, active, additive))
        {
            return false;
        }
        Cancel();
        const auto revision = m_selection.Revision();
        m_selection.PublishPrepared();
        if (m_selection.Revision() != revision)
        {
            (void) ClearCustomPivot();
        }
        return true;
    }

    bool EditorSession::SelectScene(SceneTarget target)
    {
        if (!Allowed() || (target != SceneTarget::None && target != SceneTarget::Grid) || (target != SceneTarget::None && !m_scene))
        {
            return false;
        }
        Cancel();
        const auto revision = m_selection.Revision();
        m_selection.SelectScene(target);
        if (m_selection.Revision() != revision)
        {
            (void) ClearCustomPivot();
        }
        return true;
    }

    bool EditorSession::RequestReparent(EntityID child, EntityID parent)
    {
        PendingRequest request = {};
        request.Target         = TargetFor(child);
        request.ParentTarget   = parent == INVALID_ENTITY ? EntityTarget{} : TargetFor(parent);
        if (request.Target.UUID.is_nil() || (parent != INVALID_ENTITY && request.ParentTarget.UUID.is_nil()))
        {
            return false;
        }
        const auto* link       = m_scene->GetComponent<ParentComponent>(child);
        request.ExpectedParent = link ? link->Parent : INVALID_ENTITY;
        request.Epoch          = m_epoch;
        request.Kind           = RequestKind::Reparent;
        return Enqueue(request);
    }

    bool EditorSession::Reparent(EntityID child, EntityID parent)
    {
        if (!Allowed() || !Editable() || !m_scene->CanSetParent(child, parent))
        {
            return false;
        }
        const auto* link = m_scene->GetComponent<ParentComponent>(child);
        if ((link ? link->Parent : INVALID_ENTITY) == parent)
        {
            return true;
        }
        // Structural history remains a separate delivery. Do not replay older
        // local-transform commands across an untracked parent-frame change.
        InvalidateForExternalMutation();
        const bool changed   = m_scene->SetParent(child, parent);
        m_hierarchy_pending |= changed;
        return changed;
    }

    void EditorSession::ClearSelection()
    {
        if (!Allowed())
        {
            return;
        }
        Cancel();
        m_selection.Clear();
        (void) ClearCustomPivot();
    }

    EditorSession::TransformBatchHeader& EditorSession::Batch()
    {
        return *reinterpret_cast<TransformBatchHeader*>(m_transaction);
    }

    EditorSession::TransformChange* EditorSession::Changes()
    {
        return reinterpret_cast<TransformChange*>(m_transaction + sizeof(TransformBatchHeader));
    }

    const EditorSession::TransformChange* EditorSession::Changes() const
    {
        return reinterpret_cast<const TransformChange*>(m_transaction + sizeof(TransformBatchHeader));
    }

    void EditorSession::StartInteraction(Interaction kind)
    {
        m_start_structural_revision = m_scene->StructuralRevision();
        m_start_selection_revision  = m_selection.Revision();
        m_start_hierarchy_revision  = m_scene->Hierarchy().Revision();
        m_active                    = kind;
        m_gizmo_observer_count      = 0;
        m_gizmo_root_frame_count    = 0;
        m_preview_changed           = false;
        m_last_error                = "";
    }

    bool EditorSession::BeginTransform(std::span<const EntityTarget> roots, const ZEngine::Core::Maths::Vec3f& pivot, TransformSpacePolicy space)
    {
        if (!Allowed())
        {
            return false;
        }
        if (!m_editable || !m_scene || Active() || roots.empty() || roots.size() > m_max_targets || !ValidLocalTransform({pivot}) || (space != TransformSpacePolicy::World && space != TransformSpacePolicy::Local))
        {
            return Reject("Transform interaction cannot begin in the current editor state.");
        }
        const size_t bytes = sizeof(TransformBatchHeader) + roots.size() * sizeof(TransformChange);
        if (!m_history.CanStore(bytes))
        {
            return Reject("The complete transform selection exceeds the history byte budget.");
        }
        SynchronizeHierarchy();
        static_assert(sizeof(TransformBatchHeader) % alignof(TransformChange) == 0);
        std::construct_at(reinterpret_cast<TransformBatchHeader*>(m_transaction), TransformBatchHeader{m_epoch, static_cast<uint32_t>(roots.size()), space, pivot});
        m_unique.clear();
        for (uint32_t i = 0; i < roots.size(); ++i)
        {
            const auto  entity    = m_targets.Resolve(roots[i]);
            const auto* component = m_scene->GetComponent<TransformComponent>(entity);
            const auto* frame     = m_scene->Hierarchy().Find(entity);
            if (!entity.IsValid() || !component || !frame || !frame->FrameValid || m_unique.contains(roots[i].UUID))
            {
                return Reject("Every transform target must be live, unique and have a local transform.");
            }
            const auto value = CaptureLocalTransform(*component);
            if (!ValidLocalTransform(value))
            {
                return Reject("A transform target contains non-finite authored values.");
            }
            m_unique.insert(roots[i].UUID, i);
            std::construct_at(Changes() + i, TransformChange{roots[i].UUID, value, value});
            const auto* parent = m_scene->GetComponent<ParentComponent>(entity);
            std::construct_at(m_runtime + i, TransformRuntime{.Entity = entity, .Parent = parent ? parent->Parent : EntityID{}, .PreviousPosition = component->PreviousPosition, .HadParent = parent != nullptr, .ParentWorldRevision = m_scene->Hierarchy().ParentWorldRevision(entity), .ComponentLifetime = m_scene->ComponentLifetime<TransformComponent>(entity)});
        }
        m_transaction_bytes = bytes;
        StartInteraction(Interaction::Transform);
        return true;
    }

    bool EditorSession::BeginField(const EntityTarget& target, AuthoredFloatField field)
    {
        if (!Allowed())
        {
            return false;
        }
        if (!m_editable || !m_scene || Active() || !m_history.CanStore(sizeof(FieldChange)))
        {
            return Reject("Field interaction cannot begin in the current editor state.");
        }
        SynchronizeHierarchy();
        const auto  entity   = m_targets.Resolve(target);
        const auto* frame    = m_scene->Hierarchy().Find(entity);
        const auto  lifetime = AuthoredFloatFieldLifetime(*m_scene, entity, field);
        float       value    = 0.f;
        if (!lifetime || !frame || !frame->FrameValid || !ReadAuthoredFloatField(*m_scene, entity, field, value) || !ValidAuthoredFloatField(*m_scene, entity, field, value))
        {
            return Reject("The target does not have a valid editable component field.");
        }
        m_field                    = {target.UUID, m_epoch, field, value, value};
        m_field_entity             = entity;
        m_field_frame_revision     = frame->WorldRevision;
        m_field_component_lifetime = lifetime;
        StartInteraction(Interaction::Field);
        return true;
    }

    bool EditorSession::ValidateActive()
    {
        if (m_active == Interaction::Grid)
        {
            return ValidateGrid();
        }
        if (!m_scene || !m_editable || m_scene->StructuralRevision() != m_start_structural_revision || m_selection.Revision() != m_start_selection_revision || m_scene->Hierarchy().Revision() != m_start_hierarchy_revision)
        {
            return false;
        }
        if (m_active == Interaction::Pivot)
        {
            return ValidatePivot();
        }
        if (m_active == Interaction::Transform)
        {
            for (uint32_t i = 0; i < Batch().Count; ++i)
            {
                const auto entity = m_targets.Resolve(Changes()[i].UUID);
                if (entity != m_runtime[i].Entity || !m_scene->IsAlive(entity))
                {
                    return false;
                }
                const auto* component = m_scene->GetComponent<TransformComponent>(entity);
                const auto* parent    = m_scene->GetComponent<ParentComponent>(entity);
                const auto* frame     = m_scene->Hierarchy().Find(entity);
                if (!component || !frame || !frame->FrameValid || m_scene->Hierarchy().ParentWorldRevision(entity) != m_runtime[i].ParentWorldRevision || (parent != nullptr) != m_runtime[i].HadParent || (parent && parent->Parent != m_runtime[i].Parent) || !SameLocalTransform(CaptureLocalTransform(*component), Changes()[i].After))
                {
                    return false;
                }
            }
            return ValidateGizmoObservers();
        }
        float       current = 0.f;
        const auto* frame   = m_scene->Hierarchy().Find(m_field_entity);
        return m_active == Interaction::Field && frame && frame->FrameValid && frame->WorldRevision == m_field_frame_revision && m_targets.Resolve(m_field.UUID) == m_field_entity && (!m_light.Key || ValidateLight()) && (!m_camera_gizmo.Key || ValidateCamera()) && ReadAuthoredFloatField(*m_scene, m_field_entity, m_field.Field, current) && current == m_field.After && ValidAuthoredFloatField(*m_scene, m_field_entity, m_field.Field, current);
    }

    bool EditorSession::PreviewTransforms(std::span<const LocalTransformValue> values)
    {
        if (!Allowed() || m_active != Interaction::Transform)
        {
            return false;
        }
        if (!ValidateActive())
        {
            Cancel();
            return Reject("The transform interaction was cancelled because its targets changed.");
        }
        if (values.size() != Batch().Count)
        {
            return Reject("A transform preview must contain the complete captured target set.");
        }
        for (const auto& value : values)
        {
            if (!ValidLocalTransform(value))
            {
                return Reject("A non-finite transform preview was rejected without changing any target.");
            }
        }

        bool changed_from_preview = false;
        m_preview_changed         = false;
        for (uint32_t i = 0; i < values.size(); ++i)
        {
            auto& change          = Changes()[i];
            changed_from_preview |= !SameLocalTransform(change.After, values[i]);
            AccountTransform(change.UUID, change.After, values[i]);
            ApplyLocalTransform(*m_scene->GetComponent<TransformComponent>(m_runtime[i].Entity), values[i]);
            change.After       = values[i];
            m_preview_changed |= !SameLocalTransform(change.Before, change.After);
        }
        if (changed_from_preview)
        {
            m_hierarchy_pending = true;
            ++m_document_revision;
        }
        return true;
    }

    bool EditorSession::PreviewField(float value)
    {
        if (!Allowed() || m_active != Interaction::Field)
        {
            return false;
        }
        if (!ValidateActive())
        {
            Cancel();
            return Reject("The field interaction was cancelled because its target changed.");
        }
        if (!ValidAuthoredFloatField(*m_scene, m_field_entity, m_field.Field, value))
        {
            return Reject("The component field preview is outside its valid range.");
        }
        if (m_field.After != value)
        {
            ++m_document_revision;
        }
        AccountField(m_field.UUID, m_field.Field, m_field.After, value);
        ApplyAuthoredFloatField(*m_scene, m_field_entity, m_field.Field, value);
        m_field.After     = value;
        m_preview_changed = value != m_field.Before;
        return true;
    }

    bool EditorSession::Commit()
    {
        if (!Allowed() || !Active())
        {
            return false;
        }
        if (!ValidateActive())
        {
            Cancel();
            return Reject("The interaction could not commit because its target state changed.");
        }
        if (m_active == Interaction::Pivot)
        {
            m_active = Interaction::None;
            m_pivot  = {};
            return true;
        }
        if (!m_preview_changed)
        {
            Cancel();
            return true;
        }
        // A moved gizmo carries its custom pivot with the actually authored
        // displacement. Resolve once at release, never feed the live pivot back
        // into the immutable translation mapping or add it to document history.
        Gizmo::PivotState translated_pivot = m_custom_pivot;
        if (m_active == Interaction::Transform && m_translation.Key && translated_pivot.Valid)
        {
            if (m_hierarchy_pending)
            {
                SynchronizeHierarchy();
            }
            Gizmo::TranslationSnapshot snapshot = {};
            if (!ReadTranslationSnapshot(snapshot))
            {
                Cancel();
                return Reject("The translated custom pivot could not be reconstructed safely.");
            }
            translated_pivot.Position = snapshot.Pivot;
        }
        const bool pushed = m_active == Interaction::Transform ? m_history.PushAlreadyApplied(TransformOperation(), {m_transaction, m_transaction_bytes}) : m_active == Interaction::Grid ? m_history.PushAlreadyApplied(GridOperation(), {reinterpret_cast<const std::byte*>(&m_grid), sizeof(m_grid)}) : m_history.PushAlreadyApplied(FieldOperation(), std::as_bytes(std::span(&m_field, 1)));
        if (!pushed)
        {
            Cancel();
            return Reject("The complete interaction could not be retained in shared history.");
        }
        SetCustomPivot(translated_pivot);
        m_active                   = Interaction::None;
        m_preview_changed          = false;
        m_field_component_lifetime = 0;
        m_control                  = {};
        m_translation              = {};
        m_rotation                 = {};
        m_scale                    = {};
        m_gizmo_observer_count     = 0;
        m_gizmo_root_frame_count   = 0;
        m_light                    = {};
        m_camera_gizmo             = {};
        m_grid                     = {};
        return true;
    }

    void EditorSession::Cancel()
    {
        if (!Allowed() || !Active())
        {
            return;
        }
        if (m_active == Interaction::Grid)
        {
            CancelGrid();
            return;
        }
        if (m_active == Interaction::Pivot)
        {
            SetCustomPivot(m_pivot.Before);
            m_pivot  = {};
            m_active = Interaction::None;
            return;
        }
        bool unexpected_change = m_scene && (m_scene->StructuralRevision() != m_start_structural_revision || m_scene->Hierarchy().Revision() != m_start_hierarchy_revision || !ValidateGizmoObservers());
        if (m_scene && m_active == Interaction::Transform && Batch().Epoch == m_epoch)
        {
            for (uint32_t i = 0; i < Batch().Count; ++i)
            {
                // History can resolve a restored UUID; a live drag owns only its
                // original entity AND component instance, never a remove/add replacement.
                const auto entity = m_runtime[i].Entity;
                if (!m_scene->IsAlive(entity) || m_targets.Resolve(Changes()[i].UUID) != entity || m_scene->ComponentLifetime<TransformComponent>(entity) != m_runtime[i].ComponentLifetime)
                {
                    unexpected_change = true;
                    continue;
                }
                if (auto* component = m_scene->GetComponent<TransformComponent>(entity))
                {
                    const auto* parent  = m_scene->GetComponent<ParentComponent>(entity);
                    const auto* frame   = m_scene->Hierarchy().Find(entity);
                    unexpected_change  |= !frame || !frame->FrameValid || m_scene->Hierarchy().ParentWorldRevision(entity) != m_runtime[i].ParentWorldRevision;
                    unexpected_change  |= (parent != nullptr) != m_runtime[i].HadParent || (parent && parent->Parent != m_runtime[i].Parent);
                    // A newer external authored pose is not our preview to
                    // overwrite. Still restore every other owned batch member.
                    if (SameLocalTransform(CaptureLocalTransform(*component), Changes()[i].After))
                    {
                        AccountTransform(Changes()[i].UUID, Changes()[i].After, Changes()[i].Before);
                        ApplyLocalTransform(*component, Changes()[i].Before);
                        m_hierarchy_pending         = true;
                        component->PreviousPosition = m_runtime[i].PreviousPosition;
                    }
                    else
                    {
                        unexpected_change = true;
                    }
                }
                else
                {
                    unexpected_change = true;
                }
            }
        }
        else if (m_scene && m_active == Interaction::Field && m_field.Epoch == m_epoch && m_scene->IsAlive(m_field_entity) && m_targets.Resolve(m_field.UUID) == m_field_entity && m_field_component_lifetime && AuthoredFloatFieldLifetime(*m_scene, m_field_entity, m_field.Field) == m_field_component_lifetime)
        {
            float value = 0.f;
            if (ReadAuthoredFloatField(*m_scene, m_field_entity, m_field.Field, value))
            {
                const auto* frame  = m_scene->Hierarchy().Find(m_field_entity);
                unexpected_change |= !frame || !frame->FrameValid || frame->WorldRevision != m_field_frame_revision;
                unexpected_change |= m_light.Key && !ValidateLight();
                unexpected_change |= m_camera_gizmo.Key && !ValidateCamera();
                // Roll back only our last owned preview. An external field edit
                // or a changed component constraint is not ours to overwrite.
                if (value == m_field.After && ValidAuthoredFloatField(*m_scene, m_field_entity, m_field.Field, m_field.Before))
                {
                    AccountField(m_field.UUID, m_field.Field, m_field.After, m_field.Before);
                    ApplyAuthoredFloatField(*m_scene, m_field_entity, m_field.Field, m_field.Before);
                }
                else
                {
                    unexpected_change = true;
                }
            }
            else
            {
                unexpected_change = true;
            }
        }
        else
        {
            unexpected_change = true;
        }
        if (unexpected_change)
        {
            m_history.Invalidate();
            m_saved_content_known = false;
        }
        if (m_preview_changed)
        {
            ++m_document_revision;
        }
        m_active                   = Interaction::None;
        m_preview_changed          = false;
        m_field_component_lifetime = 0;
        m_control                  = {};
        m_translation              = {};
        m_rotation                 = {};
        m_scale                    = {};
        m_gizmo_observer_count     = 0;
        m_gizmo_root_frame_count   = 0;
        m_light                    = {};
        m_camera_gizmo             = {};
    }

    bool EditorSession::Undo()
    {
        if (!Allowed() || !m_editable)
        {
            return false;
        }
        Cancel();
        return m_history.Undo(this);
    }

    bool EditorSession::Redo()
    {
        if (!Allowed() || !m_editable)
        {
            return false;
        }
        Cancel();
        return m_history.Redo(this);
    }

    bool EditorSession::MarkSaved()
    {
        if (!Allowed() || Active())
        {
            return false;
        }
        m_history.MarkSaved();
        m_saved_digest        = m_content_digest;
        m_saved_content_known = true;
        return true;
    }

    void EditorSession::InvalidateForExternalMutation(bool hierarchy_changed)
    {
        if (!Allowed())
        {
            return;
        }
        (void) DiscardEditRequests(false);
        Cancel();
        m_history.Invalidate();
        m_saved_content_known  = false;
        m_hierarchy_pending   |= hierarchy_changed;
        ++m_document_revision;
    }

    uint32_t EditorSession::TransformCount() const
    {
        return m_active == Interaction::Transform ? reinterpret_cast<const TransformBatchHeader*>(m_transaction)->Count : 0;
    }

    const LocalTransformValue* EditorSession::TransformBefore(uint32_t index) const
    {
        return index < TransformCount() ? &Changes()[index].Before : nullptr;
    }

    EditorOperation EditorSession::TransformOperation()
    {
        return {
            .Validate = ValidateTransformOperation,
            .Apply    = [](void* context, std::span<const std::byte> payload) { ApplyTransformOperation(context, payload, false); },
            .Revert   = [](void* context, std::span<const std::byte> payload) { ApplyTransformOperation(context, payload, true); },
            .Destroy  = [](std::span<std::byte>) {},
            .Name     = [](std::span<const std::byte>) -> cstring { return "Transform"; },
        };
    }

    bool EditorSession::ValidateTransformOperation(void* context, std::span<const std::byte> payload, bool undo)
    {
        auto& session = *static_cast<EditorSession*>(context);
        if (!session.Allowed() || !session.m_editable || !session.m_scene || payload.size() < sizeof(TransformBatchHeader))
        {
            return false;
        }
        const auto& header = *reinterpret_cast<const TransformBatchHeader*>(payload.data());
        if (header.Epoch != session.m_epoch || !header.Count || header.Count > session.m_max_targets || payload.size() != sizeof(header) + header.Count * sizeof(TransformChange))
        {
            return false;
        }
        const auto* changes = reinterpret_cast<const TransformChange*>(payload.data() + sizeof(header));
        for (uint32_t i = 0; i < header.Count; ++i)
        {
            const auto  entity    = session.m_targets.Resolve(changes[i].UUID);
            const auto* component = session.m_scene->GetComponent<TransformComponent>(entity);
            if (!entity.IsValid() || !component || !ValidLocalTransform(undo ? changes[i].Before : changes[i].After) || !SameLocalTransform(CaptureLocalTransform(*component), undo ? changes[i].After : changes[i].Before))
            {
                return session.Reject("Transform history no longer matches the scene; no target was changed.");
            }
        }
        return true;
    }

    void EditorSession::ApplyTransformOperation(void* context, std::span<const std::byte> payload, bool undo)
    {
        auto&       session = *static_cast<EditorSession*>(context);
        const auto& header  = *reinterpret_cast<const TransformBatchHeader*>(payload.data());
        const auto* changes = reinterpret_cast<const TransformChange*>(payload.data() + sizeof(header));
        for (uint32_t i = 0; i < header.Count; ++i)
        {
            session.AccountTransform(changes[i].UUID, undo ? changes[i].After : changes[i].Before, undo ? changes[i].Before : changes[i].After);
            ApplyLocalTransform(*session.m_scene->GetComponent<TransformComponent>(session.m_targets.Resolve(changes[i].UUID)), undo ? changes[i].Before : changes[i].After);
        }
        session.m_hierarchy_pending = true;
        ++session.m_document_revision;
    }

    EditorOperation EditorSession::FieldOperation()
    {
        return {
            .Validate = ValidateFieldOperation,
            .Apply    = [](void* context, std::span<const std::byte> payload) { ApplyFieldOperation(context, payload, false); },
            .Revert   = [](void* context, std::span<const std::byte> payload) { ApplyFieldOperation(context, payload, true); },
            .Destroy  = [](std::span<std::byte>) {},
            .Name     = [](std::span<const std::byte>) -> cstring { return "Component field"; },
        };
    }

    bool EditorSession::ValidateFieldOperation(void* context, std::span<const std::byte> payload, bool undo)
    {
        auto& session = *static_cast<EditorSession*>(context);
        if (!session.Allowed() || !session.m_editable || !session.m_scene || payload.size() != sizeof(FieldChange))
        {
            return false;
        }
        const auto& change  = *reinterpret_cast<const FieldChange*>(payload.data());
        const auto  entity  = session.m_targets.Resolve(change.UUID);
        float       current = 0.f;
        if (change.Epoch != session.m_epoch || !ReadAuthoredFloatField(*session.m_scene, entity, change.Field, current) || current != (undo ? change.After : change.Before) || !ValidAuthoredFloatField(*session.m_scene, entity, change.Field, undo ? change.Before : change.After))
        {
            return session.Reject("Component history no longer matches the scene; the field was not changed.");
        }
        return true;
    }

    void EditorSession::ApplyFieldOperation(void* context, std::span<const std::byte> payload, bool undo)
    {
        auto&       session = *static_cast<EditorSession*>(context);
        const auto& change  = *reinterpret_cast<const FieldChange*>(payload.data());
        session.AccountField(change.UUID, change.Field, undo ? change.After : change.Before, undo ? change.Before : change.After);
        ApplyAuthoredFloatField(*session.m_scene, session.m_targets.Resolve(change.UUID), change.Field, undo ? change.Before : change.After);
        ++session.m_document_revision;
    }
} // namespace Tetragrama

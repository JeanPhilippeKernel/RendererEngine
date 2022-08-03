#pragma once
#include <vector>
#include <algorithm>
#include <Maths/Math.h>
#include <Rendering/Renderers/Storages/GraphicVertex.h>
#include <Rendering/Geometries/GeometryEnum.h>

namespace ZEngine::Rendering::Geometries {

    struct IGeometry {
        explicit IGeometry() : m_position(0.0f, 0.0f, 0.0f), m_scale_size(0.5f, 0.5f, 0.5f), m_rotation_axis(0.0f, 1.0f, 0.0f), m_rotation_angle(0.0f) {
            Update();
        }

        explicit IGeometry(const Maths::Vector3& position, const Maths::Vector3& scale, const Maths::Vector3& rotation_axis, float rotation_angle)
            : m_position(position), m_scale_size(scale), m_rotation_axis(rotation_axis), m_rotation_angle(rotation_angle) {
            Update();
        }

        explicit IGeometry(const Maths::Vector3& position, const Maths::Vector3& scale, const Maths::Vector3& rotation_axis, float rotation_angle,
            std::vector<Renderers::Storages::GraphicVertex>&& vertices)
            : m_position(position), m_scale_size(scale), m_rotation_axis(rotation_axis), m_rotation_angle(rotation_angle), m_vertices(std::move(vertices)) {
            Update();
        }

        virtual ~IGeometry() = default;

        virtual void SetVertices(std::vector<Renderers::Storages::GraphicVertex>&& vertices) {
            m_vertices = std::move(vertices);
        }

        virtual void Update() {
            m_transform = translate(Maths::Matrix4(1.0f), m_position) * rotate(Maths::Matrix4(1.0f), m_rotation_angle, m_rotation_axis) * scale(Maths::Matrix4(1.0f), m_scale_size);
        }

        virtual void SetPosition(const Maths::Vector3& value) {
            m_position = value;
            Update();
        }

        virtual void SetScaleSize(const Maths::Vector3& value) {
            m_scale_size = value;
            Update();
        }

        virtual void SetRotationAxis(const Maths::Vector3& value) {
            m_rotation_axis = value;
            Update();
        }

        virtual void SetRotationAngle(float value) {
            m_rotation_angle = value;
            Update();
        }

        virtual Maths::Vector3 GetPosition() const {
            return m_position;
        }

        virtual Maths::Vector3 GetScaleSize() const {
            return m_scale_size;
        }

        virtual Maths::Vector3 GetRotationAxis() const {
            return m_rotation_axis;
        }

        virtual float GetRotationAngle() const {
            return m_rotation_angle;
        }

        virtual const Maths::Matrix4& GetTransform() const {
            return m_transform;
        }

        virtual std::vector<Renderers::Storages::GraphicVertex>& GetVertices() {
            return m_vertices;
        }

        virtual GeometryType GetGeometryType() const {
            return m_geometry_type;
        }

    protected:
        Maths::Vector3                                  m_position;
        Maths::Vector3                                  m_scale_size;
        Maths::Vector3                                  m_rotation_axis;
        float                                           m_rotation_angle;
        Maths::Matrix4                                  m_transform;
        std::vector<Renderers::Storages::GraphicVertex> m_vertices{};
        GeometryType                                    m_geometry_type{GeometryType::CUSTOM};
    };
} // namespace ZEngine::Rendering::Geometries

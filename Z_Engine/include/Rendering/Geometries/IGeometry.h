#pragma once
#include <vector>
#include <algorithm>
#include "../../dependencies/glm/glm.hpp"

#include "../Renderers/Storages/GraphicVertex.h"
#include "../Buffers/VertexBuffer.h"

#include "../../Z_EngineDef.h"


namespace Z_Engine::Rendering::Geometries {

	struct IGeometry
	{
		explicit IGeometry() = default;
		explicit IGeometry(std::vector<Renderers::Storages::GraphicVertex>&& vertices) 
			: m_vertices(std::move(vertices)) 
		{
			Update();
		}

		virtual ~IGeometry() = default;

		virtual void SetVertices(std::vector<Renderers::Storages::GraphicVertex>&& vertices) {
			m_vertices = std::move(vertices);
			Update();
		}


		virtual void Update() {
			_FillVertexBuffers();
		}

		virtual void ApplyTranslation(const glm::vec3& translation) = delete;
		virtual void ApplyRotation(const glm::vec3& axis, float rad_angle) = delete;
		virtual void ApplyScaling(const glm::vec3& size) = delete;
		
		virtual void ApplyTransform(const glm::mat4& transform) {
			std::for_each(std::begin(m_vertices), std::end(m_vertices), 
				[&](Renderers::Storages::GraphicVertex& vertex) {
					vertex.ApplyMatrixToPosition(transform);
				}
			);

			Update();
		}

		virtual std::vector<Renderers::Storages::GraphicVertex>& GetVertices() { return m_vertices; }
		virtual const Ref<Buffers::VertexBuffer<float>>& GetBuffer() const { return m_internal_geometry_buffer; }
	
	protected:
		Ref<Buffers::VertexBuffer<float>> m_internal_geometry_buffer	{ nullptr };

		std::vector<Renderers::Storages::GraphicVertex> m_vertices		{};

		void _FillVertexBuffers() {
			if (m_internal_geometry_buffer == nullptr) {
				m_internal_geometry_buffer.reset(new Buffers::VertexBuffer<float>(m_vertices.size()));
				m_internal_geometry_buffer->SetLayout(Renderers::Storages::GraphicVertex::Descriptor::GetLayout());
			}

			std::vector<float> internal_float_representation;

			std::for_each(std::begin(m_vertices), std::end(m_vertices), [&internal_float_representation](const Renderers::Storages::GraphicVertex& vertex) {
				const auto& data = vertex.GetData();
				std::copy(std::begin(data), std::end(data), std::back_inserter(internal_float_representation));
			});

			m_internal_geometry_buffer->SetData(internal_float_representation);
		}
	};
}
#pragma once
#include <vector>
#include <glm/glm.hpp>

#include "../Renderers/Storages/GraphicVertex.h"


namespace Z_Engine::Rendering::Geometries {

	struct IGeometry
	{
		explicit IGeometry() = default;
		explicit IGeometry(std::vector<Renderers::Storages::GraphicVertex>&& vertices) 
			: m_vertices(vertices) 
		{}

		virtual ~IGeometry() = default;

		virtual void SetVertices(std::vector<Renderers::Storages::GraphicVertex>&& vertices) {
			m_vertices = vertices;
		}

		virtual void ApplyTranslation(const glm::vec3& translation) = delete;
		virtual void ApplyRotation(const glm::vec3& axis, float rad_angle) = delete;
		virtual void ApplyScaling(const glm::vec3& size) = delete;
		
		virtual void ApplyTransform(const glm::mat4& transform) {
			std::for_each(std::begin(m_vertices), std::end(m_vertices), 
				[&](Renderers::Storages::GraphicVertex& vertex) {
					glm::vec4 position = glm::vec4(vertex.GetPosition(), 1.0f);
					position = transform * position;

					vertex.SetPosition(position);
				}
			);
		}

		virtual std::vector<Renderers::Storages::GraphicVertex>& GetVertices() { return m_vertices; }
	
	protected:
		std::vector<Renderers::Storages::GraphicVertex> m_vertices;
	};
}
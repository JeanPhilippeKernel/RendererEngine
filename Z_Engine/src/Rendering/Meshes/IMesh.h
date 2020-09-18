#pragma once
#include <vector>
#include <initializer_list>
#include <glm/glm.hpp>

#include <algorithm>

#include "../Renderers/Storages/GraphicVertex.h"
#include "../Materials/IMaterial.h"
#include "../../Z_EngineDef.h"

namespace Z_Engine::Rendering::Meshes {

	struct IMesh
	{
		IMesh() = default;

		IMesh(const std::initializer_list<Renderers::Storages::GraphicVertex>& graphic_vertices)
			:m_material(nullptr), m_transform(glm::mat4(1.0f)), m_graphic_vertices(graphic_vertices)
		{
		}

		virtual ~IMesh() = default;

		virtual void SetTransform(const glm::mat4& transform) { m_transform = transform; }
		virtual glm::mat4 GetTransform() const { return m_transform; }

		void SetMaterial(const Ref<Materials::IMaterial>& material)	{
			m_material = m_material;
			m_material->SetOwnerMesh(this);
		}

		Ref<Materials::IMaterial>& GetMaterial() { return m_material; }

		std::vector<Renderers::Storages::GraphicVertex> GetVertices() {

			//std::vector<Renderers::Storages::GraphicVertex> vertices;
			//
			//std::transform(std::begin(m_graphic_vertices), std::end(m_graphic_vertices), std::back_inserter(vertices),
			//	[this](Renderers::Storages::GraphicVertex& vertex) -> Renderers::Storages::GraphicVertex {
			//		auto old_pos = vertex.GetPosition();
			//		auto new_pos = m_transform * glm::vec4(old_pos, 1.0f);
			//		auto color = vertex.GetColor();
			//		auto texture = vertex.GetTexture();
			//		
			//		return { {new_pos.x, new_pos.y, new_pos.z}, color, texture };
			//	}			
			//	);

			//return vertices;

			std::for_each(std::begin(m_graphic_vertices), std::end(m_graphic_vertices), 
				[this](Renderers::Storages::GraphicVertex& vertex) {
					auto current_position = vertex.GetPosition();
					auto new_position = m_transform * glm::vec4(current_position, 1.0f);
					vertex.SetPosition(new_position);
				});

			return m_graphic_vertices; 
		}


	protected:
	   glm::mat4 m_transform;
	   std::vector<Renderers::Storages::GraphicVertex> m_graphic_vertices;
	   Ref<Materials::IMaterial> m_material;
	};
}
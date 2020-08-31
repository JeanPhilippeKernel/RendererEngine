#pragma once
#include <vector>

#include "../Buffers/IndexBuffer.h"
#include "../Buffers/BufferLayout.h"
#include "../../Managers/ShaderManager.h"
#include "../Renderer/GraphicRendererStorage.h"


namespace Z_Engine::Rendering::Meshes {

	template<typename T, typename K>
	struct IMesh
	{
		IMesh() = default;

		IMesh(const Z_Engine::Ref<Z_Engine::Rendering::Shaders::Shader>& shader,
			const std::vector<T>& data, 
			const std::vector<K>& index,
			std::initializer_list<Z_Engine::Rendering::Buffers::Layout::ElementLayout<T>> elements_list) 
			:
			m_shader(shader), m_data(data), m_index(index), m_internal_layout(elements_list),
			m_internal_storage(new Z_Engine::Rendering::Renderer::GraphicRendererStorage<T, K>(m_shader, m_data, m_index, m_internal_layout))
		{}

		virtual ~IMesh() = default;

		constexpr Z_Engine::Ref<Z_Engine::Rendering::Renderer::GraphicRendererStorage<T, K>> GetStorage() { return m_internal_storage; }

	protected:
	   std::vector<T>												m_data;
	   std::vector<K>												m_index;
	   Z_Engine::Ref<Z_Engine::Rendering::Shaders::Shader>			m_shader;
	   Z_Engine::Rendering::Buffers::Layout::BufferLayout<T>		m_internal_layout;
	   Z_Engine::Ref<Z_Engine::Rendering::Renderer::GraphicRendererStorage<T, K>>	m_internal_storage;
	};
}
#pragma once
#include <vector>

#include "BufferLayout.h"
#include "../../Z_EngineDef.h"


namespace Z_Engine::Rendering::Buffers {

	template<typename T>
	class Z_ENGINE_API GraphicBuffer {
	public:
		explicit GraphicBuffer(const std::vector<T>& data)
			: m_data(data), m_data_size(data.size()) 
		{
			m_byte_size = m_data_size * sizeof(T);
		}

		virtual ~GraphicBuffer() = default;

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		virtual size_t GetByteSize() const { return m_byte_size; }
		virtual size_t GetDataSize() const { return m_data_size; }

		virtual void SetLayout(Layout::BufferLayout<T>& layout) {
			this->m_layout = layout;
			UpdateLayout();
		}

		virtual const Layout::BufferLayout<T>& GetLayout() const { 
			return this->m_layout; 
		}

	protected:
		const std::vector<T>& m_data;
		Layout::BufferLayout<T> m_layout;

		size_t m_byte_size{ 0 };
		size_t m_data_size{ 0 };

	protected:
		virtual void UpdateLayout() {
			auto& elements = m_layout.GetElementLayout();

			auto start = elements.begin() + 1;
			auto end = elements.end();

			while (start != end)
			{
				auto current = start - 1;
				auto value = current->GetSize() + current->GetOffset();
				start->SetOffset(value);

				++start;
			}
		}
	};
}

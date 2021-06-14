#pragma once
#include <vector>
#include <algorithm>
#include <iterator>

#include <Rendering/Buffers/BufferLayout.h>
#include <Z_EngineDef.h>


namespace ZEngine::Rendering::Buffers {

	template<typename T>
	class GraphicBuffer {
	public:
		explicit GraphicBuffer();

		explicit GraphicBuffer(const std::vector<T>& data)
			: m_data(data), m_data_size(data.size()) 
		{
			m_byte_size = m_data_size * sizeof(T);
		}

		virtual ~GraphicBuffer() = default;

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		virtual void SetData(const std::vector<T>& data) {
			if(this->m_data_size > 0) m_data.clear();
			
			std::for_each(std::begin(data), std::end(data), [this](const T& x) { m_data.emplace_back(x); } );
			this->m_data_size	= data.size();
			this->m_byte_size	= m_data_size * sizeof(T);
		}

		virtual const std::vector<T>& GetData() const {
			return m_data;
		}

		virtual size_t GetByteSize() const { return m_byte_size; }
		virtual size_t GetDataSize() const { return m_data_size; }

		virtual void SetLayout(Layout::BufferLayout<T>& layout) {
			this->m_layout = layout;
			UpdateLayout();
		}

		virtual void SetLayout(Layout::BufferLayout<T>&& layout) {
			this->m_layout = std::move(layout);
			UpdateLayout();
		}

		virtual const Layout::BufferLayout<T>& GetLayout() const { 
			return this->m_layout; 
		}

	protected:
		std::vector<T> m_data;
		Layout::BufferLayout<T> m_layout;

		size_t m_byte_size{ 0 };
		size_t m_data_size{ 0 };

	protected:
		virtual void UpdateLayout() {
			auto& elements = m_layout.GetElementLayout();

			auto start = std::begin(elements) + 1;
			auto end = std::end(elements);

			while (start != end)
			{
				auto current = start - 1;
				auto value = current->GetSize() + current->GetOffset();
				start->SetOffset(value);

				++start;
			}
		}
	};

	template<typename T>
	inline GraphicBuffer<T>::GraphicBuffer()
		:m_data()
	{
	}
}

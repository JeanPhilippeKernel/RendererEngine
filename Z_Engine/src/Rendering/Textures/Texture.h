#pragma once
#include <string>
#include <GL/glew.h>


namespace Z_Engine::Rendering::Textures {
	class Texture {
	public:

		Texture(const char* path) : m_path(path) {}
		Texture(unsigned int width, unsigned int height) : m_width(width), m_height(height) {}

		virtual ~Texture() =  default;

		virtual void Bind(int slot = 0) const = 0;
		virtual void Unbind(int slot = 0) const = 0;

		virtual void SetData(const void* data) = 0;
		virtual void SetData(float r, float g, float b, float a) = 0;


		virtual bool operator==(const Texture& right) {
			return m_texture_id == right.m_texture_id;
		}

		virtual bool operator!=(const Texture& right) {
			return m_texture_id != right.m_texture_id;
		}

	protected:
		std::string m_path;
		GLuint m_texture_id{0};

		unsigned int m_width{0};
		unsigned int m_height{0};

		unsigned int m_data_format{ 0 };
		int			 m_internal_format{ 0 };
	};


	Texture * CreateTexture(const char * path);
	Texture * CreateTexture(unsigned int width, unsigned int height);
}
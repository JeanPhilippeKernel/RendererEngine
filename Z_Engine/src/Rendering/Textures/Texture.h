#pragma once
#include <string>
#include <GL/glew.h>


namespace Z_Engine::Rendering::Textures {
	class Texture {
	public:

		Texture(const char* path) : m_path(path) {}

		virtual ~Texture() =  default;

		virtual void Bind(int slot = 0) const = 0;
		virtual void Unbind(int slot = 0) const = 0;


	protected:
		std::string m_path;
		GLuint m_texture_id{0};

		unsigned int m_width{0};
		unsigned int m_height{0};
	};


	Texture * CreateTexture(const char * path);
}
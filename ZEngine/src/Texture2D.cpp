#include <Rendering/Textures/Texture2D.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>


namespace ZEngine::Rendering::Textures {

	Texture* CreateTexture(const char* path) {
		return new Texture2D(path);
	}

	Texture* CreateTexture(unsigned int width, unsigned int height) {
		return new Texture2D(width, height);
	}
}


namespace  ZEngine::Rendering::Textures {

	Texture2D::Texture2D(const char * path) 
		:Texture(path)
	{
		int width = 0, height = 0, channel = 0;
		stbi_set_flip_vertically_on_load(1);
		stbi_uc* image_data =  stbi_load(path, &width, &height, &channel, 0);

		if(image_data != nullptr) {

			unsigned int data_format{0};
			unsigned int internal_format{0};

			internal_format = (channel == 3) ? GL_RGB8	: GL_RGBA8;
			data_format		= (channel == 3) ? GL_RGB	: GL_RGBA;

			glCreateTextures(GL_TEXTURE_2D, 1, &m_texture_id);
			glTextureStorage2D(m_texture_id, 1, internal_format, width, height);

			m_width		= width;
			m_height	= height;

			glTextureParameteri(m_texture_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTextureParameteri(m_texture_id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

			glTextureParameteri(m_texture_id, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTextureParameteri(m_texture_id, GL_TEXTURE_WRAP_T, GL_REPEAT);

			glTextureSubImage2D(m_texture_id, 0, 0, 0, width, height, data_format, GL_UNSIGNED_BYTE, (const void *)(image_data));

		}
		

		stbi_image_free(image_data);
	}

	Texture2D::Texture2D(unsigned int width, unsigned int height)
		:Texture(width, height)
	{
		m_internal_format	= GL_RGBA8;
		m_data_format		= GL_RGBA;

		glCreateTextures(GL_TEXTURE_2D, 1, &m_texture_id);
		glTextureStorage2D(m_texture_id, 1, m_internal_format, width, height);

		glTextureParameteri(m_texture_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(m_texture_id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		glTextureParameteri(m_texture_id, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(m_texture_id, GL_TEXTURE_WRAP_T, GL_REPEAT);

		const char * data = "\xff\xff\xff\xff";	   //R:255 G: 255 B: 255 A: 255
		SetData(data);
	}

	void Texture2D::SetData(const void * data) {
		glTextureSubImage2D(m_texture_id, 0, 0, 0, m_width, m_height, m_data_format, GL_UNSIGNED_BYTE, data);
	}

	void Texture2D::SetData(float r, float g, float b, float a) {
		unsigned char data[5] = {0, 0, 0, 0, '\0'};
		data[0] = static_cast<unsigned char>(std::min(r, 255.0f));
		data[1] = static_cast<unsigned char>(std::min(g, 255.0f));
		data[2] = static_cast<unsigned char>(std::min(b, 255.0f));
		data[3] = static_cast<unsigned char>(std::min(a, 255.0f));

		SetData(data);
	}

	Texture2D::~Texture2D() {
		  glDeleteTextures(1, &m_texture_id);
	}

	void Texture2D::Bind(int slot) const {
		glBindTextureUnit(slot, m_texture_id);
	}

	void Texture2D::Unbind(int slot) const {
		glBindTextureUnit(slot, 0);
	}

}


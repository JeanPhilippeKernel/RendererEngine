#pragma once
#include "Texture2D.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image/stb_image.h>


namespace Z_Engine::Rendering::Textures {
	Texture* CreateTexture(const char* path) {
		return new Texture2D(path);
	}

}


namespace  Z_Engine::Rendering::Textures {

	Texture2D::Texture2D(const char * path) 
		:Texture(path)
	{
		int width = 0, height = 0, channel = 0;
		stbi_set_flip_vertically_on_load(true);
		stbi_uc* image_data =  stbi_load(path, &width, &height, &channel, 0);

		if(image_data != nullptr) {

			unsigned int data_format{0};
			int internal_format{0};

			internal_format = (channel == 3) ? GL_RGB8	: GL_RGBA8;
			data_format		= (channel == 3) ? GL_RGB	: GL_RGBA;

			glCreateTextures(GL_TEXTURE_2D, 1, &m_texture_id);
			glTextureStorage2D(m_texture_id, 1, internal_format, width, height);

			m_width		= width;
			m_height	= height;


			glTextureParameteri(m_texture_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTextureParameteri(m_texture_id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

			glTextureSubImage2D(m_texture_id, 0, 0, 0, width, height, data_format, GL_UNSIGNED_BYTE, (const void *)(image_data));
		}

		stbi_image_free(image_data);
	}

	Texture2D::~Texture2D() {
		  glDeleteTextures(1, &m_texture_id);
	}

	void Texture2D::Bind(int slot) const
	{
		glBindTextureUnit(slot, m_texture_id);
	}

	void Texture2D::Unbind(int slot) const
	{
		glBindTextureUnit(slot, 0);
	}

}


#pragma once

#include <glad/include/glad/gl.h>

namespace ZEngine::Core {
	
	struct IGraphicObject {

		virtual GLuint GetIdentifier() const = 0;
	};
}
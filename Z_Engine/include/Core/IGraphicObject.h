#pragma once

#include "../dependencies/glew/include/GL/glew.h"

namespace Z_Engine::Core {
	
	struct IGraphicObject {

		virtual GLuint GetIdentifier() const = 0;
	};
}
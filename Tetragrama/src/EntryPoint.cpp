#include <pch.h>
#include "Editor.h"

#ifdef Z_ENGINE_PLATFORM

int main(int argc, char* argv[]) {
	
	std::unique_ptr<Tetragrama::Editor> editor(new Tetragrama::Editor());
	editor->Initialize();
	editor->Run();
	return 0;
}
#endif
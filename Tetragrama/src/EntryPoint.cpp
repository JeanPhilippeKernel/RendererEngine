#include <pch.h>
#include "Editor.h"

#ifdef ZENGINE_PLATFORM

int main(int argc, char* argv[]) {
	
	std::shared_ptr<Tetragrama::Editor> editor(new Tetragrama::Editor());
	editor->Initialize();
	editor->Run();
	return 0;
}
#endif
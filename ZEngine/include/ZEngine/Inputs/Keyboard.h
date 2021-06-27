#pragma once
#include <Inputs/IDevice.h>
#include <SDL2/include/SDL_keyboard.h>

namespace ZEngine::Inputs {

	class Keyboard : public IDevice {
	public:
		Keyboard(const char* name = "keyboard_device")
			: IDevice(name)
		{}
		~Keyboard() = default;

		virtual bool IsKeyPressed(KeyCode key) const override {
			auto state = SDL_GetKeyboardState(NULL);
			return *(state + (int)key) == 1;
		}
		virtual bool IsKeyReleased(KeyCode key) const override {
			auto state = SDL_GetKeyboardState(NULL);
			return *(state + (int)key) == 0;
		}
	};
}

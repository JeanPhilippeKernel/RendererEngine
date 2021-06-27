#pragma once
#include <array>
#include <Inputs/IDevice.h>
#include <SDL2/include/SDL_mouse.h>


namespace ZEngine::Inputs {

	class Mouse : public IDevice {
	public:
		Mouse(const char * name = "mouse_device") 
			: IDevice(name) 
		{
		}

		virtual bool IsKeyPressed(KeyCode key) const override {
			bool is_pressed{ false };
			const uint32_t	state = SDL_GetMouseState(NULL, NULL);
			unsigned int	mask = 0;

			switch (key)
			{
				case KeyCode::MOUSE_LEFT:
					mask = SDL_BUTTON_LMASK; 
					break;
				case KeyCode::MOUSE_WHEEL:
					mask = SDL_BUTTON_MMASK;
					break;
				case KeyCode::MOUSE_RIGHT:
					mask = SDL_BUTTON_RMASK;
					break;
				default:
					break;
			}

			is_pressed = (state & mask) >= 1 ? true : false;
			return is_pressed;
		}

		virtual bool IsKeyReleased(KeyCode key) const override {
			return !IsKeyPressed(key);
		}

		std::array<int, 2> GetMousePosition() const {
			int x, y;
			const auto state =  SDL_GetMouseState(&x, &y);
			return std::array<int, 2> {x,y};
		} 
	};
}


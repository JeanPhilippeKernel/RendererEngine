#pragma  once
#include <cstdint>

namespace Z_Engine::Inputs {
	
	typedef enum class KeyCode : std::int32_t {
		A = 0x004,
		B = 0x005,
		C = 0x006,
		D = 0x007,
		E = 0x008,
		F = 0x009,
		G = 0x00A,
		H = 0x00B,
		I = 0x00C,
		J = 0x00D,
		K = 0x00E,
		L = 0x00F,
		M = 0x010,
		N = 0x011,
		O = 0x012,
		P = 0x013,
		Q = 0x014,
		R = 0x015,
		S = 0x016,
		T = 0x017,
		U = 0x018,
		V = 0x019,
		W = 0x01A,
		X = 0x01B,
		Y = 0x01C,
		Z = 0x01D,


		UP = 0x052,
		DOWN = 0x051,
		LEFT = 0x050,
		RIGHT = 0x04F,

		MOUSE_LEFT = 1,
		MOUSE_WHEEL =  2,
		MOUSE_RIGHT = 3,


		//ToDo : add other keyboard key....

	} Key;

}

#define Z_ENGINE_KEY_A  Z_Engine::Inputs::Key::A
#define Z_ENGINE_KEY_B  Z_Engine::Inputs::Key::B
#define Z_ENGINE_KEY_C  Z_Engine::Inputs::Key::C
#define Z_ENGINE_KEY_D  Z_Engine::Inputs::Key::D
#define Z_ENGINE_KEY_E  Z_Engine::Inputs::Key::E
#define Z_ENGINE_KEY_F  Z_Engine::Inputs::Key::F
#define Z_ENGINE_KEY_G  Z_Engine::Inputs::Key::G
#define Z_ENGINE_KEY_H  Z_Engine::Inputs::Key::H
#define Z_ENGINE_KEY_I  Z_Engine::Inputs::Key::I
#define Z_ENGINE_KEY_J  Z_Engine::Inputs::Key::J
#define Z_ENGINE_KEY_K  Z_Engine::Inputs::Key::K
#define Z_ENGINE_KEY_L  Z_Engine::Inputs::Key::L
#define Z_ENGINE_KEY_M  Z_Engine::Inputs::Key::M
#define Z_ENGINE_KEY_N  Z_Engine::Inputs::Key::N
#define Z_ENGINE_KEY_O  Z_Engine::Inputs::Key::O
#define Z_ENGINE_KEY_P  Z_Engine::Inputs::Key::P
#define Z_ENGINE_KEY_Q  Z_Engine::Inputs::Key::Q
#define Z_ENGINE_KEY_R  Z_Engine::Inputs::Key::R
#define Z_ENGINE_KEY_S  Z_Engine::Inputs::Key::S
#define Z_ENGINE_KEY_T  Z_Engine::Inputs::Key::T
#define Z_ENGINE_KEY_U  Z_Engine::Inputs::Key::U
#define Z_ENGINE_KEY_V  Z_Engine::Inputs::Key::V
#define Z_ENGINE_KEY_W  Z_Engine::Inputs::Key::W
#define Z_ENGINE_KEY_X  Z_Engine::Inputs::Key::X
#define Z_ENGINE_KEY_Y  Z_Engine::Inputs::Key::Y
#define Z_ENGINE_KEY_Z  Z_Engine::Inputs::Key::Z


#define Z_ENGINE_KEY_UP		Z_Engine::Inputs::Key::UP
#define Z_ENGINE_KEY_DOWN	Z_Engine::Inputs::Key::DOWN
#define Z_ENGINE_KEY_LEFT	Z_Engine::Inputs::Key::LEFT
#define Z_ENGINE_KEY_RIGHT	Z_Engine::Inputs::Key::RIGHT

#define Z_ENGINE_KEY_MOUSE_LEFT		Z_Engine::Inputs::Key::MOUSE_LEFT
#define Z_ENGINE_KEY_MOUSE_RIGHT	Z_Engine::Inputs::Key::MOUSE_RIGHT
#define Z_ENGINE_KEY_MOUSE_WHEEL	Z_Engine::Inputs::Key::MOUSE_WHEEL
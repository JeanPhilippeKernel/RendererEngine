#pragma once
#include <cstdint>

namespace Z_Engine::Core::Input {

	typedef enum class KeyCode : std::int32_t 
	{
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


	   UP		= 0x052,
	   DOWN		= 0x051,
	   LEFT		= 0x050,
	   RIGHT	= 0x04F,

	   //ToDo : add other keyboard key....

	} Key;



#define Z_ENGINE_KEY_A  ::Z_Engine::Core::Key::A
#define Z_ENGINE_KEY_B  ::Z_Engine::Core::Key::B
#define Z_ENGINE_KEY_C  ::Z_Engine::Core::Key::C
#define Z_ENGINE_KEY_D  ::Z_Engine::Core::Key::D
#define Z_ENGINE_KEY_E  ::Z_Engine::Core::Key::E
#define Z_ENGINE_KEY_F  ::Z_Engine::Core::Key::F
#define Z_ENGINE_KEY_G  ::Z_Engine::Core::Key::G
#define Z_ENGINE_KEY_H  ::Z_Engine::Core::Key::H
#define Z_ENGINE_KEY_I  ::Z_Engine::Core::Key::I
#define Z_ENGINE_KEY_J  ::Z_Engine::Core::Key::J
#define Z_ENGINE_KEY_K  ::Z_Engine::Core::Key::K
#define Z_ENGINE_KEY_L  ::Z_Engine::Core::Key::L
#define Z_ENGINE_KEY_M  ::Z_Engine::Core::Key::M
#define Z_ENGINE_KEY_N  ::Z_Engine::Core::Key::N
#define Z_ENGINE_KEY_O  ::Z_Engine::Core::Key::O
#define Z_ENGINE_KEY_P  ::Z_Engine::Core::Key::P
#define Z_ENGINE_KEY_Q  ::Z_Engine::Core::Key::Q
#define Z_ENGINE_KEY_R  ::Z_Engine::Core::Key::R
#define Z_ENGINE_KEY_S  ::Z_Engine::Core::Key::S
#define Z_ENGINE_KEY_T  ::Z_Engine::Core::Key::T
#define Z_ENGINE_KEY_U  ::Z_Engine::Core::Key::U
#define Z_ENGINE_KEY_V  ::Z_Engine::Core::Key::V
#define Z_ENGINE_KEY_W  ::Z_Engine::Core::Key::W
#define Z_ENGINE_KEY_X  ::Z_Engine::Core::Key::X
#define Z_ENGINE_KEY_Y  ::Z_Engine::Core::Key::Y
#define Z_ENGINE_KEY_Z  ::Z_Engine::Core::Key::Z

}

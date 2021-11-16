#pragma once
#include <algorithm>
#include <cstdint>
#include <glad/include/glad/glad.h>
#include <imgui.h>
#include <memory>
#include <regex>
#include <SDL_timer.h>
#include <set>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <sstream>
#include <cstdint>

#ifdef ZENGINE_WINDOW_SDL
    #include <SDL2/include/SDL.h>
#else
    #include <GLFW/glfw3.h>
#endif
#pragma once

#include <ZEngine/Profiling/MemoryProfiler.h>
#include <ZEngine/Profiling/ProfilerBuffer.h>

#if ZENGINE_TRACY
#include <tracy/Tracy.hpp>
#endif

#if ZENGINE_PROFILING && ZENGINE_TRACY
#define ZENGINE_PROFILE_ALLOC(ptr, size) TracyAlloc(ptr, size)
#define ZENGINE_PROFILE_FREE(ptr)        TracyFree(ptr)
#elif ZENGINE_PROFILING
// Lightweight fallback: allocation tracking via MemoryProfiler.
#define ZENGINE_PROFILE_ALLOC(ptr, size) ZEngine::Profiling::MemoryProfiler::RecordAlloc(ptr, size)
#define ZENGINE_PROFILE_FREE(ptr)        ZEngine::Profiling::MemoryProfiler::RecordFree(ptr)
#else
#define ZENGINE_PROFILE_ALLOC(ptr, size)
#define ZENGINE_PROFILE_FREE(ptr)
#endif

#if ZENGINE_PROFILING && ZENGINE_TRACY
#define ZENGINE_PROFILE_VALUE(name, value) TracyPlot(name, static_cast<double>(value))
#elif ZENGINE_PROFILING
#define ZENGINE_PROFILE_VALUE(name, value) ZEngine::Profiling::ProfilerBuffer::RecordValue(name, static_cast<float>(value))
#else
#define ZENGINE_PROFILE_VALUE(name, value)
#endif

#define ZENGINE_CONCAT_IMPL(a, b) a##b
#define ZENGINE_CONCAT(a, b)      ZENGINE_CONCAT_IMPL(a, b)

#if ZENGINE_PROFILING && ZENGINE_TRACY
#define ZENGINE_PROFILE_SCOPE(name) ZoneScopedN(name)
#elif ZENGINE_PROFILING
#define ZENGINE_PROFILE_SCOPE(name) ZEngine::Profiling::ScopeGuard ZENGINE_CONCAT(_zscope_, __LINE__)(name)
#else
#define ZENGINE_PROFILE_SCOPE(name)
#endif

#if ZENGINE_PROFILING && ZENGINE_TRACY
#define ZENGINE_PROFILE_FUNCTION() ZoneScoped
#elif ZENGINE_PROFILING
#define ZENGINE_PROFILE_FUNCTION() ZEngine::Profiling::ScopeGuard ZENGINE_CONCAT(_zfn_, __LINE__)(__func__)
#else
#define ZENGINE_PROFILE_FUNCTION()
#endif

#if ZENGINE_PROFILING && ZENGINE_TRACY
#define ZENGINE_PROFILE_FRAME(name) FrameMarkNamed(name)
#elif ZENGINE_PROFILING
#define ZENGINE_PROFILE_FRAME(name) ZEngine::Profiling::ProfilerBuffer::BeginFrame()
#else
#define ZENGINE_PROFILE_FRAME(name)
#endif

#if ZENGINE_PROFILING && ZENGINE_TRACY
#define ZENGINE_PROFILE_THREAD(name) tracy::SetThreadName(name)
#elif ZENGINE_PROFILING
#define ZENGINE_PROFILE_THREAD(name)
#else
#define ZENGINE_PROFILE_THREAD(name)
#endif
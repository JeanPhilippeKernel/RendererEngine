#include <pch.h>
#include <Inputs/IDevice.h>

namespace ZEngine::Windows::Inputs
{
    std::map<const char*, ZRawPtr(IDevice)> IDevice::Devices = {};
    Core::Memory::ArenaAllocator*           IDevice::Arena   = nullptr;
} // namespace ZEngine::Windows::Inputs

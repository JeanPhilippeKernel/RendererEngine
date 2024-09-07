#include <pch.h>
#include <Inputs/IDevice.h>

namespace ZEngine::Inputs
{

    std::unordered_map<std::string, Ref<IDevice>> IDevice::m_devices;
}

#include <pch.h>
#include <Inputs/IDevice.h>

namespace ZEngine::Inputs {

    std::unordered_map<const char*, Ref<IDevice>> IDevice::m_devices;
}

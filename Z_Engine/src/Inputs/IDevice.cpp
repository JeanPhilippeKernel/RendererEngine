#include "IDevice.h"

namespace Z_Engine::Inputs {

		std::unordered_map<const char*, std::shared_ptr<IDevice>> IDevice::m_devices;
}




#pragma once
#include <string>
#include <type_traits>
#include <memory>
#include <unordered_map>
#include <algorithm>

#include <Inputs/KeyCode.h>
#include <Z_EngineDef.h>

#include <string.h>

namespace ZEngine::Inputs {

	 struct IDevice {
	 public:
		 virtual ~IDevice() =  default;

		
		 template<typename T, typename = std::enable_if_t<std::is_base_of_v<IDevice, T>>>
		 static const T* As() noexcept {
			 
			const auto& type = typeid(T);		   
			auto  it  =  std::find_if(std::begin(m_devices), std::end(m_devices), 
				[&type] (const std::pair<const char *, Ref<IDevice>>& item) {
					return strcmp(item.first,  type.name()) == 0;
				}
			);

			if(it !=  std::end(m_devices)) {
				return dynamic_cast<T*>(it->second.get());
			}

			Ref<IDevice> device_ptr;
			device_ptr.reset(new T());

			auto pair = m_devices.emplace(std::make_pair(type.name(), device_ptr));
			return dynamic_cast<T*>(pair.first->second.get());
		 }
	 
		 virtual bool IsKeyPressed(KeyCode key) const = 0;
		 virtual bool IsKeyReleased(KeyCode key) const = 0;

		 virtual const std::string& GetName() const { return m_name; }

	 protected:
		 IDevice(const char * name =  "abstract_device")
			 :m_name(name)
		 {}
		 static std::unordered_map<const char *, Ref<IDevice>> m_devices;
		 std::string m_name;
	 };
}

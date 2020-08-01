#pragma once
#include <typeinfo>
#include <string>

namespace Z_Engine::Core::Utility {
	
	unsigned int ToGraphicCardType(const std::string& type_name) {
		
		if(strcmp(typeid(float).name(), type_name.c_str()) == 0 ) {
			   return  0x1406;
		}

		else if (strcmp(typeid(int).name(), type_name.c_str()) == 0) {
			return  0x1404;
		}

		else if (strcmp(typeid(unsigned int).name(), type_name.c_str()) == 0) {
			return  0x1405;
		}

		throw std::exception("unrecognized type name");
	}
}

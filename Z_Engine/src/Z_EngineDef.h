#pragma once


#ifdef Z_ENGINE_PLATFORM_WINDOW
	#ifdef Z_ENGINE_BUILD_DLL
		#ifdef 	Z_ENGINE_BUILD
			#define Z_ENGINE_API __declspec(dllexport)		   
		#else
			#define Z_ENGINE_API __declspec(dllimport)
		#endif
	
	#else
		#define Z_ENGINE_API
	#endif // Z_ENGINE_BUILD_DLL

#else
	#error platform not supported
#endif //Z_ENGINE_PLATFORM_WINDOW



#define BIT(x) (1 << x)
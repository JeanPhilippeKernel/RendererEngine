#pragma once

namespace Z_Engine::Core {
	
	class TimeStep {
	public:
		TimeStep(float time = 0.0f) 
			: m_time(time)
		{}
		
		operator float() { return m_time; }

		float GetSecond() const { return m_time / 1000.0f; }
		float GetMillisecond() const { return m_time * 1000.0f; }	
	
	private:
		float m_time{0.0f};
	};
}

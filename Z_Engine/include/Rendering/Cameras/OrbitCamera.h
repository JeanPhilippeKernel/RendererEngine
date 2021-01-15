#pragma once
#include "PerspectiveCamera.h"


namespace Z_Engine::Rendering::Cameras {

	class OrbitCamera : public PerspectiveCamera {
	public:
		explicit OrbitCamera(float field_of_view, float aspect_ratio, float near, float far, float yaw, float pitch);
		~OrbitCamera() = default;

		void SetYawAngle(float value);
		void SetPitchAngle(float value);

		void SetRadius(float value);

		float GetYawAngle()		const { return glm::degrees(m_yaw_angle); }
		float GetPitchAngle()	const { return glm::degrees(m_pitch_angle); }
		float GetRadius()		const { return m_radius; }


		void SetTarget(const glm::vec3& target) override;

		void SetPosition(float yaw, float pitch);
		void SetPosition(const glm::vec3& position) override;

	protected:
		float m_radius;
		float m_yaw_angle;
		float m_pitch_angle;
	};

}

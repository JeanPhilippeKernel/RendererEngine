#pragma once
#include <Rendering/Cameras/PerspectiveCamera.h>


namespace ZEngine::Rendering::Cameras {

	class OrbitCamera : public PerspectiveCamera {
	public:
		explicit OrbitCamera(float field_of_view, float aspect_ratio, float near, float far, float yaw, float pitch);
		~OrbitCamera() = default;

		void SetYawAngle(float value);
		void SetPitchAngle(float value);

		void SetRadius(float value);

		float GetYawAngle()		const { return Maths::degrees(m_yaw_angle); }
		float GetPitchAngle()	const { return Maths::degrees(m_pitch_angle); }
		float GetRadius()		const { return m_radius; }

		void SetTarget(const Maths::Vector3& target) override;

		void SetPosition(float yaw, float pitch);
		void SetPosition(const Maths::Vector3& position) override;

	};

}

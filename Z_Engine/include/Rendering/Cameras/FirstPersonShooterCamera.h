#pragma once
#include "PerspectiveCamera.h"


namespace Z_Engine::Rendering::Cameras {

	class FirstPersonShooterCamera : public PerspectiveCamera {
	public:
		explicit FirstPersonShooterCamera(float field_of_view, float aspect_ratio, float near, float far, float yaw, float pitch);
		~FirstPersonShooterCamera() = default;

		float GetRadius() const { return m_radius; }

		void SetYawAngle(float value);
		void SetPitchAngle(float value);

		float GetYawAngle()		const { return Maths::degrees(m_yaw_angle); }
		float GetPitchAngle()	const { return Maths::degrees(m_pitch_angle); }


		void SetTarget(const Maths::Vector3& target) override;
		void SetPosition(const Maths::Vector3& position) override;

		void SetPosition(float yaw, float pitch);
		void Move(const Maths::Vector3& offset);

	protected:
		virtual void UpdateCoordinateVectors() override;

	};

}

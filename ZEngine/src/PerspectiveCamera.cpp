#include <pch.h>
#include <Rendering/Cameras/PerspectiveCamera.h>


namespace ZEngine::Rendering::Cameras {

	PerspectiveCamera::PerspectiveCamera(float field_of_view, float aspect_ratio, float near, float far) 
		:
		m_yaw_angle(0.0f),
		m_pitch_angle(0.0f),
		m_radius(0.0f)
	{
		m_projection = Maths::perspective(field_of_view, aspect_ratio, near, far);
		UpdateCoordinateVectors();
		UpdateViewMatrix();
	}

	void PerspectiveCamera::SetTarget(const Maths::Vector3& target) {
		Camera::SetTarget(target);
		UpdateCoordinateVectors();
		UpdateViewMatrix();
	}

	void PerspectiveCamera::SetFieldOfView(float rad_angle) {
		m_field_of_view = rad_angle;
		UpdateCoordinateVectors();
		UpdateViewMatrix();
	}

	void PerspectiveCamera::SetPosition(const Maths::Vector3& position) {
		Camera::SetPosition(position);
		UpdateCoordinateVectors();
		UpdateViewMatrix();
	}

	void PerspectiveCamera::SetProjectionMatrix(const Maths::Matrix4& projection) {
		Camera::SetProjectionMatrix(projection);
		UpdateCoordinateVectors();
		UpdateViewMatrix();
	}

	void PerspectiveCamera::UpdateCoordinateVectors() {
		m_forward			= Maths::normalize(m_position - m_target);
		m_right				= Maths::normalize(Maths::cross(m_world_up, m_forward));
		m_up				= Maths::cross(m_forward, m_right);
	}


	void PerspectiveCamera::UpdateViewMatrix() {
		
		m_view_matrix		= Maths::lookAt(m_position, m_target, m_up);
		m_view_projection	= m_projection * m_view_matrix;
	}
}
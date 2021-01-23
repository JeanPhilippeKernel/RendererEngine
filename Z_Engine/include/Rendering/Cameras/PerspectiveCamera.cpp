#include "PerspectiveCamera.h"


namespace Z_Engine::Rendering::Cameras {

	PerspectiveCamera::PerspectiveCamera(float field_of_view, float aspect_ratio, float near, float far) 
	{
		m_projection = glm::perspective(field_of_view, aspect_ratio, near, far);
		UpdateCoordinateVectors();
		UpdateViewMatrix();
	}

	void PerspectiveCamera::SetTarget(const glm::vec3& target) {
		Camera::SetTarget(target);
		UpdateCoordinateVectors();
		UpdateViewMatrix();
	}

	void PerspectiveCamera::SetFieldOfView(float rad_angle) {
		m_field_of_view = rad_angle;
		UpdateCoordinateVectors();
		UpdateViewMatrix();
	}

	void PerspectiveCamera::SetPosition(const glm::vec3& position) {
		Camera::SetPosition(position);
		UpdateCoordinateVectors();
		UpdateViewMatrix();
	}

	void PerspectiveCamera::SetProjectionMatrix(const glm::mat4& projection) {
		Camera::SetProjectionMatrix(projection);
		UpdateCoordinateVectors();
		UpdateViewMatrix();
	}

	void PerspectiveCamera::UpdateCoordinateVectors() {
		m_forward			= glm::normalize(m_position - m_target);
		m_right				= glm::normalize(glm::cross(m_world_up, m_forward));
		m_up				= glm::cross(m_forward, m_right);
	}


	void PerspectiveCamera::UpdateViewMatrix() {
		m_view_matrix		= glm::lookAt(m_position, m_target, m_up);
		m_view_projection	= m_projection * m_view_matrix;
	}
}
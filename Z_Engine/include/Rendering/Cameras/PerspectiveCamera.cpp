#include "PerspectiveCamera.h"


namespace Z_Engine::Rendering::Cameras {

	PerspectiveCamera::PerspectiveCamera(float field_of_view, float aspect_ratio, float near, float far) 
		: m_up({0.0f, 1.0f, 0.0f})
	{
		m_projection = glm::perspective(field_of_view, aspect_ratio, near, far);
		UpdateViewMatrix();
	}

	void PerspectiveCamera::SetTarget(const glm::vec3& target) {
		m_target = target;
		UpdateViewMatrix();
	}

	void PerspectiveCamera::SetFieldOfView(float rad_angle) {
		m_field_of_view = rad_angle;
		UpdateViewMatrix();
	}

	void PerspectiveCamera::SetPosition(const glm::vec3& position) {
		Camera::SetPosition(position);
		UpdateViewMatrix();
	}

	void PerspectiveCamera::SetProjectionMatrix(const glm::mat4& projection) {
		Camera::SetProjectionMatrix(projection);
		UpdateViewMatrix();
	}

	void PerspectiveCamera::UpdateViewMatrix() {

		const auto forward	= glm::normalize(m_position - m_target);
		const auto right	= glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward));
		m_up				= glm::cross(forward, right);
		
		m_view_matrix		= glm::lookAt(m_position, m_target, m_up);
		m_view_projection	= m_projection * m_view_matrix;
	}
}
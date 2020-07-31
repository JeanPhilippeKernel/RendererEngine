#include "OrthographicCamera.h"


namespace Z_Engine::Rendering::Cameras {
	
	OrthographicCamera::OrthographicCamera(float left, float right, float bottom, float top) {
		m_projection = glm::ortho(left, right, bottom, top, -1.0f, 1.0f);
		UpdateViewMatrix();
	}

	OrthographicCamera::OrthographicCamera(float left, float right, float bottom, float top, float degree_angle)
	{
		m_projection = glm::ortho(left, right, bottom, top, -1.0f, 1.0f);
		m_angle = glm::radians(degree_angle);
		UpdateViewMatrix();
	}

	void OrthographicCamera::SetRotation(float radian_angle) {
		m_angle = radian_angle;
		UpdateViewMatrix();
	}


	void OrthographicCamera::SetPosition(const glm::vec3 position) {
		Camera::SetPosition(position);
		UpdateViewMatrix();
	}


	void OrthographicCamera::UpdateViewMatrix() {
		const auto transform =
			glm::translate(glm::mat4(1.0f), m_position) *
			glm::rotate(glm::mat4(1.0f), m_angle, glm::vec3(0, 0, 1));

		m_view_matrix = (m_position.z > 0) ? glm::inverse(transform) : transform;

		m_view_projection = m_projection * m_view_matrix;
	}
}
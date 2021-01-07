#pragma once
#include "Camera.h"
#include "../../dependencies/glm/gtc/matrix_transform.hpp"


namespace Z_Engine::Rendering::Cameras {
	
	class PerspectiveCamera : public Camera {
	public:
		PerspectiveCamera(float field_of_view, float aspect_ratio, float near, float far);
		~PerspectiveCamera() = default;

		void SetTarget(const glm::vec3& target);
		void SetFieldOfView(float rad_angle);

		void SetPosition(const glm::vec3& position) override;
		void SetProjectionMatrix(const glm::mat4& projection) override;

	private:
		float m_field_of_view{ 0.0f };

		glm::vec3 m_target;
		glm::vec3 m_up;

		void UpdateViewMatrix();
	};
}

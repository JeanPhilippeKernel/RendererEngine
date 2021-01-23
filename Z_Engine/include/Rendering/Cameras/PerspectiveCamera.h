#pragma once
#include "Camera.h"
#include "../../dependencies/glm/gtc/matrix_transform.hpp"


namespace Z_Engine::Rendering::Cameras {
	
	class PerspectiveCamera : public Camera {
	public:
		PerspectiveCamera(float field_of_view, float aspect_ratio, float near, float far);
		virtual ~PerspectiveCamera() = default;

		virtual void SetTarget(const glm::vec3& target)					override;
		virtual void SetFieldOfView(float rad_angle);

		virtual void SetPosition(const glm::vec3& position)				override;
		virtual void SetProjectionMatrix(const glm::mat4& projection)	override;

	protected:
		virtual void UpdateCoordinateVectors();
		virtual void UpdateViewMatrix()									override;

	protected:
		float m_field_of_view{ 0.0f };
	};
}

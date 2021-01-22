#pragma once
#include "../../dependencies/glm/glm.hpp"
#include "../../Z_EngineDef.h"

namespace Z_Engine::Rendering::Cameras
{
	class Camera
	{
	public:
		Camera()
			:
			m_position({ 0.0f, 0.0f, 0.5f }),
			m_projection(glm::mat4(1.0f)),
			m_view_matrix(glm::mat4(1.0f)),
			m_view_projection(glm::mat4(1.0f))
		{
		}

		virtual ~Camera() =  default;

		virtual const glm::vec3& GetPosition() const { return m_position;}
		virtual void SetPosition(const glm::vec3& position) { m_position =  position; }
		virtual void SetProjectionMatrix(const glm::mat4& projection) { m_projection =  projection; }

		
		virtual const glm::mat4& GetViewMatrix() const  { return  m_view_matrix; }
		virtual const glm::mat4& GetProjectionMatrix() const  { return  m_projection; }
		virtual const glm::mat4& GetViewProjectionMatrix() const { return  m_view_projection; }


		virtual const glm::vec3& GetUp() const { return m_up; }
		virtual const glm::vec3& GetForward() const { return m_forward; }
		virtual const glm::vec3& GetRight() const { return m_right; }
		
		virtual void SetTarget(const glm::vec3& target) { m_target = target; }
		virtual const glm::vec3& GetTarget() const { return m_target; }

 									
	protected:
		virtual void UpdateViewMatrix() = 0;

	protected:
		glm::vec3 m_position;
		glm::mat4 m_view_matrix;
		glm::mat4 m_projection;
		glm::mat4 m_view_projection;


		glm::vec3 m_target;
		glm::vec3 m_up;
		glm::vec3 m_forward;
		glm::vec3 m_right;

		const glm::vec3 m_world_up{0.0f, 1.0f, 0.0f };
	};
}

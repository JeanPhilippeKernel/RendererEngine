#pragma once
#include "../../dependencies/glm/glm.hpp"
#include "../../Z_EngineDef.h"

namespace Z_Engine::Rendering::Cameras
{
	class Camera
	{
	public:
		Camera()
			: m_position({0.0f, 0.0f, 0.5f})
		{
		}
		virtual ~Camera() =  default;

		virtual const glm::vec3& GetPosition() const { return m_position;}
		virtual void SetPosition(const glm::vec3 position) { m_position =  position; }
		virtual void SetProjectionMatrix(const glm::mat4 projection) { m_projection =  projection; }

		
		virtual const glm::mat4& GetViewMatrix() const  { return  m_view_matrix; }
		virtual const glm::mat4& GetProjectionMatrix() const  { return  m_projection; }
		virtual const glm::mat4& GetViewProjectionMatrix() const { return  m_view_projection; }

 									    
	protected:
		glm::vec3 m_position;
		glm::mat4 m_view_matrix;
		glm::mat4 m_projection;
		glm::mat4 m_view_projection;
	};
}

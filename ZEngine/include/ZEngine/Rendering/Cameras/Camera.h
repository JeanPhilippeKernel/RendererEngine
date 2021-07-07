#pragma once
#include <Maths/Math.h>
#include <ZEngineDef.h>

namespace ZEngine::Rendering::Cameras
{
	class Camera
	{
	public:
		Camera()
			:
			m_position({ 0.0f, 0.0f, 0.5f }),
			m_projection(Maths::identity<Maths::Matrix4>()),
			m_view_matrix(Maths::identity<Maths::Matrix4>()),
			m_view_projection(Maths::identity<Maths::Matrix4>())
		{
		}

		virtual ~Camera() =  default;

		virtual const Maths::Vector3& GetPosition() const { return m_position; }
		virtual void SetPosition(const Maths::Vector3& position) { m_position =  position; }
		virtual void SetProjectionMatrix(const Maths::Matrix4& projection) { m_projection = projection; }

		
		virtual const Maths::Matrix4& GetViewMatrix() const { return  m_view_matrix; }
		virtual const Maths::Matrix4& GetProjectionMatrix() const { return  m_projection; }
		virtual const Maths::Matrix4& GetViewProjectionMatrix() const { return  m_view_projection; }


		virtual const Maths::Vector3& GetUp() const { return m_up; }
		virtual const Maths::Vector3& GetForward() const { return m_forward; }
		virtual const Maths::Vector3& GetRight() const { return m_right; }
		
		virtual void SetTarget(const Maths::Vector3& target) { m_target = target; }
		virtual const Maths::Vector3& GetTarget() const { return m_target; }

 									
	protected:
		virtual void UpdateViewMatrix() = 0;

	protected:
		Maths::Vector3 m_position;
		Maths::Vector3 m_target;
		Maths::Vector3 m_up;
		Maths::Vector3 m_forward;
		Maths::Vector3 m_right;

		const Maths::Vector3 m_world_up{ 0.0f, 1.0f, 0.0f };
		
		Maths::Matrix4 m_view_matrix;
		Maths::Matrix4 m_projection;
		Maths::Matrix4 m_view_projection;
	};
}

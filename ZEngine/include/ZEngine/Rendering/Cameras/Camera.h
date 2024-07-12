#pragma once
#include <Helpers/IntrusivePtr.h>
#include <Rendering/Cameras/CameraEnum.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace ZEngine::Rendering::Cameras
{
    struct Camera : public Helpers::RefCounted
    {
        float Fov         = 0.0f;
        float AspectRatio = 0.0f;
        float ClipNear    = 0.1f;
        float ClipFar     = 1000.0f;
        /*
         * Coordinate Vectors
         */
        glm::vec3       Position = {0.0f, 0.0f, 0.0f};
        glm::vec3       Up       = {0.0f, 1.0f, 0.0f};
        const glm::vec3 WorldUp  = {0.0f, 1.0f, 0.0f};
        glm::vec3       Right    = {0.0f, 0.0f, 0.0f};
        glm::vec3       Forward  = {0.0f, 0.0f, 0.0f};
        glm::vec3       Target   = {0.0f, 0.0f, -1.0f};
        /*
         * Matrices
         */
        glm::mat4 View           = glm::identity<glm::mat4>();
        glm::mat4 Projection     = glm::identity<glm::mat4>();
        glm::mat4 ViewProjection = glm::identity<glm::mat4>();
        /*
         * Extras data
         */
        CameraType Type = CameraType::UNDEFINED;

        virtual glm::mat4 GetViewMatrix()              = 0;
        virtual glm::mat4 GetPerspectiveMatrix() const = 0;
        virtual glm::vec3 GetPosition() const          = 0;
    };
} // namespace ZEngine::Rendering::Cameras

//#pragma once
//#include <Rendering/Meshes/Mesh.h>
//#include <Rendering/Lights/Light.h>
//#include <Rendering/Textures/Texture2D.h>
//#include <ZEngineDef.h>
//
//namespace ZEngine::Rendering::Meshes {
//
//    struct MeshBuilder {
//
//        MeshBuilder()                   = delete;
//        MeshBuilder(const MeshBuilder&) = delete;
//
//        static Mesh* CreateQuad(const Maths::Vector2& position, const Maths::Vector2& size, float angle);
//        static Mesh* CreateQuad(const Maths::Vector2& position, const Maths::Vector2& size, const Maths::Vector3& color, float angle);
//        static Mesh* CreateQuad(const Maths::Vector2& position, const Maths::Vector2& size, const Maths::Vector4& color, float angle);
//        static Mesh* CreateQuad(const Maths::Vector2& position, const Maths::Vector2& size, float angle, Rendering::Textures::Texture* const texture);
//        static Mesh* CreateQuad(const Maths::Vector2& position, const Maths::Vector2& size, float angle, Rendering::Textures::Texture2D* const texture);
//        static Mesh* CreateQuad(const Maths::Vector2& position, const Maths::Vector2& size, float angle, const Ref<Rendering::Textures::Texture>& texture);
//        static Mesh* CreateQuad(const Maths::Vector2& position, const Maths::Vector2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture);
//        static Mesh* CreateQuad(const Maths::Vector2& position, const Maths::Vector2& size, float angle, Rendering::Materials::ShaderMaterial* const material);
//        static Mesh* CreateQuad(const Maths::Vector2& position, const Maths::Vector2& size, float angle, const Ref<Rendering::Materials::ShaderMaterial>& material);
//
//        static Mesh* CreateQuad(const Maths::Vector3& position, const Maths::Vector2& size, float angle);
//        static Mesh* CreateQuad(const Maths::Vector3& position, const Maths::Vector2& size, const Maths::Vector3& color, float angle);
//        static Mesh* CreateQuad(const Maths::Vector3& position, const Maths::Vector2& size, const Maths::Vector4& color, float angle);
//        static Mesh* CreateQuad(const Maths::Vector3& position, const Maths::Vector2& size, float angle, Rendering::Textures::Texture* const texture);
//        static Mesh* CreateQuad(const Maths::Vector3& position, const Maths::Vector2& size, float angle, Rendering::Textures::Texture2D* const texture);
//        static Mesh* CreateQuad(const Maths::Vector3& position, const Maths::Vector2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture);
//        static Mesh* CreateQuad(const Maths::Vector3& position, const Maths::Vector2& size, float angle, const Ref<Rendering::Textures::Texture>& texture);
//        static Mesh* CreateQuad(const Maths::Vector3& position, const Maths::Vector2& size, float angle, Rendering::Materials::ShaderMaterial* const material);
//        static Mesh* CreateQuad(const Maths::Vector3& position, const Maths::Vector2& size, float angle, const Ref<Rendering::Materials::ShaderMaterial>& material);
//
//        static Mesh* CreateSquare(const Maths::Vector2& position, const Maths::Vector2& size, float angle);
//        static Mesh* CreateSquare(const Maths::Vector2& position, const Maths::Vector2& size, const Maths::Vector3& color, float angle);
//        static Mesh* CreateSquare(const Maths::Vector2& position, const Maths::Vector2& size, const Maths::Vector4& color, float angle);
//        static Mesh* CreateSquare(const Maths::Vector2& position, const Maths::Vector2& size, float angle, Rendering::Textures::Texture* const texture);
//        static Mesh* CreateSquare(const Maths::Vector2& position, const Maths::Vector2& size, float angle, Rendering::Textures::Texture2D* const texture);
//        static Mesh* CreateSquare(const Maths::Vector2& position, const Maths::Vector2& size, float angle, const Ref<Rendering::Textures::Texture>& texture);
//        static Mesh* CreateSquare(const Maths::Vector2& position, const Maths::Vector2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture);
//        static Mesh* CreateSquare(const Maths::Vector2& position, const Maths::Vector2& size, float angle, Rendering::Materials::ShaderMaterial* const material);
//        static Mesh* CreateSquare(const Maths::Vector2& position, const Maths::Vector2& size, float angle, const Ref<Rendering::Materials::ShaderMaterial>& material);
//
//        static Mesh* CreateSquare(const Maths::Vector3& position, const Maths::Vector2& size, float angle);
//        static Mesh* CreateSquare(const Maths::Vector3& position, const Maths::Vector2& size, const Maths::Vector3& color, float angle);
//        static Mesh* CreateSquare(const Maths::Vector3& position, const Maths::Vector2& size, const Maths::Vector4& color, float angle);
//        static Mesh* CreateSquare(const Maths::Vector3& position, const Maths::Vector2& size, float angle, Rendering::Textures::Texture* const texture);
//        static Mesh* CreateSquare(const Maths::Vector3& position, const Maths::Vector2& size, float angle, Rendering::Textures::Texture2D* const texture);
//        static Mesh* CreateSquare(const Maths::Vector3& position, const Maths::Vector2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture);
//        static Mesh* CreateSquare(const Maths::Vector3& position, const Maths::Vector2& size, float angle, const Ref<Rendering::Textures::Texture>& texture);
//        static Mesh* CreateSquare(const Maths::Vector3& position, const Maths::Vector2& size, float angle, Rendering::Materials::ShaderMaterial* const material);
//        static Mesh* CreateSquare(const Maths::Vector3& position, const Maths::Vector2& size, float angle, const Ref<Rendering::Materials::ShaderMaterial>& material);
//
//        static Mesh* CreateCube(const Maths::Vector2& position, const Maths::Vector3& size, float angle, const Maths::Vector3& axis);
//        static Mesh* CreateCube(const Maths::Vector2& position, const Maths::Vector3& size, const Maths::Vector3& color, float angle, const Maths::Vector3& axis);
//        static Mesh* CreateCube(const Maths::Vector2& position, const Maths::Vector3& size, const Maths::Vector4& color, float angle, const Maths::Vector3& axis);
//        static Mesh* CreateCube(const Maths::Vector2& position, const Maths::Vector3& size, float angle, const Maths::Vector3& axis, Rendering::Textures::Texture* const texture);
//        static Mesh* CreateCube(const Maths::Vector2& position, const Maths::Vector3& size, float angle, const Maths::Vector3& axis, Rendering::Textures::Texture2D* const texture);
//        static Mesh* CreateCube(
//            const Maths::Vector2& position, const Maths::Vector3& size, float angle, const Maths::Vector3& axis, const Ref<Rendering::Textures::Texture>& texture);
//        static Mesh* CreateCube(
//            const Maths::Vector2& position, const Maths::Vector3& size, float angle, const Maths::Vector3& axis, const Ref<Rendering::Textures::Texture2D>& texture);
//        static Mesh* CreateCube(
//            const Maths::Vector2& position, const Maths::Vector3& size, float angle, const Maths::Vector3& axis, Rendering::Materials::ShaderMaterial* const material);
//        static Mesh* CreateCube(
//            const Maths::Vector2& position, const Maths::Vector3& size, float angle, const Maths::Vector3& axis, const Ref<Rendering::Materials::ShaderMaterial>& material);
//
//        static Mesh* CreateCube(const Maths::Vector3& position, const Maths::Vector3& size, float angle, const Maths::Vector3& axis);
//        static Mesh* CreateCube(const Maths::Vector3& position, const Maths::Vector3& size, const Maths::Vector3& color, float angle, const Maths::Vector3& axis);
//        static Mesh* CreateCube(const Maths::Vector3& position, const Maths::Vector3& size, const Maths::Vector4& color, float angle, const Maths::Vector3& axis);
//        static Mesh* CreateCube(const Maths::Vector3& position, const Maths::Vector3& size, float angle, const Maths::Vector3& axis, Rendering::Textures::Texture* const texture);
//        static Mesh* CreateCube(const Maths::Vector3& position, const Maths::Vector3& size, float angle, const Maths::Vector3& axis, Rendering::Textures::Texture2D* const texture);
//        static Mesh* CreateCube(
//            const Maths::Vector3& position, const Maths::Vector3& size, float angle, const Maths::Vector3& axis, const Ref<Rendering::Textures::Texture2D>& texture);
//        static Mesh* CreateCube(
//            const Maths::Vector3& position, const Maths::Vector3& size, float angle, const Maths::Vector3& axis, const Ref<Rendering::Textures::Texture>& texture);
//        static Mesh* CreateCube(
//            const Maths::Vector3& position, const Maths::Vector3& size, float angle, const Maths::Vector3& axis, Rendering::Materials::ShaderMaterial* const material);
//        static Mesh* CreateCube(
//            const Maths::Vector3& position, const Maths::Vector3& size, float angle, const Maths::Vector3& axis, const Ref<Rendering::Materials::ShaderMaterial>& material);
//    };
//} // namespace ZEngine::Rendering::Meshes

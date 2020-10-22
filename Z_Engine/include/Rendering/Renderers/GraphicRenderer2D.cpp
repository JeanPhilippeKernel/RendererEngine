#pragma once
#include "GraphicRenderer2D.h"
#include "../Buffers/BufferLayout.h"
#include "../../Managers/ShaderManager.h"
#include "../../Managers/TextureManager.h"

#include "../Materials/SimpleMaterial2D.h"
#include "../Materials/StandardMaterial.h"


#include "../Geometries/SquareGeometry.h"
#include "../Geometries/QuadGeometry.h"

#include <memory>

using namespace Z_Engine::Rendering::Meshes;

using namespace Z_Engine::Rendering::Buffers::Layout;
using namespace Z_Engine::Managers;

namespace Z_Engine::Rendering::Renderers {


	void GraphicRenderer2D::Initialize() {
		GraphicRenderer::Initialize();

		m_graphic_storage->SetVertexBufferLayout(
			{
				ElementLayout<float>(1,	"mesh_index"),
				ElementLayout<float>(3,	"position"),
				ElementLayout<float>(4,	"color"),

				ElementLayout<float>(1,	"texture_id"),
				ElementLayout<float>(2,	"texture_coord")
			});
	}



	void GraphicRenderer2D::BeginScene(const Ref<Cameras::Camera>& camera) {
		GraphicRenderer::BeginScene(camera);
		m_graphic_storage->StartBacth();
	}

	void GraphicRenderer2D::EndScene() {
		m_graphic_storage->EndBatch();
	}

	void GraphicRenderer2D::DrawRect(Meshes::Mesh& mesh, const glm::vec3& color) {
		
		Ref<Textures::Texture> texture(Textures::CreateTexture(1, 1));
		texture->SetData(color.x, color.y, color.z, 255.f);
		mesh.GetMaterial()->SetTexture(texture);

		m_graphic_storage->AddMesh(mesh);
	}



	void GraphicRenderer2D::DrawRect(Meshes::Mesh& mesh, const glm::vec2& position, const glm::vec2& size, const glm::vec3& color, float angle) {
		glm::mat4 transform =
			glm::translate(glm::mat4(1.0f), { position.x, position.y, 0.0f }) *
			glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f)) *
			glm::scale(glm::mat4(1.0f), { size.x, size.y, 0.0f });

		Ref<Textures::Texture> texture(Textures::CreateTexture(1, 1));
		texture->SetData(color.x, color.y, color.z, 255.f);
		mesh.GetMaterial()->SetTexture(texture);
		mesh.GetGeometry()->ApplyTransform(transform);

		m_graphic_storage->AddMesh(mesh);
	}



	void GraphicRenderer2D::DrawRect(const glm::vec2& position, const glm::vec2& size, const glm::vec3& color, float angle) {
		DrawRect({position.x, position.y, 0.0f}, size, color, angle);
	}

	void GraphicRenderer2D::DrawRect(const glm::vec3& position, const glm::vec2& size, const glm::vec3& color, float angle) {
		_DrawRect({position.x, position.y, position.z, 0.0f}, size, {color.r, color.g, color.b, 255}, angle);
	}

	void GraphicRenderer2D::DrawRect(const glm::vec2& position, const glm::vec2& size, float angle, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture>& texture) {
		auto texture_2d = std::dynamic_pointer_cast<Z_Engine::Rendering::Textures::Texture2D>(texture);
		assert(texture_2d != nullptr);
		DrawRect(position, size, angle, texture_2d);
	}

	void GraphicRenderer2D::DrawRect(const glm::vec3& position, const glm::vec2& size, float angle, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture>& texture) {
		auto texture_2d = std::dynamic_pointer_cast<Z_Engine::Rendering::Textures::Texture2D>(texture);
		assert(texture_2d != nullptr);
		DrawRect(position, size, angle, texture_2d);
	}

	void GraphicRenderer2D::DrawRect(const glm::vec2& position, const glm::vec2& size, float angle, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture2D>& texture) {
		DrawRect({position.x, position.y, 0.0f}, size, angle, texture);
	}

	void GraphicRenderer2D::DrawRect(const glm::vec3& position, const glm::vec2& size, float angle, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture2D>& texture) {
		_DrawRect({position.x, position.y, position.z, 0.0f}, size, angle, texture);
	}


	void GraphicRenderer2D::DrawRect(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Z_Engine::Rendering::Textures::Texture>& texture, const glm::vec4& tint_color, float tiling_factor) {
		auto texture_2d = std::dynamic_pointer_cast<Z_Engine::Rendering::Textures::Texture2D>(texture);
		assert(texture_2d != nullptr);
		DrawRect(position, size, angle, texture_2d, tint_color, tiling_factor);
	}
	
	void GraphicRenderer2D::DrawRect(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Z_Engine::Rendering::Textures::Texture>& texture, const glm::vec4& tint_color, float tiling_factor) {
		auto texture_2d = std::dynamic_pointer_cast<Z_Engine::Rendering::Textures::Texture2D>(texture);
		assert(texture_2d != nullptr);
		DrawRect(position, size, angle, texture_2d, tint_color, tiling_factor);
	}
	
	void GraphicRenderer2D::DrawRect(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Z_Engine::Rendering::Textures::Texture2D>& texture, const glm::vec4& tint_color, float tiling_factor){
		DrawRect({position.x, position.y, 0.0f}, size, angle, texture, tint_color, tiling_factor);
	}
	
	void GraphicRenderer2D::DrawRect(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Z_Engine::Rendering::Textures::Texture2D>& texture, const glm::vec4& tint_color, float tiling_factor) {
		_DrawRect({position.x, position.y, position.z, 0.0f}, size, angle, texture, tint_color, tiling_factor);
	}



	void GraphicRenderer2D::DrawTriangle(const glm::vec2& position, const glm::vec2& size, const glm::vec3& color, float angle) {
		DrawTriangle({position.x, position.y, 0.0f}, size, color, angle);
	}

	void GraphicRenderer2D::DrawTriangle(const glm::vec3& position, const glm::vec2& size, const glm::vec3& color, float angle) {
		_DrawTriangle({position.x, position.y, position.z, 0.0f}, size, {color.r, color.g, color.b, 255.0f}, angle);
	}

	void GraphicRenderer2D::DrawTriangle(const glm::vec2& position, const glm::vec2& size, float angle, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture>& texture) {
		auto texture_2d = std::dynamic_pointer_cast<Z_Engine::Rendering::Textures::Texture2D>(texture);
		assert(texture_2d != nullptr);
		DrawTriangle(position, size, angle, texture_2d);
	}

	void GraphicRenderer2D::DrawTriangle(const glm::vec3& position, const glm::vec2& size, float angle, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture>& texture) {
		auto texture_2d = std::dynamic_pointer_cast<Z_Engine::Rendering::Textures::Texture2D>(texture);
		assert(texture_2d != nullptr);
		DrawTriangle(position, size, angle, texture_2d);
	}

	void GraphicRenderer2D::DrawTriangle(const glm::vec2& position, const glm::vec2& size, float angle, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture2D>& texture) {
		DrawTriangle({ position.x, position.y, 0.0f }, size, angle, texture);
	}

	void GraphicRenderer2D::DrawTriangle(const glm::vec3& position, const glm::vec2& size, float angle, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture2D>& texture) {
		_DrawTriangle({ position.x, position.y, position.z, 0.0f }, size, angle, texture);
	}



	void GraphicRenderer2D::DrawTriangle(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Z_Engine::Rendering::Textures::Texture>& texture, const glm::vec4& tint_color, float tiling_factor) {
		auto texture_2d = std::dynamic_pointer_cast<Z_Engine::Rendering::Textures::Texture2D>(texture);
		assert(texture_2d != nullptr);
		DrawTriangle(position, size, angle, texture_2d, tint_color, tiling_factor);
	}

	void GraphicRenderer2D::DrawTriangle(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Z_Engine::Rendering::Textures::Texture>& texture, const glm::vec4& tint_color, float tiling_factor) {
		auto texture_2d = std::dynamic_pointer_cast<Z_Engine::Rendering::Textures::Texture2D>(texture);
		assert(texture_2d != nullptr);
		DrawTriangle(position, size, angle, texture_2d, tint_color, tiling_factor);
	}

	void GraphicRenderer2D::DrawTriangle(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Z_Engine::Rendering::Textures::Texture2D>& texture, const glm::vec4& tint_color, float tiling_factor) {
		DrawTriangle({ position.x, position.y, 0.0f }, size, angle, texture, tint_color, tiling_factor);
	}

	void GraphicRenderer2D::DrawTriangle(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Z_Engine::Rendering::Textures::Texture2D>& texture, const glm::vec4& tint_color, float tiling_factor) {
		_DrawTriangle({ position.x, position.y, position.z, 0.0f }, size, angle, texture, tint_color, tiling_factor);
	}



	void GraphicRenderer2D::_DrawRect(const glm::vec4& position, const glm::vec2& size, const glm::vec4& color, float angle) {
		  
		glm::mat4 transform =
			glm::translate(glm::mat4(1.0f), { position.x, position.y, position.z }) *
			glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f)) *
			glm::scale(glm::mat4(1.0f), { size.x, size.y, 0.0f });

		Ref<Textures::Texture> texture(Textures::CreateTexture(1, 1));
		texture->SetData(color.r, color.g, color.b, color.a);
		
		Ref<Geometries::QuadGeometry> quad_geometry(new Geometries::QuadGeometry());
		Ref<Materials::StandardMaterial> simple_material(new Materials::StandardMaterial()); 

		quad_geometry->ApplyTransform(transform);
		simple_material->SetTexture(texture);

		Mesh quad_mesh{quad_geometry, simple_material};
		m_graphic_storage->AddMesh(quad_mesh);
	}

	void GraphicRenderer2D::_DrawRect(const glm::vec4& position, const glm::vec2& size, float angle, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture2D>& texture) {
		glm::mat4 transform =
			glm::translate(glm::mat4(1.0f), { position.x, position.y, position.z }) *
			glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f)) *
			glm::scale(glm::mat4(1.0f), { size.x, size.y, 0.0f });

		Ref<Geometries::QuadGeometry> quad_geometry(new Geometries::QuadGeometry());
		Ref<Materials::StandardMaterial> simple_material(new Materials::StandardMaterial());

		quad_geometry->ApplyTransform(transform);
		simple_material->SetTexture(texture);

		Mesh quad_mesh{ quad_geometry, simple_material };
		m_graphic_storage->AddMesh(quad_mesh);

	}

	void GraphicRenderer2D::_DrawRect(const glm::vec4& position, const glm::vec2& size, float angle, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture2D>& texture, const glm::vec4& tint_color, float tiling_factor) {
		glm::mat4 transform =
			glm::translate(glm::mat4(1.0f), { position.x, position.y, position.z }) *
			glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f)) *
			glm::scale(glm::mat4(1.0f), { size.x, size.y, 0.0f });

		Ref<Geometries::QuadGeometry> quad_geometry(new Geometries::QuadGeometry());
		Ref<Materials::StandardMaterial> simple_material(new Materials::StandardMaterial());

		quad_geometry->ApplyTransform(transform);
		simple_material->SetTexture(texture);
		//simple_material->SetTextureTilingFactor(tiling_factor);
		//simple_material->SetTextureTintColor(tint_color);

		Mesh quad_mesh{ quad_geometry, simple_material };
		m_graphic_storage->AddMesh(quad_mesh);

	}

	void GraphicRenderer2D::_DrawTriangle(const glm::vec4& position, const glm::vec2& size, const glm::vec4& color, float angle) {

		glm::mat4 transform =
			glm::translate(glm::mat4(1.0f), { position.x, position.y, position.z }) *
			glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f)) *
			glm::scale(glm::mat4(1.0f), { size.x, size.y, 0.0f });

		Ref<Textures::Texture> texture(Textures::CreateTexture(1, 1));
		texture->SetData(color.r, color.g, color.b, color.a);

		Ref<Geometries::SquareGeometry> square_geometry(new Geometries::SquareGeometry());
		Ref<Materials::StandardMaterial> simple_material(new Materials::StandardMaterial());

		square_geometry->ApplyTransform(transform);
		simple_material->SetTexture(texture);

		Mesh triangle_mesh{ square_geometry, simple_material };
		m_graphic_storage->AddMesh(triangle_mesh);
	}

	void GraphicRenderer2D::_DrawTriangle(const glm::vec4& position, const glm::vec2& size, float angle, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture2D>& texture) {
		glm::mat4 transform =
			glm::translate(glm::mat4(1.0f), { position.x, position.y, position.z }) *
			glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f)) *
			glm::scale(glm::mat4(1.0f), { size.x, size.y, 0.0f });

		Ref<Geometries::SquareGeometry> square_geometry(new Geometries::SquareGeometry());
		Ref<Materials::StandardMaterial> simple_material(new Materials::StandardMaterial());

		square_geometry->ApplyTransform(transform);
		simple_material->SetTexture(texture);

		Mesh triangle_mesh{ square_geometry, simple_material };
		m_graphic_storage->AddMesh(triangle_mesh);

	}

	void GraphicRenderer2D::_DrawTriangle(const glm::vec4& position, const glm::vec2& size, float angle, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture2D>& texture, const glm::vec4& tint_color, float tiling_factor) {
		glm::mat4 transform =
			glm::translate(glm::mat4(1.0f), { position.x, position.y, position.z }) *
			glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f)) *
			glm::scale(glm::mat4(1.0f), { size.x, size.y, 0.0f });


		Ref<Geometries::SquareGeometry> square_geometry(new Geometries::SquareGeometry());
		Ref<Materials::StandardMaterial> simple_material(new Materials::StandardMaterial());

		square_geometry->ApplyTransform(transform);
		simple_material->SetTexture(texture);
		//simple_material->SetTextureTilingFactor(tiling_factor);
		//simple_material->SetTextureTintColor(tint_color);

		Mesh triangle_mesh{ square_geometry, simple_material };
		m_graphic_storage->AddMesh(triangle_mesh);

	}
}
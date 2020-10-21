#pragma once
#include "../Materials/ShaderMaterial.h"
#include "../Geometries/IGeometry.h"

#include "../../Z_EngineDef.h"

namespace Z_Engine::Rendering::Meshes {

	class Mesh
	{
	public:
		explicit Mesh();
		explicit Mesh(Ref<Geometries::IGeometry>&& geometry, Ref<Materials::ShaderMaterial>&& material);
		explicit Mesh(Ref<Geometries::IGeometry>& geometry, Ref<Materials::ShaderMaterial>& material);

		virtual ~Mesh() = default;

		void SetIdentifier(unsigned int value);
		void SetMaterial(const Ref<Materials::ShaderMaterial>& material);
		void SetGeometry(const Ref<Geometries::IGeometry>& geometry);

		unsigned int GetIdentifier() const;
		const Ref<Materials::ShaderMaterial>& GetMaterial() const;
		const Ref<Geometries::IGeometry>& GetGeometry() const;

	private:
		virtual void OnSetMaterialEvent();
		virtual void OnSetIdentifierEvent();

	private:
		unsigned int m_unique_identifier;
		Ref<Materials::ShaderMaterial> m_material {nullptr};
		Ref<Geometries::IGeometry> m_geometry {nullptr};

	};
}
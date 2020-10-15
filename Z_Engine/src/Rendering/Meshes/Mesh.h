#pragma once
#include "../Materials/IMaterial.h"
#include "../Geometries/IGeometry.h"

#include "../../Z_EngineDef.h"

namespace Z_Engine::Rendering::Meshes {

	class Mesh
	{
	public:
		explicit Mesh();
		explicit Mesh(Ref<Geometries::IGeometry>&& geometry, Ref<Materials::IMaterial>&& material);
		explicit Mesh(Ref<Geometries::IGeometry>& geometry, Ref<Materials::IMaterial>& material);

		virtual ~Mesh() = default;

		void SetMaterial(const Ref<Materials::IMaterial>& material);
		void SetGeometry(const Ref<Geometries::IGeometry>& geometry);

		const Ref<Materials::IMaterial>& GetMaterial() const;
		const Ref<Geometries::IGeometry>& GetGeometry() const;

	private:
		virtual void OnSetMaterialEvent();

	private:
		Ref<Materials::IMaterial> m_material {nullptr};
		Ref<Geometries::IGeometry> m_geometry {nullptr};

	};
}
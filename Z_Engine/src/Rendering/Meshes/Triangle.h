#pragma once
#include "Mesh2D.h"


namespace Z_Engine::Rendering::Meshes {

	struct Triangle2D : public Mesh2D
	{
		Triangle2D() : Mesh2D(

			Ref<Geometries::IGeometry>(new Geometries::IGeometry({
					Renderers::Storages::GraphicVertex({0.5f, -0.5f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f},	 1.0f),
					Renderers::Storages::GraphicVertex({-0.5f, -0.5f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, 1.0f),
					Renderers::Storages::GraphicVertex({0.0f,  0.5f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f},	 1.0f),
																													 
					Renderers::Storages::GraphicVertex({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f},	 1.0f),
					Renderers::Storages::GraphicVertex({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f},	 1.0f),
					Renderers::Storages::GraphicVertex({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f},	 1.0f),
				})
			),

			Ref<Materials::SimpleMaterial2D>(new Materials::SimpleMaterial2D())
		)
		{}

		virtual ~Triangle2D() = default;
	};
}
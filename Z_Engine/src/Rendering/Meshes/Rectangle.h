#pragma once
#include "Mesh2D.h"
#include "../Materials/SimpleMaterial2D.h"


namespace Z_Engine::Rendering::Meshes {

	struct Rectangle2D : public Mesh2D
	{
		Rectangle2D() : Mesh2D(
			Ref<Geometries::IGeometry>(new Geometries::IGeometry({
					Renderers::Storages::GraphicVertex({-0.75f, -0.5f, 1.0f},	{0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, 0.0f),
					Renderers::Storages::GraphicVertex({0.75f, -0.5f, 1.0f,},	{0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, 0.0f),
					Renderers::Storages::GraphicVertex({0.75f, 0.5f, 1.0f},		{0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 1.0f}, 0.0f),
					Renderers::Storages::GraphicVertex({-0.75f, 0.5f, 1.0f},	{0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f}, 0.0f)
				})
			),

			Ref<Materials::SimpleMaterial2D>(new Materials::SimpleMaterial2D())
		)
		{}

		virtual ~Rectangle2D() = default;
	};
}
#pragma once
#include "QuadGeometry.h"

namespace Z_Engine::Rendering::Geometries
{
	QuadGeometry::QuadGeometry()
		: IGeometry(
			{
					Renderers::Storages::GraphicVertex({-0.75f, -0.5f, 1.0f},	{0.0f, 0.0f, 0.0f, 0.0f},	0.0f, {0.0f, 0.0f}),
					Renderers::Storages::GraphicVertex({0.75f, -0.5f, 1.0f,},	{0.0f, 0.0f, 0.0f, 0.0f},	0.0f, {1.0f, 0.0f}),
					Renderers::Storages::GraphicVertex({0.75f, 0.5f, 1.0f},		{0.0f, 0.0f, 0.0f, 0.0f},	0.0f, {1.0f, 1.0f}),
					Renderers::Storages::GraphicVertex({-0.75f, 0.5f, 1.0f},	{0.0f, 0.0f, 0.0f, 0.0f},	0.0f, {0.0f, 1.0f})
			})
	{
	}
}

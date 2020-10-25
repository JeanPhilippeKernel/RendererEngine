#pragma once
#include "QuadGeometry.h"

using namespace Z_Engine::Rendering::Renderers::Storages;

namespace Z_Engine::Rendering::Geometries
{
	QuadGeometry::QuadGeometry()
		: IGeometry(
			{
					GraphicVertex(0.0f, {-0.75f, -0.5f, 1.0f},	{0.0f, 0.0f, 0.0f, 0.0f},	0.0f, {0.0f, 0.0f}),
					GraphicVertex(0.0f, {0.75f, -0.5f, 1.0f,},	{0.0f, 0.0f, 0.0f, 0.0f},	0.0f, {1.0f, 0.0f}),
					GraphicVertex(0.0f, {0.75f, 0.5f, 1.0f},	{0.0f, 0.0f, 0.0f, 0.0f},	0.0f, {1.0f, 1.0f}),
					GraphicVertex(0.0f, {-0.75f, 0.5f, 1.0f},	{0.0f, 0.0f, 0.0f, 0.0f},	0.0f, {0.0f, 1.0f})
			})
	{
	}
}

#include <pch.h>
#include <Rendering/Geometries/QuadGeometry.h>

using namespace ZEngine::Rendering::Renderers::Storages;

namespace ZEngine::Rendering::Geometries
{
	QuadGeometry::QuadGeometry()
		: IGeometry(
			{
					GraphicVertex({-0.75f, -0.5f, 1.0f},	{0.0f, 0.0f}),
					GraphicVertex({0.75f, -0.5f, 1.0f},		{1.0f, 0.0f}),
					GraphicVertex({0.75f, 0.5f, 1.0f},		{1.0f, 1.0f}),
					GraphicVertex({-0.75f, 0.5f, 1.0f},		{0.0f, 1.0f})
			})
	{
	}
}

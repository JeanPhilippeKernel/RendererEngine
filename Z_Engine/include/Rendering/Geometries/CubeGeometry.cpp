#pragma once
#include "CubeGeometry.h"

using namespace Z_Engine::Rendering::Renderers::Storages;

namespace Z_Engine::Rendering::Geometries
{
	CubeGeometry::CubeGeometry()
		: IGeometry(
			{
					GraphicVertex({-0.5f, -0.5f, -0.5f},	{0.0f, 0.0f}),
					GraphicVertex({0.5f, -0.5f, -0.5f},		{1.0f, 0.0f}),
					GraphicVertex({0.5f,  0.5f, -0.5f},		{1.0f, 1.0f}),
					GraphicVertex({0.5f,  0.5f, -0.5f},		{1.0f, 1.0f}),
					GraphicVertex({-0.5f,  0.5f, -0.5f},	{0.0f, 1.0f}),
					GraphicVertex({-0.5f, -0.5f, -0.5f},	{0.0f, 0.0f}),

					GraphicVertex({-0.5f, -0.5f,  0.5f},	{0.0f, 0.0f}),
					GraphicVertex({0.5f, -0.5f,  0.5f},		{1.0f, 0.0f}),
					GraphicVertex({0.5f,  0.5f,  0.5f},		{1.0f, 1.0f}),
					GraphicVertex({0.5f,  0.5f,  0.5f},		{1.0f, 1.0f}),
					GraphicVertex({-0.5f,  0.5f,  0.5f},	{0.0f, 1.0f}),
					GraphicVertex({-0.5f, -0.5f,  0.5f},	{0.0f, 0.0f}),

					GraphicVertex({-0.5f,  0.5f,  0.5f},	{1.0f, 0.0f}),
					GraphicVertex({-0.5f,  0.5f, -0.5f},	{1.0f, 1.0f}),
					GraphicVertex({-0.5f, -0.5f, -0.5f},	{0.0f, 1.0f}),
					GraphicVertex({-0.5f, -0.5f, -0.5f},	{0.0f, 1.0f}),
					GraphicVertex({-0.5f, -0.5f,  0.5f},	{0.0f, 0.0f}),
					GraphicVertex({-0.5f,  0.5f,  0.5f},	{1.0f, 0.0f}),

					GraphicVertex({0.5f, 0.5f, 0.5f},		{1.0f, 0.0f}),
					GraphicVertex({0.5f, 0.5f,  -0.5f},		{1.0f, 1.0f}),
					GraphicVertex({0.5f,  -0.5f,  -0.5f},	{0.0f, 1.0f}),
					GraphicVertex({0.5f, -0.5f, -0.5f},		{0.0f, 1.0f}),
					GraphicVertex({0.5f, -0.5f,  0.5f},		{0.0f, 0.0f}),
					GraphicVertex({0.5f,  0.5f,  0.5f},		{1.0f, 0.0f}),


					GraphicVertex({-0.5f, -0.5f, -0.5f},	{0.0f, 1.0f}),
					GraphicVertex({0.5f, -0.5f,  -0.5f},	{1.0f, 1.0f}),
					GraphicVertex({0.5f,  -0.5f,  0.5f},	{1.0f, 0.0f}),
					GraphicVertex({0.5f, -0.5f, 0.5f},		{1.0f, 0.0f}),
					GraphicVertex({-0.5f, -0.5f,  0.5f},	{0.0f, 0.0f}),
					GraphicVertex({-0.5f,  -0.5f,  -0.5f},	{0.0f, 1.0f}),


					GraphicVertex({-0.5f, 0.5f, -0.5f},		{0.0f, 1.0f}),
					GraphicVertex({0.5f, 0.5f,  -0.5f},		{1.0f, 1.0f}),
					GraphicVertex({0.5f,  0.5f,  0.5f},		{1.0f, 0.0f}),
					GraphicVertex({0.5f, 0.5f, 0.5f},		{1.0f, 0.0f}),
					GraphicVertex({-0.5f, 0.5f,  0.5f},		{0.0f, 0.0f}),
					GraphicVertex({-0.5f,  0.5f,  -0.5f},	{0.0f, 1.0f}),
			})
	{
	}
}

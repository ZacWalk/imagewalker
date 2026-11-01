#pragma once
#include "Geometry.h"
#include <iw/imaging/EditGeometry.h>
namespace iw::edits
{
	enum class CropHandle { none, move, left, top, right, bottom, topLeft, topRight, bottomRight, bottomLeft };
	CropHandle crop_handle(app::recti crop, app::pointi point, int radius);
	app::recti drag_crop(app::recti initial, app::recti bounds, CropHandle handle, app::pointi delta);
}

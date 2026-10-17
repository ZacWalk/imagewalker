#pragma once
#include <libiw/EditGeometry.h>
namespace iw::edits
{
	enum class CropHandle { none, move, left, top, right, bottom, topLeft, topRight, bottomRight, bottomLeft };
	CropHandle crop_handle(recti crop, pointi point, int radius);
	recti drag_crop(recti initial, recti bounds, CropHandle handle, pointi delta);
}

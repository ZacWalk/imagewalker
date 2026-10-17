#pragma once

namespace IW
{
	inline bool CopyRenderedBitmap(Image& image, HBITMAP bitmap)
	{
		BITMAP info{};
		if (!::GetObject(bitmap, sizeof(info), &info)) return false;
		CRender render;
		if (!render.Create(nullptr, CRect(0, 0, info.bmWidth, info.bmHeight))) return false;
		CDC source;
		if (!source.CreateCompatibleDC(nullptr)) return false;
		const HBITMAP previous = source.SelectBitmap(bitmap);
		if (!previous) return false;
		CDCRender destination(render);
		const bool copied = StretchBlt(destination, 0, 0, info.bmWidth, info.bmHeight,
			source, 0, 0, info.bmWidth, info.bmHeight, SRCCOPY) != FALSE;
		source.SelectBitmap(previous);
		return copied && render.RenderToSurface(image);
	}
}

#pragma once

IW_TEST(ColorChannelsRetainEveryByte)
{
	static_assert(std::is_same_v<decltype(IW::GetR(0)), BYTE>);
	static_assert(std::is_same_v<decltype(IW::GetG(0)), BYTE>);
	static_assert(std::is_same_v<decltype(IW::GetB(0)), BYTE>);
	static_assert(std::is_same_v<decltype(IW::GetA(0)), BYTE>);
	for (int value = 0; value <= 255; ++value)
	{
		const int green = 255 - value;
		const int blue = value ^ 0x55;
		const int alpha = value ^ 0xaa;
		const COLORREF pixel = IW::RGBA(value, green, blue, alpha);
		IW_CHECK(IW::GetR(pixel) == value);
		IW_CHECK(IW::GetG(pixel) == green);
		IW_CHECK(IW::GetB(pixel) == blue);
		IW_CHECK(IW::GetA(pixel) == alpha);
	}
}

// Included by each 2.x test translation unit after ViewRenderSurface.h.
IW_TEST(RenderSurfaceDrawLineClipsAndRestoresDC)
{
	RenderSurface surface;
	const CRect clip(10, 20, 18, 28);
	IW_CHECK(surface.Create(nullptr, clip));
	surface.Fill(RGB(0, 0, 0), nullptr);

	HDC dc = surface.GetDC();
	::MoveToEx(dc, 12, 23, nullptr);
	const HGDIOBJ oldPen = ::GetCurrentObject(dc, OBJ_PEN);
	const COLORREF color = RGB(17, 83, 201);
	surface.DrawLine(8, 24, 21, 24, color, 0);
	COLORREF pixel = 0;
	surface.GetLine(&pixel, 4, 0, 1);
	IW_CHECK((pixel & 0xFFFFFF) == IW::SwapRB(color));

	POINT current = {};
	IW_CHECK(::GetCurrentPositionEx(dc, &current) != FALSE);
	IW_CHECK(current.x == 12 && current.y == 23);
	IW_CHECK(::GetCurrentObject(dc, OBJ_PEN) == oldPen);

	for (int y = clip.top; y < clip.bottom; ++y)
		for (int x = clip.left; x < clip.right; ++x)
			IW_CHECK(::GetPixel(dc, x, y) == (y == 24 ? color : RGB(0, 0, 0)));
}

IW_TEST(RenderSurfaceDrawLineHonorsWidth)
{
	RenderSurface surface;
	IW_CHECK(surface.Create(nullptr, CRect(0, 0, 12, 12)));
	surface.Fill(RGB(0, 0, 0), nullptr);
	const COLORREF color = RGB(220, 40, 90);
	surface.DrawLine(2, 6, 10, 6, color, 3);
	::GdiFlush();

	HDC dc = surface.GetDC();
	for (int y = 4; y <= 8; ++y)
		IW_CHECK(::GetPixel(dc, 6, y) == (y >= 5 && y <= 7 ? color : RGB(0, 0, 0)));
}

IW_TEST(RenderSurfaceFlipClampsOpacity)
{
	const CRect bounds(0, 0, 4, 4);
	RenderSurface source;
	RenderSurface destination;
	IW_CHECK(source.Create(nullptr, bounds));
	IW_CHECK(destination.Create(nullptr, bounds));
	const COLORREF foreground = RGB(255, 0, 0);
	const COLORREF background = RGB(0, 0, 255);
	source.Fill(foreground, nullptr);
	destination.Fill(background, nullptr);

	source.Flip(CDCHandle(destination.GetDC()), bounds, -1);
	::GdiFlush();
	IW_CHECK(::GetPixel(destination.GetDC(), 1, 1) == background);

	source.Flip(CDCHandle(destination.GetDC()), bounds, 256);
	::GdiFlush();
	IW_CHECK(::GetPixel(destination.GetDC(), 1, 1) == foreground);
}

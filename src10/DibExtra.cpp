#include "stdafx.h"
#include "Dib.h"
#include "LoadAny.h"
#include "iw/logfile.h"
#include <libiw/Document.h>
#include <libiw/ImageEdits.h>

// Members that ArtMate's copy of dib.lib had and 1.05's does not. That library's
// sources are not in the repository at any revision, so these are written from
// their call sites rather than recovered.

// Loads an image held as a raw resource; src000 already has a loader that takes
// a resource id. A zero size means "no thumbnailing, decode at full size".
BOOL CDib::LoadResource(UINT nID, CSize size)
{
	Free();

	CLoadAny load;
	const BOOL bThumb = (size.cx > 0 && size.cy > 0);

	return load.Load(this, static_cast<int>(nID), nullptr, bThumb);
}

// Adopts the contents of another dib.
BOOL CDib::Set(CDib* pDib)
{
	if (pDib == nullptr)
	{
		Free();
		return FALSE;
	}

	pDib->CopyTo(*this);
	return IsOpen();
}

CFontHandle* CDibDC::SelectObject(HFONT hFont)
{
	if (hFont == nullptr)
		return nullptr;

	m_fontOld = SelectFont(hFont);
	return &m_fontOld;
}

// The scrolled origin, so tiling and drawing line up with the scroll position.
void CDibDC::SetMapping(CPoint pointScroll, CRect rect)
{
	m_pointMapping = pointScroll;
	m_rectMapping = rect;
	SetViewportOrg(-pointScroll.x, -pointScroll.y);
}

void CDibDC::Tile(CDib& dib, CPoint pointOffset)
{
	if (!dib.IsOpen())
		return;

	const int cx = static_cast<int>(dib.Width());
	const int cy = static_cast<int>(dib.Height());
	if (cx <= 0 || cy <= 0)
		return;

	CRect r = m_rectMapping;
	if (r.IsRectEmpty())
		r = CRect(0, 0, cx, cy);

	// m_rectMapping is a device rect; Draw() below takes logical coordinates.
	r -= GetViewportOrg();

	// Start on the tile boundary at or before the top-left of the target.
	const int xStart = r.left - (((r.left - pointOffset.x) % cx) + cx) % cx;
	const int yStart = r.top - (((r.top - pointOffset.y) % cy) + cy) % cy;

	for (int y = yStart; y < r.bottom; y += cy)
	{
		for (int x = xStart; x < r.right; x += cx)
			Draw(dib, CPoint(x, y));
	}
}

CDibScale::CDibScale()
{
}

CDibScale::~CDibScale()
{
}

BOOL CDibScale::Scale(CDib& dibSrc, CSize sizeDst, CStatus* pStatus)
{
	if (!dibSrc.IsOpen() || sizeDst.cx <= 0 || sizeDst.cy <= 0)
	{
		IW::Logging::Warn(_T("Invalid image scaling request"));
		return FALSE;
	}
	if (pStatus && pStatus->Exit()) return FALSE;
	try
	{
		using Format = iw::imaging::PixelStorage;
		iw::imaging::FrameView frame;
		frame.width = static_cast<int>(dibSrc.Width());
		frame.height = static_cast<int>(dibSrc.Height());
		frame.stride = dibSrc.m_nStorageWidth;
		frame.pixels = {dibSrc.GetBitmap(), static_cast<size_t>(frame.stride) * frame.height};
		switch (dibSrc.Bpp())
		{
		case 1: frame.format = Format::indexed1; break;
		case 4: frame.format = Format::indexed4; break;
		case 8: frame.format = Format::indexed8; break;
		case 16: frame.format = Format::bgr555; break;
		case 24: frame.format = Format::bgr24; break;
		case 32: frame.format = Format::bgra32; break;
		default:
			IW::Logging::Error(_T("Unsupported DIB storage for image scaling"));
			return FALSE;
		}
		if (dibSrc.Bpp() <= 8)
			frame.paletteBgra = {reinterpret_cast<const std::uint8_t*>(dibSrc.GetColor()),
				(size_t{1} << dibSrc.Bpp()) * sizeof(RGBQUAD)};
		auto source = iw::imaging::pixels_from_frame(frame);
		auto scaled = iw::edits::resize_image(source, {sizeDst.cx, sizeDst.cy});
		if (pStatus && pStatus->Exit()) return FALSE;
		CDib result;
		if (scaled.pixels.empty() || !result.Create(scaled.width, scaled.height, 32, FALSE))
		{
			IW::Logging::Error(_T("Unable to allocate the scaled image"));
			return FALSE;
		}
		for (int y = 0; y < scaled.height; ++y)
			memcpy(result.GetBitmap(y), scaled.pixels.data() + static_cast<size_t>(y) * scaled.width,
				static_cast<size_t>(scaled.width) * 4);
		result.m_sizeOriginal = dibSrc.OriginalSize();
		result.m_strInfo = dibSrc.m_strInfo;
		m_dib.Attach(result);
		if (pStatus) pStatus->Status(sizeDst.cy, sizeDst.cy);
		return TRUE;
	}
	catch (const std::bad_alloc&)
	{
		IW::Logging::Error(_T("Not enough memory to scale the image"));
		return FALSE;
	}
	catch (const std::invalid_argument& error)
	{
		IW::Logging::Error(_T("Invalid image storage: %hs"), error.what());
		return FALSE;
	}
}

void CDibDC::Draw(CDib& dib, CRect r, CDibScale* pScale)
{
	// The cached result is already at the right size when it is usable.
	if (pScale != nullptr && pScale->m_dib.IsOpen() &&
		static_cast<int>(pScale->m_dib.Width()) == r.Width() &&
		static_cast<int>(pScale->m_dib.Height()) == r.Height())
	{
		Draw(pScale->m_dib, r.TopLeft());
		return;
	}

	Draw(dib, r);
}

CDibScale* CreateScale(CDib& dib, CSize sizeDst, CStatus* pStatus)
{
	auto pScale = new CDibScale;

	if (!pScale->Scale(dib, sizeDst, pStatus))
	{
		delete pScale;
		return nullptr;
	}

	return pScale;
}

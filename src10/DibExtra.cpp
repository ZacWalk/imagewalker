#include "stdafx.h"
#include "Dib.h"
#include "LoadAny.h"
#include "iw/logfile.h"
#include <libfiles/document.h>

// Members that ImageWalker 1.0's copy of dib.lib had and 1.05's does not. That library's
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

// The scrolled origin, so tiling and drawing line up with the scroll position.
void CDibDC::SetMapping(CPoint pointScroll, CRect rect)
{
	m_pointMapping = pointScroll;
	m_rectMapping = rect;
	SetViewportOrg(-pointScroll.x, -pointScroll.y);
	SetClip(rect);
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

namespace
{
	struct Sample
	{
		unsigned first, second, weight;
	};

	Sample Locate(unsigned x, unsigned source, unsigned target)
	{
		const double center = (static_cast<double>(x) + 0.5) * source / target - 0.5;
		const double position = std::clamp(center, 0.0, static_cast<double>(source - 1));
		const auto first = static_cast<unsigned>(position);
		return {first, min(first + 1, source - 1), static_cast<unsigned>((position - first) * 256 + 0.5)};
	}

	std::uint32_t Interpolate(const std::uint32_t (&colors)[4], unsigned x, unsigned y)
	{
		const unsigned weights[] = {(256 - x) * (256 - y), x * (256 - y), (256 - x) * y, x * y};
		unsigned alpha = 0, blue = 0, green = 0, red = 0;
		for (unsigned i = 0; i < 4; ++i)
		{
			// Interpolate with alpha-weighted RGB so transparent edges do not bleed.
			const unsigned a = weights[i] * (colors[i] >> 24);
			alpha += a;
			blue += a * (colors[i] & 255);
			green += a * ((colors[i] >> 8) & 255);
			red += a * ((colors[i] >> 16) & 255);
		}
		if (!alpha) return 0;
		const unsigned half = alpha / 2;
		return ((alpha + 32768) >> 16) << 24 | ((red + half) / alpha) << 16 |
			((green + half) / alpha) << 8 | (blue + half) / alpha;
	}
}

BOOL CDibScale::Scale(CDib& dibSrc, CSize sizeDst, CStatus* pStatus)
{
	if (!dibSrc.IsOpen() || sizeDst.cx <= 0 || sizeDst.cy <= 0)
	{
		IW::Logging::Warn(_T("Invalid image scaling request"));
		return FALSE;
	}
	if (pStatus && pStatus->Exit()) return FALSE;
	// The display can scale the visible portion directly when this optional cache is too large.
	constexpr std::uint64_t maxCachedPixels = 256ull * 1024 * 1024 / sizeof(std::uint32_t);
	if (static_cast<std::uint64_t>(sizeDst.cx) * sizeDst.cy > maxCachedPixels)
		return FALSE;
	try
	{
		using Format = libfiles::document_storage;
		libfiles::pixel_rows frame;
		frame.width = static_cast<std::uint32_t>(dibSrc.Width());
		frame.height = static_cast<std::uint32_t>(dibSrc.Height());
		frame.stride = static_cast<std::size_t>(dibSrc.m_nStorageWidth);
		switch (dibSrc.Bpp())
		{
		case 1: frame.storage = Format::indexed1; break;
		case 4: frame.storage = Format::indexed4; break;
		case 8: frame.storage = Format::indexed8; break;
		case 16: frame.storage = Format::bgr555; break;
		case 24: frame.storage = Format::bgr24; break;
		case 32: frame.storage = Format::bgra32; break;
		default:
			IW::Logging::Error(_T("Unsupported DIB storage for image scaling"));
			return FALSE;
		}
		if (dibSrc.Bpp() <= 8)
			frame.palette = {reinterpret_cast<const std::uint32_t*>(dibSrc.GetColor()), size_t{1} << dibSrc.Bpp()};
		CDib result;
		if (!result.Create(sizeDst.cx, sizeDst.cy, 32, FALSE))
			return FALSE;

		std::vector<Sample> columns(result.Width());
		for (unsigned x = 0; x < result.Width(); ++x) columns[x] = Locate(x, frame.width, result.Width());
		std::vector<std::uint32_t> row0(frame.width), row1(frame.width);
		const auto read = [&](unsigned y, std::vector<std::uint32_t>& row)
		{
			auto single = frame;
			single.height = 1;
			single.pixels = {dibSrc.GetBitmap(y), frame.stride};
			libfiles::copy_bgra(single, row, frame.width, false);
		};
		unsigned first = UINT_MAX, second = UINT_MAX;
		for (unsigned y = 0; y < result.Height(); ++y)
		{
			if (pStatus && pStatus->Status(static_cast<int>(y), sizeDst.cy)) return FALSE;
			const auto vertical = Locate(y, frame.height, result.Height());
			if (vertical.first != first)
			{
				if (vertical.first == second)
				{
					row0.swap(row1);
					second = first;
				}
				else
					read(vertical.first, row0);
				first = vertical.first;
			}
			if (vertical.second != first && vertical.second != second)
			{
				read(vertical.second, row1);
				second = vertical.second;
			}
			const auto& lower = vertical.second == first ? row0 : row1;
			auto* row = reinterpret_cast<std::uint32_t*>(result.GetBitmap(y));
			for (unsigned x = 0; x < result.Width(); ++x)
			{
				const auto horizontal = columns[x];
				const std::uint32_t colors[] = {row0[horizontal.first], row0[horizontal.second],
				                                lower[horizontal.first], lower[horizontal.second]};
				row[x] = Interpolate(colors, horizontal.weight, vertical.weight);
			}
		}
		if (pStatus && pStatus->Exit()) return FALSE;
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

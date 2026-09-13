#include "stdafx.h"
#include "Dib.h"
#include "LoadAny.h"

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
		return FALSE;

	CDibDC ddc;
	if (!ddc.Create(sizeDst.cx, sizeDst.cy, 24))
		return FALSE;

	ddc.Draw(dibSrc, CRect(0, 0, sizeDst.cx, sizeDst.cy));

	// GDI has to finish with the DIB section before the CPU reads its bits.
	::GdiFlush();

	ddc.GetDib().CopyTo(m_dib);

	if (pStatus != nullptr)
		pStatus->Status(sizeDst.cy, sizeDst.cy);

	return m_dib.IsOpen();
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

// DibDC.cpp: implementation of the CDibDC class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "Dib.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CDibDC::CDibDC()
{
	m_h = nullptr;
}

CDibDC::~CDibDC()
{
	if (m_h)
		SelectObject(m_h);
}


BOOL CDibDC::Create(int x, int y, int bpp)
{
	if (m_hDC != nullptr)
	{
		if (m_h)
			SelectObject(m_h);

		// Detach() already clears m_hDC.
		::DeleteDC(Detach());
		m_h = nullptr;
	}

	if (!m_dib.Create(x, y, bpp, TRUE))
		return FALSE;

	if (!CreateCompatibleDC(nullptr))
		return FALSE;

	m_h = SelectObject(m_dib.GetHBitmap());
	SetBkMode(TRANSPARENT);

	// Default Clipping
	SetClip(CRect(0, 0, x, y));

	return TRUE;
}


#define Mask1 0x000000ff
#define Mask2 0x0000ff00
#define Mask3 0x00ff0000
#define Mask4 0xff000000


inline void Blend24(UINT* dest, UINT source)
{
	UINT alpha = source >> 24;
	UINT ialpha = 256 - alpha;

	auto p = (LPBYTE)dest;

	// Three bytes in, three bytes out: the last pixel of a DIB has no fourth byte.
	UINT d = p[0] | (p[1] << 8) | (static_cast<UINT>(p[2]) << 16);

	UINT lo = (((Mask2 & ((source & Mask1) * alpha +
			(d & Mask1) * ialpha)) |
		(Mask3 & ((source & Mask2) * alpha +
			(d & Mask2) * ialpha))) >> 8);

	p[0] = static_cast<BYTE>(lo);
	p[1] = static_cast<BYTE>(lo >> 8);

	p[2] = static_cast<BYTE>((((Mask4 & ((source & Mask3) * alpha +
		(d & Mask3) * ialpha))) >> 24));
}


template <class Iterator>
class CDibBlend24 : public CIteratorDib<Iterator>
{
public:
	CDibBlend24(CDib* pDibDst, CDib* pDibSrc,
	            CPoint point, CRect rectClip)
		: CIteratorDib<Iterator>(pDibSrc)
	{
		UINT nWidth = rectClip.Width();
		UINT nHeight = rectClip.Height();
		UINT uStart = rectClip.left - point.x;

		m_pLineIn = new COLORREF[nWidth];

		if (!m_pLineIn)
			throw E_OUTOFMEMORY;

		m_pByte = pDibSrc->GetBitmap(rectClip.top - point.y);

		for (UINT y = 0; y < nHeight; y++)
		{
			auto pDst = pDibDst->GetBitmap(rectClip.left, rectClip.top + y);
			GetLine(m_pLineIn, static_cast<LPBYTE>(ScanLine()), uStart, nWidth + uStart);
			auto pSrc = (BYTE*)m_pLineIn;

			for (UINT x = 0; x < nWidth; x += 1)
			{
				Blend24(((UINT*)pDst), *((UINT*)pSrc));

				pDst += 3;
				pSrc += 4;
			}
		}
	}

	~CDibBlend24()
	{
		delete [] m_pLineIn;
	}

	COLORREF* m_pLineIn;
};


void CDibDC::Draw(CDib& dib, CPoint point)
{
	// Callers draw in scrolled (logical) coordinates; m_rectClip is in dib coordinates.
	point += GetViewportOrg();

	CRect r;

	if (!r.IntersectRect(m_rectClip, CRect(point, dib.Size()))
		|| (r.top >= r.bottom) || (r.left >= r.right))
		return;

	switch (m_dib.Bpp() * 100 + dib.Bpp())
	{
	case 2401:
		{
			CDibBlend24<CDibIterator1>(&m_dib, &dib, point, r);
		}
		break;

	case 2404:
		{
			CDibBlend24<CDibIterator4>(&m_dib, &dib, point, r);
		}
		break;


	case 2408:
		{
			CDibBlend24<CDibIterator8>(&m_dib, &dib, point, r);
		}
		break;


	case 2424:
		{
			CDibBlend24<CDibIterator24>(&m_dib, &dib, point, r);
		}
		break;

	case 2432:
		{
			CDibBlend24<CDibIterator32>(&m_dib, &dib, point, r);
		}
		break;

	default:
		ASSERT(0);
	}
}


void CDibDC::Draw(LPRECT pRect, unsigned rgb)
{
	CRect rectDst(pRect);
	rectDst += GetViewportOrg();

	CRect r;

	if (!r.IntersectRect(m_rectClip, rectDst)
		|| (r.top >= r.bottom) || (r.left >= r.right))
		return;

	ASSERT(m_dib.Bpp() == 24);

	UINT nWidth = r.Width();
	UINT nHeight = r.Height();

	BYTE* pDst = m_dib.GetBitmap(r.left, r.top);
	UINT nDstSkip = m_dib.m_nStorageWidth + nWidth * 3;

	for (UINT yy = 0; yy < nHeight; yy += 1)
	{
		for (UINT xx = 0; xx < nWidth; xx += 1)
		{
			Blend24((UINT*)pDst, rgb);

			pDst += 3;
		}

		pDst -= nDstSkip;
	}
}


template <class IteratorIn>
class CDibScaleQuick24 : public IteratorIn
{
public:
	void Scale(CDib& dibDst, CDib& dibSrc, CRect& rectDst, CRect& rectClip)
	{
		CRect rectDraw(rectDst & rectClip);

		if (rectDraw.IsRectEmpty() ||
			rectDraw.IsRectNull())
			return;


		UINT nWidthDst = rectDst.Width();
		UINT nHeightDst = rectDst.Height();
		UINT nWidthSrc = dibSrc.Width();
		UINT nHeightSrc = dibSrc.Height();
		UINT nWidthDraw = rectDraw.Width();
		UINT nHeightDraw = rectDraw.Height();

		UINT nTop = rectDraw.top - rectDst.top;
		UINT nLeft = rectDraw.left - rectDst.left;

		m_pRgb = (COLORREF*)dibSrc.GetColor();

		for (UINT y = 0; y < nHeightDraw; y++)
		{
			LPBYTE pLineDst = dibDst.GetBitmap(rectDraw.left, rectDraw.top + y);
			LPBYTE pLineSrc = dibSrc.GetBitmap(((y + nTop) * nHeightSrc) / nHeightDst);

			for (UINT x = 0; x < nWidthDraw; x++)
			{
				UINT u = GetPixel(pLineSrc, ((x + nLeft) * nWidthSrc) / nWidthDst);
				Blend24((UINT*)pLineDst, u);
				pLineDst += 3;
			}
		}
	}
};


void CDibDC::Draw(CDib& dib, CRect rectDst)
{
	CRect r(m_rectClip);
	rectDst += GetViewportOrg();

	switch (m_dib.Bpp() * 100 + dib.Bpp())
	{
	case 2401:
		CDibScaleQuick24<CDibIterator1>().Scale(m_dib, dib, rectDst, r);
		break;

	case 2404:
		CDibScaleQuick24<CDibIterator4>().Scale(m_dib, dib, rectDst, r);
		break;


	case 2408:
		CDibScaleQuick24<CDibIterator8>().Scale(m_dib, dib, rectDst, r);
		break;

	case 2424:
		CDibScaleQuick24<CDibIterator24>().Scale(m_dib, dib, rectDst, r);
		break;

	case 2432:
		CDibScaleQuick24<CDibIterator32>().Scale(m_dib, dib, rectDst, r);
		break;

	default:
		ASSERT(0);
	}
}

// dib.cpp : implementation file
//
//

#include "stdafx.h"

#include "Dib.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CDib

CDib::CDib()
{
	m_pBMI = nullptr;
	m_pBits = nullptr;
	m_hBitmap = nullptr;
	m_nStorageWidth = 0;
	m_nOriginalType = DIB_UNKNOWN;
	m_uFlags = 0;
}

CDib::CDib(const CDib& dib)
{
	m_pBMI = nullptr;
	m_pBits = nullptr;
	m_hBitmap = nullptr;
	m_nStorageWidth = 0;
	m_nOriginalType = DIB_UNKNOWN;
	m_uFlags = 0;

	dib.CopyTo(*this);
}


const CDib& CDib::operator =(const CDib& dib)
{
	ASSERT_VALID(this);

	dib.CopyTo(*this);

	return *this;
}

void CDib::CopyTo(CDib& dib) const
{
	ASSERT_VALID(this);

	if (!dib.Create(Width(), Height(), Bpp(), FALSE))
		return;

	// The parentheses matter: '<<' binds looser than '*', so the original
	// 1 << Bpp() * sizeof(RGBQUAD) shifted by Bpp() * 4. At 8bpp that is a
	// shift of 32, which MSVC masks to 0, so exactly one byte of the palette
	// was copied; at 4bpp it shifted by 16 and copied 64KB into 64 bytes.
	if (Bpp() <= 8)
		CopyMemory(dib.GetColor(), GetColor(), (size_t(1) << Bpp()) * sizeof(RGBQUAD));

	CopyMemory(dib.GetBitmap(), GetBitmap(), m_nStorageWidth * Height());

	dib.m_nOriginalType = m_nOriginalType;

	// The description is what the items pane's tooltip reports, and the source
	// size is what the image pane sizes a stand-in thumbnail by. Leaving them
	// behind here made the tooltip's format line unreachable.
	dib.m_strInfo = m_strInfo;
	dib.m_uFlags = m_uFlags;
	dib.m_sizeOriginal = m_sizeOriginal;
}

// Takes the other DIB's pixels and leaves it closed, rather than duplicating
// them: a full-size decode handed over by a job is tens of megabytes.
void CDib::Attach(CDib& dib)
{
	if (&dib == this)
		return;

	Free();

	m_pBMI = dib.m_pBMI;
	m_pBits = dib.m_pBits;
	m_hBitmap = dib.m_hBitmap;
	m_nStorageWidth = dib.m_nStorageWidth;
	m_nOriginalType = dib.m_nOriginalType;
	m_uFlags = dib.m_uFlags;
	m_strInfo = dib.m_strInfo;
	m_sizeOriginal = dib.m_sizeOriginal;

	dib.m_pBMI = nullptr;
	dib.m_pBits = nullptr;
	dib.m_hBitmap = nullptr;
	dib.m_nStorageWidth = 0;
}

CDib::~CDib()
{
	Free();
}

/////////////////////////////////////////////////////////////////////////////
// Private functions

CSize CDib::Size()
{
	ASSERT_VALID(this);

	return CSize(Width(), Height());
}

CSize CDib::HalfSize()
{
	ASSERT_VALID(this);

	return CSize(Width() / 2, Height() / 2);
}


// The one blit the app does: a clip rectangle of this DIB to a screen DC.
void CDib::Draw(HDC hDC, const POINT& point, const RECT& rect)
{
	ASSERT_VALID(this);

	CSize size(rect.right - rect.left, rect.bottom - rect.top);

	StretchDIBits(hDC,
	              point.x, // Destination x
	              point.y, // Destination y
	              size.cx, // Destination width
	              size.cy, // Destination height
	              rect.left, // Source x
	              Height() - rect.top - size.cy, // Source y
	              size.cx, // Source width
	              size.cy, // Source height
	              GetBitmap(), // Pointer to bits
	              GetInfo(), // BITMAPINFO
	              DIB_RGB_COLORS, // Options
	              SRCCOPY);
}


int CDib::SetDIBits(HDC hdc, HBITMAP hbm, UINT firstline, UINT lastline, UINT flags)
{
	ASSERT_VALID(this);

	if (lastline == -1)
		lastline = Height();

	return ::SetDIBits(hdc, // handle of device context 
	                   hbm, // handle of bitmap 
	                   firstline, // starting scan line 
	                   lastline, // number of scan lines 
	                   GetBitmap(), // Pointer to bits
	                   GetInfo(), // BITMAPINFO
	                   flags); // Options
}

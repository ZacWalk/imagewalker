// ImageWalker by Zac Walker
//
// Purpose: The DIB section behind CRender, and the blitting and text drawing
//          done into it.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "iw/logfile.h"


#include "iw/imaging/RenderImage.h"

class RenderSurface
{
protected:

	typedef RenderSurface ThisClass;

	CRect m_rcClip;

	HDC m_hdcMem;
	HDC m_hdc;

	HBITMAP m_hbitmapOld;
	HBITMAP m_hbitmapDibSection;
	LPBYTE  m_pByte;

	HFONT m_hFontSelected;

public:


	RenderSurface() :
	  m_hdcMem(0),
		  m_hdc(0),
		  m_hbitmapOld(0),
		  m_hbitmapDibSection(0),
		  m_pByte(0),
		  m_hFontSelected(0)
	  {
	  }

	  ~RenderSurface()
	  {
		  Free();
	  }

	  static HANDLE GetBitmapMemory()
	  {
		  //static HANDLE h = CreateFileMapping( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 2048 * 2048 * 4, NULL);
		  //return h;
		  return NULL;
	  }

	  bool Create(HDC hdc, const CRect &rectClip)
	  {
		  Free(); // Reset in case this is not the first create

		  // Skip empty rects
		  if (rectClip.right <= rectClip.left || rectClip.bottom <= rectClip.top)
		  {
			  CRect rc(0, 0, 1, 1);
			  m_rcClip = rc;
		  }
		  else
		  {
			  m_rcClip = rectClip;
		  }

		  m_hdc = hdc;
		  m_hdcMem = ::CreateCompatibleDC(hdc);

		  if (m_hdcMem == NULL)
		  {
			  Free();
			  return false;
		  }

		  int width = m_rcClip.right - m_rcClip.left;
		  int height = m_rcClip.bottom - m_rcClip.top;

		  DWORD  rMask, gMask, bMask;

		  //if (!GetRGBMasks(hdc, rMask, gMask, bMask))
		  {
			  rMask = 0x0FF0000;
			  gMask = 0x000FF00;
			  bMask = 0x00000FF;
		  }

		  BITMAPINFOANDPALETTE info;
		  IW::MemZero(&info, sizeof(info));
		  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);

		  info.bmiHeader.biWidth = width;
		  info.bmiHeader.biHeight = -height;
		  info.bmiHeader.biPlanes = 1;
		  info.bmiHeader.biBitCount = 32;
		  info.bmiHeader.biCompression = BI_BITFIELDS;
		  info.bmiHeader.biSizeImage = 0;
		  info.bmiHeader.biXPelsPerMeter = 0;
		  info.bmiHeader.biYPelsPerMeter = 0;
		  info.bmiHeader.biClrUsed = 0;
		  info.bmiHeader.biClrImportant = 0;
		  info.bmiColors[0] = rMask;
		  info.bmiColors[1] = gMask;
		  info.bmiColors[2] = bMask;

		  m_pByte = NULL;
		  m_hbitmapDibSection = ::CreateDIBSection(
			  m_hdcMem,
			  (LPBITMAPINFO)&info,
			  DIB_RGB_COLORS,
			  (LPVOID*)&m_pByte,
			  GetBitmapMemory(),
			  0);

		  if (m_hbitmapDibSection == NULL || m_pByte == NULL)
		  {
			  Free();
			  return false;
		  }

		  // Select the buffer into a device context
		  m_hbitmapOld = (HBITMAP)::SelectObject(m_hdcMem, m_hbitmapDibSection);
		  ::SetViewportOrgEx(m_hdcMem, -m_rcClip.left, -m_rcClip.top, NULL);

		  // Default font settings
		  ::SelectObject(m_hdcMem, ::GetStockObject(HOLLOW_BRUSH));
		  ::SetBkMode(m_hdcMem, TRANSPARENT);



		  return true;
	  }

	  void Free()
	  {
		  if (m_hbitmapDibSection)
		  {
			  ::SelectObject(m_hdcMem, m_hbitmapOld);
			  ::DeleteObject(m_hbitmapDibSection);
			  m_hbitmapDibSection = 0;
		  }

		  if (m_hdcMem)
		  {
			  ::SelectObject(m_hdcMem, ::GetStockObject(DEFAULT_GUI_FONT));
			  ::DeleteDC(m_hdcMem);
			  m_hdcMem = 0;
		  }
	  }

	  void Flip()
	  {
		  if (m_hdcMem)
		  {
			  ::BitBlt(
				  m_hdc,
				  m_rcClip.left,
				  m_rcClip.top,
				  m_rcClip.right - m_rcClip.left,
				  m_rcClip.bottom - m_rcClip.top,
				  m_hdcMem,
				  m_rcClip.left,
				  m_rcClip.top,
				  SRCCOPY);
		  }
	  }

	  void Flip(CDCHandle dc, const CRect &rectOut, int opacity)
	  {
		  if (m_hdcMem == nullptr)
			  return;

		  BLENDFUNCTION bf = {0};
		  bf.BlendOp = AC_SRC_OVER;
		  bf.SourceConstantAlpha = static_cast<BYTE>(IW::Clamp(opacity, 0, 255));

		  // AlphaBlend takes independent source and destination extents, so it
		  // scales by itself. This used to stage through a compatible bitmap the
		  // size of the destination purely to get a HALFTONE StretchBlt first.
		  dc.AlphaBlend(rectOut.left, rectOut.top, rectOut.Width(), rectOut.Height(),
			  m_hdcMem, m_rcClip.left, m_rcClip.top, m_rcClip.Width(), m_rcClip.Height(), bf);
	  }

	  CRect GetClipRect() const
	  {
		  return m_rcClip;
	  }


	  HDC GetDC()
	  {
		  return m_hdcMem;
	  }

#include <pshpack1.h>

	  struct BITMAPINFOANDPALETTE
	  {
		  BITMAPINFOHEADER    bmiHeader;
		  DWORD             bmiColors[256];
	  } ;

	  typedef struct
	  {
		  WORD r;
		  WORD g;
		  WORD b;
		  WORD a;
		  WORD cr;
		  WORD cg;
		  WORD cb;
		  WORD ca;
	  }
	  SUM;

#include <poppack.h>


	  void DrawImage(const IW::Page &page, const CRect &rectDstIn, const CRect &rectSrcIn)
	  {
		  if (m_rcClip.IsRectEmpty())
		  {
			  assert(0); //??
			  return;
		  }

		  WithBlitter([&](auto &blitter)
		  {
			  RenderImage<ThisClass, std::remove_reference_t<decltype(blitter)>> render(*this, blitter);
			  render.DrawImage(page, rectDstIn, rectSrcIn);
		  });
	  }

	  void DrawText(LPCTSTR sz, CRect &rect, HFONT hFont, DWORD format, COLORREF clr)
	  {
		  if (m_hFontSelected != hFont)
		  {
			  ::SelectObject(m_hdcMem, hFont);
			  m_hFontSelected = hFont;
		  }

		  ::SetTextColor(m_hdcMem, clr & 0xFFFFFF);
		  ::DrawText(m_hdcMem, sz, -1, rect, format);
	  }


	  LPCBYTE GetBitmapLine(int nLine) const
	  {
		  return const_cast<RenderSurface*>(this)->GetBitmapLine(nLine);
	  }

	  unsigned GetStorageWidth() const
	  {
		  return IW::CalcStorageWidth(m_rcClip.right - m_rcClip.left, IW::PixelFormat::PF32);
	  }

	  LPBYTE GetBitmapLine(int nLine)
	  {
		  // We dont support clipping here
		  assert(nLine >= 0 && nLine < (m_rcClip.bottom - m_rcClip.top));

		  const unsigned nStorageWidth = GetStorageWidth();
		  const unsigned nOffset = nStorageWidth * nLine;
		  LPBYTE p = m_pByte + nOffset;

		  // Not any overflow
		  assert(m_pByte <= p);
		  assert(m_pByte + ((unsigned)nStorageWidth * (m_rcClip.bottom - m_rcClip.top)) > p);

		  return p;
	  }

	  void SetLine(IW::LPCCOLORREF pLineSrc, const int y, const int x, const int cx)
	  {
		  LPBYTE pDestination = GetBitmapLine(y) + (x * 4);
		  IW::MemCopy(pDestination , pLineSrc, cx * 4);
	  }

	  void GetLine(LPCOLORREF pLineDst, const int y, const int x, const int cx)
	  {
		  LPCBYTE pSource = GetBitmapLine(y) + (x * 4);
		  IW::MemCopy(pLineDst, pSource, cx * 4);
	  }

	  void Blend(COLORREF clr, LPCRECT pDestRect)
	  {
		  WithBlitter([&](auto &blitter) { Blend(blitter, clr, pDestRect); });
	  }

	  template<class TBlitter>
	  void Blend(TBlitter &blitter, COLORREF clr, LPCRECT pDestRect)
	  {
		  clr = IW::SwapRB(clr);

		  if (pDestRect)
		  {
			  CRect rc;
			  if (rc.IntersectRect(m_rcClip, pDestRect))
			  {
				  rc.OffsetRect(-m_rcClip.TopLeft());
				  int nWidth = rc.right - rc.left;

				  for(int y = rc.top; y < rc.bottom; ++y)
				  {
					  LPDWORD pLine = (LPDWORD)GetBitmapLine(y);
					  blitter.BlendColor32(pLine + rc.left, clr, nWidth);
				  }
			  }
		  }
		  else
		  {
			  int nHeight = m_rcClip.bottom - m_rcClip.top;
			  int nWidth = m_rcClip.right - m_rcClip.left;

			  blitter.BlendColor32((LPDWORD)m_pByte, clr, nWidth * nHeight);
		  }
	  }

	  void Fill(COLORREF clr, LPCRECT pDestRect)
	  {
		  WithBlitter([&](auto &blitter) { Fill(blitter, clr, pDestRect); });
	  }

	  template<class TBlitter>
	  void Fill(TBlitter &blitter, COLORREF clr, LPCRECT pDestRect)
	  {
		  clr = IW::SwapRB(clr);

		  if (pDestRect)
		  {
			  CRect rc;
			  if (rc.IntersectRect(m_rcClip, pDestRect))
			  {
				  rc.OffsetRect(-m_rcClip.TopLeft());
				  int nWidth = rc.right - rc.left;

				  for(int y = rc.top; y < rc.bottom; ++y)
				  {
					  LPDWORD pLine = (LPDWORD)GetBitmapLine(y);
					  blitter.Fill32(pLine + rc.left, clr, nWidth);
				  }
			  }
		  }
		  else
		  {
			  int nHeight = m_rcClip.bottom - m_rcClip.top;
			  int nWidth = m_rcClip.right - m_rcClip.left;

			  blitter.Fill32((LPDWORD)m_pByte, clr, nWidth * nHeight);
		  }
	  }

	  void DrawLine(int x1, int y1, int x2, int y2, COLORREF clr, int nWidth)
	  {
		  if (m_hdcMem == nullptr)
		  {
			  IW::Logging::Error(_T("Cannot draw a line on an uninitialised render surface."));
			  return;
		  }

		  CPen pen;
		  if (pen.CreatePen(PS_SOLID, IW::Max(nWidth, 0), clr & 0xFFFFFF) == nullptr)
		  {
			  IW::Logging::Error(_T("Cannot create a render surface line pen."));
			  return;
		  }

		  const int saved = ::SaveDC(m_hdcMem);
		  if (saved == 0)
		  {
			  IW::Logging::Error(_T("Cannot save the render surface line drawing state."));
			  return;
		  }

		  const HGDIOBJ previous = ::SelectObject(m_hdcMem, pen);
		  const bool drawn = previous != nullptr && previous != HGDI_ERROR &&
			  ::MoveToEx(m_hdcMem, x1, y1, nullptr) && ::LineTo(m_hdcMem, x2, y2);
		  if (!::RestoreDC(m_hdcMem, saved))
			  IW::Logging::Error(_T("Cannot restore the render surface line drawing state."));
		  if (!drawn)
			  IW::Logging::Error(_T("Cannot draw the render surface line."));
		  // Other surface operations access the DIB bits directly.
		  ::GdiFlush();
	  }

	  friend class CDCRenderSurface;
};

class CDCRenderSurface : public CDCHandle
{
public:
	RenderSurface *_pSurface;

	CDCRenderSurface(RenderSurface *pSurface) : _pSurface(pSurface), CDCHandle(pSurface->m_hdcMem)
	{
	}

	~CDCRenderSurface()
	{
	}
};

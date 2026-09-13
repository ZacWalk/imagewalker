// ImageWalker by Zac Walker
//
// Purpose: The DIB section behind CRender, and the blitting and text drawing
//          done into it.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "iw/logfile.h"


template<class TCanvas, class TBlitter>
class RenderImage
{
private:
	TCanvas &_canvas;
	TBlitter &_blitter;

public:

	RenderImage(TCanvas &canvas, TBlitter &blitter) : _canvas(canvas), _blitter(blitter)
	{
	}

	void DrawImage(const IW::Page &page, const CRect &rectDstIn, const CRect &rectSrcIn)
	{
		const int nWidthIn = rectSrcIn.right - rectSrcIn.left;
		const int nHeightIn = rectSrcIn.bottom - rectSrcIn.top;
		const int nWidthOut = rectDstIn.right - rectDstIn.left;
		const int nHeightOut = rectDstIn.bottom - rectDstIn.top;

		CRect rDst, rSrc, rcClip(_canvas.GetClipRect());	

		if (!rDst.IntersectRect(rectDstIn, rcClip))
		{
			return;
		}

		const int nOffsetX = rDst.left - rectDstIn.left;
		const int nOffsetY = rDst.top - rectDstIn.top; 

		rSrc.left = MulDiv(nOffsetX, nWidthIn, nWidthOut) + rectSrcIn.left;
		rSrc.top = MulDiv(nOffsetY, nHeightIn, nHeightOut) + rectSrcIn.top;
		rSrc.right = MulDiv(rDst.right - rectDstIn.left, nWidthIn, nWidthOut) + rectSrcIn.left;
		rSrc.bottom = MulDiv(rDst.bottom - rectDstIn.top, nHeightIn, nHeightOut)+ rectSrcIn.top;

		rDst.OffsetRect(-rcClip.TopLeft());

		const CSize sizeDst(rDst.Size());
		const CSize sizeSrc(rSrc.Size());

		// Need Blend?
		IW::PixelFormat pf = page.GetPixelFormat();

		bool bNeedBlend = page.GetFlags() & IW::PageFlags::HasTransparent || pf.HasAlpha();	

		IW::ConstIImageSurfaceLockPtr pLock = page.GetSurfaceLock();

		// Do we need to scale?
		if (nWidthOut == nWidthIn && nHeightOut == nHeightIn)
		{
			// No scale		
			if (bNeedBlend)
			{
				DrawImageNoScaleBlend(rDst, rSrc, sizeSrc, pLock);
			}
			else
			{
				DrawImageNoScale(rDst, rSrc, sizeSrc, pLock);
			}
		}
			else if (nWidthOut < nWidthIn)
			{
				DrawImageScaleUp(rDst, rSrc, rectSrcIn.top, nOffsetX, nWidthIn, nWidthOut, nOffsetY, nHeightIn, nHeightOut, bNeedBlend, sizeSrc, pLock, sizeDst);
			}
		else
		{
			DrawImageScaleDown(rectDstIn, nWidthIn, nWidthOut, nHeightIn, nHeightOut, bNeedBlend, pLock, sizeDst);
		}
	}

private:
	void DrawImageNoScaleBlend(CRect& rDst, CRect& rSrc, const CSize& sizeSrc, const IW::IImageSurfaceLock* pLock)
	{
		IW::CBuffer<COLORREF> pLineIn(sizeSrc.cx + 1);
		IW::CBuffer<COLORREF> pLineOut(sizeSrc.cx + 1);

		for(int y = 0; y < sizeSrc.cy; y++)			
		{
			// RenderAlphaLine composites over whatever is already in pLineOut,
			// so the destination has to be read back first.
			_canvas.GetLine(pLineOut, y + rDst.top, rDst.left, sizeSrc.cx);
			// GetLine, not RenderLine: RenderLine consumes the alpha channel, so
			// RenderAlphaLine would see 0 for every pixel.
			pLock->GetLine(pLineIn, y + rSrc.top, rSrc.left, sizeSrc.cx);
			_blitter.RenderAlphaLine(pLineOut, pLineIn, sizeSrc.cx);
			_canvas.SetLine(pLineOut, y + rDst.top, rDst.left, sizeSrc.cx);
		}
	}

	void DrawImageNoScale(CRect& rDst, CRect& rSrc, const CSize& sizeSrc, const IW::IImageSurfaceLock* pLock)
	{
		IW::CBuffer<COLORREF> pLineBuffer(sizeSrc.cx + 1);

		for(int y = 0; y < sizeSrc.cy; y++)
		{
			pLock->RenderLine(pLineBuffer, y + rSrc.top, rSrc.left, sizeSrc.cx);
			_canvas.SetLine(pLineBuffer, y + rDst.top, rDst.left, sizeSrc.cx);
		}
	}

	void DrawImageScaleUp(CRect& rDst, CRect& rSrc, const int nSrcTop, const int nOffsetX, const int nWidthIn, const int nWidthOut, const int nOffsetY, const int nHeightIn, const int nHeightOut, bool bNeedBlend, const CSize& sizeSrc, const IW::IImageSurfaceLock* pLock, const CSize& sizeDst)
	{
		// Figure out x position
		int xOut = rDst.top - nOffsetX;
		int xSum = nWidthOut >> 1; 
		int xx = 0;

		while (xx < nWidthIn) 
		{
			xSum += nWidthIn;

			if (xOut >= rDst.top && xOut < rDst.bottom)
			{				
				break;
			}

			while (xSum >= nWidthOut) 
			{
				++xx;						
				xSum -= nWidthOut;
			}

			++xOut;
		}

		// Do the Y
		int nSumSize = sizeDst.cx * sizeof(DWORD) * 4;
		IW::CBuffer<DWORD> pSum(sizeDst.cx * 4);

		IW::CBuffer<COLORREF> pLineIn(sizeSrc.cx + 1);
		IW::CBuffer<COLORREF> pLineBuffer(sizeDst.cx + 1);

		int yOut = rDst.top - nOffsetY;
		int nCount = 0;
		int ySum = nHeightOut >> 1; 
		int yy = 0;

		while (yy < nHeightIn) 
		{
			IW::MemZero(pSum, nSumSize);

			ySum += nHeightIn;
			nCount = 0;

			if (yOut >= rDst.top && yOut < rDst.bottom)
			{				
				while (ySum >= nHeightOut) 
				{
					// yy counts source rows from rectSrcIn.top; rSrc.top already
					// includes that plus the clip offset, so adding it double-counts.
					// GetLine preserves alpha for the blend below.
					if (bNeedBlend)
						pLock->GetLine(pLineIn, yy + nSrcTop, rSrc.left, sizeSrc.cx);
					else
						pLock->RenderLine(pLineIn, yy + nSrcTop, rSrc.left, sizeSrc.cx);

					if (0 == nCount)
					{						
						_blitter.ScaleDownLine(pSum, pLineIn, sizeSrc.cx, sizeDst.cx);
					}
					else
					{
						_blitter.ScaleDownLineFast(pSum, pLineIn, sizeSrc.cx, sizeDst.cx);
					}
					++yy;
					++nCount;

					ySum -= nHeightOut;
				}

				// Check for overflow
				assert(sizeDst.cx + nOffsetX <= nWidthOut);	

				if (nCount)
				{
					// RenderScaleDownLine composites the averaged sample over the
					// destination, so the destination row has to be read back first.
					// It used to composite over the previous source line and then
					// blend that over the previous output line, so a transparent
					// pixel returned the pixel above it: every alpha image smeared
					// its last opaque row down the rest of the picture.
					if (bNeedBlend)
						_canvas.GetLine(pLineBuffer, yOut, rDst.left, sizeDst.cx);

					_blitter.RenderScaleDownLine(pLineBuffer, pSum, sizeDst.cx);
					_canvas.SetLine(pLineBuffer, yOut, rDst.left, sizeDst.cx);
				}
			}
			else					
			{
				while (ySum >= nHeightOut) 
				{
					++yy;						
					ySum -= nHeightOut;
				}
			}

			++yOut;
		}
	}

	void DrawImageScaleDown(const CRect& rectDstIn, const int nWidthIn, const int nWidthOut, const int nHeightIn, const int nHeightOut, bool bNeedBlend, const IW::IImageSurfaceLock* pLock, const CSize& sizeDst)
	{
		CRect rcClip(_canvas.GetClipRect());
		IW::CBuffer<COLORREF> pLineIn(sizeDst.cx + 1);
		IW::CBuffer<COLORREF> pLineBuffer(sizeDst.cx + 1);

		// Scale up
		CRect rImage = rectDstIn;
		rImage.OffsetRect(-rcClip.TopLeft());

		assert(rcClip.bottom > rcClip.top);
		assert(rcClip.right > rcClip.left);

		// Render area
		CRect rIntersect;
		rIntersect.IntersectRect(&rectDstIn, &rcClip);
		rIntersect.OffsetRect(-rcClip.TopLeft());

		int nIntersectHeight = rIntersect.bottom - rIntersect.top;
		int nIntersectWidth = rIntersect.right - rIntersect.left;

		if (nIntersectHeight && nIntersectWidth)
		{

			// Create Lookup maps
			IW::CBuffer<DWORD> pLookupX(nIntersectWidth + 1);
			IW::CBuffer<DWORD> pLookupXDiff(nIntersectWidth + 1);


			//assert(r.left <= 0); // expected drawing rect should be less than 0

			// Get to start position
			int xx = rImage.left;
			int x = nWidthOut / 2;
			int xSrcLine = 0, xSrcLineLast = 0;

			for(; xx < 0; xx++)
			{
				x -= nWidthIn;

				if (x < nWidthIn) 
				{
					xSrcLineLast = xSrcLine;
					x += nWidthOut;					
					if (nWidthIn-1 > xSrcLine) xSrcLine++;					
				}				
			}

			// Start processing the line
			for (xx = 0; xx < nIntersectWidth; xx++)
			{
				x -= nWidthIn;

				int strength1 = MulDiv(nWidthOut - x, 255, nWidthOut);
				int strength2 = 255 - strength1;

				pLookupXDiff[xx] = MAKELONG(strength1, strength2);
				pLookupX[xx] = MAKELONG(xSrcLine, xSrcLineLast);

				assert(nWidthIn > xSrcLine); // Check source overflow					

				if (x < nWidthIn) 
				{
					xSrcLineLast = xSrcLine;
					x += nWidthOut;					
					if (nWidthIn-1 > xSrcLine) xSrcLine++;					
				}
			}

			// Did we set all the lookups?
			assert(xx == nIntersectWidth); // Check source overflow

			IW::CBuffer<COLORREF> pLineInNotScaled(nWidthIn + 1);
			IW::CBuffer<COLORREF> scaledStorage1(nIntersectWidth + 1);
			IW::CBuffer<COLORREF> scaledStorage2(nIntersectWidth + 1);

			// The two scaled lines are swapped as the loop walks down the image.
			LPCOLORREF pLineInScaled1 = scaledStorage1;
			LPCOLORREF pLineInScaled2 = scaledStorage2;

			int y = nHeightOut / 2;
			int ySrcLine = 0, ySrcLineLast = 0;
			int yy = rImage.top;

			// Find the start point
			for(; yy < 0; yy++)
			{
				y -= nHeightIn;

				if (y < nHeightIn) 
				{
					ySrcLineLast = ySrcLine;
					y += nHeightOut;					
					if (nHeightIn-1 > ySrcLine) ySrcLine++;				
				}
			}			

			// Get the two starting rasta lines
			pLock->RenderLine(pLineInNotScaled, ySrcLine, 0, nWidthIn);
			_blitter.InterpolateLine(pLineInScaled1, pLineInNotScaled, pLookupX, pLookupXDiff, nIntersectWidth);

			pLock->RenderLine(pLineInNotScaled, ySrcLineLast, 0, nWidthIn);
			_blitter.InterpolateLine(pLineInScaled2, pLineInNotScaled, pLookupX, pLookupXDiff, nIntersectWidth);

			// Render the following lines
			for(yy = 0; yy < nIntersectHeight; yy++)
			{
				y -= nHeightIn;

				int strength1 = MulDiv(nHeightOut - y, 0xff, nHeightOut);
				int strength2 = 255 - strength1;

				// Draw Line
				COLORREF *pLineOut = pLineBuffer;			

				if (bNeedBlend)
				{
					_blitter.InterpolateLine(pLineIn, pLineInScaled1, pLineInScaled2, strength1, strength2, nIntersectWidth);
					_blitter.RenderAlphaLine(pLineOut, pLineIn, nIntersectWidth);
				}
				else
				{
					_blitter.InterpolateLine(pLineOut, pLineInScaled1, pLineInScaled2, strength1, strength2, nIntersectWidth);
				}					

				if (y < nHeightIn) 
				{
					y += nHeightOut;					
					if (nHeightIn-1 > ySrcLine) ySrcLine++;				
					IW::Swap(pLineInScaled1, pLineInScaled2);
					pLock->RenderLine(pLineInNotScaled, ySrcLine, 0, nWidthIn);
					_blitter.InterpolateLine(pLineInScaled1, pLineInNotScaled, pLookupX, pLookupXDiff, nIntersectWidth);
				}

				_canvas.SetLine(pLineBuffer, yy + rIntersect.top, rIntersect.left, nIntersectWidth);
			}	
		}
	}
};

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

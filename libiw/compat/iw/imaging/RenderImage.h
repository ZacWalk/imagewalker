#pragma once

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

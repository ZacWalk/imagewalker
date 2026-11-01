#pragma once

#include "ImagingLock.h"

template<class TCanvas, class TBlitter>
class RenderImage
{
private:
	TCanvas &_canvas;
	TBlitter &_blitter;
	struct SourceLock
	{
		const IW::Page& page;
		const IW::IImageSurfaceLock* lock;
		void GetLine(COLORREF* pixels, int y, int x, int count) const
		{
			IW::ReadStraightLine(page, lock, pixels, y, x, count);
		}
		void RenderLine(COLORREF* pixels, int y, int x, int count) const
		{
			lock->RenderLine(pixels, y, x, count);
		}
	};

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
		if (nWidthIn <= 0 || nHeightIn <= 0 || nWidthOut <= 0 || nHeightOut <= 0) return;

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
		bool bNeedBlend = IW::HasSourceAlpha(page);

		IW::ConstIImageSurfaceLockPtr lock = page.GetSurfaceLock();
		const SourceLock source{page, lock};
		const auto* pLock = &source;

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
			DrawImageScaleDown(rectDstIn, rectSrcIn, nWidthIn, nWidthOut, nHeightIn, nHeightOut, bNeedBlend, pLock, sizeDst);
		}
	}

private:
	// Display blitters assume opaque rows; keep alpha sampling premultiplied until source-over.
	void CompositeLine(COLORREF* destination, const COLORREF* source, int count)
	{
		constexpr bool destinationAlpha = []
		{
			if constexpr (requires { TCanvas::storesAlpha; }) return TCanvas::storesAlpha;
			else return false;
		}();
		for (int x = 0; x < count; ++x)
		{
			const auto pixel = source[x];
			const unsigned alpha = IW::GetA(pixel);
			if (!alpha) continue;
			if (alpha == 255) { destination[x] = pixel; continue; }
			const auto background = destination[x];
			const unsigned backgroundAlpha = destinationAlpha ? IW::GetA(background) : 255;
			const unsigned backgroundWeight = backgroundAlpha * (255 - alpha);
			const unsigned denominator = alpha * 255 + backgroundWeight;
			const auto channel = [&](int shift)
			{
				return (((pixel >> shift & 255) * alpha * 255) +
					((background >> shift & 255) * backgroundWeight) + denominator / 2) / denominator;
			};
			destination[x] = IW::RGBA(channel(0), channel(8), channel(16), (denominator + 127) / 255);
		}
	}

	static void PremultiplyLine(COLORREF* pixels, int count)
	{
		for (int x = 0; x < count; ++x) pixels[x] = IW::PremultiplyStraight(pixels[x]);
	}

	static COLORREF InterpolateAlpha(COLORREF first, COLORREF second, unsigned firstWeight)
	{
		firstWeight = (std::min)(firstWeight, 255u);
		const unsigned secondWeight = 255 - firstWeight;
		COLORREF result{};
		for (int shift = 0; shift < 32; shift += 8)
			result |= (((first >> shift & 255) * firstWeight +
				(second >> shift & 255) * secondWeight + 127) / 255) << shift;
		return result;
	}

	void CompositeSums(COLORREF* destination, const DWORD* sums, int count)
	{
		using Sum = typename TBlitter::SUM;
		const auto* rows = reinterpret_cast<const Sum*>(sums);
		for (int x = 0; x < count; ++x)
		{
			const auto& sum = rows[x];
			if (!sum.cr) continue;
			const auto pixel = IW::UnpremultiplyStraight(IW::RGBA(
				(sum.r + sum.cr / 2) / sum.cr, (sum.g + sum.cr / 2) / sum.cr,
				(sum.b + sum.cr / 2) / sum.cr, (sum.a + sum.cr / 2) / sum.cr));
			CompositeLine(destination + x, &pixel, 1);
		}
	}

	void DrawImageNoScaleBlend(CRect& rDst, CRect& rSrc, const CSize& sizeSrc, const SourceLock* pLock)
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
			CompositeLine(pLineOut, pLineIn, sizeSrc.cx);
			_canvas.SetLine(pLineOut, y + rDst.top, rDst.left, sizeSrc.cx);
		}
	}

	void DrawImageNoScale(CRect& rDst, CRect& rSrc, const CSize& sizeSrc, const SourceLock* pLock)
	{
		IW::CBuffer<COLORREF> pLineBuffer(sizeSrc.cx + 1);

		for(int y = 0; y < sizeSrc.cy; y++)
		{
			pLock->RenderLine(pLineBuffer, y + rSrc.top, rSrc.left, sizeSrc.cx);
			_canvas.SetLine(pLineBuffer, y + rDst.top, rDst.left, sizeSrc.cx);
		}
	}

	void DrawImageScaleUp(CRect& rDst, CRect& rSrc, const int nSrcTop, const int nOffsetX, const int nWidthIn, const int nWidthOut, const int nOffsetY, const int nHeightIn, const int nHeightOut, bool bNeedBlend, const CSize& sizeSrc, const SourceLock* pLock, const CSize& sizeDst)
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
		IW::CBuffer<DWORD> pSum(static_cast<size_t>(sizeDst.cx) * 4);

		IW::CBuffer<COLORREF> pLineIn(sizeSrc.cx + 1);
		IW::CBuffer<COLORREF> pLineBuffer(sizeDst.cx + 1);

		int yOut = rDst.top - nOffsetY;
		int nCount = 0;
		int ySum = nHeightOut >> 1;
		int yy = 0;

		while (yy < nHeightIn)
		{
			std::fill_n(pSum.data(), pSum.size(), DWORD{0});

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
					{
						pLock->GetLine(pLineIn, yy + nSrcTop, rSrc.left, sizeSrc.cx);
						PremultiplyLine(pLineIn, sizeSrc.cx);
					}
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

					if (bNeedBlend) CompositeSums(pLineBuffer, pSum, sizeDst.cx);
					else _blitter.RenderScaleDownLine(pLineBuffer, pSum, sizeDst.cx);
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

	void DrawImageScaleDown(const CRect& rectDstIn, const CRect& rectSrcIn, const int nWidthIn, const int nWidthOut, const int nHeightIn, const int nHeightOut, bool bNeedBlend, const SourceLock* pLock, const CSize& sizeDst)
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

				const int alphaWeight = std::clamp(strength1, 0, 255);
				pLookupXDiff[xx] = bNeedBlend ? MAKELONG(alphaWeight, 255 - alphaWeight) :
					MAKELONG(strength1, strength2);
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

			const auto readRow = [&](COLORREF* target, int sourceY)
			{
				if (bNeedBlend)
				{
					pLock->GetLine(pLineInNotScaled, sourceY + rectSrcIn.top, rectSrcIn.left, nWidthIn);
					PremultiplyLine(pLineInNotScaled, nWidthIn);
					for (int x = 0; x < nIntersectWidth; ++x)
						target[x] = InterpolateAlpha(pLineInNotScaled[LOWORD(pLookupX[x])],
							pLineInNotScaled[HIWORD(pLookupX[x])], LOWORD(pLookupXDiff[x]));
				}
				else
				{
					pLock->RenderLine(pLineInNotScaled, sourceY + rectSrcIn.top, rectSrcIn.left, nWidthIn);
					_blitter.InterpolateLine(target, pLineInNotScaled, pLookupX, pLookupXDiff, nIntersectWidth);
				}
			};
			readRow(pLineInScaled1, ySrcLine);
			readRow(pLineInScaled2, ySrcLineLast);

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
					const auto weight = static_cast<unsigned>(std::clamp(strength1, 0, 255));
					for (int outputX = 0; outputX < nIntersectWidth; ++outputX)
						pLineIn[outputX] = IW::UnpremultiplyStraight(
							InterpolateAlpha(pLineInScaled1[outputX], pLineInScaled2[outputX], weight));
					_canvas.GetLine(pLineOut, yy + rIntersect.top, rIntersect.left, nIntersectWidth);
					CompositeLine(pLineOut, pLineIn, nIntersectWidth);
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
					readRow(pLineInScaled1, ySrcLine);
				}

				_canvas.SetLine(pLineBuffer, yy + rIntersect.top, rIntersect.left, nIntersectWidth);
			}
		}
	}
};

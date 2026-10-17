// ImageWalker by Zac Walker
//
// Purpose: Applies the edit stack to an image, plus the auto colour and auto
//          straighten that populate it.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

//
// ImageEdits: applying the edit stack to pixels.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "Imaging.h"
#include "ImagingStreams.h"
#include "ImagingEdits.h"

#include <cmath>

static const double s_pi = 3.14159265358979323846;

///////////////////////////////////////////////////////////////////////
// Serialisation

CSize ImageEdits::TransformedSize(CSize sizeIn) const
{
	CSize size = sizeIn;

	if (_rotate == 1 || _rotate == 3)
	{
		const int cx = size.cx;
		size.cx = size.cy;
		size.cy = cx;
	}

	// The warp keeps the frame it was given; only the crop changes the extent.
	return size;
}

namespace
{
	// Defined below, beside the matrix it needs: walks a rectangle out to the
	// edge of the warped picture one side at a time.
	CRect ShrinkToWarpedPicture(const ImageEdits &edits, CSize size, const CRect &rc);
}

CRect ImageEdits::CropBounds(CSize sizeIn) const
{
	const CSize size = TransformedSize(sizeIn);
	CRect rc(0, 0, size.cx, size.cy);

	if (size.cx < 2 || size.cy < 2)
		return rc;

	if (_straighten != 0)
	{
		const double angle = fabs(_straighten / 10.0) * s_pi / 180.0;
		const double sa = fabs(sin(angle));
		const double ca = fabs(cos(angle));

		const double w = size.cx;
		const double h = size.cy;
		const bool bWide = w >= h;
		const double longSide = bWide ? w : h;
		const double shortSide = bWide ? h : w;

		double wr, hr;

		// The largest-area upright rectangle inside a w x h rectangle turned by
		// this angle. Past the half-way case the limit is one pair of corners
		// rather than all four, which is what the first branch covers.
		if (shortSide <= 2.0 * sa * ca * longSide || fabs(sa - ca) < 1e-10)
		{
			const double half = 0.5 * shortSide;
			wr = bWide ? half / sa : half / ca;
			hr = bWide ? half / ca : half / sa;
		}
		else
		{
			const double cos2a = ca * ca - sa * sa;
			wr = (w * ca - h * sa) / cos2a;
			hr = (h * ca - w * sa) / cos2a;
		}

		const int cx = IW::Clamp(static_cast<int>(wr), 1, size.cx);
		const int cy = IW::Clamp(static_cast<int>(hr), 1, size.cy);

		rc = CRect(0, 0, cx, cy);
		rc.OffsetRect((size.cx - cx) / 2, (size.cy - cy) / 2);
	}

	// Perspective empties a band along one edge of the frame and a wedge at
	// each end of it, and the shape is not symmetric, so there is no closed
	// form to fold into the case above.
	if (_perspectiveH != 0 || _perspectiveV != 0)
		rc = ShrinkToWarpedPicture(*this, size, rc);

	return rc;
}

///////////////////////////////////////////////////////////////////////
// Tone and colour

namespace
{
	// A weight that peaks where a tone band lives, so the three band sliders
	// overlap the way a curve control does rather than acting as hard splits.
	double BandWeight(double v, double center, double width)
	{
		const double d = (v - center) / width;
		return exp(-d * d);
	}

	class ColorTransform
	{
	public:

		BYTE _lutR[256];
		BYTE _lutG[256];
		BYTE _lutB[256];
		double _saturation;
		double _vibrance;
		bool _hasLut;
		bool _hasSaturation;

		explicit ColorTransform(const ImageEdits& edits)
		{
			const double brightness = edits._brightness / 200.0;	// +/- 0.5
			const double contrast = 1.0 + edits._contrast / 100.0;	// 0 .. 2
			const double darks = edits._darks / 250.0;
			const double midtones = edits._midtones / 250.0;
			const double lights = edits._lights / 250.0;

			// Temperature warms towards red and cools towards blue; tint runs
			// the green axis against both, which is what the two of them are
			// for on a white balance control.
			const double t = edits._temperature / 400.0;
			const double g = edits._tint / 400.0;

			const double gainR = 1.0 + t + g;
			const double gainG = 1.0 - g;
			const double gainB = 1.0 - t + g;

			for (int i = 0; i < 256; i++)
			{
				double v = i / 255.0;

				v += darks * BandWeight(v, 0.18, 0.22);
				v += midtones * BandWeight(v, 0.50, 0.26);
				v += lights * BandWeight(v, 0.82, 0.22);

				v = 0.5 + (v - 0.5) * contrast;
				v += brightness;

				// Tone and white balance in the same table. Rounding to a byte
				// between them cost a count on every pixel, and clamping between
				// them flattened a highlight the balance was about to pull back
				// down into the range.
				_lutR[i] = IW::ByteClamp(static_cast<int>(v * gainR * 255.0 + 0.5));
				_lutG[i] = IW::ByteClamp(static_cast<int>(v * gainG * 255.0 + 0.5));
				_lutB[i] = IW::ByteClamp(static_cast<int>(v * gainB * 255.0 + 0.5));
			}

			_saturation = 1.0 + edits._saturation / 100.0;
			_vibrance = edits._vibrance / 100.0;

			_hasLut = edits._brightness != 0 || edits._contrast != 0 ||
				edits._darks != 0 || edits._midtones != 0 || edits._lights != 0 ||
				edits._temperature != 0 || edits._tint != 0;
			_hasSaturation = edits._saturation != 0 || edits._vibrance != 0;
		}

		void Process(LPCOLORREF pOut, IW::LPCCOLORREF pIn, int nLength) const
		{
			for (int x = 0; x < nLength; ++x)
			{
				const COLORREF c = pIn[x];

				int r = IW::GetR(c);
				int gr = IW::GetG(c);
				int b = IW::GetB(c);
				const int a = IW::GetA(c);

				if (_hasLut)
				{
					r = _lutR[r];
					gr = _lutG[gr];
					b = _lutB[b];
				}

				if (_hasSaturation)
				{
					const double luma = 0.299 * r + 0.587 * gr + 0.114 * b;

					double scale = _saturation;

					if (_vibrance != 0.0)
					{
						// Move the muted pixels most, which is what separates
						// vibrance from a flat saturation boost.
						const int mx = IW::Max(r, IW::Max(gr, b));
						const int mn = IW::Min(r, IW::Min(gr, b));
						const double current = mx == 0 ? 0.0 : (mx - mn) / static_cast<double>(mx);
						scale += _vibrance * (1.0 - current);
					}

					r = IW::ByteClamp(static_cast<int>(luma + (r - luma) * scale + 0.5));
					gr = IW::ByteClamp(static_cast<int>(luma + (gr - luma) * scale + 0.5));
					b = IW::ByteClamp(static_cast<int>(luma + (b - luma) * scale + 0.5));
				}

				pOut[x] = IW::RGBA(r, gr, b, a);
			}
		}
	};
}

bool IW::ApplyEditColor(const Image& imageIn, Image& imageOut, const ImageEdits& edits, IStatus* pStatus)
{
	const ColorTransform transform(edits);

	IW::IterateImageMetaData(imageIn, imageOut, pStatus);

	for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
	{
		const PixelFormat pf = pageIn->GetPixelFormat();
		const int nWidth = pageIn->GetWidth();
		const int nHeight = pageIn->GetHeight();
		const int nStorageWidth = IW::CalcStorageWidth(nWidth, pf);

		Page pageOut = imageOut.CreatePage(pageIn->GetPageRect(), pf);

		const int nPaletteEntries = pf.NumberOfPaletteEntries();

		if (nPaletteEntries > 0)
		{
			transform.Process(pageOut.GetPalette(), pageIn->GetPalette(), nPaletteEntries);

			for (int y = 0; y < nHeight; y++)
			{
				IW::MemCopy(pageOut.GetBitmapLine(y), pageIn->GetBitmapLine(y), nStorageWidth);
			}
		}
		else
		{
			ConstIImageSurfaceLockPtr pLockIn = pageIn->GetSurfaceLock();
			IImageSurfaceLockPtr pLockOut = pageOut.GetSurfaceLock();

			IW::CBuffer<COLORREF> pLine(nWidth);

			for (int y = 0; y < nHeight; y++)
			{
				// GetLine, not RenderLine: RenderLine composites the pixel over
				// the page background and returns it with a zero alpha byte, so
				// writing the result back made the whole page transparent.
				// Without a palette the two are the same for every other format.
				pLockIn->GetLine(pLine, y, 0, nWidth);
				transform.Process(pLine, pLine, nWidth);
				pLockOut->SetLine(pLine, y, 0, nWidth);

				pStatus->Progress(y, nHeight);

				if (pStatus->QueryCancel())
					return false;
			}
		}

		pageOut.CopyExtraInfo(*pageIn);
	}

	imageOut.Normalize();

	return true;
}

///////////////////////////////////////////////////////////////////////
// Straighten and perspective

namespace
{
	// A 3x3 projective matrix, row major. Straighten and the two perspective
	// sliders compose into one of these so the image is resampled once.
	struct Matrix3
	{
		double m[9];

		static Matrix3 Identity()
		{
			Matrix3 r = {{1, 0, 0, 0, 1, 0, 0, 0, 1}};
			return r;
		}

		Matrix3 operator*(const Matrix3& o) const
		{
			Matrix3 r = {{0}};

			for (int i = 0; i < 3; i++)
				for (int j = 0; j < 3; j++)
					for (int k = 0; k < 3; k++)
						r.m[i * 3 + j] += m[i * 3 + k] * o.m[k * 3 + j];

			return r;
		}

		bool Invert(Matrix3& out) const
		{
			const double a = m[0], b = m[1], c = m[2];
			const double d = m[3], e = m[4], f = m[5];
			const double g = m[6], h = m[7], i = m[8];

			const double A = e * i - f * h;
			const double B = -(d * i - f * g);
			const double C = d * h - e * g;
			const double det = a * A + b * B + c * C;

			if (fabs(det) < 1e-12)
				return false;

			const double id = 1.0 / det;

			out.m[0] = A * id;
			out.m[1] = -(b * i - c * h) * id;
			out.m[2] = (b * f - c * e) * id;
			out.m[3] = B * id;
			out.m[4] = (a * i - c * g) * id;
			out.m[5] = -(a * f - c * d) * id;
			out.m[6] = C * id;
			out.m[7] = -(a * h - b * g) * id;
			out.m[8] = (a * e - b * d) * id;

			return true;
		}
	};

	Matrix3 BuildWarp(const ImageEdits& edits, int cx, int cy)
	{
		// Everything happens about the centre, so the frame does not drift when
		// only one slider moves.
		const double halfX = cx / 2.0;
		const double halfY = cy / 2.0;

		Matrix3 toOrigin = Matrix3::Identity();
		toOrigin.m[2] = -halfX;
		toOrigin.m[5] = -halfY;

		Matrix3 fromOrigin = Matrix3::Identity();
		fromOrigin.m[2] = halfX;
		fromOrigin.m[5] = halfY;

		const double angle = (edits._straighten / 10.0) * s_pi / 180.0;
		const double cs = cos(angle);
		const double sn = sin(angle);

		Matrix3 rotate = Matrix3::Identity();
		rotate.m[0] = cs;
		rotate.m[1] = -sn;
		rotate.m[3] = sn;
		rotate.m[4] = cs;

		Matrix3 perspective = Matrix3::Identity();

		if (halfX > 0) perspective.m[6] = (edits._perspectiveH / 100.0) * 0.5 / halfX;
		if (halfY > 0) perspective.m[7] = (edits._perspectiveV / 100.0) * 0.5 / halfY;

		// A projective row makes the image shrink towards one edge; scale it
		// back up about the centre so the frame stays filled.
		const double scale = 1.0 + 0.5 * (fabs(edits._perspectiveH) + fabs(edits._perspectiveV)) / 100.0;

		Matrix3 zoom = Matrix3::Identity();
		zoom.m[0] = scale;
		zoom.m[4] = scale;

		return fromOrigin * (perspective * (zoom * (rotate * toOrigin)));
	}

	// Does the warped picture reach this point of the frame? The inverse map is
	// what the resampler uses, so this asks the empty corners the same question
	// they are going to be asked a pixel at a time.
	bool IsCovered(const Matrix3& inverse, double x, double y, int cx, int cy)
	{
		const double w = inverse.m[6] * x + inverse.m[7] * y + inverse.m[8];

		if (fabs(w) < 1e-9)
			return false;

		const double sx = (inverse.m[0] * x + inverse.m[1] * y + inverse.m[2]) / w;
		const double sy = (inverse.m[3] * x + inverse.m[4] * y + inverse.m[5]) / w;

		return sx >= 0 && sy >= 0 && sx <= cx && sy <= cy;
	}

	bool IsRectCovered(const Matrix3& inverse, const CRect& rc, int cx, int cy)
	{
		constexpr int nSteps = 16;

		for (int i = 0; i <= nSteps; i++)
		{
			const double x = rc.left + rc.Width() * i / static_cast<double>(nSteps);
			const double y = rc.top + rc.Height() * i / static_cast<double>(nSteps);

			if (!IsCovered(inverse, x, rc.top, cx, cy) ||
				!IsCovered(inverse, x, rc.bottom, cx, cy) ||
				!IsCovered(inverse, rc.left, y, cx, cy) ||
				!IsCovered(inverse, rc.right, y, cx, cy))
				return false;
		}

		return true;
	}

	CRect ShrinkToWarpedPicture(const ImageEdits& edits, CSize size, const CRect& rc)
	{
		Matrix3 inverse;

		if (!BuildWarp(edits, size.cx, size.cy).Invert(inverse))
			return rc;

		// Every one of these warps keeps the centre of the frame where it is,
		// so that is the one point known to be on the picture to grow from.
		const CPoint pt = rc.CenterPoint();
		CRect rcOut(pt.x, pt.y, pt.x + 1, pt.y + 1);

		if (!IsRectCovered(inverse, rcOut, size.cx, size.cy))
			return rcOut;

		// One side at a time, each as far as it will go. Moving all four
		// together would stop at whichever corner ran out first and give away
		// the rest of the picture with it.
		auto grow = [&](LONG& edge, LONG nTarget)
		{
			const LONG nStart = edge;
			const int nRange = static_cast<int>(nTarget - nStart);

			edge = nTarget;

			if (IsRectCovered(inverse, rcOut, size.cx, size.cy))
				return;

			int lo = 0, hi = 1000;	// permille of the way out to nTarget

			while (hi - lo > 1)
			{
				const int nMid = (lo + hi) / 2;
				edge = nStart + MulDiv(nRange, nMid, 1000);

				if (IsRectCovered(inverse, rcOut, size.cx, size.cy))
					lo = nMid;
				else
					hi = nMid;
			}

			edge = nStart + MulDiv(nRange, lo, 1000);
		};

		grow(rcOut.left, rc.left);
		grow(rcOut.right, rc.right);
		grow(rcOut.top, rc.top);
		grow(rcOut.bottom, rc.bottom);

		return rcOut;
	}

	// One lerp of two packed RGBA pixels, t in 0..256. The two byte pairs are
	// held apart so a 16 bit product cannot carry out of its own channel.
	inline COLORREF Lerp(COLORREF a, COLORREF b, unsigned t)
	{
		const unsigned it = 256 - t;

		const COLORREF lo = ((((a & 0x00FF00FF) * it) + ((b & 0x00FF00FF) * t)) >> 8) & 0x00FF00FF;
		const COLORREF hi = ((((a >> 8) & 0x00FF00FF) * it) + (((b >> 8) & 0x00FF00FF) * t)) & 0xFF00FF00;

		return lo | hi;
	}

	// 255 / a, in 16.16. A premultiplied channel is never greater than the
	// alpha it was multiplied by, so channel * table[a] cannot overflow.
	struct UnpremultiplyTable
	{
		unsigned v[256];

		UnpremultiplyTable()
		{
			v[0] = 0;

			for (int i = 1; i < 256; i++)
				v[i] = (255u << 16) / static_cast<unsigned>(i);
		}
	};

	inline COLORREF Unpremultiply(COLORREF c)
	{
		static const UnpremultiplyTable table;

		const unsigned a = IW::GetA(c);

		if (a == 0)
			return 0;

		if (a == 255)
			return c;

		const unsigned k = table.v[a];

		return IW::RGBA(static_cast<int>((IW::GetR(c) * k + 0x8000) >> 16),
		                static_cast<int>((IW::GetG(c) * k + 0x8000) >> 16),
		                static_cast<int>((IW::GetB(c) * k + 0x8000) >> 16),
		                static_cast<int>(a));
	}

	// Bilinear, in 16.16 source coordinates, over PREMULTIPLIED source pixels.
	//
	// Premultiplied is the fix for the artifact: interpolating straight alpha
	// averages the RGB of a fully transparent pixel into its neighbours, so any
	// image whose transparent area carries a flat colour -- which is most PNGs
	// with an alpha channel -- bled that colour along its own edges as soon as
	// the straighten slider moved.
	//
	// A tap outside the picture contributes nothing at all rather than being
	// clamped to the nearest edge pixel, which would end the wedge on a fully
	// opaque pixel of smeared edge colour instead of one pixel of alpha ramp.
	inline COLORREF SampleWarp(const COLORREF* p, int cx, int cy, int x16, int y16)
	{
		const int x0 = x16 >> 16;
		const int y0 = y16 >> 16;

		if (x0 < -1 || y0 < -1 || x0 >= cx || y0 >= cy)
			return 0;

		const unsigned fx = (x16 >> 8) & 0xFF;
		const unsigned fy = (y16 >> 8) & 0xFF;

		COLORREF c00 = 0, c01 = 0, c10 = 0, c11 = 0;

		if (x0 >= 0 && y0 >= 0 && x0 + 1 < cx && y0 + 1 < cy)
		{
			const COLORREF* q = p + static_cast<size_t>(y0) * cx + x0;

			c00 = q[0];
			c01 = q[1];
			c10 = q[cx];
			c11 = q[cx + 1];
		}
		else
		{
			const bool bLeft = x0 >= 0 && x0 < cx;
			const bool bRight = x0 + 1 >= 0 && x0 + 1 < cx;
			const bool bTop = y0 >= 0 && y0 < cy;
			const bool bBottom = y0 + 1 >= 0 && y0 + 1 < cy;

			const COLORREF* pTop = p + static_cast<size_t>(bTop ? y0 : 0) * cx;
			const COLORREF* pBottom = p + static_cast<size_t>(bBottom ? y0 + 1 : 0) * cx;

			if (bTop && bLeft) c00 = pTop[x0];
			if (bTop && bRight) c01 = pTop[x0 + 1];
			if (bBottom && bLeft) c10 = pBottom[x0];
			if (bBottom && bRight) c11 = pBottom[x0 + 1];
		}

		return Unpremultiply(Lerp(Lerp(c00, c01, fx), Lerp(c10, c11, fx), fy));
	}

	// 16.16, rounded down so the fractional part is always positive.
	inline int ToFixed(double v)
	{
		return static_cast<int>(floor(v * 65536.0));
	}

	// 16.16 cannot hold a coordinate past +/-32768, and the row is walked by
	// repeated addition, so a row that reaches out that far is stepped in
	// double instead. Only a warp of an image tens of thousands of pixels wide
	// can get here.
	constexpr double s_fixedLimit = 30000.0;

	bool ApplyWarp(const IW::Image& imageIn, IW::Image& imageOut, const ImageEdits& edits, IW::IStatus* pStatus)
	{
		IW::IterateImageMetaData(imageIn, imageOut, pStatus);

		for (auto pageIn = imageIn.Pages.begin(); pageIn != imageIn.Pages.end(); ++pageIn)
		{
			const int cx = pageIn->GetWidth();
			const int cy = pageIn->GetHeight();

			if (cx <= 0 || cy <= 0)
				continue;

			Matrix3 inverse;

			if (!BuildWarp(edits, cx, cy).Invert(inverse))
				return false;

			// A projective map has no bounded source band per output row, so the
			// page is materialised rather than streamed.
			IW::CBuffer<COLORREF> source(static_cast<size_t>(cx) * cy);

			{
				IW::ConstIImageSurfaceLockPtr pLockIn = pageIn->GetSurfaceLock();

				// The warp writes a PF32Alpha page, so it has to read real alpha.
				// RenderLine composites it away; GetLine cannot resolve a palette,
				// and a palettised page has no alpha channel to lose.
				const bool bAlpha = pageIn->GetPixelFormat().HasAlpha();

				for (int y = 0; y < cy; y++)
				{
					COLORREF *pLine = source.data() + static_cast<size_t>(y) * cx;

					if (bAlpha)
					{
						pLockIn->GetLine(pLine, y, 0, cx);

						// The sampler works premultiplied; an opaque source
						// already is, so only this branch has to pay for it.
						for (int x = 0; x < cx; x++)
						{
							const COLORREF c = pLine[x];
							const unsigned a = IW::GetA(c);

							if (a != 255)
								pLine[x] = IW::RGBA(static_cast<int>((IW::GetR(c) * a + 127) / 255),
								                    static_cast<int>((IW::GetG(c) * a + 127) / 255),
								                    static_cast<int>((IW::GetB(c) * a + 127) / 255),
								                    static_cast<int>(a));
						}
					}
					else
					{
						// Neither call promises an alpha byte for a format that
						// has none, and the sampler averages whatever is there.
						pLockIn->RenderLine(pLine, y, 0, cx);

						for (int x = 0; x < cx; x++)
							pLine[x] |= 0xFF000000;
					}
				}
			}

			// The page rect, not just its size: a frame of a multi-page image
			// sits at an offset, and dropping it moves the frame to the corner.
			IW::Page pageOut = imageOut.CreatePage(pageIn->GetPageRect(), IW::PixelFormat::PF32Alpha);
			IW::IImageSurfaceLockPtr pLockOut = pageOut.GetSurfaceLock();

			IW::CBuffer<COLORREF> pLine(cx);

			// Straighten on its own leaves the bottom row of the matrix alone,
			// and that is the case the slider drives: no divide, and the source
			// coordinate can be stepped rather than evaluated.
			const bool bAffine = fabs(inverse.m[6]) < 1e-15 && fabs(inverse.m[7]) < 1e-15;

			for (int y = 0; y < cy; y++)
			{
				const double dy = y + 0.5;

				// Everything below is the row's value at x = 0.5 plus a step.
				const double nx = inverse.m[0] * 0.5 + inverse.m[1] * dy + inverse.m[2];
				const double ny = inverse.m[3] * 0.5 + inverse.m[4] * dy + inverse.m[5];
				const double nw = inverse.m[6] * 0.5 + inverse.m[7] * dy + inverse.m[8];

				bool bStepped = bAffine && fabs(nw) > 1e-9;

				if (bStepped)
				{
					const double invw = 1.0 / nw;
					const double sx = nx * invw - 0.5;
					const double sy = ny * invw - 0.5;
					const double ex = sx + inverse.m[0] * invw * (cx - 1);
					const double ey = sy + inverse.m[3] * invw * (cx - 1);

					bStepped = fabs(sx) < s_fixedLimit && fabs(sy) < s_fixedLimit &&
						fabs(ex) < s_fixedLimit && fabs(ey) < s_fixedLimit;

					if (bStepped)
					{
						int x16 = ToFixed(sx);
						int y16 = ToFixed(sy);

						const int dx16 = ToFixed(inverse.m[0] * invw);
						const int dy16 = ToFixed(inverse.m[3] * invw);

						for (int x = 0; x < cx; x++, x16 += dx16, y16 += dy16)
							pLine[x] = SampleWarp(source, cx, cy, x16, y16);
					}
				}

				if (!bStepped)
				{
					double px = nx, py = ny, pw = nw;

					for (int x = 0; x < cx; x++,
					     px += inverse.m[0], py += inverse.m[3], pw += inverse.m[6])
					{
						if (fabs(pw) < 1e-9)
						{
							pLine[x] = 0;
							continue;
						}

						const double invw = 1.0 / pw;
						const double sx = px * invw - 0.5;
						const double sy = py * invw - 0.5;

						pLine[x] = (fabs(sx) < s_fixedLimit && fabs(sy) < s_fixedLimit)
							           ? SampleWarp(source, cx, cy, ToFixed(sx), ToFixed(sy))
							           : 0;
					}
				}

				pLockOut->SetLine(pLine, y, 0, cx);

				pStatus->Progress(y, cy);

				if (pStatus->QueryCancel())
					return false;
			}

			pageOut.CopyExtraInfo(*pageIn);
		}

		imageOut.Normalize();

		return true;
	}

	bool ApplyRotate(const IW::Image& imageIn, IW::Image& imageOut, int quarterTurns, IW::IStatus* pStatus)
	{
		switch (quarterTurns & 3)
		{
		case 1: return IW::Rotate90(imageIn, imageOut, pStatus);
		case 2: return IW::Rotate180(imageIn, imageOut, pStatus);
		case 3: return IW::Rotate270(imageIn, imageOut, pStatus);
		default: break;
		}

		imageOut = imageIn;
		return true;
	}
}

bool IW::ApplyEditGeometry(const Image& imageIn, Image& imageOut, const ImageEdits& edits, IStatus* pStatus)
{
	if (!edits.HasGeometry())
	{
		imageOut = imageIn;
		return true;
	}

	Image rotated;

	if (!ApplyRotate(imageIn, rotated, edits._rotate, pStatus))
		return false;

	if (edits.HasWarp())
	{
		Image warped;

		if (!ApplyWarp(rotated, warped, edits, pStatus))
			return false;

		rotated = warped;
	}

	if (edits.HasCrop())
	{
		CRect rc;

		if (rc.IntersectRect(rotated.GetBoundingRect(), edits._crop) && !rc.IsRectEmpty())
			return Crop(rotated, imageOut, rc, pStatus);
	}

	imageOut = rotated;
	return true;
}

bool IW::ApplyEdits(const Image& imageIn, Image& imageOut, const ImageEdits& edits, IStatus* pStatus)
{
	if (edits.IsEmpty())
	{
		imageOut = imageIn;
		return true;
	}

	if (!edits.HasColor())
		return ApplyEditGeometry(imageIn, imageOut, edits, pStatus);

	if (!edits.HasGeometry())
		return ApplyEditColor(imageIn, imageOut, edits, pStatus);

	Image geometry;

	if (!ApplyEditGeometry(imageIn, geometry, edits, pStatus))
		return false;

	return ApplyEditColor(geometry, imageOut, edits, pStatus);
}

///////////////////////////////////////////////////////////////////////
// Auto adjustments

namespace
{
	// The value at which nPercent of the histogram's weight lies below.
	int HistogramPercentile(const int* pCounts, int nTotal, double percent)
	{
		const int target = static_cast<int>(nTotal * percent);
		int running = 0;

		for (int i = 0; i < IW::Histogram::MaxValue; i++)
		{
			running += pCounts[i];

			if (running >= target)
				return i;
		}

		return 255;
	}
}

void IW::AutoColor(const Image& imageIn, ImageEdits& edits)
{
	if (imageIn.IsEmpty())
		return;

	Histogram histogram;
	imageIn.GetHistogram(histogram);

	int total = 0;

	for (int i = 0; i < Histogram::MaxValue; i++)
		total += histogram._g[i];

	if (total == 0)
		return;

	// Stretch the middle 98% of the luminance range to fill the scale, which is
	// a brightness offset plus a contrast gain in this model.
	const int low = HistogramPercentile(histogram._g, total, 0.01);
	const int high = HistogramPercentile(histogram._g, total, 0.99);

	if (high > low)
	{
		const double gain = 255.0 / (high - low);
		const double mid = (low + high) / 2.0;

		edits._contrast = IW::Clamp(static_cast<int>((gain - 1.0) * 100.0),
		                            static_cast<int>(ImageEdits::ColorMin),
		                            static_cast<int>(ImageEdits::ColorMax));

		// The curve stretches about mid grey and adds brightness after it, so
		// the offset that carries mid up to mid grey has to be multiplied by
		// the gain that actually gets applied -- clamped, not asked for.
		// Without it every underexposed picture came back underexposed.
		const double applied = 1.0 + edits._contrast / 100.0;

		edits._brightness = IW::Clamp(static_cast<int>(applied * (127.5 - mid) / 127.5 * 100.0),
		                              static_cast<int>(ImageEdits::ColorMin),
		                              static_cast<int>(ImageEdits::ColorMax));
	}

	// Grey-world white balance across the red and blue means.
	double meanR = 0, meanG = 0, meanB = 0;

	for (int i = 0; i < Histogram::MaxValue; i++)
	{
		meanR += static_cast<double>(i) * histogram._r[i];
		meanG += static_cast<double>(i) * histogram._g[i];
		meanB += static_cast<double>(i) * histogram._b[i];
	}

	meanR /= total;
	meanG /= total;
	meanB /= total;

	if (meanR > 0 && meanB > 0)
	{
		const double warmth = (meanB - meanR) / IW::Max(1, static_cast<int>(meanG));

		edits._temperature = IW::Min(ImageEdits::ColorMax,
		                             IW::Max(ImageEdits::ColorMin, static_cast<int>(warmth * 200.0)));
	}
}

namespace
{
	// One strong edge pixel, and how strong.
	struct EdgePoint
	{
		float x;
		float y;
		float weight;
	};

	// How sharply the points fall into bands at this angle. Points on the same
	// straight line share a band once the angle matches the line, so the sum of
	// squares peaks at the angle that undoes the tilt.
	//
	// bHorizontal picks which family of lines is being tested: bands stacked up
	// the picture (edges that run across it) or across it (edges that run up).
	double BandSharpness(const std::vector<EdgePoint>& points, double cs, double sn,
	                     bool bHorizontal, std::vector<double>& bins, int nOffset)
	{
		if (points.empty())
			return 0;

		std::fill(bins.begin(), bins.end(), 0.0);

		const int nBins = static_cast<int>(bins.size());

		for (const EdgePoint& p : points)
		{
			const double r = bHorizontal ? (p.y * cs - p.x * sn) : (p.x * cs + p.y * sn);
			const int bin = static_cast<int>(r + 0.5) + nOffset;

			if (bin >= 0 && bin < nBins)
				bins[bin] += p.weight;
		}

		double score = 0;

		for (int i = 0; i < nBins; i++)
			score += bins[i] * bins[i];

		return score;
	}
}

void IW::AutoStraighten(const Image& imageIn, ImageEdits& edits)
{
	if (imageIn.IsEmpty())
		return;

	const Page page = imageIn.GetFirstPage();

	const int cx = page.GetWidth();
	const int cy = page.GetHeight();

	if (cx < 8 || cy < 8)
		return;

	// A gradient direction taken from a 3x3 neighbourhood cannot resolve a
	// fraction of a degree: on a quantised edge tilted by 1.5 degrees the
	// staircase reads as exactly horizontal for 38 pixels at a time, so a
	// histogram of Sobel angles puts every vote in the zero bucket and the
	// button did nothing at all. What does resolve it is the whole line: rotate
	// the edge points through each candidate angle and keep the one that packs
	// them into the fewest bands.
	const int step = IW::Max(1, IW::Max(cx, cy) / 384);

	const int cxs = cx / step;
	const int cys = cy / step;

	if (cxs < 3 || cys < 3)
		return;

	IW::CBuffer<int> luma(static_cast<size_t>(cxs) * cys);

	{
		ConstIImageSurfaceLockPtr pLock = page.GetSurfaceLock();
		IW::CBuffer<COLORREF> pLine(cx);

		for (int y = 0; y < cys; y++)
		{
			pLock->RenderLine(pLine, y * step, 0, cx);

			for (int x = 0; x < cxs; x++)
			{
				const COLORREF c = pLine[x * step];
				luma[static_cast<size_t>(y) * cxs + x] =
					(IW::GetR(c) * 77 + IW::GetG(c) * 151 + IW::GetB(c) * 28) >> 8;
			}
		}
	}

	// Edges that run across the picture and edges that run up it are two
	// separate votes on the same tilt, so they are collected separately and
	// scored against their own family of lines.
	std::vector<EdgePoint> across, upright;

	for (int y = 1; y < cys - 1; y++)
	{
		for (int x = 1; x < cxs - 1; x++)
		{
			const int* p = luma.data() + static_cast<size_t>(y) * cxs + x;

			const int gx = p[-cxs + 1] + 2 * p[1] + p[cxs + 1] - p[-cxs - 1] - 2 * p[-1] - p[cxs - 1];
			const int gy = p[cxs - 1] + 2 * p[cxs] + p[cxs + 1] - p[-cxs - 1] - 2 * p[-cxs] - p[-cxs + 1];

			const int magnitude = abs(gx) + abs(gy);

			if (magnitude < 120)
				continue;

			const EdgePoint pt = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(magnitude)};

			// The gradient is perpendicular to the edge, so a mostly vertical
			// gradient belongs to a line running across the picture.
			if (abs(gy) > abs(gx))
				across.push_back(pt);
			else
				upright.push_back(pt);
		}
	}

	if (across.size() + upright.size() < 64)
		return;

	const int nOffset = cxs + cys;
	std::vector<double> bins(static_cast<size_t>(nOffset) * 2 + 2, 0.0);

	int best = 0;
	double bestScore = 0;

	// +/- 5 degrees in tenths. Anything larger is a deliberate rotation, which
	// is what the two quarter-turn buttons are for.
	for (int tenth = -50; tenth <= 50; tenth++)
	{
		const double angle = (tenth / 10.0) * s_pi / 180.0;
		const double cs = cos(angle);
		const double sn = sin(angle);

		const double score = BandSharpness(across, cs, sn, true, bins, nOffset) +
			BandSharpness(upright, cs, sn, false, bins, nOffset);

		if (score > bestScore)
		{
			bestScore = score;
			best = tenth;
		}
	}

	edits._straighten = -best;
}

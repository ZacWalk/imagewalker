// ImageWalker by Zac Walker
//
// Purpose: Pixel format conversion - every depth up to 32-bit RGBA, and the
//          running sums the scaler uses.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

//
// IW::Image : implementation file
//
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "ImagingLock.h"
#include "iw/channelaverage.h"

using IW::LockDetail::masks1;
using IW::LockDetail::masks2;
using IW::LockDetail::shift2;

void IW::IndexesToRGBA(LPCOLORREF pBitsOut, const int nSize, LPCCOLORREF pPalette)
{
	for (int x = 0; x < nSize; x++)
	{
		pBitsOut[x] = pPalette[pBitsOut[x]] | 0xFF000000;
	}
}

void IW::SumLineTo32(LPBYTE pLine, const RGBSUM* ps, int cx)
{
	for (int i = 0; i < cx; i++)
	{
		pLine[0] = ChannelAverage(ps->r, ps->c, true);
		pLine[1] = ChannelAverage(ps->g, ps->c, true);
		pLine[2] = ChannelAverage(ps->b, ps->c, true);
		// Alpha has its own sample count (transparent pixels omit RGB).
		pLine[3] = ChannelAverage(ps->a, ps->ac, true);

		++ps;
		pLine += 4;
	}
}

void IW::SumLineTo16(LPBYTE pLine, const RGBSUM* ps, int cx)
{
	assert((reinterpret_cast<UINT_PTR>(pLine) & 0x03) == 0); // Should be aligned to align??

	int denom, denom2;
	const RGBSUM* ps2;

	LPBYTE pLineEnd = pLine + (cx * 2);

	while (pLine < (pLineEnd - 2))
	{
		// blend with white
		denom = Max(ps->c, 1u);

		ps2 = ps + 1;
		denom2 = Max(ps2->c, 1u);

		*((int*)pLine) =
			(((ps->r / denom) >> 3) & 0x0000001F) |
			(((ps->g / denom) << 2) & 0x000003E0) |
			(((ps->b / denom) << 7) & 0x00007C00) |
			(((ps2->r / denom2) << 13) & 0x001F0000) |
			(((ps2->g / denom2) << 18) & 0x03E00000) |
			(((ps2->b / denom2) << 23) & 0x7C000000);

		ps += 2;
		pLine += 4;
	}

	// May be a last one!!
	while (pLine < pLineEnd)
	{
		denom = Max(ps->c, 1u);

		*((short*)pLine) =
			(((ps->r / denom) >> (3)) & 0x0000001F) |
			(((ps->g / denom) << (2)) & 0x000003E0) |
			(((ps->b / denom) << (7)) & 0x00007C00);

		ps++;
		pLine += 2;
	}
}

void IW::ToSums(LPRGBSUM pSum, LPDWORD pLineIn, int cxOut, int cxIn, bool isHighQuality, bool bFirstY)
{
	int x = (cxOut >> 1) + cxIn;

	LPCRGBSUM pSumEnd = pSum + cxOut;
	LPCDWORD pLineEnd = pLineIn + cxIn;
	DWORD c, a;

	if (isHighQuality)
	{
		while (pLineIn < pLineEnd)
		{
			assert(pSum < pSumEnd); // Check for out buffer overflow

			c = *pLineIn++;
			a = GetA(c);

			if (a)
			{
				pSum->r += GetR(c);
				pSum->g += GetG(c);
				pSum->b += GetB(c);
				pSum->c++;
				pSum->a += a;
			}

			pSum->ac++;
			x -= cxOut;
			if (x < cxOut)
			{
				pSum++;
				x += cxIn;
			}
		}
	}
	else
	{
		if (bFirstY)
		{
			while (pLineIn < pLineEnd)
			{
				assert(pSum < pSumEnd); // Check for out buffer overflow

				c = *pLineIn++;
				pSum->r += GetR(c);
				pSum->g += GetG(c);
				pSum->b += GetB(c);
				pSum->c++;

				x -= cxOut;
				if (x < cxOut)
				{
					pSum++;
					x += cxIn;
				}
			}
		}
		else
		{
			for (int i = 0; i < cxOut; i++)
			{
				int xx = (i * cxIn) / cxOut;
				assert(xx < cxIn);

				c = pLineIn[xx];

				pSum->r += GetR(c);
				pSum->g += GetG(c);
				pSum->b += GetB(c);
				pSum->c++;

				pSum++;
			}
		}
	}
}


void IW::Convert1to32(LPCOLORREF pBitsOut, LPCBYTE pBitsInIn, const int nStart, const int nSize)
{
	const int nSizeWithStart = nSize + nStart;
	auto pBitsIn = (LPCDWORD)pBitsInIn;
	DWORD c = pBitsIn[nStart >> 5];
	int xn;

	for (int xx = nStart; xx < nSizeWithStart; xx++)
	{
		xn = xx & 31;
		if (xn == 0)
		{
			c = pBitsIn[xx >> 5];
		}

		*pBitsOut++ = (c & masks1[xn]) ? 1 : 0;
	}
}

void IW::Convert1to32(LPCOLORREF pBitsOut, LPCBYTE pBitsInIn, const int nStart, const int nSize, LPCCOLORREF pPalette)
{
	Convert1to32(pBitsOut, pBitsInIn, nStart, nSize);
	IndexesToRGBA(pBitsOut, nSize, pPalette);
}

void IW::Convert2to32(LPCOLORREF pBitsOut, LPCBYTE pBitsIn, const int nStart, const int nSize)
{
	const int nSizeWithStart = nSize + nStart;

	for (int xx = nStart; xx < nSizeWithStart; xx++)
	{
		BYTE b = (pBitsIn[xx >> 2] & masks2[xx & 3]) >> shift2[xx & 3];
		*pBitsOut++ = b;
	}
}

void IW::Convert2to32(LPCOLORREF pBitsOut, LPCBYTE pBitsIn, const int nStart, const int nSize, LPCCOLORREF pPalette)
{
	Convert2to32(pBitsOut, pBitsIn, nStart, nSize);
	IndexesToRGBA(pBitsOut, nSize, pPalette);
}

void IW::Convert4to32(LPCOLORREF pBitsOut, LPCBYTE pBitsIn, const int nStart, const int nSize)
{
	const int nSizeWithStart = nSize + nStart;
	int c;

	for (int xx = nStart; xx < nSizeWithStart; xx++)
	{
		c = (xx & 1) ? pBitsIn[xx >> 1] & 0x0f : pBitsIn[xx >> 1] >> 4;
		*pBitsOut++ = c;
	}
}

void IW::Convert4to32(LPCOLORREF pBitsOut, LPCBYTE pBitsIn, const int nStart, const int nSize, LPCCOLORREF pPalette)
{
	Convert4to32(pBitsOut, pBitsIn, nStart, nSize);
	IndexesToRGBA(pBitsOut, nSize, pPalette);
}

void IW::Convert8to32(LPCOLORREF pBitsOut, LPCBYTE pBitsIn, const int nStart, const int nSize)
{
	const int nSizeWithStart = nSize + nStart;
	pBitsIn += nStart;

	for (int n = nStart; n < nSizeWithStart; n++)
	{
		*pBitsOut++ = *pBitsIn++;
	}
}

void IW::Convert8to32(LPCOLORREF pBitsOut, LPCBYTE pBitsIn, const int nStart, const int nSize, LPCCOLORREF pPalette)
{
	Convert8to32(pBitsOut, pBitsIn, nStart, nSize);
	IndexesToRGBA(pBitsOut, nSize, pPalette);
}

void IW::Convert8Alphato32(LPCOLORREF pBitsOut, LPCBYTE pBitsIn, const int nStart, const int nSize,
                           LPCCOLORREF pPalette)
{
	const int nSizeWithStart = nSize + nStart;
	pBitsIn += nStart;

	for (int n = nStart; n < nSizeWithStart; n++)
	{
		*pBitsOut++ = pPalette[*pBitsIn++];
	}
}

void IW::Convert8GrayScaleto32(LPCOLORREF pBitsOut, LPCBYTE pBitsIn, const int nStart, const int nSize)
{
	int c;
	const int nSizeWithStart = nSize + nStart;
	pBitsIn += nStart;

	for (int n = nStart; n < nSizeWithStart; n++)
	{
		c = *pBitsIn++;
		// Opaque, like every other ConvertNto32. Alpha 0 here made the
		// high-quality downscale accumulator, which weights by alpha, drop
		// every sample.
		*pBitsOut++ = RGB(c, c, c) | 0xFF000000;
	}
}

void IW::Convert555to32(LPCOLORREF pBitsOut, LPCBYTE pBitsInIn, const int nStart, const int nSize)
{
	// Get a pointer to the end
	const COLORREF* pBitsOutEnd = pBitsOut + nSize;
	auto pBitsIn = (LPDWORD)(pBitsInIn + (nStart * 2));
	DWORD c;

	while (pBitsOut < pBitsOutEnd)
	{
		c = *pBitsIn++;
		*pBitsOut++ = RGB555to888(c) | 0xFF000000;

		if (pBitsOut >= pBitsOutEnd)
			return;

		c >>= 16;
		*pBitsOut++ = RGB555to888(c) | 0xFF000000;
	}
}

void IW::Convert565to32(LPCOLORREF pBitsOut, LPCBYTE pBitsInIn, const int nStart, const int nSize)
{
	// Get a pointer to the end
	const COLORREF* pBitsOutEnd = pBitsOut + nSize;
	LPCBYTE pBitsIn = pBitsInIn + nStart * 2;
	DWORD c;

	// Move to an aligned bit part of memory
	while ((0 != (reinterpret_cast<UINT_PTR>(pBitsOut) & 3)) && (pBitsOut < pBitsOutEnd))
	{
		c = *reinterpret_cast<const unsigned short*>(pBitsIn);
		*pBitsOut++ = RGB565to888(c) | 0xFF000000;
		pBitsIn += 2;
	}

	while (pBitsOut < pBitsOutEnd)
	{
		c = *((LPDWORD)pBitsIn);
		*pBitsOut++ = RGB565to888(c) | 0xFF000000;

		if (pBitsOut >= pBitsOutEnd)
			return;

		c >>= 16;
		*pBitsOut++ = RGB565to888(c) | 0xFF000000;
		pBitsIn += 4;
	}
}

void IW::Convert24to32(LPCOLORREF pBitsOut, LPCBYTE pBitsIn, const int nStart, const int nSize)
{
	// Get a pointer to the end
	const COLORREF* pBitsOutEnd = pBitsOut + nSize;

	// Find start location
	if (nStart)
		pBitsIn += 3 * nStart;
	// Move to an aligned bit part of memory
	auto pOut = (LPBYTE)pBitsOut;
	// Align to 8 byte boundary
	while ((0 != (reinterpret_cast<UINT_PTR>(pBitsIn) & 3)) && (pOut < (LPCBYTE)pBitsOutEnd))
	{
		*pOut++ = *pBitsIn++;
		*pOut++ = *pBitsIn++;
		*pOut++ = *pBitsIn++;
		*pOut++ = static_cast<BYTE>(0xff);
	}

	pBitsOut = (LPDWORD)pOut;

	{
		// Continue processing
		UINT nPixel1, nPixel2;
		auto pIn = (LPCDWORD)pBitsIn;
		const COLORREF* pBitsOutEnd2 = pBitsOutEnd - 4;

		while (pBitsOut < pBitsOutEnd2)
		{
			// DWORD 1
			nPixel1 = *pIn++;

			// pixel 1
			*pBitsOut++ = (nPixel1 & 0x00ffffff) | 0xff000000;

			// pixel 2
			nPixel2 = (nPixel1 >> 24) & 0xff;

			// DWORD 2
			nPixel1 = *pIn++;

			nPixel2 |= (nPixel1 << 8) & 0x00ffff00;

			*pBitsOut++ = nPixel2 | 0xff000000;
			// pixel 3
			nPixel2 = (nPixel1 >> 16) & 0x0000ffff;

			// DWORD 3
			nPixel1 = *pIn++;
			nPixel2 |= (nPixel1 << 16) & 0x00ff0000;
			*pBitsOut++ = nPixel2 | 0xff000000;

			// pixel 4
			*pBitsOut++ = (nPixel1 >> 8) | 0xff000000;
		}
		pBitsIn = (LPCBYTE)pIn;
		pOut = (LPBYTE)pBitsOut;
	}

	// Complete any incomplete work
	while (pOut < (LPCBYTE)pBitsOutEnd)
	{
		*pOut++ = *pBitsIn++;
		*pOut++ = *pBitsIn++;
		*pOut++ = *pBitsIn++;
		*pOut++ = static_cast<BYTE>(0xff);
	}
}

////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////

void IW::ConvertARGBtoABGR(LPBYTE pBitsOutX, LPCBYTE pBitsInX, const int nSize)
{
	auto pBitsIn = (LPDWORD)pBitsInX;
	auto pBitsOut = (LPDWORD)pBitsOutX;
	const LPDWORD pBitsOutEnd = pBitsOut + nSize;

	while (pBitsOut < pBitsOutEnd)
	{
		*pBitsOut++ = SwapRB(*pBitsIn++);
	}
}


void IW::ConvertRGBtoBGR(LPBYTE pBitsOut, LPCBYTE pBitsIn, const int nSize)
{
	const LPBYTE pBitsOutEnd = pBitsOut + (nSize * 3);
	BYTE r, g, b;

	while (pBitsOut < pBitsOutEnd)
	{
		r = *pBitsIn++;
		g = *pBitsIn++;
		b = *pBitsIn++;

		*pBitsOut++ = b;
		*pBitsOut++ = g;
		*pBitsOut++ = r;
	}
}

void IW::ConvertYCbCrtoBGR(LPBYTE pBitsOut, LPCBYTE pBitsIn, const int nSize)
{
	for (int i = 0; i < nSize; i++)
	{
		const int Y = pBitsIn[0];
		const int Cb = pBitsIn[1] - 128;
		const int Cr = pBitsIn[2] - 128;

		pBitsOut[0] = ByteClamp(Y + ((454 * Cb) >> 8));
		pBitsOut[1] = ByteClamp(Y - ((88 * Cb + 183 * Cr) >> 8));
		pBitsOut[2] = ByteClamp(Y + ((359 * Cr) >> 8));

		pBitsIn += 3;
		pBitsOut += 3;
	}
}

void IW::ConvertCMYKtoBGR(LPBYTE pBitsOut, LPCBYTE pBitsIn, const int nSize, bool bInverted)
{
	for (int i = 0; i < nSize; i++)
	{
		// Work in "how much light gets through", which is what an inverted file
		// already stores and what a plain one is 255 minus.
		int c = pBitsIn[0];
		int m = pBitsIn[1];
		int y = pBitsIn[2];
		int k = pBitsIn[3];

		if (!bInverted)
		{
			c = 255 - c;
			m = 255 - m;
			y = 255 - y;
			k = 255 - k;
		}

		pBitsOut[0] = static_cast<BYTE>((y * k) / 255);
		pBitsOut[1] = static_cast<BYTE>((m * k) / 255);
		pBitsOut[2] = static_cast<BYTE>((c * k) / 255);

		pBitsIn += 4;
		pBitsOut += 3;
	}
}

void IW::ConvertCIELABtoBGR(LPBYTE pBitsOut, LPCBYTE pBitsIn, const int nSize)
{
	int x = 0;
	int R, G, B;
	int L, a, b;

	for (int i = 0; i < nSize; i++)
	{
		// TIFF CIELAB samples are stored L*, a*, b*.
		L = pBitsIn[x + 0];
		a = pBitsIn[x + 1];
		b = pBitsIn[x + 2];

		LABtoRGB(L, a, b, R, G, B);

		// The destination is a DIB scanline: byte 0 is blue.
		// LABtoRGB clamps all three channels to [0, 255].
		pBitsOut[x + 0] = static_cast<BYTE>(B);
		pBitsOut[x + 1] = static_cast<BYTE>(G);
		pBitsOut[x + 2] = static_cast<BYTE>(R);
		x += 3;
	}
}

void IW::ConvertICCLABtoBGR(LPBYTE pBitsOut, LPCBYTE pBitsIn, const int nSize)
{
	ConvertCIELABtoBGR(pBitsOut, pBitsIn, nSize);
}

void IW::ConvertITULABtoBGR(LPBYTE pBitsOut, LPCBYTE pBitsIn, const int nSize)
{
	ConvertCIELABtoBGR(pBitsOut, pBitsIn, nSize);
}

void IW::ConvertLOGLtoBGR(LPBYTE pBitsOut, LPCBYTE pBitsIn, const int nSize)
{
}

void IW::ConvertLOGLUVtoBGR(LPBYTE pBitsOut, LPCBYTE pBitsIn, const int nSize)
{
}


////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////

void IW::RGBtoHSL(int R, int G, int B, int& H, int& S, int& L)
{
	constexpr int HSLMAX = 255; /* H,L, and S vary over 0-HSLMAX */
	constexpr int RGBMAX = 255; /* R,G, and B vary over 0-RGBMAX */
	/* HSLMAX BEST IF DIVISIBLE BY 6 */
	/* RGBMAX, HSLMAX must each fit in a BYTE. */
	/* Hue is undefined if Saturation is 0 (grey-scale) */
	/* This value determines where the Hue scrollbar is */
	/* initially set for achromatic colors */
	constexpr int UNDEFINED = (HSLMAX * 2 / 3);

	WORD Rdelta, Gdelta, Bdelta; /* intermediate value: % of spread from max*/
	const int cMax = Max(Max(R, G), B); /* calculate lightness */
	const int cMin = Min(Min(R, G), B);

	L = static_cast<BYTE>((((cMax + cMin) * HSLMAX) + RGBMAX) / (2 * RGBMAX));

	if (cMax == cMin) /* r=g=b --> achromatic case */
	{
		S = 0; /* saturation */
		H = UNDEFINED; /* hue */
	}
	else
	{
		/* chromatic case */
		if (L <= (HSLMAX / 2)) /* saturation */
			S = static_cast<BYTE>((((cMax - cMin) * HSLMAX) + ((cMax + cMin) / 2)) / (cMax + cMin));
		else
			S = static_cast<BYTE>((((cMax - cMin) * HSLMAX) + ((2 * RGBMAX - cMax - cMin) / 2)) / (2 * RGBMAX - cMax -
				cMin));
		/* hue */
		Rdelta = static_cast<WORD>((((cMax - R) * (HSLMAX / 6)) + ((cMax - cMin) / 2)) / (cMax - cMin));
		Gdelta = static_cast<WORD>((((cMax - G) * (HSLMAX / 6)) + ((cMax - cMin) / 2)) / (cMax - cMin));
		Bdelta = static_cast<WORD>((((cMax - B) * (HSLMAX / 6)) + ((cMax - cMin) / 2)) / (cMax - cMin));

		if (R == cMax)
			H = static_cast<BYTE>(Bdelta - Gdelta);
		else if (G == cMax)
			H = static_cast<BYTE>((HSLMAX / 3) + Rdelta - Bdelta);
		else /* B == cMax */
			H = static_cast<BYTE>(((2 * HSLMAX) / 3) + Gdelta - Rdelta);

		if (H < 0) H += HSLMAX;
		if (H > HSLMAX) H -= HSLMAX;
	}
}


////////////////////////////////////////////////////////////////////////////////
static float HueToRGB(float n1, float n2, float hue)
{
	//<F. Livraghi> fixed implementation for HSL2RGB routine
	float rValue;

	if (hue > 360)
		hue = hue - 360;
	else if (hue < 0)
		hue = hue + 360;

	if (hue < 60)
		rValue = n1 + (n2 - n1) * hue / 60.0f;
	else if (hue < 180)
		rValue = n2;
	else if (hue < 240)
		rValue = n1 + (n2 - n1) * (240 - hue) / 60;
	else
		rValue = n1;

	return rValue;
}

////////////////////////////////////////////////////////////////////////////////


void IW::HSLtoRGB(int H, int S, int L, int& r, int& g, int& b)
{
	//<F. Livraghi> fixed implementation for HSL2RGB routine
	const float h = static_cast<float>(H) * 360.0f / 255.0f;
	const float s = static_cast<float>(S) / 255.0f;
	const float l = static_cast<float>(L) / 255.0f;
	const float m2 = (l <= 0.5) ? l * (1 + s) : l + s - l * s;
	const float m1 = 2 * l - m2;

	if (s == 0)
	{
		r = g = b = static_cast<BYTE>(l * 255.0f);
	}
	else
	{
		r = static_cast<int>(HueToRGB(m1, m2, h + 120) * 255.0f);
		g = static_cast<int>(HueToRGB(m1, m2, h) * 255.0f);
		b = static_cast<int>(HueToRGB(m1, m2, h - 120) * 255.0f);
	}
}


void IW::RGBtoLAB(int R, int G, int B, int& L, int& a, int& b)
{
	// Convert between RGB and CIE-Lab color spaces
	// Uses ITU-R recommendation BT.709 with D65 as reference white.
	// algorithm contributed by "Mark A. Ruzon" <ruzon@CS.Stanford.EDU>
	double fX, fY, fZ;
	double X = 0.412453 * R + 0.357580 * G + 0.180423 * B;
	double Y = 0.212671 * R + 0.715160 * G + 0.072169 * B;
	double Z = 0.019334 * R + 0.119193 * G + 0.950227 * B;

	X /= (255 * 0.950456);
	Y /= 255;
	Z /= (255 * 1.088754);

	if (Y > 0.008856)
	{
		fY = pow(Y, 1.0 / 3.0);
		L = static_cast<int>(116.0 * fY - 16.0 + 0.5);
	}
	else
	{
		fY = 7.787 * Y + 16.0 / 116.0;
		L = static_cast<int>(903.3 * Y + 0.5);
	}

	if (X > 0.008856)
		fX = pow(X, 1.0 / 3.0);
	else
		fX = 7.787 * X + 16.0 / 116.0;

	if (Z > 0.008856)
		fZ = pow(Z, 1.0 / 3.0);
	else
		fZ = 7.787 * Z + 16.0 / 116.0;

	a = static_cast<int>(500.0 * (fX - fY) + 0.5);
	b = static_cast<int>(200.0 * (fY - fZ) + 0.5);
}

void IW::LABtoRGB(int L, int a, int b, int& R, int& G, int& B)
{
	// Convert between RGB and CIE-Lab color spaces
	// Uses ITU-R recommendation BT.709 with D65 as reference white.
	// algorithm contributed by "Mark A. Ruzon" <ruzon@CS.Stanford.EDU>
	double X, Y, Z;
	double fY = pow((L + 16.0) / 116.0, 3.0);
	if (fY < 0.008856)
		fY = L / 903.3;
	Y = fY;

	if (fY > 0.008856)
		fY = pow(fY, 1.0 / 3.0);
	else
		fY = 7.787 * fY + 16.0 / 116.0;

	double fX = a / 500.0 + fY;
	if (fX > 0.206893)
		X = pow(fX, 3.0);
	else
		X = (fX - 16.0 / 116.0) / 7.787;

	double fZ = fY - b / 200.0;
	if (fZ > 0.206893)
		Z = pow(fZ, 3.0);
	else
		Z = (fZ - 16.0 / 116.0) / 7.787;

	X *= (0.950456 * 255);
	Y *= 255;
	Z *= (1.088754 * 255);

	int RR = static_cast<int>(3.240479 * X - 1.537150 * Y - 0.498535 * Z + 0.5);
	int GG = static_cast<int>(-0.969256 * X + 1.875992 * Y + 0.041556 * Z + 0.5);
	int BB = static_cast<int>(0.055648 * X - 0.204043 * Y + 1.057311 * Z + 0.5);

	R = RR < 0 ? 0 : RR > 255 ? 255 : RR;
	G = GG < 0 ? 0 : GG > 255 ? 255 : GG;
	B = BB < 0 ? 0 : BB > 255 ? 255 : BB;
}

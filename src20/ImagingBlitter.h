// ImageWalker by Zac Walker
//
// Purpose: The scalar reference blitter. ImagingBlitterSimd.h derives the
//          SSE2 and AVX2 ones from it, and must match it bit for bit.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "stdafx.h"


//////////////////////////////////////////////////////////////////////////////////
	/// Blitter interface

class CBlitter
{
public:


	CBlitter() {};
	~CBlitter() {};

	static DWORD Make4(DWORD dw)
	{
		DWORD dwOut = ((dw & 0xff) << 8) | (dw & 0xff);
		return dwOut | (dwOut << 16);
	}


	inline void RenderAlphaLine(COLORREF *pLineOut, const COLORREF *pLineIn, int nLength)
	{
		DWORD cIn, cOut, aIn, aOut, r, g, b, a;
		const COLORREF *pLineInEnd = pLineIn + nLength;

		while(pLineIn < pLineInEnd)
		{
			cIn = *pLineIn;
			aIn = IW::GetA(cIn);
			aOut = 0xff - aIn;

			if (aIn == 0xff)
			{
				*pLineOut = cIn;
			}
			else if (aOut != 0x00)
			{					
				cOut = *pLineOut;			

				r = (IW::GetR(cIn) * aIn) + (IW::GetR(cOut) * aOut);
				g = (IW::GetG(cIn) * aIn) + (IW::GetG(cOut) * aOut);
				b = (IW::GetB(cIn) * aIn) + (IW::GetB(cOut) * aOut);
				a = (IW::GetA(cIn) * aIn) + (IW::GetA(cOut) * aOut);

				*pLineOut = IW::RGBA((r >> 8), (g >> 8), (b >> 8), (a >> 8));
			}

			++pLineIn;
			++pLineOut;
		}
	}


	void InterpolateLine(COLORREF *pLineOut, COLORREF *pLineInScaled1, COLORREF *pLineInScaled2, const DWORD &a1, const DWORD &a2, const int nWidth)
	{
		const COLORREF *pLineOutEnd = pLineOut + nWidth;
		DWORD c1, c2;

		while(pLineOut < pLineOutEnd)
		{
			c1 = *pLineInScaled1++;
			c2 = *pLineInScaled2++;

			*pLineOut++ = IW::RGBA(
				(((IW::GetR(c1) * a1) + (IW::GetR(c2) * a2)) >> 0x8), 
				(((IW::GetG(c1) * a1) + (IW::GetG(c2) * a2)) >> 0x8), 
				(((IW::GetB(c1) * a1) + (IW::GetB(c2) * a2)) >> 0x8), 
				0xff);
		}
	}

	void InterpolateLine(COLORREF *pLineOut, COLORREF *pLineIn, DWORD *pLookupX, DWORD *pLookupXDiff, int nSize)
	{
		DWORD c1, c2, a1, a2;

		for(int x = 0; x < nSize; x++)
		{
			c1 = pLineIn[LOWORD(pLookupX[x])];
			c2 = pLineIn[HIWORD(pLookupX[x])];

			a1 = LOWORD(pLookupXDiff[x]);
			a2 = HIWORD(pLookupXDiff[x]);

			pLineOut[x] = IW::RGBA(
				(((IW::GetR(c1) * a1) + (IW::GetR(c2) * a2)) >> 0x8), 
				(((IW::GetG(c1) * a1) + (IW::GetG(c2) * a2)) >> 0x8), 
				(((IW::GetB(c1) * a1) + (IW::GetB(c2) * a2)) >> 0x8), 
				0xff);
		}
	}

	// Average two pixels per channel: the low bit of each channel is masked off
	// so the halves can be summed without carrying between channels.
	void Blend32(LPDWORD pDst, LPDWORD pSrc, int nSize)
	{
		for (int i = 0; i < nSize; ++i)
		{
			pDst[i] = ((pDst[i] & 0xFEFEFEFE) >> 1) + ((pSrc[i] & 0xFEFEFEFE) >> 1);
		}
	}

	void BlendColor32(LPDWORD pDst, COLORREF clr, int nSize)
	{
		const DWORD half = (static_cast<DWORD>(clr) & 0xFEFEFEFE) >> 1;

		for (int i = 0; i < nSize; ++i)
		{
			pDst[i] = ((pDst[i] & 0xFEFEFEFE) >> 1) + half;
		}
	}

	void Fill32(LPDWORD pDst, COLORREF clr, int nSize)
	{
		for (int i = 0; i < nSize; ++i)
		{
			pDst[i] = clr;
		}
	}



#include <pshpack1.h>

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


	void ScaleDownLine(LPDWORD pSumIn, COLORREF *pLineIn, const int cxIn, const int cxOut)
	{
		int x = (cxOut >> 1) + cxIn;
		DWORD c;
		SUM *pSum = (SUM*)pSumIn;
		COLORREF *pLineEnd = pLineIn + cxIn;

		while(pLineIn < pLineEnd) 
		{
			if (pSum->cr < 0xff)
			{
				c = *pLineIn++;

				pSum->r += static_cast<WORD>(IW::GetR(c));
				pSum->g += static_cast<WORD>(IW::GetG(c));
				pSum->b += static_cast<WORD>(IW::GetB(c));
				pSum->a += static_cast<WORD>(IW::GetA(c));
				pSum->cr++;
				pSum->cg++;
				pSum->cb++;
				pSum->ca++;
				
			}
			else
			{
				++pLineIn;
			}

			x -= cxOut;
			if (x < cxOut) 
			{
				++pSum;
				x += cxIn;
			}
		}
	}

	//#pragma optimize( "", off )

	void ScaleDownLineFast(LPDWORD pSumIn, COLORREF *pLineIn, const int cxIn, const int cxOut)
	{
		int x = (cxOut >> 1) + cxIn;
		DWORD c;
		SUM *pSum = (SUM*)pSumIn;
		COLORREF *pLineEnd = pLineIn + cxIn;
		bool bFirst;

		while(pLineIn < pLineEnd) 
		{
			bFirst = true;

			if (bFirst && pSum->cr < 0xff)
			{
				c = *pLineIn;

				pSum->r += static_cast<WORD>(IW::GetR(c));
				pSum->g += static_cast<WORD>(IW::GetG(c));
				pSum->b += static_cast<WORD>(IW::GetB(c));
				pSum->a += static_cast<WORD>(IW::GetA(c));
				pSum->cr++;
				pSum->cg++;
				pSum->cb++;
				pSum->ca++;

				bFirst = false;				
			}

			++pLineIn;
			x -= cxOut;

			if (x < cxOut) 
			{
				++pSum;
				x += cxIn;
			}
		}
	}


	//#pragma optimize( "", on )

	void RenderScaleDownLine(COLORREF *pLineOut, LPDWORD pSumIn, const int nWidth)
	{
		const SUM *ps = (SUM*)pSumIn;

		DWORD rr, gg, bb, aa, aOut, c;

		for(int x = 0; x < nWidth; x++)
		{		
			/*rr = ps->r / ps->cr;
			gg = ps->g / ps->cg;
			bb = ps->b / ps->cb;
			aa = ps->a / ps->ca;		*/

			// An output column that received no input sample leaves cr at zero.
			c = ps->cr ? ps->cr : 1;

			rr = ps->r / c;
			gg = ps->g / c;
			bb = ps->b / c;
			aa = ps->a / c;

			aOut = 0xff - aa;

			if (aa == 0x00)
			{
				// Nothing to composite. The blend below divides by 256, so running
				// it took a count off every channel of the destination instead.
				++pLineOut;
				++ps;
				continue;
			}

			if (aa > 0xf0)
			{
				c = IW::RGBA(rr, gg, bb, 0xff);
			}
			else if (aOut != 0x00)
			{					
				c = *pLineOut;

				rr = (rr * aa) + (IW::GetR(c) * aOut);
				gg = (gg * aa) + (IW::GetG(c) * aOut);
				bb = (bb * aa) + (IW::GetB(c) * aOut);
				// The other three lines are channel*a + dst*(255-a); this one had the
				// source channel replaced by aa itself, squaring the alpha.
				aa = (aa << 8) + (IW::GetA(c) * aOut);

				c = IW::RGBA((rr >> 8), (gg >> 8), (bb >> 8), (aa >> 8));
			}

			*pLineOut++ = c;			
			++ps;								
		}
	}

};




#include "ImagingBlitterSimd.h"

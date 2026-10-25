// ImageWalker by Zac Walker
//
// Purpose: SSE2 and AVX2 blitters, chosen at run time by ImagingSimd.cpp.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

//
// ImagingBlitterSimd.h: SSE2 and AVX2 replacements for the 1998 MMX blitter.
//
// The original tree carried one hand-written MMX blitter in x86 assembly.
// MSVC will not assemble __asm for x64, so on the only architecture this
// builds for, CBlitterMMX inherited every method from CBlitter and did
// nothing at all -- the runtime check in front of it chose between two
// identical scalar paths.
//
// Every method here MUST be bit exact with the CBlitter it derives from.
// Which one runs is a property of the machine, so a picture that differed
// between them would make a saved file depend on the CPU that wrote it.
// `BlittersAgree` in test.cpp compares all three over pseudo-random data.

#pragma once

#include <immintrin.h>

class CBlitterSSE2 : public CBlitter
{
public:

	void RenderAlphaLine(COLORREF *pLineOut, const COLORREF *pLineIn, int nLength)
	{
		int i = 0;

		for (; i + 4 <= nLength; i += 4)
		{
			const __m128i src = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pLineIn + i));
			const __m128i dst = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pLineOut + i));
			_mm_storeu_si128(reinterpret_cast<__m128i*>(pLineOut + i), Composite(dst, src));
		}

		if (i < nLength)
			CBlitter::RenderAlphaLine(pLineOut + i, pLineIn + i, nLength - i);
	}

	void InterpolateLine(COLORREF *pLineOut, COLORREF *pLineInScaled1, COLORREF *pLineInScaled2,
	                     const DWORD &a1, const DWORD &a2, const int nWidth)
	{
		const __m128i w1 = _mm_set1_epi16(static_cast<short>(a1));
		const __m128i w2 = _mm_set1_epi16(static_cast<short>(a2));
		const __m128i opaque = _mm_set1_epi32(static_cast<int>(0xFF000000));

		int x = 0;

		for (; x + 4 <= nWidth; x += 4)
		{
			const __m128i c1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pLineInScaled1 + x));
			const __m128i c2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pLineInScaled2 + x));

			const __m128i r = Weight(c1, w1, w1, c2, w2, w2);
			_mm_storeu_si128(reinterpret_cast<__m128i*>(pLineOut + x), _mm_or_si128(r, opaque));
		}

		if (x < nWidth)
			CBlitter::InterpolateLine(pLineOut + x, pLineInScaled1 + x, pLineInScaled2 + x, a1, a2, nWidth - x);
	}

	void InterpolateLine(COLORREF *pLineOut, COLORREF *pLineIn, DWORD *pLookupX, DWORD *pLookupXDiff, int nSize)
	{
		const __m128i opaque = _mm_set1_epi32(static_cast<int>(0xFF000000));

		int x = 0;

		for (; x + 4 <= nSize; x += 4)
		{
			const DWORD i0 = pLookupX[x], i1 = pLookupX[x + 1];
			const DWORD i2 = pLookupX[x + 2], i3 = pLookupX[x + 3];

			// No gather before AVX2, so the two taps are read one pixel at a
			// time and assembled; the arithmetic below is still four wide.
			const __m128i c1 = _mm_setr_epi32(
				static_cast<int>(pLineIn[LOWORD(i0)]), static_cast<int>(pLineIn[LOWORD(i1)]),
				static_cast<int>(pLineIn[LOWORD(i2)]), static_cast<int>(pLineIn[LOWORD(i3)]));

			const __m128i c2 = _mm_setr_epi32(
				static_cast<int>(pLineIn[HIWORD(i0)]), static_cast<int>(pLineIn[HIWORD(i1)]),
				static_cast<int>(pLineIn[HIWORD(i2)]), static_cast<int>(pLineIn[HIWORD(i3)]));

			const __m128i diff = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pLookupXDiff + x));
			const __m128i a1 = _mm_and_si128(diff, _mm_set1_epi32(0xFFFF));
			const __m128i a2 = _mm_srli_epi32(diff, 16);

			const __m128i r = Weight(c1, SpreadLo(a1), SpreadHi(a1),
			                         c2, SpreadLo(a2), SpreadHi(a2));

			_mm_storeu_si128(reinterpret_cast<__m128i*>(pLineOut + x), _mm_or_si128(r, opaque));
		}

		if (x < nSize)
			CBlitter::InterpolateLine(pLineOut + x, pLineIn, pLookupX + x, pLookupXDiff + x, nSize - x);
	}

	void Blend32(LPDWORD pDst, LPDWORD pSrc, int nSize)
	{
		const __m128i mask = _mm_set1_epi32(static_cast<int>(0xFEFEFEFE));

		int i = 0;

		for (; i + 4 <= nSize; i += 4)
		{
			const __m128i d = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pDst + i));
			const __m128i s = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pSrc + i));

			// The low bit of every byte is masked off first, so shifting the
			// whole 32-bit lane cannot carry between channels.
			const __m128i h = _mm_srli_epi32(_mm_and_si128(d, mask), 1);
			const __m128i k = _mm_srli_epi32(_mm_and_si128(s, mask), 1);

			_mm_storeu_si128(reinterpret_cast<__m128i*>(pDst + i), _mm_add_epi32(h, k));
		}

		if (i < nSize)
			CBlitter::Blend32(pDst + i, pSrc + i, nSize - i);
	}

	void BlendColor32(LPDWORD pDst, COLORREF clr, int nSize)
	{
		const __m128i mask = _mm_set1_epi32(static_cast<int>(0xFEFEFEFE));
		const __m128i half = _mm_srli_epi32(_mm_and_si128(_mm_set1_epi32(static_cast<int>(clr)), mask), 1);

		int i = 0;

		for (; i + 4 <= nSize; i += 4)
		{
			const __m128i d = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pDst + i));
			const __m128i h = _mm_srli_epi32(_mm_and_si128(d, mask), 1);

			_mm_storeu_si128(reinterpret_cast<__m128i*>(pDst + i), _mm_add_epi32(h, half));
		}

		if (i < nSize)
			CBlitter::BlendColor32(pDst + i, clr, nSize - i);
	}

	void Fill32(LPDWORD pDst, COLORREF clr, int nSize)
	{
		const __m128i v = _mm_set1_epi32(static_cast<int>(clr));

		int i = 0;

		for (; i + 4 <= nSize; i += 4)
			_mm_storeu_si128(reinterpret_cast<__m128i*>(pDst + i), v);

		for (; i < nSize; ++i)
			pDst[i] = clr;
	}

protected:

	// (c1 * w1 + c2 * w2) >> 8 per byte. Both callers keep w1 + w2 <= 256 and
	// every channel <= 255, so the 16-bit intermediate cannot exceed 65280 and
	// the shifted result cannot exceed 255 -- which is why the saturating pack
	// gives the same answer as CBlitter's cast to BYTE.
	static __m128i Weight(__m128i c1, __m128i w1Lo, __m128i w1Hi,
	                      __m128i c2, __m128i w2Lo, __m128i w2Hi)
	{
		const __m128i zero = _mm_setzero_si128();

		const __m128i lo = _mm_srli_epi16(
			_mm_add_epi16(_mm_mullo_epi16(_mm_unpacklo_epi8(c1, zero), w1Lo),
			              _mm_mullo_epi16(_mm_unpacklo_epi8(c2, zero), w2Lo)), 8);

		const __m128i hi = _mm_srli_epi16(
			_mm_add_epi16(_mm_mullo_epi16(_mm_unpackhi_epi8(c1, zero), w1Hi),
			              _mm_mullo_epi16(_mm_unpackhi_epi8(c2, zero), w2Hi)), 8);

		return _mm_packus_epi16(lo, hi);
	}

	// One 16-bit weight per pixel, copied across that pixel's four channels.
	static __m128i SpreadLo(__m128i w)
	{
		const __m128i t = _mm_shufflelo_epi16(w, _MM_SHUFFLE(2, 2, 0, 0));
		return _mm_unpacklo_epi16(t, t);
	}

	static __m128i SpreadHi(__m128i w)
	{
		const __m128i t = _mm_shufflehi_epi16(w, _MM_SHUFFLE(2, 2, 0, 0));
		return _mm_unpackhi_epi16(t, t);
	}

	static __m128i Composite(__m128i dst, __m128i src)
	{
		const __m128i zero = _mm_setzero_si128();
		const __m128i ones = _mm_set1_epi8(static_cast<char>(0xFF));

		// Alpha is byte 3; copy it over the other three so one multiply covers
		// the whole pixel, the alpha channel included.
		__m128i a = _mm_srli_epi32(src, 24);
		a = _mm_or_si128(a, _mm_slli_epi32(a, 8));
		a = _mm_or_si128(a, _mm_slli_epi32(a, 16));

		const __m128i ia = _mm_sub_epi8(ones, a);

		// a + (255 - a) == 255, so src*a + dst*(255-a) <= 255*255 and the
		// 16-bit lanes hold it.
		const __m128i blended = Weight(src, _mm_unpacklo_epi8(a, zero), _mm_unpackhi_epi8(a, zero),
		                               dst, _mm_unpacklo_epi8(ia, zero), _mm_unpackhi_epi8(ia, zero));

		// CBlitter copies a fully opaque pixel through untouched rather than
		// running it through the >>8, which would cost it a count.
		const __m128i opaque = _mm_cmpeq_epi8(a, ones);

		return _mm_or_si128(_mm_and_si128(opaque, src), _mm_andnot_si128(opaque, blended));
	}
};

class CBlitterAVX2 : public CBlitterSSE2
{
public:

	void RenderAlphaLine(COLORREF *pLineOut, const COLORREF *pLineIn, int nLength)
	{
		int i = 0;

		for (; i + 8 <= nLength; i += 8)
		{
			const __m256i src = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pLineIn + i));
			const __m256i dst = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pLineOut + i));
			_mm256_storeu_si256(reinterpret_cast<__m256i*>(pLineOut + i), Composite8(dst, src));
		}

		_mm256_zeroupper();

		if (i < nLength)
			CBlitterSSE2::RenderAlphaLine(pLineOut + i, pLineIn + i, nLength - i);
	}

	void InterpolateLine(COLORREF *pLineOut, COLORREF *pLineInScaled1, COLORREF *pLineInScaled2,
	                     const DWORD &a1, const DWORD &a2, const int nWidth)
	{
		const __m256i w1 = _mm256_set1_epi16(static_cast<short>(a1));
		const __m256i w2 = _mm256_set1_epi16(static_cast<short>(a2));
		const __m256i opaque = _mm256_set1_epi32(static_cast<int>(0xFF000000));

		int x = 0;

		for (; x + 8 <= nWidth; x += 8)
		{
			const __m256i c1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pLineInScaled1 + x));
			const __m256i c2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pLineInScaled2 + x));

			const __m256i r = Weight8(c1, w1, w1, c2, w2, w2);
			_mm256_storeu_si256(reinterpret_cast<__m256i*>(pLineOut + x), _mm256_or_si256(r, opaque));
		}

		_mm256_zeroupper();

		if (x < nWidth)
			CBlitterSSE2::InterpolateLine(pLineOut + x, pLineInScaled1 + x, pLineInScaled2 + x, a1, a2, nWidth - x);
	}

	void InterpolateLine(COLORREF *pLineOut, COLORREF *pLineIn, DWORD *pLookupX, DWORD *pLookupXDiff, int nSize)
	{
		const __m256i opaque = _mm256_set1_epi32(static_cast<int>(0xFF000000));
		const __m256i lowWord = _mm256_set1_epi32(0xFFFF);

		int x = 0;

		for (; x + 8 <= nSize; x += 8)
		{
			const __m256i lookup = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pLookupX + x));

			// Both tap indices are packed into one DWORD, and both are already
			// range checked by the caller that built the table.
			const __m256i c1 = _mm256_i32gather_epi32(reinterpret_cast<const int*>(pLineIn),
			                                          _mm256_and_si256(lookup, lowWord), 4);
			const __m256i c2 = _mm256_i32gather_epi32(reinterpret_cast<const int*>(pLineIn),
			                                          _mm256_srli_epi32(lookup, 16), 4);

			const __m256i diff = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pLookupXDiff + x));
			const __m256i a1 = _mm256_and_si256(diff, lowWord);
			const __m256i a2 = _mm256_srli_epi32(diff, 16);

			const __m256i r = Weight8(c1, Spread8Lo(a1), Spread8Hi(a1),
			                          c2, Spread8Lo(a2), Spread8Hi(a2));

			_mm256_storeu_si256(reinterpret_cast<__m256i*>(pLineOut + x), _mm256_or_si256(r, opaque));
		}

		_mm256_zeroupper();

		if (x < nSize)
			CBlitterSSE2::InterpolateLine(pLineOut + x, pLineIn, pLookupX + x, pLookupXDiff + x, nSize - x);
	}

	void Blend32(LPDWORD pDst, LPDWORD pSrc, int nSize)
	{
		const __m256i mask = _mm256_set1_epi32(static_cast<int>(0xFEFEFEFE));

		int i = 0;

		for (; i + 8 <= nSize; i += 8)
		{
			const __m256i d = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pDst + i));
			const __m256i s = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pSrc + i));

			const __m256i h = _mm256_srli_epi32(_mm256_and_si256(d, mask), 1);
			const __m256i k = _mm256_srli_epi32(_mm256_and_si256(s, mask), 1);

			_mm256_storeu_si256(reinterpret_cast<__m256i*>(pDst + i), _mm256_add_epi32(h, k));
		}

		_mm256_zeroupper();

		if (i < nSize)
			CBlitterSSE2::Blend32(pDst + i, pSrc + i, nSize - i);
	}

	void BlendColor32(LPDWORD pDst, COLORREF clr, int nSize)
	{
		const __m256i mask = _mm256_set1_epi32(static_cast<int>(0xFEFEFEFE));
		const __m256i half = _mm256_srli_epi32(
			_mm256_and_si256(_mm256_set1_epi32(static_cast<int>(clr)), mask), 1);

		int i = 0;

		for (; i + 8 <= nSize; i += 8)
		{
			const __m256i d = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pDst + i));
			const __m256i h = _mm256_srli_epi32(_mm256_and_si256(d, mask), 1);

			_mm256_storeu_si256(reinterpret_cast<__m256i*>(pDst + i), _mm256_add_epi32(h, half));
		}

		_mm256_zeroupper();

		if (i < nSize)
			CBlitterSSE2::BlendColor32(pDst + i, clr, nSize - i);
	}

	void Fill32(LPDWORD pDst, COLORREF clr, int nSize)
	{
		const __m256i v = _mm256_set1_epi32(static_cast<int>(clr));

		int i = 0;

		for (; i + 8 <= nSize; i += 8)
			_mm256_storeu_si256(reinterpret_cast<__m256i*>(pDst + i), v);

		_mm256_zeroupper();

		for (; i < nSize; ++i)
			pDst[i] = clr;
	}

private:

	// unpack and packus both work within each 128-bit half, and in the same
	// order, so the halves land back where they started.
	static __m256i Weight8(__m256i c1, __m256i w1Lo, __m256i w1Hi,
	                       __m256i c2, __m256i w2Lo, __m256i w2Hi)
	{
		const __m256i zero = _mm256_setzero_si256();

		const __m256i lo = _mm256_srli_epi16(
			_mm256_add_epi16(_mm256_mullo_epi16(_mm256_unpacklo_epi8(c1, zero), w1Lo),
			                 _mm256_mullo_epi16(_mm256_unpacklo_epi8(c2, zero), w2Lo)), 8);

		const __m256i hi = _mm256_srli_epi16(
			_mm256_add_epi16(_mm256_mullo_epi16(_mm256_unpackhi_epi8(c1, zero), w1Hi),
			                 _mm256_mullo_epi16(_mm256_unpackhi_epi8(c2, zero), w2Hi)), 8);

		return _mm256_packus_epi16(lo, hi);
	}

	static __m256i Spread8Lo(__m256i w)
	{
		const __m256i t = _mm256_shufflelo_epi16(w, _MM_SHUFFLE(2, 2, 0, 0));
		return _mm256_unpacklo_epi16(t, t);
	}

	static __m256i Spread8Hi(__m256i w)
	{
		const __m256i t = _mm256_shufflehi_epi16(w, _MM_SHUFFLE(2, 2, 0, 0));
		return _mm256_unpackhi_epi16(t, t);
	}

	static __m256i Composite8(__m256i dst, __m256i src)
	{
		const __m256i zero = _mm256_setzero_si256();
		const __m256i ones = _mm256_set1_epi8(static_cast<char>(0xFF));

		// pshufb is per 128-bit lane, so the same index pattern serves both.
		const __m256i spreadA = _mm256_setr_epi8(
			3, 3, 3, 3, 7, 7, 7, 7, 11, 11, 11, 11, 15, 15, 15, 15,
			3, 3, 3, 3, 7, 7, 7, 7, 11, 11, 11, 11, 15, 15, 15, 15);

		const __m256i a = _mm256_shuffle_epi8(src, spreadA);
		const __m256i ia = _mm256_sub_epi8(ones, a);

		const __m256i blended = Weight8(src, _mm256_unpacklo_epi8(a, zero), _mm256_unpackhi_epi8(a, zero),
		                                dst, _mm256_unpacklo_epi8(ia, zero), _mm256_unpackhi_epi8(ia, zero));

		const __m256i opaque = _mm256_cmpeq_epi8(a, ones);

		return _mm256_or_si256(_mm256_and_si256(opaque, src), _mm256_andnot_si256(opaque, blended));
	}
};

// The one place that decides which blitter a caller gets. Takes the blitter by
// reference to a named local: RenderImage holds a TBlitter&, and the call sites
// used to bind that to a temporary that died at the end of the statement.
template<class TFunc>
inline void WithBlitter(TFunc &&fn)
{
	switch (IW::GetSimdLevel())
	{
	case IW::SimdLevel::Avx2:
		{
			CBlitterAVX2 blitter;
			fn(blitter);
		}
		break;

	case IW::SimdLevel::Sse2:
		{
			CBlitterSSE2 blitter;
			fn(blitter);
		}
		break;

	default:
		{
			CBlitter blitter;
			fn(blitter);
		}
		break;
	}
}

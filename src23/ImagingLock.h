// ImageWalker by Zac Walker
//
// Purpose: Surface locks. One traits struct per storage layout, one lock
//          template holding the addressing, and a thin virtual adapter for the
//          call sites that cannot be templated.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

//
// SurfaceLock<PF> is the fast path: a plain class with no vtable, so a caller
// that names the format gets inlined pixel access. SurfaceLockRef<PF> wraps one
// behind IW::IImageSurfaceLock for everything else. Page::GetSurfaceLock hands
// out the latter; WithSurfaceLock gives you the former.
//

#pragma once

namespace IW
{
	namespace LockDetail
	{
		// Indexed two ways: by (x & 7) against a byte, and by (x & 31) against a
		// little-endian DWORD. Both readings are live.
		inline constexpr DWORD masks1[] = {
			0x00000080, 0x00000040, 0x00000020, 0x00000010,
			0x00000008, 0x00000004, 0x00000002, 0x00000001,
			0x00008000, 0x00004000, 0x00002000, 0x00001000,
			0x00000800, 0x00000400, 0x00000200, 0x00000100,
			0x00800000, 0x00400000, 0x00200000, 0x00100000,
			0x00080000, 0x00040000, 0x00020000, 0x00010000,
			0x80000000, 0x40000000, 0x20000000, 0x10000000,
			0x08000000, 0x04000000, 0x02000000, 0x01000000
		};

		inline constexpr int masks2[] = { 0xC0, 0x30, 0x0C, 0x03 };
		inline constexpr int shift2[] = { 6, 4, 2, 0 };
	}

	////////////////////////////////////////////////////////////
	//
	// PixelTraits
	//
	// The format-specific half of a lock. Static only: the caller supplies the
	// line pointer, so the traits know nothing about pages or addressing.
	//
	template<PixelFormat::Format PF>
	struct PixelTraits;

	template<>
	struct PixelTraits<PixelFormat::PF1>
	{
		static UINT GetPixel(LPCBYTE pLine, const int x)
		{
			auto pSrc = reinterpret_cast<IW::LPCDWORD>(pLine);
			return (pSrc[x >> 5] & LockDetail::masks1[x & 31]) ? 0x1 : 0x00;
		}

		static void SetPixel(LPBYTE pLine, const int x, const UINT c)
		{
			if (c)
				pLine[x >> 3] |= static_cast<BYTE>(LockDetail::masks1[x & 7]);
			else
				pLine[x >> 3] &= static_cast<BYTE>(~LockDetail::masks1[x & 7]);
		}

		static void GetLine(LPCOLORREF pLineDst, LPCBYTE pLine, const int x, const int cx)
		{
			IW::Convert1to32(pLineDst, pLine, x, cx);
		}

		static void SetLine(LPBYTE pLine, IW::LPCCOLORREF pLineSrc, const int x, const int cx)
		{
			const int cxWithStart = cx + x;

			for (int xx = x; xx < cxWithStart; xx++)
			{
				if (*pLineSrc++)
					pLine[xx >> 3] |= static_cast<BYTE>(LockDetail::masks1[xx & 7]);
				else
					pLine[xx >> 3] &= static_cast<BYTE>(~LockDetail::masks1[xx & 7]);
			}
		}
	};

	template<>
	struct PixelTraits<PixelFormat::PF2>
	{
		static UINT GetPixel(LPCBYTE pLine, const int x)
		{
			return (pLine[x >> 2] & LockDetail::masks2[x & 3]) >> LockDetail::shift2[x & 3];
		}

		static void SetPixel(LPBYTE pLine, const int x, const UINT c)
		{
			const auto cc = static_cast<BYTE>((c & 0x03) << LockDetail::shift2[x & 3]);
			pLine[x >> 2] &= static_cast<BYTE>(~LockDetail::masks2[x & 3]);
			pLine[x >> 2] |= cc;
		}

		static void GetLine(LPCOLORREF pLineDst, LPCBYTE pLine, const int x, const int cx)
		{
			IW::Convert2to32(pLineDst, pLine, x, cx);
		}

		// GetLine on a PF2 page yields indices, so this is index space too.
		static void SetLine(LPBYTE pLine, IW::LPCCOLORREF pLineSrc, const int x, const int cx)
		{
			const int cxWithStart = cx + x;

			for (int xx = x; xx < cxWithStart; xx++)
			{
				const auto cc = static_cast<BYTE>((*pLineSrc++ & 0x03) << LockDetail::shift2[xx & 3]);
				pLine[xx >> 2] &= static_cast<BYTE>(~LockDetail::masks2[xx & 3]);
				pLine[xx >> 2] |= cc;
			}
		}
	};

	template<>
	struct PixelTraits<PixelFormat::PF4>
	{
		static UINT GetPixel(LPCBYTE pLine, const int x)
		{
			return ((x & 1) ? pLine[x >> 1] & 0x0f : (pLine[x >> 1] >> 4)) | 0xff000000;
		}

		static void SetPixel(LPBYTE pLine, const int x, const UINT c)
		{
			LPBYTE p = pLine + (x >> 1);

			if (x & 1)
				*p = static_cast<BYTE>((*p & 0xf0) | (c & 0x0f));
			else
				*p = static_cast<BYTE>((*p & 0x0f) | ((c << 4) & 0xf0));
		}

		static void GetLine(LPCOLORREF pLineDst, LPCBYTE pLine, const int x, const int cx)
		{
			IW::Convert4to32(pLineDst, pLine, x, cx);
		}

		// GetLine on a PF4 page yields indices, so this is index space too.
		static void SetLine(LPBYTE pLine, IW::LPCCOLORREF pLineSrc, const int x, const int cx)
		{
			const int cxWithStart = cx + x;

			for (int xx = x; xx < cxWithStart; xx++)
			{
				const auto c = static_cast<BYTE>(*pLineSrc++ & 0x0f);
				LPBYTE p = pLine + (xx >> 1);

				if (xx & 1)
					*p = static_cast<BYTE>((*p & 0xf0) | c);
				else
					*p = static_cast<BYTE>((*p & 0x0f) | (c << 4));
			}
		}
	};

	// PF8, PF8Alpha and PF8GrayScale differ only in what a stored byte means, so
	// they share everything but GetPixel and GetLine.
	struct PixelTraits8Base
	{
		static void SetPixel(LPBYTE pLine, const int x, const UINT c)
		{
			pLine[x] = static_cast<BYTE>(c);
		}

		static void SetLine(LPBYTE pLine, IW::LPCCOLORREF pLineSrc, const int x, const int cx)
		{
			LPBYTE pLineDst = pLine + x;

			for (int n = 0; n < cx; n++)
			{
				*pLineDst++ = static_cast<BYTE>(*pLineSrc++);
			}
		}
	};

	template<>
	struct PixelTraits<PixelFormat::PF8> : PixelTraits8Base
	{
		static UINT GetPixel(LPCBYTE pLine, const int x)
		{
			return pLine[x] | 0xff000000;
		}

		static void GetLine(LPCOLORREF pLineDst, LPCBYTE pLine, const int x, const int cx)
		{
			IW::Convert8to32(pLineDst, pLine, x, cx);
		}
	};

	template<>
	struct PixelTraits<PixelFormat::PF8Alpha> : PixelTraits8Base
	{
		static UINT GetPixel(LPCBYTE pLine, const int x)
		{
			return pLine[x];
		}

		static void GetLine(LPCOLORREF pLineDst, LPCBYTE pLine, const int x, const int cx)
		{
			IW::Convert8to32(pLineDst, pLine, x, cx);
		}
	};

	template<>
	struct PixelTraits<PixelFormat::PF8GrayScale> : PixelTraits8Base
	{
		static UINT GetPixel(LPCBYTE pLine, const int x)
		{
			return pLine[x] | 0xff000000;
		}

		static void GetLine(LPCOLORREF pLineDst, LPCBYTE pLine, const int x, const int cx)
		{
			IW::Convert8GrayScaleto32(pLineDst, pLine, x, cx);
		}
	};

	// PF555 and PF565 differ only in the pack/unpack pair.
	template<COLORREF (*TTo888)(const COLORREF), COLORREF (*TTo16)(const COLORREF)>
	struct PixelTraits16Base
	{
		static UINT GetPixel(LPCBYTE pLine, const int x)
		{
			const short c = *(reinterpret_cast<const short*>(pLine) + x);
			return TTo888(c) | 0xff000000;
		}

		static void SetPixel(LPBYTE pLine, const int x, const UINT c)
		{
			reinterpret_cast<short*>(pLine)[x] = static_cast<short>(TTo16(c));
		}

		static void SetLine(LPBYTE pLine, IW::LPCCOLORREF pLineSrc, const int x, const int cx)
		{
			IW::LPCCOLORREF pLineSrcEnd = pLineSrc + cx;
			LPBYTE pLineDst = pLine + (x * 2);
			DWORD c1, c2;

			// Move to an aligned bit part of memory
			while ((0 != (reinterpret_cast<UINT_PTR>(pLineDst) & 3)) && (pLineSrc < pLineSrcEnd))
			{
				c1 = *pLineSrc++;
				*(reinterpret_cast<unsigned short*>(pLineDst)) = static_cast<unsigned short>(TTo16(c1));
				pLineDst += 2;
			}

			// Round down to an even number of remaining pixels: the pair loop below
			// consumes two per test, so an odd count used to read one past the end.
			auto pLineSrcEndEven = pLineSrc + ((pLineSrcEnd - pLineSrc) & ~1);

			while (pLineSrc < pLineSrcEndEven)
			{
				c1 = *pLineSrc++;
				c2 = *pLineSrc++;

				*(reinterpret_cast<LPDWORD>(pLineDst)) = TTo16(c1) | (TTo16(c2) << 16);

				pLineDst += 4;
			}

			// if a tail exists
			while (pLineSrc < pLineSrcEnd)
			{
				c1 = *pLineSrc++;
				*(reinterpret_cast<unsigned short*>(pLineDst)) = static_cast<unsigned short>(TTo16(c1));
				pLineDst += 2;
			}
		}
	};

	template<>
	struct PixelTraits<PixelFormat::PF555> : PixelTraits16Base<RGB555to888, RGB888to555>
	{
		static void GetLine(LPCOLORREF pLineDst, LPCBYTE pLine, const int x, const int cx)
		{
			IW::Convert555to32(pLineDst, pLine, x, cx);
		}
	};

	template<>
	struct PixelTraits<PixelFormat::PF565> : PixelTraits16Base<RGB565to888, RGB888to565>
	{
		static void GetLine(LPCOLORREF pLineDst, LPCBYTE pLine, const int x, const int cx)
		{
			IW::Convert565to32(pLineDst, pLine, x, cx);
		}
	};

	template<>
	struct PixelTraits<PixelFormat::PF24>
	{
		static UINT GetPixel(LPCBYTE pLine, const int x)
		{
			// Three bytes, not a DWORD: at a width that is a multiple of four the
			// storage width is exactly 3*cx, and line 0 of a bottom-up DIB is last
			// in memory, so the fourth byte is past the page.
			LPCBYTE p = pLine + (x * 3);
			return p[0] | (p[1] << 8) | (p[2] << 16) | 0xff000000;
		}

		static void SetPixel(LPBYTE pLine, const int x, const UINT c)
		{
			LPBYTE p = pLine + (x * 3);
			p[0] = static_cast<BYTE>(IW::GetR(c));
			p[1] = static_cast<BYTE>(IW::GetG(c));
			p[2] = static_cast<BYTE>(IW::GetB(c));
		}

		static void GetLine(LPCOLORREF pLineDst, LPCBYTE pLine, const int x, const int cx)
		{
			IW::Convert24to32(pLineDst, pLine, x, cx);
		}

		static void SetLine(LPBYTE pLine, IW::LPCCOLORREF pLineSrc, const int x, const int cx)
		{
			IW::LPCCOLORREF pLineSrcEnd = pLineSrc + cx;
			IW::LPCCOLORREF pLineSrcEnd2 = pLineSrcEnd - 4;

			LPBYTE pLineDst = pLine + 3 * x;
			DWORD c, c1, c2;

			// Move to an aligned bit part of memory
			while ((0 != (reinterpret_cast<UINT_PTR>(pLineDst) & 3)) && (pLineSrc < pLineSrcEnd))
			{
				c = *pLineSrc++;
				*pLineDst++ = static_cast<BYTE>(IW::GetR(c));
				*pLineDst++ = static_cast<BYTE>(IW::GetG(c));
				*pLineDst++ = static_cast<BYTE>(IW::GetB(c));
			}

			// Continue processing
			IW::LPCCOLORREF pIn = pLineSrc;
			auto pOut = reinterpret_cast<LPCOLORREF>(pLineDst);

			while (pIn < pLineSrcEnd2)
			{
				c1 = *pIn++;
				c2 = *pIn++;

				*pOut++ = ((c2 << 24) & 0xff000000) | (c1 & 0xffffff);

				c1 = *pIn++;
				*pOut++ = ((c1 << 16) & 0xffff0000) | ((c2 >> 8) & 0xffff);

				c2 = *pIn++;
				*pOut++ = ((c2 << 8) & 0xffffff00) | ((c1 >> 16) & 0x00ff);
			}

			pLineDst = reinterpret_cast<LPBYTE>(pOut);

			// Complete any incomplete work
			while (pIn < pLineSrcEnd)
			{
				c = *pIn++;
				*pLineDst++ = static_cast<BYTE>(IW::GetR(c));
				*pLineDst++ = static_cast<BYTE>(IW::GetG(c));
				*pLineDst++ = static_cast<BYTE>(IW::GetB(c));
			}
		}
	};

	template<>
	struct PixelTraits<PixelFormat::PF32>
	{
		static UINT GetPixel(LPCBYTE pLine, const int x)
		{
			return reinterpret_cast<const DWORD*>(pLine)[x] | 0xff000000;
		}

		static void SetPixel(LPBYTE pLine, const int x, const UINT c)
		{
			reinterpret_cast<LPDWORD>(pLine)[x] = c;
		}

		// The stored fourth byte is not alpha for this format, so it is replaced
		// rather than copied.
		static void GetLine(LPCOLORREF pLineDst, LPCBYTE pLine, const int x, const int cx)
		{
			auto pLineSrc = reinterpret_cast<const DWORD*>(pLine) + x;
			COLORREF* pLineDstEnd = pLineDst + cx;

			while (pLineDst < pLineDstEnd)
			{
				const DWORD c = *pLineSrc++;
				*pLineDst++ = IW::RGBA(IW::GetR(c), IW::GetG(c), IW::GetB(c), 0xff);
			}
		}

		static void SetLine(LPBYTE pLine, IW::LPCCOLORREF pLineSrc, const int x, const int cx)
		{
			IW::MemCopy(reinterpret_cast<LPDWORD>(pLine) + x, pLineSrc, cx * sizeof(COLORREF));
		}
	};

	template<>
	struct PixelTraits<PixelFormat::PF32Alpha>
	{
		static UINT GetPixel(LPCBYTE pLine, const int x)
		{
			return reinterpret_cast<const DWORD*>(pLine)[x];
		}

		static void SetPixel(LPBYTE pLine, const int x, const UINT c)
		{
			reinterpret_cast<LPDWORD>(pLine)[x] = c;
		}

		static void GetLine(LPCOLORREF pLineDst, LPCBYTE pLine, const int x, const int cx)
		{
			IW::MemCopy(pLineDst, reinterpret_cast<const DWORD*>(pLine) + x, cx * sizeof(COLORREF));
		}

		static void SetLine(LPBYTE pLine, IW::LPCCOLORREF pLineSrc, const int x, const int cx)
		{
			IW::MemCopy(reinterpret_cast<LPDWORD>(pLine) + x, pLineSrc, cx * sizeof(COLORREF));
		}
	};

	////////////////////////////////////////////////////////////
	//
	// SurfaceLock
	//
	// No vtable and no heap: name the format and every call below inlines. The
	// base pointer, stride, palette and background are resolved once here rather
	// than recomputed from the pixel format on every access.
	//
	template<PixelFormat::Format PF>
	class SurfaceLock
	{
	public:
		typedef PixelTraits<PF> Traits;

		explicit SurfaceLock(IW::Page &page) :
			_page(page),
			_pLine0(page.GetBitmapLine(0)),
			_nStride(-CalcStorageWidth(page.GetWidth(), PixelFormat(PF))),
			_nWidth(page.GetWidth()),
			_nHeight(page.GetHeight()),
			_pPalette(page.GetPalette()),
			_clrBackground(IW::SwapRB(page.GetBackGround()))
		{
			assert(page.GetPixelFormat()._pf == PF);
		}

		// The DIB is bottom-up, so the stride is negative and line 0 is the last
		// row in memory.
		LPBYTE Line(const int y) const
		{
			return _pLine0 + (_nStride * y);
		}

		UINT GetPixel(const int x, const int y) const
		{
			// We dont support clipping here
			assert(x >= 0 && x < _nWidth);
			assert(y >= 0 && y < _nHeight);

			return Traits::GetPixel(Line(y), x);
		}

		void SetPixel(const int x, const int y, const UINT &c)
		{
			// We dont support clipping here
			assert(x >= 0 && x < _nWidth);
			assert(y >= 0 && y < _nHeight);

			Traits::SetPixel(Line(y), x, c);
		}

		void GetLine(LPCOLORREF pLineDst, const int y, const int x, const int cx) const
		{
			// We dont support clipping here
			assert(x >= 0 && x < _nWidth);
			assert(cx + x <= _nWidth);
			assert(y >= 0 && y < _nHeight);

			Traits::GetLine(pLineDst, Line(y), x, cx);
		}

		void SetLine(IW::LPCCOLORREF pLineSrc, const int y, const int x, const int cx)
		{
			// We dont support clipping here
			assert(x >= 0 && x < _nWidth);
			assert(cx + x <= _nWidth);
			assert(y >= 0 && y < _nHeight);

			Traits::SetLine(Line(y), pLineSrc, x, cx);
		}

		void RenderLine(COLORREF *pLineDst, const int y, const int x, const int cx) const
		{
			constexpr int nPaletteEntries = PixelFormat(PF).NumberOfPaletteEntries();
			constexpr bool bHasAlpha = PixelFormat(PF).HasAlpha();

			GetLine(pLineDst, y, x, cx);

			if constexpr (nPaletteEntries > 0)
			{
				IW::LPCCOLORREF pRGB = _pPalette;

				for (int xx = 0; xx < cx; xx++)
				{
					pLineDst[xx] = pRGB[pLineDst[xx]];
				}
			}

			if constexpr (bHasAlpha)
			{
				const COLORREF clgBG = _clrBackground;

				// round(v / 255) for any v a 255x255 blend can reach. The old >>8 divided
				// by 256, so even a fully opaque pixel came back a count short of itself.
				auto Div255 = [](const COLORREF v)
				{
					const COLORREF t = v + 128;
					return (t + (t >> 8)) >> 8;
				};

				for (int xx = 0; xx < cx; xx++)
				{
					const COLORREF rr = IW::GetR(clgBG);
					const COLORREF gg = IW::GetG(clgBG);
					const COLORREF bb = IW::GetB(clgBG);

					// Blend this pixel with background color
					const COLORREF c = pLineDst[xx];
					const COLORREF aa = IW::GetA(c);
					const COLORREF aaNeg = 255 - aa;

					pLineDst[xx] = RGB(Div255((rr * aaNeg) + (IW::GetR(c) * aa)),
					                   Div255((gg * aaNeg) + (IW::GetG(c) * aa)),
					                   Div255((bb * aaNeg) + (IW::GetB(c) * aa))) | 0xFF000000;
				}
			}
			else
			{
				// A render is opaque by definition, and the byte the source happens to
				// carry there is not alpha: a palette entry's fourth byte is whatever the
				// loader left, and every GIF palette leaves it zero. The high quality
				// downscale weights its samples by alpha, so a zero there drew every
				// palettised image as a black rectangle.
				for (int xx = 0; xx < cx; xx++)
					pLineDst[xx] |= 0xFF000000;
			}
		}

		CRect GetClipRect() const
		{
			return _page.GetClipRect();
		}

	private:
		IW::Page _page; // held by value so the blob outlives the lock
		LPBYTE _pLine0;
		int _nStride;
		int _nWidth;
		int _nHeight;
		IW::LPCCOLORREF _pPalette;
		COLORREF _clrBackground;
	};

	////////////////////////////////////////////////////////////
	//
	// SurfaceLockRef
	//
	// The virtual face of a lock, for the call sites that take a page of any
	// format. One forwarding layer, entered once per scan line.
	//
	template<PixelFormat::Format PF>
	class SurfaceLockRef : public IW::IImageSurfaceLock
	{
	public:
		explicit SurfaceLockRef(IW::Page &page) : _lock(page)
		{
		}

		UINT GetPixel(const int x, const int y) const override
		{
			return _lock.GetPixel(x, y);
		}

		void SetPixel(const int x, const int y, const UINT &c) override
		{
			_lock.SetPixel(x, y, c);
		}

		void GetLine(LPCOLORREF pLineDst, const int y, const int x, const int cx) const override
		{
			_lock.GetLine(pLineDst, y, x, cx);
		}

		void SetLine(IW::LPCCOLORREF pLineSrc, const int y, const int x, const int cx) override
		{
			_lock.SetLine(pLineSrc, y, x, cx);
		}

		void RenderLine(COLORREF *pLineDst, const int y, const int x, const int cx) const override
		{
			_lock.RenderLine(pLineDst, y, x, cx);
		}

		CRect GetClipRect() const override
		{
			return _lock.GetClipRect();
		}

	private:
		SurfaceLock<PF> _lock;
	};

	////////////////////////////////////////////////////////////
	//
	// WithSurfaceLock
	//
	// The one place that maps a run-time pixel format onto a concrete lock.
	// Mirrors WithBlitter: the lock is a stack local of known type, so the body
	// below it is statically dispatched, inlinable, and costs no allocation.
	//
	// Only worth reaching for when the body touches individual pixels. A caller
	// that works a scan line at a time should keep the interface and stay at one
	// instantiation.
	//
	template<class TFunc>
	inline void WithSurfaceLock(IW::Page &page, TFunc &&fn)
	{
		switch (page.GetPixelFormat()._pf)
		{
		case PixelFormat::PF1:
			{ SurfaceLock<PixelFormat::PF1> lock(page); fn(lock); }
			break;
		case PixelFormat::PF2:
			{ SurfaceLock<PixelFormat::PF2> lock(page); fn(lock); }
			break;
		case PixelFormat::PF4:
			{ SurfaceLock<PixelFormat::PF4> lock(page); fn(lock); }
			break;
		case PixelFormat::PF8:
			{ SurfaceLock<PixelFormat::PF8> lock(page); fn(lock); }
			break;
		case PixelFormat::PF8Alpha:
			{ SurfaceLock<PixelFormat::PF8Alpha> lock(page); fn(lock); }
			break;
		case PixelFormat::PF8GrayScale:
			{ SurfaceLock<PixelFormat::PF8GrayScale> lock(page); fn(lock); }
			break;
		case PixelFormat::PF555:
			{ SurfaceLock<PixelFormat::PF555> lock(page); fn(lock); }
			break;
		case PixelFormat::PF565:
			{ SurfaceLock<PixelFormat::PF565> lock(page); fn(lock); }
			break;
		case PixelFormat::PF24:
			{ SurfaceLock<PixelFormat::PF24> lock(page); fn(lock); }
			break;
		case PixelFormat::PF32:
			{ SurfaceLock<PixelFormat::PF32> lock(page); fn(lock); }
			break;
		case PixelFormat::PF32Alpha:
			{ SurfaceLock<PixelFormat::PF32Alpha> lock(page); fn(lock); }
			break;
		}
	}
}


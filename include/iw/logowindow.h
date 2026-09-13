#pragma once

// The animated plasma logo, shared by the four WTL applications.
//
// The later historical releases tied CLogoWindow to IW::Image; earlier ones
// used a rebar band image (REBARINFO + RBBIM_IMAGE) that disappeared with the
// rebar. ArtMate 1.0 originally had no logo.
//
// This version owns an 8bpp DIB and a 256-entry palette directly, so it needs
// nothing from any app's imaging layer and can sit in all four. The plasma and
// the palette ramps are the original arithmetic, kept bit-for-bit; only the
// buffer underneath changed.
//
// The animation state (p1..p4, and the fade bias) lived in function-level
// statics in both original copies, which is shared mutable state between any two
// instances. It is per-object here.

#include <windows.h>
#include <commctrl.h>

#include <vector>

namespace IW
{
	class CLogoWindow : public ATL::CWindowImpl<CLogoWindow>
	{
	public:
		// -1 leaves hbrBackground null: every pixel is painted in DoPaint.
		DECLARE_WND_CLASS_EX(_T("IWLogoWindow"), CS_HREDRAW | CS_VREDRAW, -1)

		CLogoWindow() = default;

		// Centred overlay drawn on top of the plasma. Either may be set; an
		// image list wins if both are.
		void SetOverlayIcon(HICON hIcon, int cx = 16, int cy = 16)
		{
			_hIcon = hIcon;
			_sizeIcon.cx = cx;
			_sizeIcon.cy = cy;
		}

		void SetOverlayImage(HIMAGELIST hImageList, int nImage = 0)
		{
			_hImageList = hImageList;
			_nImage = nImage;
		}

		// Brightens while a background task is running, fades back when it ends.
		void SetWorking(bool bWorking)
		{
			if (_bWorking != bWorking)
			{
				_bWorking = bWorking;
				SetFadeStep();
			}
		}

		// Drive from the frame's timer. Steps the plasma and repaints, but only
		// while visible, so a hidden logo costs nothing.
		void OnTimer()
		{
			if (m_hWnd == nullptr || !IsWindowVisible())
				return;

			if (_nColorFade != 0)
				InitPalette();

			StepPlasma();
			Invalidate(FALSE);
		}

		BEGIN_MSG_MAP(CLogoWindow)
			MESSAGE_HANDLER(WM_CREATE, OnCreate)
			MESSAGE_HANDLER(WM_SIZE, OnSize)
			MESSAGE_HANDLER(WM_PAINT, OnPaint)
			MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
			MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
			MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
			MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
		END_MSG_MAP()

	private:
		LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL& bHandled)
		{
			bHandled = FALSE;
			Rebuild();
			return 0;
		}

		LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL& bHandled)
		{
			bHandled = FALSE;
			Rebuild();
			return 0;
		}

		LRESULT OnEraseBackground(UINT, WPARAM, LPARAM, BOOL&)
		{
			return 1; // DoPaint covers the whole client area.
		}

		LRESULT OnPaint(UINT, WPARAM wParam, LPARAM, BOOL&)
		{
			if (wParam != 0)
			{
				DoPaint(reinterpret_cast<HDC>(wParam));
			}
			else
			{
				PAINTSTRUCT ps = {};
				const HDC hdc = ::BeginPaint(m_hWnd, &ps);
				DoPaint(hdc);
				::EndPaint(m_hWnd, &ps);
			}

			return 0;
		}

		LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&)
		{
			if (!_bHover)
			{
				_bHover = true;
				SetFadeStep();

				TRACKMOUSEEVENT tme = {sizeof(tme)};
				tme.dwFlags = TME_LEAVE;
				tme.hwndTrack = m_hWnd;
				::TrackMouseEvent(&tme);
			}

			return 0;
		}

		LRESULT OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&)
		{
			_bHover = false;
			SetFadeStep();
			return 0;
		}

		void SetFadeStep()
		{
			_nColorFade = (_bWorking || _bHover) ? 2 : -1;
		}

		// Double-buffered so the overlay does not flicker over the plasma.
		void DoPaint(HDC hdc)
		{
			RECT rc = {};
			GetClientRect(&rc);

			const int cx = rc.right - rc.left;
			const int cy = rc.bottom - rc.top;

			if (cx <= 0 || cy <= 0 || _cx <= 0 || _cy <= 0)
				return;

			const HDC hdcMem = ::CreateCompatibleDC(hdc);
			if (hdcMem == nullptr)
				return;

			const HBITMAP hbm = ::CreateCompatibleBitmap(hdc, cx, cy);
			if (hbm == nullptr)
			{
				::DeleteDC(hdcMem);
				return;
			}

			const HGDIOBJ hbmOld = ::SelectObject(hdcMem, hbm);

			::StretchDIBits(hdcMem, 0, 0, cx, cy, 0, 0, _cx, _cy, _pixels.data(),
			                reinterpret_cast<const BITMAPINFO*>(&_bmi), DIB_RGB_COLORS, SRCCOPY);

			if (_hImageList != nullptr)
			{
				int cxImage = 0, cyImage = 0;
				if (::ImageList_GetIconSize(_hImageList, &cxImage, &cyImage))
				{
					::ImageList_Draw(_hImageList, _nImage, hdcMem, (cx - cxImage) / 2, (cy - cyImage) / 2,
					                 ILD_TRANSPARENT);
				}
			}
			else if (_hIcon != nullptr)
			{
				::DrawIconEx(hdcMem, (cx - _sizeIcon.cx) / 2, (cy - _sizeIcon.cy) / 2, _hIcon,
				             _sizeIcon.cx, _sizeIcon.cy, 0, nullptr, DI_NORMAL);
			}

			::BitBlt(hdc, 0, 0, cx, cy, hdcMem, 0, 0, SRCCOPY);

			::SelectObject(hdcMem, hbmOld);
			::DeleteObject(hbm);
			::DeleteDC(hdcMem);
		}

		void Rebuild()
		{
			RECT rc = {};
			GetClientRect(&rc);

			const int cx = rc.right - rc.left;
			const int cy = rc.bottom - rc.top;

			if (cx <= 0 || cy <= 0 || (cx == _cx && cy == _cy))
				return;

			_cx = cx;
			_cy = cy;
			_stride = (cx + 3) & ~3; // StepPlasma writes whole DWORDs per row.

			_pixels.assign(static_cast<size_t>(_stride) * _cy, 0);

			_bmi.header.biSize = sizeof(BITMAPINFOHEADER);
			_bmi.header.biWidth = _stride;
			_bmi.header.biHeight = _cy;
			_bmi.header.biPlanes = 1;
			_bmi.header.biBitCount = 8;
			_bmi.header.biCompression = BI_RGB;
			_bmi.header.biClrUsed = 256;
			_bmi.header.biClrImportant = 256;

			InitPalette();
			StepPlasma();
		}

		// The palette is the animation: the pixel data is a fixed interference
		// pattern and the ramp below is what fades it up and down.
		void InitPalette()
		{
			if (_nColorFade > 0)
			{
				_nBias += _nColorFade;

				if (_nBias >= 20)
				{
					_nColorFade = 0;
					_nBias = 20;
				}
			}
			else if (_nColorFade < 0)
			{
				_nBias += _nColorFade;

				if (_nBias <= 0)
				{
					_nColorFade = 0;
					_nBias = 0;
				}
			}

			for (int i = 0; i < 256; i++)
			{
				const int c1 = Cosinus(static_cast<BYTE>(i));
				const int c2 = Cosinus(static_cast<BYTE>((i + 32) & 0xff));
				const int c3 = Cosinus(static_cast<BYTE>((i + 64) & 0xff));
				const int base = ((c1 + c2 + c3) >> 2) + 64;

				int r = 0, g = 0, b = 0;

				if (_nBias <= 0)
				{
					r = g = b = base;
				}
				else if (_nBias <= 4)
				{
					const int c = base * 15;
					r = (c + c1) >> 4;
					g = (c + c2) >> 4;
					b = (c + c3) >> 4;
				}
				else if (_nBias <= 8)
				{
					const int c = base * 7;
					r = (c + c1) >> 3;
					g = (c + c2) >> 3;
					b = (c + c3) >> 3;
				}
				else if (_nBias <= 12)
				{
					const int c = base * 3;
					r = (c + c1) >> 2;
					g = (c + c2) >> 2;
					b = (c + c3) >> 2;
				}
				else if (_nBias <= 16)
				{
					r = (base + c1) >> 1;
					g = (base + c2) >> 1;
					b = (base + c3) >> 1;
				}
				else
				{
					r = c1;
					g = c2;
					b = c3;
				}

				// The originals stored a COLORREF straight into the RGBQUAD
				// slots, so the red and blue ramps land swapped. Kept, because
				// that is the colour the plasma has always been.
				_bmi.colors[i].rgbBlue = static_cast<BYTE>(r);
				_bmi.colors[i].rgbGreen = static_cast<BYTE>(g);
				_bmi.colors[i].rgbRed = static_cast<BYTE>(b);
				_bmi.colors[i].rgbReserved = 0;
			}
		}

		void StepPlasma()
		{
			if (_pixels.empty())
				return;

			BYTE* pVidMem = _pixels.data();
			int v = 0;

			BYTE t1 = _p1;
			BYTE t2 = _p2;

			for (int y = 0; y < _cy; y++)
			{
				BYTE t3 = _p3;
				BYTE t4 = _p4;
				const int t = Cosinus(t1) + Cosinus(t2);

				for (int x = 0; x < _stride; x += 4)
				{
					DWORD c = static_cast<DWORD>((t + Cosinus(t3++) + Cosinus(t4)) >> 2);
					t4 += 3;
					c |= (static_cast<DWORD>(t + Cosinus(t3++) + Cosinus(t4)) << 6) & 0x0000ff00;
					t4 += 3;
					c |= (static_cast<DWORD>(t + Cosinus(t3++) + Cosinus(t4)) << 14) & 0x00ff0000;
					t4 += 3;
					c |= (static_cast<DWORD>(t + Cosinus(t3++) + Cosinus(t4)) << 22) & 0xff000000;
					t4 += 3;

					*reinterpret_cast<DWORD*>(pVidMem + v) = c;
					v += 4;
				}

				t1 += 2;
				t2 += 1;
			}

			_p1 += 1;
			_p2 -= 2;
			_p3 += 3;
			_p4 -= 4;
		}

		static int Cosinus(BYTE n)
		{
			// (127 * cos(i * PI / 64)) + 128, quantised, as the originals had it
			// spelled out as a literal table.
			static const int table[256] = {
				0xFF, 0xFE, 0xFE, 0xFD, 0xFC, 0xFB, 0xF9, 0xF7,
				0xF5, 0xF2, 0xF0, 0xEC, 0xE9, 0xE6, 0xE2, 0xDE,
				0xD9, 0xD5, 0xD0, 0xCB, 0xC6, 0xC1, 0xBB, 0xB6,
				0xB0, 0xAA, 0xA4, 0x9E, 0x98, 0x92, 0x8C, 0x86,
				0x7F, 0x79, 0x73, 0x6D, 0x67, 0x61, 0x5B, 0x55,
				0x4F, 0x49, 0x44, 0x3E, 0x39, 0x34, 0x2F, 0x2A,
				0x26, 0x21, 0x1D, 0x19, 0x16, 0x13, 0x0F, 0x0D,
				0x0A, 0x08, 0x06, 0x04, 0x03, 0x02, 0x01, 0x01,
				0x01, 0x01, 0x01, 0x02, 0x03, 0x04, 0x06, 0x08,
				0x0A, 0x0D, 0x0F, 0x13, 0x16, 0x19, 0x1D, 0x21,
				0x26, 0x2A, 0x2F, 0x34, 0x39, 0x3E, 0x44, 0x49,
				0x4F, 0x55, 0x5B, 0x61, 0x67, 0x6D, 0x73, 0x79,
				0x80, 0x86, 0x8C, 0x92, 0x98, 0x9E, 0xA4, 0xAA,
				0xB0, 0xB6, 0xBB, 0xC1, 0xC6, 0xCB, 0xD0, 0xD5,
				0xD9, 0xDE, 0xE2, 0xE6, 0xE9, 0xEC, 0xF0, 0xF2,
				0xF5, 0xF7, 0xF9, 0xFB, 0xFC, 0xFD, 0xFE, 0xFE,
				0xFF, 0xFE, 0xFE, 0xFD, 0xFC, 0xFB, 0xF9, 0xF7,
				0xF5, 0xF2, 0xF0, 0xEC, 0xE9, 0xE6, 0xE2, 0xDE,
				0xD9, 0xD5, 0xD0, 0xCB, 0xC6, 0xC1, 0xBB, 0xB6,
				0xB0, 0xAA, 0xA4, 0x9E, 0x98, 0x92, 0x8C, 0x86,
				0x7F, 0x79, 0x73, 0x6D, 0x67, 0x61, 0x5B, 0x55,
				0x4F, 0x49, 0x44, 0x3E, 0x39, 0x34, 0x2F, 0x2A,
				0x26, 0x21, 0x1D, 0x19, 0x16, 0x13, 0x0F, 0x0D,
				0x0A, 0x08, 0x06, 0x04, 0x03, 0x02, 0x01, 0x01,
				0x01, 0x01, 0x01, 0x02, 0x03, 0x04, 0x06, 0x08,
				0x0A, 0x0D, 0x0F, 0x13, 0x16, 0x19, 0x1D, 0x21,
				0x26, 0x2A, 0x2F, 0x34, 0x39, 0x3E, 0x44, 0x49,
				0x4F, 0x55, 0x5B, 0x61, 0x67, 0x6D, 0x73, 0x79,
				0x80, 0x86, 0x8C, 0x92, 0x98, 0x9E, 0xA4, 0xAA,
				0xB0, 0xB6, 0xBB, 0xC1, 0xC6, 0xCB, 0xD0, 0xD5,
				0xD9, 0xDE, 0xE2, 0xE6, 0xE9, 0xEC, 0xF0, 0xF2,
				0xF5, 0xF7, 0xF9, 0xFB, 0xFC, 0xFD, 0xFE, 0xFE
			};

			return table[n];
		}

		struct PaletteInfo
		{
			BITMAPINFOHEADER header;
			RGBQUAD colors[256];
		};

		PaletteInfo _bmi = {};
		std::vector<BYTE> _pixels;

		HICON _hIcon = nullptr;
		HIMAGELIST _hImageList = nullptr;
		int _nImage = 0;
		SIZE _sizeIcon = {16, 16};

		int _cx = 0;
		int _cy = 0;
		int _stride = 0;

		int _nColorFade = 0;
		int _nBias = 0;
		bool _bWorking = false;
		bool _bHover = false;

		BYTE _p1 = 0, _p2 = 0, _p3 = 0, _p4 = 0;
	};
}

#pragma once

#include "ImagingLock.h"
#include "ImagingBlitter.h"
#include "RenderImage.h"

namespace IW
{
	class ScreenDC
	{
	public:
		ScreenDC() : handle_(GetDC(nullptr)) {}
		~ScreenDC() { if (handle_) ReleaseDC(nullptr, handle_); }
		ScreenDC(const ScreenDC&) = delete;
		ScreenDC& operator=(const ScreenDC&) = delete;
		operator HDC() const { return handle_; }
		int GetDeviceCaps(int capability) const { return ::GetDeviceCaps(handle_, capability); }
	private:
		HDC handle_{};
	};

	class DibSurface
	{
	public:
		DibSurface() = default;
		DibSurface(const DibSurface&) = delete;
		DibSurface& operator=(const DibSurface&) = delete;
		~DibSurface()
		{
			if (old_) SelectObject(dc_, old_);
			if (bitmap_) DeleteObject(bitmap_);
			if (dc_) DeleteDC(dc_);
		}

		bool Create(HDC reference, const CRect& bounds)
		{
			if (dc_ || bounds.Width() <= 0 || bounds.Height() <= 0 ||
				static_cast<std::uint64_t>(bounds.Width()) * bounds.Height() > kMaxPageBytes / 4)
				return false;
			bounds_ = bounds;
			dc_ = CreateCompatibleDC(reference);
			if (!dc_) return false;
			BITMAPINFO info{};
			info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			info.bmiHeader.biWidth = bounds.Width();
			info.bmiHeader.biHeight = -bounds.Height();
			info.bmiHeader.biPlanes = 1;
			info.bmiHeader.biBitCount = 32;
			info.bmiHeader.biCompression = BI_RGB;
			void* pixels = nullptr;
			bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
			if (!bitmap_ || !pixels) return false;
			pixels_ = static_cast<std::uint32_t*>(pixels);
			old_ = SelectObject(dc_, bitmap_);
			if (!old_ || old_ == HGDI_ERROR) { old_ = nullptr; return false; }
			Fill(RGB(0, 0, 0));
			return true;
		}

		HDC dc() const { return dc_; }
		CRect GetClipRect() const { return bounds_; }

		void Fill(COLORREF value)
		{
			std::fill_n(pixels_, static_cast<size_t>(bounds_.Width()) * bounds_.Height(), SwapRB(value));
		}

		void GetLine(COLORREF* output, int y, int x, int count) const
		{
			const auto* input = pixels_ + static_cast<size_t>(y) * bounds_.Width() + x;
			for (int i = 0; i < count; ++i) output[i] = input[i] | 0xff000000;
		}

		void SetLine(const COLORREF* input, int y, int x, int count)
		{
			auto* output = pixels_ + static_cast<size_t>(y) * bounds_.Width() + x;
			for (int i = 0; i < count; ++i) output[i] = input[i];
		}

		void DrawImage(const Page& page, int x, int y)
		{
			CRect destination = page.GetPageRect();
			destination.OffsetRect(x, y);
			WithBlitter([&](auto& blitter)
			{
				RenderImage<DibSurface, std::remove_reference_t<decltype(blitter)>> render(*this, blitter);
				render.DrawImage(page, destination, CRect(0, 0, page.GetWidth(), page.GetHeight()));
			});
		}

		bool RenderToSurface(Page& page, long, long) const
		{
			GdiFlush();
			const int width = (std::min)(page.GetWidth(), bounds_.Width());
			const int height = (std::min)(page.GetHeight(), bounds_.Height());
			IImageSurfaceLockPtr lock = page.GetSurfaceLock();
			CBuffer<COLORREF> line(width);
			for (int y = 0; y < height; ++y)
			{
				GetLine(line, y, 0, width);
				lock->SetLine(line, y, 0, width);
			}
			return true;
		}

		bool RenderToSurface(Image& image) const
		{
			return RenderToSurface(image.CreatePage(bounds_.Width(), bounds_.Height(), PixelFormat::PF24), 0, 0);
		}

		bool RenderToSurface(IImageStream* output) const
		{
			GdiFlush();
			output->CreatePage(CRect(0, 0, bounds_.Width(), bounds_.Height()), PixelFormat::PF32, true);
			for (int y = 0; y < bounds_.Height(); ++y)
				output->SetBitmap(y, pixels_ + static_cast<size_t>(y) * bounds_.Width());
			output->Flush();
			return true;
		}

		void Play(HENHMETAFILE metafile)
		{
			Fill(RGB(255, 255, 255));
			RECT rectangle{0, 0, bounds_.Width(), bounds_.Height()};
			if (!PlayEnhMetaFile(dc_, metafile, &rectangle))
				throw invalid_file("Could not render the enhanced metafile.");
			GdiFlush();
		}

		void Play(HMETAFILE metafile, int width, int height)
		{
			Fill(RGB(255, 255, 255));
			SetMapMode(dc_, MM_ANISOTROPIC);
			SetWindowExtEx(dc_, width, height, nullptr);
			SetViewportExtEx(dc_, bounds_.Width(), bounds_.Height(), nullptr);
			if (!PlayMetaFile(dc_, metafile)) throw invalid_file("Could not render the metafile.");
			GdiFlush();
		}

	private:
		CRect bounds_{0, 0, 0, 0};
		HDC dc_{};
		HBITMAP bitmap_{};
		HGDIOBJ old_{};
		std::uint32_t* pixels_{};
	};
}

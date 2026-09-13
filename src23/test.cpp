// ImageWalker by Zac Walker
//
// Purpose: The unit tests, run by passing /test. Covers the loaders, the
//          pixel pipeline, the edit stack and the metadata readers.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "StdAfx.h"

#include "ImagingStreams.h"
#include "FilePng.h"
#include "FileBmp.h"
#include "FileGif.h"
#include "FileJpeg.h"
#include "FileTiff.h"
#include "FilePcx.h"
#include "FilePsd.h"
#include "FileWpg.h"
#include "FileFormatList.h"
#include "FileFormatAny.h"
#include "ImagingFilter.h"
#include "ImagingEdits.h"
#include "ImagingTransform.h"
#include "MetadataExif.h"
#include "ImagingBlitter.h"
#include "ViewRenderSurface.h"
#include "ViewRenameDlg.h"
#include "ViewAboutDlg.h"

#include "iw/commontests.h"
#include "iw/rendertests.h"

#include "AppCommands.h"
#include "ViewModelState.h"
#include "ToolContactSheet.h"
#include "iw/legacyworkflowtests.h"
#include "iw/legacyboundarytests.h"
#include "iw/legacynativeworkflowtests.h"

// ImageWalker 2.3 -- run with /test.

namespace
{

// A recognisable gradient, so a wrong stride or a truncated scanline shows up
// as a mismatch rather than as plausible-looking noise.
IW::Image MakeTestImage(int cx, int cy)
{
	IW::Image image;
	IW::Page &page = image.CreatePage(cx, cy, IW::PixelFormat::PF32);
	IW::IImageSurfaceLockPtr pLock = page.GetSurfaceLock();

	std::vector<COLORREF> line(cx);

	for (int y = 0; y < cy; y++)
	{
		for (int x = 0; x < cx; x++)
			line[x] = IW::RGBA(x & 0xFF, y & 0xFF, (x + y) & 0xFF);

		pLock->SetLine(&line[0], y, 0, cx);
	}

	return image;
}

COLORREF PixelAt(const IW::Image &image, int x, int y)
{
	IW::Page page = image.GetFirstPage();
	IW::ConstIImageSurfaceLockPtr pLock = page.GetSurfaceLock();

	COLORREF pixel = 0;
	pLock->RenderLine(&pixel, y, x, 1);

	return pixel;
}

// RenderLine composites alpha away, so a test about alpha has to read the
// stored pixel rather than the rendered one.
COLORREF RawPixelAt(const IW::Image &image, int x, int y)
{
	IW::Page page = image.GetFirstPage();
	IW::ConstIImageSurfaceLockPtr pLock = page.GetSurfaceLock();

	COLORREF pixel = 0;
	pLock->GetLine(&pixel, y, x, 1);

	return pixel;
}

IW::Image MakeAlphaImage(int cx, int cy, int alpha)
{
	IW::Image image;
	IW::Page &page = image.CreatePage(cx, cy, IW::PixelFormat::PF32Alpha);
	IW::IImageSurfaceLockPtr pLock = page.GetSurfaceLock();

	std::vector<COLORREF> line(cx);

	for (int y = 0; y < cy; y++)
	{
		for (int x = 0; x < cx; x++)
			line[x] = IW::RGBA(40 + (x & 0x3F), 80 + (y & 0x3F), 120, alpha);

		pLock->SetLine(&line[0], y, 0, cx);
	}

	return image;
}

// Stands in for RenderSurface so a drawing test needs no DC.
struct TestCanvas
{
	static const int cx = 16;
	static const int cy = 16;
	static const COLORREF clrBack = 0x00204060;

	std::vector<COLORREF> bits;

	TestCanvas() : bits(cx * cy, clrBack) {}

	COLORREF At(int x, int y) const { return bits[(y * cx) + x]; }

	CRect GetClipRect() const { return CRect(0, 0, cx, cy); }
	void GetLine(LPCOLORREF p, int y, int x, int n) const { IW::MemCopy(p, &bits[(y * cx) + x], n * 4); }
	void SetLine(IW::LPCCOLORREF p, int y, int x, int n) { IW::MemCopy(&bits[(y * cx) + x], p, n * 4); }
};

} // namespace

// Which blitter runs is a property of the machine, so a picture that came out
// different on one of them would make a saved file depend on the CPU that wrote
// it. Every method is compared over pseudo-random data at a length that leaves
// a tail for both vector widths.
IW_TEST(BlittersAgree)
{
	constexpr int nLength = 61;

	auto fill = [](std::vector<COLORREF> &v, unsigned seed)
	{
		unsigned s = seed;

		for (auto &c : v)
		{
			s = s * 1664525u + 1013904223u;
			c = s;
		}
	};

	std::vector<COLORREF> src(nLength), dstA(nLength), dstB(nLength), dstC(nLength);
	fill(src, 12345);

	CBlitter scalar;
	CBlitterSSE2 sse2;
	CBlitterAVX2 avx2;

	// A composite over a destination the caller has already read back.
	fill(dstA, 999); dstB = dstA; dstC = dstA;
	scalar.RenderAlphaLine(dstA.data(), src.data(), nLength);
	sse2.RenderAlphaLine(dstB.data(), src.data(), nLength);
	avx2.RenderAlphaLine(dstC.data(), src.data(), nLength);
	IW_CHECK(dstA == dstB);
	IW_CHECK(dstA == dstC);

	// The vertical interpolation: one pair of weights for the whole line.
	std::vector<COLORREF> s2(nLength);
	fill(s2, 555);
	scalar.InterpolateLine(dstA.data(), src.data(), s2.data(), 100, 155, nLength);
	sse2.InterpolateLine(dstB.data(), src.data(), s2.data(), 100, 155, nLength);
	avx2.InterpolateLine(dstC.data(), src.data(), s2.data(), 100, 155, nLength);
	IW_CHECK(dstA == dstB);
	IW_CHECK(dstA == dstC);

	// The horizontal one: two source indices and two weights per output pixel.
	std::vector<DWORD> lookup(nLength), diff(nLength);

	for (int i = 0; i < nLength; i++)
	{
		const int a = (i * 7) % nLength;
		const int b = (i * 13 + 3) % nLength;
		lookup[i] = MAKELONG(a, b);

		const int w = (i * 11) % 256;
		diff[i] = MAKELONG(w, 255 - w);
	}

	scalar.InterpolateLine(dstA.data(), src.data(), lookup.data(), diff.data(), nLength);
	sse2.InterpolateLine(dstB.data(), src.data(), lookup.data(), diff.data(), nLength);
	avx2.InterpolateLine(dstC.data(), src.data(), lookup.data(), diff.data(), nLength);
	IW_CHECK(dstA == dstB);
	IW_CHECK(dstA == dstC);

	fill(dstA, 4242); dstB = dstA; dstC = dstA;
	scalar.Blend32(dstA.data(), src.data(), nLength);
	sse2.Blend32(dstB.data(), src.data(), nLength);
	avx2.Blend32(dstC.data(), src.data(), nLength);
	IW_CHECK(dstA == dstB);
	IW_CHECK(dstA == dstC);

	fill(dstA, 777); dstB = dstA; dstC = dstA;
	scalar.BlendColor32(dstA.data(), 0x8040C020, nLength);
	sse2.BlendColor32(dstB.data(), 0x8040C020, nLength);
	avx2.BlendColor32(dstC.data(), 0x8040C020, nLength);
	IW_CHECK(dstA == dstB);
	IW_CHECK(dstA == dstC);

	scalar.Fill32(dstA.data(), 0x11223344, nLength);
	sse2.Fill32(dstB.data(), 0x11223344, nLength);
	avx2.Fill32(dstC.data(), 0x11223344, nLength);
	IW_CHECK(dstA == dstB);
	IW_CHECK(dstA == dstC);
}

// The screen path, not the scaler: RenderImage minifying a page that has alpha.
// It composited each output line over the previous output line and over the
// previous *source* line rather than over the canvas, so a transparent pixel
// came back as the pixel above it and every alpha image smeared its last opaque
// row down the rest of the picture.
IW_TEST(MinifyingAnAlphaPageLeavesTheCanvasWhereItIsTransparent)
{
	const int cxIn = 64, cyIn = 64;

	IW::Image image;
	IW::Page &page = image.CreatePage(cxIn, cyIn, IW::PixelFormat::PF32Alpha);

	{
		IW::IImageSurfaceLockPtr pLock = page.GetSurfaceLock();
		std::vector<COLORREF> line(cxIn);

		for (int y = 0; y < cyIn; y++)
		{
			// Opaque white across the top quarter, fully transparent below it.
			const COLORREF c = (y < cyIn / 4) ? IW::RGBA(255, 255, 255, 255) : IW::RGBA(0, 0, 0, 0);
			std::fill(line.begin(), line.end(), c);
			pLock->SetLine(&line[0], y, 0, cxIn);
		}
	}

	TestCanvas canvas;
	CBlitter blitter;
	RenderImage<TestCanvas, CBlitter> render(canvas, blitter);
	render.DrawImage(page, CRect(0, 0, TestCanvas::cx, TestCanvas::cy), CRect(0, 0, cxIn, cyIn));

	// The band landed, and nothing below it moved.
	IW_CHECK_EQ(IW::GetR(canvas.At(TestCanvas::cx / 2, 1)), 255u);

	for (int y = TestCanvas::cy / 2; y < TestCanvas::cy; y++)
		IW_CHECK_EQ(canvas.At(TestCanvas::cx / 2, y), TestCanvas::clrBack);
}

IW_TEST(ImagePagesRoundTripThroughASurfaceLock)
{
	const IW::Image image = MakeTestImage(64, 48);

	IW_CHECK_EQ(image.GetPageCount(), 1u);
	IW_CHECK_EQ(image.GetFirstPage().GetWidth(), 64);
	IW_CHECK_EQ(image.GetFirstPage().GetHeight(), 48);

	IW_CHECK_EQ(IW::GetR(PixelAt(image, 10, 20)), 10);
	IW_CHECK_EQ(IW::GetG(PixelAt(image, 10, 20)), 20);
	IW_CHECK_EQ(IW::GetB(PixelAt(image, 10, 20)), 30);
}

// The scaler's contribution tables and line buffers were all alloca'd.
// _pRGBSum is allocated once for exactly the thumbnail box, and the scaled
// extent is derived with two MulDivs -- which round to nearest. 200x200 against
// a 6000x6000 source came out 201x201, so every row of the accumulator was
// written a pixel past its end and the last row entirely outside it.
IW_TEST(AThumbnailNeverOverrunsItsBox)
{
	const int sources[] = {4096, 5000, 6000, 8000, 10000, 16384};
	const CSize boxes[] = {CSize(200, 200), CSize(160, 160), CSize(128, 96)};

	for (int b = 0; b < countof(boxes); b++)
	{
		for (int s = 0; s < countof(sources); s++)
		{
			IW::Image thumb;
			IW::ImageStreamThumbnail<IW::IImageStream> stream(thumb, Search::Any, boxes[b]);

			stream.CreatePage(CRect(0, 0, sources[s], sources[s]),
			                  IW::PixelFormat(IW::PixelFormat::PF24), true);
			stream.Flush();

			const IW::Page page = thumb.GetFirstPage();

			IW_CHECK(page.GetWidth() <= boxes[b].cx);
			IW_CHECK(page.GetHeight() <= boxes[b].cy);
			IW_CHECK(page.GetWidth() >= 1);
			IW_CHECK(page.GetHeight() >= 1);
		}
	}
}

IW_TEST(ScaleProducesTheRequestedSize)
{
	const IW::Image image = MakeTestImage(64, 48);

	IW::Image smaller;
	IW_CHECK(IW::Scale(image, smaller, CSize(16, 12), IW::CNullStatus::Instance));
	IW_CHECK_EQ(smaller.GetFirstPage().GetWidth(), 16);
	IW_CHECK_EQ(smaller.GetFirstPage().GetHeight(), 12);

	IW::Image larger;
	IW_CHECK(IW::Scale(image, larger, CSize(200, 150), IW::CNullStatus::Instance));
	IW_CHECK_EQ(larger.GetFirstPage().GetWidth(), 200);
	IW_CHECK_EQ(larger.GetFirstPage().GetHeight(), 150);
}

// Four right angles must land back on the original, which catches an off-by-one
// in either shear buffer.
IW_TEST(FourRotationsReturnTheOriginal)
{
	const IW::Image image = MakeTestImage(40, 24);

	IW::Image a, b, c, d;
	IW_CHECK(IW::Rotate90(image, a, IW::CNullStatus::Instance));
	IW_CHECK_EQ(a.GetFirstPage().GetWidth(), 24);
	IW_CHECK_EQ(a.GetFirstPage().GetHeight(), 40);

	IW_CHECK(IW::Rotate90(a, b, IW::CNullStatus::Instance));
	IW_CHECK(IW::Rotate90(b, c, IW::CNullStatus::Instance));
	IW_CHECK(IW::Rotate90(c, d, IW::CNullStatus::Instance));

	IW_CHECK_EQ(d.GetFirstPage().GetWidth(), 40);
	IW_CHECK_EQ(d.GetFirstPage().GetHeight(), 24);
	IW_CHECK_EQ(PixelAt(d, 7, 11), PixelAt(image, 7, 11));
	IW_CHECK_EQ(PixelAt(d, 39, 23), PixelAt(image, 39, 23));
}

// ItemRotater has two paths for one button: JPEG goes through libjpeg's
// coefficient transform, everything else is re-encoded from a rotated page.
// They mapped Left to opposite turns, so the same command turned a PNG one way
// and a JPEG the other. Image::FixOrientation is the tie-breaker: it pairs
// JXFORM_ROT_270 with Rotate270.
IW_TEST(RotateLeftTurnsAPageTheSameWayAsTheLosslessPath)
{
	const IW::Image image = MakeTestImage(40, 24);
	ImageLoaders loaders;

	{
		ItemRotater rotater(loaders, IW::Rotation::Left);

		IW::Image rotated, expected;
		IW_CHECK(rotater.Transform(const_cast<IW::Image&>(image), rotated, IW::CNullStatus::Instance));
		IW_CHECK(IW::Rotate270(image, expected, IW::CNullStatus::Instance));

		IW_CHECK_EQ(rotated.GetFirstPage().GetWidth(), expected.GetFirstPage().GetWidth());
		IW_CHECK_EQ(PixelAt(rotated, 5, 9), PixelAt(expected, 5, 9));
		IW_CHECK_EQ(PixelAt(rotated, 17, 33), PixelAt(expected, 17, 33));
	}

	{
		ItemRotater rotater(loaders, IW::Rotation::Right);

		IW::Image rotated, expected;
		IW_CHECK(rotater.Transform(const_cast<IW::Image&>(image), rotated, IW::CNullStatus::Instance));
		IW_CHECK(IW::Rotate90(image, expected, IW::CNullStatus::Instance));

		IW_CHECK_EQ(PixelAt(rotated, 5, 9), PixelAt(expected, 5, 9));
		IW_CHECK_EQ(PixelAt(rotated, 17, 33), PixelAt(expected, 17, 33));
	}
}

IW_TEST(CropTakesTheRequestedRectangle)
{
	const IW::Image image = MakeTestImage(64, 48);

	IW::Image cropped;
	IW_CHECK(IW::Crop(image, cropped, CRect(8, 4, 40, 28), IW::CNullStatus::Instance));

	IW_CHECK_EQ(cropped.GetFirstPage().GetWidth(), 32);
	IW_CHECK_EQ(cropped.GetFirstPage().GetHeight(), 24);
	IW_CHECK_EQ(PixelAt(cropped, 0, 0), PixelAt(image, 8, 4));
	IW_CHECK_EQ(PixelAt(cropped, 5, 6), PixelAt(image, 13, 10));
}

// The edit view fits the photo to its pane with this. It used to run the two
// ratios through IW::Min, which takes ints, so every ratio below 1 truncated to
// 0 and any photo bigger than the pane came out as a 1x1 rectangle -- an edit
// mode that showed an empty canvas.
IW_TEST(FitToRectShrinksAPhotoLargerThanThePane)
{
	double scale = 0.0;
	const CRect rc = IW::FitToRect(CSize(1600, 1200), CRect(0, 0, 880, 660), scale);

	IW_CHECK(scale > 0.5 && scale < 0.56);
	IW_CHECK_EQ(rc.Width(), 880);
	IW_CHECK_EQ(rc.Height(), 660);
}

IW_TEST(FitToRectNeverMagnifiesAndStaysCentred)
{
	double scale = 0.0;
	const CRect rc = IW::FitToRect(CSize(100, 50), CRect(0, 0, 900, 700), scale);

	IW_CHECK_EQ(static_cast<int>(scale * 100.0 + 0.5), 100);
	IW_CHECK_EQ(rc.Width(), 100);
	IW_CHECK_EQ(rc.Height(), 50);
	IW_CHECK_EQ(rc.left, 400);
	IW_CHECK_EQ(rc.top, 325);
}

IW_TEST(FitToRectKeepsTheAspectRatioOfATallImage)
{
	double scale = 0.0;
	const CRect rc = IW::FitToRect(CSize(600, 1200), CRect(0, 0, 800, 600), scale);

	IW_CHECK_EQ(rc.Height(), 600);
	IW_CHECK_EQ(rc.Width(), 300);
	IW_CHECK_EQ(rc.left, 250);
	IW_CHECK_EQ(rc.top, 0);
}

// Both edit paths used to read pixels back with RenderLine, which composites an
// alpha page over its background and returns a zero alpha byte. Writing that
// back stored alpha 0 for every pixel, so one nudge of any colour slider turned
// a PNG with alpha into a fully transparent image -- on screen and on save.
IW_TEST(ColourAdjustmentKeepsTheAlphaChannel)
{
	const IW::Image image = MakeAlphaImage(32, 24, 160);

	ImageEdits edits;
	edits._brightness = 30;

	IW::Image out;
	IW_CHECK(IW::ApplyEdits(image, out, edits, IW::CNullStatus::Instance));
	IW_CHECK(!out.IsEmpty());

	IW_CHECK_EQ(IW::GetA(RawPixelAt(out, 5, 5)), 160u);
	IW_CHECK_EQ(IW::GetA(RawPixelAt(out, 31, 23)), 160u);
}

IW_TEST(StraightenKeepsTheAlphaChannel)
{
	const IW::Image image = MakeAlphaImage(64, 64, 200);
	IW_CHECK_EQ(IW::GetA(RawPixelAt(image, 32, 32)), 200);

	ImageEdits edits;
	edits._straighten = 50;

	IW::Image out;
	IW_CHECK(IW::ApplyEdits(image, out, edits, IW::CNullStatus::Instance));
	IW_CHECK(!out.IsEmpty());

	// The centre is always on the picture whatever the angle. The sampler
	// truncates each weighted term, so a resampled channel lands a count low.
	const IW::Page page = out.GetFirstPage();
	IW_CHECK(page.GetPixelFormat() == IW::PixelFormat::PF32Alpha);
	IW_CHECK(abs(IW::GetA(RawPixelAt(out, page.GetWidth() / 2, page.GetHeight() / 2)) - 200) <= 2);
}

// An image with no alpha channel has to come out of the warp opaque. Neither
// GetLine nor RenderLine promises an alpha byte for a format that has none, and
// the sampler averages whatever is in it.
IW_TEST(StraightenLeavesAnOpaqueImageOpaque)
{
	const IW::Image image = MakeTestImage(64, 64);

	ImageEdits edits;
	edits._straighten = 50;

	IW::Image out;
	IW_CHECK(IW::ApplyEdits(image, out, edits, IW::CNullStatus::Instance));

	const IW::Page page = out.GetFirstPage();
	IW_CHECK(IW::GetA(RawPixelAt(out, page.GetWidth() / 2, page.GetHeight() / 2)) >= 253);
}

// The wedge boundary has to be an alpha ramp, not a cliff. Clamping a tap that
// falls off the picture to the nearest edge pixel -- which is the obvious way
// to write this sampler, and what the one before it did -- makes the last pixel
// of the fringe fully opaque with smeared edge colour, sitting against the empty
// corner. Dropping the tap instead leaves one pixel of partial alpha.
IW_TEST(StraightenLeavesARampAtTheEdgeNotAFringe)
{
	const IW::Image image = MakeTestImage(64, 64);

	ImageEdits edits;
	edits._straighten = 50;

	IW::Image out;
	IW_CHECK(IW::ApplyEdits(image, out, edits, IW::CNullStatus::Instance));

	const IW::Page page = out.GetFirstPage();
	const int cx = page.GetWidth();
	const int cy = page.GetHeight();

	IW::ConstIImageSurfaceLockPtr pLock = page.GetSurfaceLock();

	std::vector<COLORREF> pixels(static_cast<size_t>(cx) * cy);

	for (int y = 0; y < cy; y++)
		pLock->GetLine(&pixels[static_cast<size_t>(y) * cx], y, 0, cx);

	auto alphaAt = [&](int x, int y) { return IW::GetA(pixels[static_cast<size_t>(y) * cx + x]); };

	int nCliffs = 0;

	for (int y = 1; y < cy - 1; y++)
	{
		for (int x = 1; x < cx - 1; x++)
		{
			if (alphaAt(x, y) != 255)
				continue;

			if (alphaAt(x - 1, y) == 0 || alphaAt(x + 1, y) == 0 ||
				alphaAt(x, y - 1) == 0 || alphaAt(x, y + 1) == 0)
				nCliffs++;
		}
	}

	IW_CHECK_EQ(nCliffs, 0);

	// And the empty corner really is empty rather than smeared edge colour.
	IW_CHECK_EQ(IW::GetA(RawPixelAt(out, 0, 0)), 0);
}

// Interpolating straight alpha averages the RGB of a fully transparent pixel
// into its neighbours, so a PNG whose transparent area carries a flat colour
// bled that colour into its own edges. The sampler works premultiplied.
IW_TEST(StraightenDoesNotBleedTransparentColour)
{
	IW::Image image;
	IW::Page &page = image.CreatePage(64, 64, IW::PixelFormat::PF32Alpha);

	{
		IW::IImageSurfaceLockPtr pLock = page.GetSurfaceLock();
		std::vector<COLORREF> line(64);

		for (int y = 0; y < 64; y++)
		{
			for (int x = 0; x < 64; x++)
			{
				// Opaque grey on the left, invisible pure red on the right.
				line[x] = (x < 32) ? IW::RGBA(128, 128, 128, 255) : IW::RGBA(255, 0, 0, 0);
			}

			pLock->SetLine(&line[0], y, 0, 64);
		}
	}

	ImageEdits edits;
	edits._straighten = 20;

	IW::Image out;
	IW_CHECK(IW::ApplyEdits(image, out, edits, IW::CNullStatus::Instance));

	const IW::Page pageOut = out.GetFirstPage();
	int nRedTinged = 0;

	for (int y = 8; y < pageOut.GetHeight() - 8; y++)
	{
		for (int x = 8; x < pageOut.GetWidth() - 8; x++)
		{
			const COLORREF c = RawPixelAt(out, x, y);

			if (IW::GetA(c) == 0)
				continue;

			// Grey stays grey; any red pulled out of the invisible half shows
			// up as a channel spread the source never had.
			if (IW::GetR(c) > IW::GetG(c) + 8)
				nRedTinged++;
		}
	}

	IW_CHECK_EQ(nRedTinged, 0);
}

// Straightening turns the picture inside a frame of the same size, so the
// corners of the frame are empty. The crop handles have to stay off them.
IW_TEST(CropBoundsShrinkWhenStraightened)
{
	ImageEdits edits;
	IW_CHECK_EQ(edits.CropBounds(CSize(400, 300)), CRect(0, 0, 400, 300));

	edits._straighten = 50;	// 5 degrees
	const CRect rc = edits.CropBounds(CSize(400, 300));

	IW_CHECK(rc.Width() < 400);
	IW_CHECK(rc.Height() < 300);
	IW_CHECK(rc.Width() > 300);
	IW_CHECK(rc.Height() > 200);

	// Centred, so the same amount comes off each side.
	IW_CHECK_EQ(rc.left, 400 - rc.right);
	IW_CHECK_EQ(rc.top, 300 - rc.bottom);
}

// A picture of straight lines tilted by a known angle has to come back as the
// angle that undoes it. Anything that leaves _straighten at 0 -- no edge passes
// the gradient threshold, every vote lands outside the range -- is a button
// that silently does nothing.
IW_TEST(AutoStraightenRecoversAKnownTilt)
{
	const int nTilts[] = {-30, -15, 15, 30};

	for (int i = 0; i < countof(nTilts); i++)
	{
		IW::Image image;
		IW::Page &page = image.CreatePage(320, 240, IW::PixelFormat::PF24);
		IW::IImageSurfaceLockPtr pLock = page.GetSurfaceLock();

		const double angle = (nTilts[i] / 10.0) * 3.14159265358979323846 / 180.0;
		const double cs = cos(angle);
		const double sn = sin(angle);

		std::vector<COLORREF> line(320);

		for (int y = 0; y < 240; y++)
		{
			for (int x = 0; x < 320; x++)
			{
				// Rotate the sample point rather than the picture, so the bands
				// stay hard edged instead of being resampled into gradients.
				const double dx = x - 160.0;
				const double dy = y - 120.0;
				const double v = -dx * sn + dy * cs;

				const int band = static_cast<int>(floor(v / 16.0));
				const BYTE c = (band & 1) ? 230 : 25;

				line[x] = IW::RGBA(c, c, c);
			}

			pLock->SetLine(&line[0], y, 0, 320);
		}

		pLock = nullptr;

		ImageEdits edits;
		IW::AutoStraighten(image, edits);

		IW_CHECK(edits._straighten != 0);
		IW_CHECK(abs(edits._straighten + nTilts[i]) <= 3);
	}
}

IW_TEST(CropBoundsFollowAQuarterTurn)
{
	ImageEdits edits;
	edits._rotate = 1;

	IW_CHECK_EQ(edits.CropBounds(CSize(400, 300)), CRect(0, 0, 300, 400));
}

// Every corner of the bounds has to land on the picture. Walking them back
// through the rotation is what a wrong formula fails.
IW_TEST(CropBoundsStayInsideTheStraightenedPicture)
{
	const int nAngles[] = { 10, 50, 120, 300, 450, -50, -200 };
	const CSize size(400, 300);

	for (int i = 0; i < countof(nAngles); i++)
	{
		ImageEdits edits;
		edits._straighten = nAngles[i];

		const CRect rc = edits.CropBounds(size);
		const double angle = -(nAngles[i] / 10.0) * 3.14159265358979323846 / 180.0;
		const double cs = cos(angle);
		const double sn = sin(angle);

		const CPoint corners[] =
		{
			CPoint(rc.left, rc.top), CPoint(rc.right, rc.top),
			CPoint(rc.right, rc.bottom), CPoint(rc.left, rc.bottom)
		};

		for (int j = 0; j < countof(corners); j++)
		{
			const double dx = corners[j].x - size.cx / 2.0;
			const double dy = corners[j].y - size.cy / 2.0;

			const double sx = dx * cs - dy * sn + size.cx / 2.0;
			const double sy = dx * sn + dy * cs + size.cy / 2.0;

			IW_CHECK(sx >= -1.0 && sx <= size.cx + 1.0);
			IW_CHECK(sy >= -1.0 && sy <= size.cy + 1.0);
		}
	}
}

// Perspective empties a band along one edge of the frame and a wedge at each
// end of it, and CropBounds used to return early unless straighten was set --
// so the handles sat over the empty part and Save wrote it into the file.
IW_TEST(CropBoundsStayInsideThePerspectivePicture)
{
	const int nSliders[][2] = { {60, 0}, {0, 60}, {-80, 0}, {40, -40}, {100, 0} };

	for (int i = 0; i < countof(nSliders); i++)
	{
		ImageEdits edits;
		edits._perspectiveH = nSliders[i][0];
		edits._perspectiveV = nSliders[i][1];

		const IW::Image image = MakeTestImage(160, 120);
		const CRect rc = edits.CropBounds(CSize(160, 120));

		IW_CHECK(rc.Width() > 16 && rc.Height() > 16);

		IW::Image out;
		IW_CHECK(IW::ApplyEdits(image, out, edits, IW::CNullStatus::Instance));

		// Every pixel the handles can enclose has to be on the picture. The
		// warp leaves everything else at alpha 0.
		int nEmpty = 0;

		for (int y = rc.top; y < rc.bottom; y++)
			for (int x = rc.left; x < rc.right; x++)
				if (IW::GetA(RawPixelAt(out, x, y)) < 250)
					nEmpty++;

		IW_CHECK_EQ(nEmpty, 0);
	}
}

// A free rotation is three shears through two scratch images. The first of them
// was being created in the destination, and CreatePage(cx, cy, pf) frees the
// image first -- so each page rotated discarded the pages before it.
IW_TEST(FreeRotationKeepsEveryPage)
{
	IW::Image image;

	for (int page = 0; page < 3; page++)
		image.CreatePage(CRect(0, 0, 40, 24), IW::PixelFormat::PF32);

	IW_CHECK_EQ(image.GetPageCount(), 3u);

	const float angles[] = { 10.0f, 30.0f, 100.0f, 200.0f, 300.0f };

	for (int i = 0; i < countof(angles); i++)
	{
		IW::Image rotated;
		IW_CHECK(IW::Rotate(image, rotated, angles[i], IW::CNullStatus::Instance));
		IW_CHECK_EQ(rotated.GetPageCount(), 3u);
	}
}

// filters[0] ("Box Point") declares zero support, so any output centre that did
// not land exactly on a source pixel gathered no samples and came out black.
// 49 and 61 are chosen because 96/64 and 72/48 map them between source pixels.
IW_TEST(ResamplingScalerKeepsTheImageForEveryFilter)
{
	const IW::Image image = MakeTestImage(64, 48);

	for (int nFilter = 0; nFilter < 16; nFilter++)
	{
		IW::Image scaled;
		IW_CHECK(IW::Scale(CSize(96, 72), nFilter, image, scaled, IW::CNullStatus::Instance));

		IW_CHECK_EQ(scaled.GetPageCount(), 1u);
		IW_CHECK_EQ(scaled.GetFirstPage().GetWidth(), 96);
		IW_CHECK_EQ(scaled.GetFirstPage().GetHeight(), 72);

		// Source green is the row index, so output row 61 samples source row 40.
		IW_CHECK(IW::GetG(PixelAt(scaled, 49, 61)) > 20);
	}
}

// The filtered scaler creates a PF32Alpha page when the source has alpha, then
// used RenderLine to read it -- which composites the alpha away and hands back
// 0xFF, flattening a transparent PNG onto its background. Read with GetLine,
// not RenderLine, or the test cannot see the bug.
IW_TEST(TheResamplingScalerKeepsTheAlphaChannel)
{
	const int cx = 64;
	const int cy = 48;

	IW::Image image;
	IW::Page &page = image.CreatePage(cx, cy, IW::PixelFormat::PF32Alpha);

	{
		IW::IImageSurfaceLockPtr pLock = page.GetSurfaceLock();
		std::vector<COLORREF> line(cx);

		for (int y = 0; y < cy; y++)
		{
			// A large flat block of each, so a resampled pixel well inside one
			// of them cannot be an edge blend of the other.
			for (int x = 0; x < cx; x++)
				line[x] = IW::RGBA(200, 30, 40, (x < cx / 2) ? 255 : 0);

			pLock->SetLine(&line[0], y, 0, cx);
		}
	}

	for (int nFilter = 1; nFilter < 16; nFilter++)
	{
		IW::Image scaled;
		IW_CHECK(IW::Scale(CSize(cx * 2, cy * 2), nFilter, image, scaled, IW::CNullStatus::Instance));
		IW_CHECK(scaled.GetFirstPage().GetPixelFormat().HasAlpha());

		// Deep inside each block, so no filter's support reaches the boundary.
		IW_CHECK(IW::GetA(RawPixelAt(scaled, 8, cy)) >= 250);
		IW_CHECK(IW::GetA(RawPixelAt(scaled, cx * 2 - 8, cy)) <= 5);
	}
}

// Every indexed format must survive a SetPixel/GetPixel round trip for all of
// its indices. This is what catches a wrong sub-byte mask table: 2bpp shipped
// with masks2[] = {0xC0,0x30,0xC0,0x30} indexed by (x & 2), which decoded three
// pixels in four wrongly and produced palette indices well past the 4 entries
// a PF2 page has.
IW_TEST(SubByteIndicesRoundTripForEveryPixel)
{
	const IW::PixelFormat formats[] = {
		IW::PixelFormat::PF1, IW::PixelFormat::PF2,
		IW::PixelFormat::PF4, IW::PixelFormat::PF8
	};

	for (int f = 0; f < countof(formats); f++)
	{
		const IW::PixelFormat pf = formats[f];
		const int nEntries = pf.NumberOfPaletteEntries();
		const int cx = 37; // deliberately not a multiple of 8
		const int cy = 3;

		IW::Image image;
		IW::Page &page = image.CreatePage(cx, cy, pf);
		IW::IImageSurfaceLockPtr pLock = page.GetSurfaceLock();

		for (int y = 0; y < cy; y++)
			for (int x = 0; x < cx; x++)
				pLock->SetPixel(x, y, (x + y) % nEntries);

		for (int y = 0; y < cy; y++)
			for (int x = 0; x < cx; x++)
				// The indexed locks OR in an opaque alpha byte on the way out
				IW_CHECK_EQ(static_cast<int>(pLock->GetPixel(x, y) & 0x00FFFFFF),
				            (x + y) % nEntries);
	}
}

// Crop and Rotate180 on a PF2/PF4 page go through CImageLock2/4::SetLine, which
// used to be an assert(0) stub. Page storage is never zero-filled, so in Release
// that left the destination page holding raw heap.
static void FillIndexedTestPage(IW::Page &page, int cx, int cy, int nEntries)
{
	LPCOLORREF pPalette = page.GetPalette();

	// Entries must be distinct so the index is recoverable. Byte 0 is also kept
	// different from the index, so the test stays valid if this tree ever adopts
	// 2.30's palette-space GetLine.
	for (int i = 0; i < nEntries; i++)
		pPalette[i] = IW::RGBA((i * 13 + 7) & 0xff, (i * 7) & 0xff, i);

	IW::IImageSurfaceLockPtr pLock = page.GetSurfaceLock();

	for (int y = 0; y < cy; y++)
		for (int x = 0; x < cx; x++)
			pLock->SetPixel(x, y, (x * 3 + y * 5) % nEntries);
}

IW_TEST(CropPreservesPaletteIndices)
{
	const IW::PixelFormat formats[] = {
		IW::PixelFormat::PF1, IW::PixelFormat::PF2,
		IW::PixelFormat::PF4, IW::PixelFormat::PF8
	};
	const int cx = 20;
	const int cy = 12;

	for (int f = 0; f < countof(formats); f++)
	{
		const IW::PixelFormat pf = formats[f];
		const int nEntries = pf.NumberOfPaletteEntries();

		IW::Image image;
		IW::Page &page = image.CreatePage(cx, cy, pf);
		FillIndexedTestPage(page, cx, cy, nEntries);

		IW::Image cropped;
		IW_CHECK(IW::Crop(image, cropped, CRect(4, 3, 16, 11), IW::CNullStatus::Instance));

		IW::Page pageOut = cropped.GetFirstPage();
		IW_CHECK_EQ(pageOut.GetWidth(), 12);
		IW_CHECK_EQ(pageOut.GetHeight(), 8);

		IW::ConstRefPtr<IW::IImageSurfaceLock> pLockOut = pageOut.GetSurfaceLock();

		for (int y = 0; y < 8; y++)
			for (int x = 0; x < 12; x++)
				IW_CHECK_EQ(static_cast<int>(pLockOut->GetPixel(x, y) & 0x00FFFFFF),
				            ((x + 4) * 3 + (y + 3) * 5) % nEntries);
	}
}

IW_TEST(RotationPreservesPaletteIndices)
{
	const IW::PixelFormat formats[] = {
		IW::PixelFormat::PF1, IW::PixelFormat::PF2,
		IW::PixelFormat::PF4, IW::PixelFormat::PF8
	};
	const int cx = 13;
	const int cy = 7;

	for (int f = 0; f < countof(formats); f++)
	{
		const IW::PixelFormat pf = formats[f];
		const int nEntries = pf.NumberOfPaletteEntries();

		IW::Image image;
		IW::Page &page = image.CreatePage(cx, cy, pf);
		FillIndexedTestPage(page, cx, cy, nEntries);

		IW::Image r90, r180, r270;

		IW_CHECK(IW::Rotate90(image, r90, IW::CNullStatus::Instance));
		IW_CHECK(IW::Rotate180(image, r180, IW::CNullStatus::Instance));
		IW_CHECK(IW::Rotate270(image, r270, IW::CNullStatus::Instance));

		IW_CHECK_EQ(r90.GetFirstPage().GetWidth(), cy);
		IW_CHECK_EQ(r90.GetFirstPage().GetHeight(), cx);
		IW_CHECK_EQ(r180.GetFirstPage().GetWidth(), cx);
		IW_CHECK_EQ(r180.GetFirstPage().GetHeight(), cy);
		IW_CHECK_EQ(r270.GetFirstPage().GetWidth(), cy);
		IW_CHECK_EQ(r270.GetFirstPage().GetHeight(), cx);

		IW::Page page90 = r90.GetFirstPage();
		IW::Page page180 = r180.GetFirstPage();
		IW::Page page270 = r270.GetFirstPage();

		IW::ConstRefPtr<IW::IImageSurfaceLock> p90 = page90.GetSurfaceLock();
		IW::ConstRefPtr<IW::IImageSurfaceLock> p180 = page180.GetSurfaceLock();
		IW::ConstRefPtr<IW::IImageSurfaceLock> p270 = page270.GetSurfaceLock();

		for (int y = 0; y < cy; y++)
		{
			for (int x = 0; x < cx; x++)
			{
				const int nIndex = (x * 3 + y * 5) % nEntries;

				IW_CHECK_EQ(static_cast<int>(p90->GetPixel(cy - (y + 1), x) & 0x00FFFFFF), nIndex);
				IW_CHECK_EQ(static_cast<int>(p180->GetPixel(cx - (x + 1), cy - (y + 1)) & 0x00FFFFFF), nIndex);
				IW_CHECK_EQ(static_cast<int>(p270->GetPixel(y, cx - (x + 1)) & 0x00FFFFFF), nIndex);
			}
		}
	}
}

// The right-angle rotations transpose a tile of source rows at a time, so only a
// page taller than one tile reaches the second pass and the short final tile.
// Every other rotation test here fits in the first tile.
IW_TEST(RotationIsExactAcrossTileBoundaries)
{
	const IW::PixelFormat formats[] = {
		IW::PixelFormat::PF1, IW::PixelFormat::PF2,
		IW::PixelFormat::PF4, IW::PixelFormat::PF8
	};
	const int cx = 13;
	const int cy = 150;

	for (int f = 0; f < countof(formats); f++)
	{
		const IW::PixelFormat pf = formats[f];
		const int nEntries = pf.NumberOfPaletteEntries();

		IW::Image image;
		IW::Page &page = image.CreatePage(cx, cy, pf);
		FillIndexedTestPage(page, cx, cy, nEntries);

		IW::Image r90, r270;
		IW_CHECK(IW::Rotate90(image, r90, IW::CNullStatus::Instance));
		IW_CHECK(IW::Rotate270(image, r270, IW::CNullStatus::Instance));

		IW::Page page90 = r90.GetFirstPage();
		IW::Page page270 = r270.GetFirstPage();

		IW_CHECK_EQ(page90.GetWidth(), cy);
		IW_CHECK_EQ(page90.GetHeight(), cx);
		IW_CHECK_EQ(page270.GetWidth(), cy);
		IW_CHECK_EQ(page270.GetHeight(), cx);

		IW::ConstRefPtr<IW::IImageSurfaceLock> p90 = page90.GetSurfaceLock();
		IW::ConstRefPtr<IW::IImageSurfaceLock> p270 = page270.GetSurfaceLock();

		for (int y = 0; y < cy; y++)
		{
			for (int x = 0; x < cx; x++)
			{
				const int nIndex = (x * 3 + y * 5) % nEntries;

				IW_CHECK_EQ(static_cast<int>(p90->GetPixel(cy - (y + 1), x) & 0x00FFFFFF), nIndex);
				IW_CHECK_EQ(static_cast<int>(p270->GetPixel(y, cx - (x + 1)) & 0x00FFFFFF), nIndex);
			}
		}
	}
}

// Mirroring reverses a whole row rather than swapping pixel pairs. An odd width
// is what catches a centre column dropped, or written twice.
IW_TEST(MirrorReversesEveryRow)
{
	const int cx = 37;
	const int cy = 19;
	const IW::Image image = MakeTestImage(cx, cy);

	IW::Image mirrored;
	IW_CHECK(IW::MirrorLR(image, mirrored, IW::CNullStatus::Instance));

	IW_CHECK_EQ(mirrored.GetFirstPage().GetWidth(), cx);
	IW_CHECK_EQ(mirrored.GetFirstPage().GetHeight(), cy);

	for (int y = 0; y < cy; y++)
		for (int x = 0; x < cx; x++)
			IW_CHECK_EQ(PixelAt(mirrored, x, y), PixelAt(image, cx - (x + 1), y));
}

// Same shape on a palettised page, where the line the mirror reverses holds
// indices rather than colours.
IW_TEST(MirrorPreservesPaletteIndices)
{
	const int cx = 13;
	const int cy = 7;
	const IW::PixelFormat pf = IW::PixelFormat::PF4;
	const int nEntries = pf.NumberOfPaletteEntries();

	IW::Image image;
	IW::Page &page = image.CreatePage(cx, cy, pf);
	FillIndexedTestPage(page, cx, cy, nEntries);

	IW::Image mirrored;
	IW_CHECK(IW::MirrorLR(image, mirrored, IW::CNullStatus::Instance));

	IW::Page pageOut = mirrored.GetFirstPage();
	IW::ConstRefPtr<IW::IImageSurfaceLock> pLock = pageOut.GetSurfaceLock();

	for (int y = 0; y < cy; y++)
		for (int x = 0; x < cx; x++)
			IW_CHECK_EQ(static_cast<int>(pLock->GetPixel(x, y) & 0x00FFFFFF),
			            ((cx - (x + 1)) * 3 + y * 5) % nEntries);
}

// PNG is lossless, so a save/load round trip must be pixel exact. This is the
// test that would catch a wrong row-buffer size in the encoder or decoder.
IW_TEST(PngRoundTripIsLossless)
{
	const IW::Image image = MakeTestImage(37, 19);

	CLoadPng loader;
	IW::SimpleBlob data;
	IW::StreamBlob<IW::SimpleBlob> streamOut(data);

	IW_CHECK(loader.Write(_T("PNG"), &streamOut, image, IW::CodecSettings(), IW::CNullStatus::Instance));
	IW_CHECK(data.GetDataSize() > 0);

	IW::StreamConstBlob streamIn(data);
	IW::Image reloaded;
	IW::ImageStream<IW::IImageStream> imageOut(reloaded);

	IW_CHECK(loader.Read(_T("PNG"), &streamIn, &imageOut, IW::CNullStatus::Instance));
	IW_CHECK_EQ(reloaded.GetFirstPage().GetWidth(), 37);
	IW_CHECK_EQ(reloaded.GetFirstPage().GetHeight(), 19);

	for (int y = 0; y < 19; y++)
	{
		for (int x = 0; x < 37; x++)
			IW_CHECK_EQ(PixelAt(reloaded, x, y) & 0xFFFFFF, PixelAt(image, x, y) & 0xFFFFFF);
	}
}

// The encoder read its RGBA rows back with RenderLine, which composites the pixel
// over the page background and returns alpha 0 -- so saving any 32-bit PNG wrote
// a fully transparent file, and a rotate or a filter blanked the picture on disk.
// Read with RawPixelAt: RenderLine would hide the bug on both sides.
IW_TEST(PngRoundTripKeepsTheAlphaChannel)
{
	const IW::Image image = MakeAlphaImage(37, 19, 0x60);

	CLoadPng loader;
	IW::SimpleBlob data;
	IW::StreamBlob<IW::SimpleBlob> streamOut(data);

	IW_CHECK(loader.Write(_T("PNG"), &streamOut, image, IW::CodecSettings(), IW::CNullStatus::Instance));
	IW_CHECK(data.GetDataSize() > 0);

	IW::StreamConstBlob streamIn(data);
	IW::Image reloaded;
	IW::ImageStream<IW::IImageStream> imageOut(reloaded);

	IW_CHECK(loader.Read(_T("PNG"), &streamIn, &imageOut, IW::CNullStatus::Instance));
	IW_CHECK(reloaded.GetFirstPage().GetPixelFormat() == IW::PixelFormat::PF32Alpha);

	for (int y = 0; y < 19; y++)
	{
		for (int x = 0; x < 37; x++)
			IW_CHECK_EQ(RawPixelAt(reloaded, x, y), RawPixelAt(image, x, y));
	}
}

// GIF is 8bpp and LZW is lossless, so a palette image must survive a save/load
// round trip exactly -- including the colour map, whose channel order is the one
// thing a GIF encoder is easy to get wrong.
IW_TEST(GifRoundTripIsLossless)
{
	const int cx = 37; // deliberately not a multiple of 8
	const int cy = 19;
	const int nEntries = 256;

	IW::Image image;
	IW::Page &page = image.CreatePage(cx, cy, IW::PixelFormat::PF8);
	FillIndexedTestPage(page, cx, cy, nEntries);

	CLoadGif loader;
	IW::SimpleBlob data;
	IW::StreamBlob<IW::SimpleBlob> streamOut(data);

	IW_CHECK(loader.Write(_T("GIF"), &streamOut, image, IW::CodecSettings(), IW::CNullStatus::Instance));
	IW_CHECK(data.GetDataSize() > 0);

	IW::StreamConstBlob streamIn(data);
	IW::Image reloaded;
	IW::ImageStream<IW::IImageStream> imageOut(reloaded);

	IW_CHECK(loader.Read(_T("GIF"), &streamIn, &imageOut, IW::CNullStatus::Instance));
	IW_CHECK_EQ(reloaded.GetFirstPage().GetWidth(), cx);
	IW_CHECK_EQ(reloaded.GetFirstPage().GetHeight(), cy);

	for (int y = 0; y < cy; y++)
		for (int x = 0; x < cx; x++)
			IW_CHECK_EQ(PixelAt(reloaded, x, y) & 0xFFFFFF, PixelAt(image, x, y) & 0xFFFFFF);
}

namespace
{

// libjpeg directly, so a test can produce the kinds of file this app must read
// but cannot itself write: grayscale, Adobe's inverted CMYK, and a file carrying
// a marker no loader here understands.
void EncodeRawJpeg(IW::SimpleBlob &data, int cx, int cy, J_COLOR_SPACE cs,
                   const BYTE *pSamples, const char *szComment)
{
	IW::StreamBlob<IW::SimpleBlob> streamOut(data);

	jpeg_compress_struct cinfo;
	jpeg_error_mgr jerr;

	memset(&cinfo, 0, sizeof(cinfo));
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	jpeg_iw_dest(&cinfo, &streamOut);

	cinfo.image_width = cx;
	cinfo.image_height = cy;
	cinfo.in_color_space = cs;
	cinfo.input_components = (cs == JCS_GRAYSCALE) ? 1 : ((cs == JCS_CMYK) ? 4 : 3);

	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, 95, TRUE);
	jpeg_start_compress(&cinfo, TRUE);

	if (szComment != nullptr)
	{
		jpeg_write_marker(&cinfo, JPEG_COM, reinterpret_cast<const JOCTET *>(szComment),
		                  static_cast<unsigned>(strlen(szComment)));
	}

	const int nStride = cx * cinfo.input_components;

	for (int y = 0; y < cy; y++)
	{
		JSAMPROW pRow = const_cast<JSAMPROW>(pSamples) + (y * nStride);
		jpeg_write_scanlines(&cinfo, &pRow, 1);
	}

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
}

// ImageStream always answers "no thumbnail", so the decoder's scaling branch is
// unreachable through it.
class ThumbnailStream : public IW::ImageStream<IW::IImageStream>
{
public:

	CSize _sizeThumbnail;

	ThumbnailStream(IW::Image &image, int cx, int cy) :
		IW::ImageStream<IW::IImageStream>(image), _sizeThumbnail(cx, cy)
	{
	}

	CSize GetThumbnailSize() override { return _sizeThumbnail; }
};

bool Contains(const IW::SimpleBlob &data, const char *szNeedle)
{
	const size_t nNeedle = strlen(szNeedle);
	const size_t nData = data.GetDataSize();
	LPCBYTE p = data.GetData();

	for (size_t i = 0; i + nNeedle <= nData; i++)
	{
		if (memcmp(p + i, szNeedle, nNeedle) == 0)
			return true;
	}

	return false;
}

IW::Image DecodeJpeg(const IW::SimpleBlob &data)
{
	IW::StreamConstBlob streamIn(data);
	IW::Image image;
	IW::ImageStream<IW::IImageStream> imageOut(image);

	CLoadJpg loader;
	loader.Read(_T("JPG"), &streamIn, &imageOut, IW::CNullStatus::Instance);

	return image;
}

} // namespace

// scale_denom is a divisor, and 0 is not "no scaling". libjpeg-turbo's ladder is
// a chain of "scale_num * 8 <= scale_denom * N" tests, so a zero denominator
// fails every one and falls into the final branch -- 16/8, a 2x enlargement.
// Every image too small to downscale was being decoded at double size.
IW_TEST(AThumbnailDecodeNeverEnlargesTheImage)
{
	const int cx = 64;
	const int cy = 48;

	std::vector<BYTE> samples(cx * cy * 3);

	for (int i = 0; i < cx * cy; i++)
	{
		samples[i * 3 + 0] = static_cast<BYTE>((i / cx) * 4);
		samples[i * 3 + 1] = static_cast<BYTE>((i % cx) * 4);
		samples[i * 3 + 2] = 0x40;
	}

	IW::SimpleBlob data;
	EncodeRawJpeg(data, cx, cy, JCS_RGB, &samples[0], nullptr);

	CLoadJpg loader;

	{
		// Room to spare, so no scaling is called for at all.
		IW::StreamConstBlob streamIn(data);
		IW::Image reloaded;
		ThumbnailStream imageOut(reloaded, 200, 200);

		IW_CHECK(loader.Read(_T("JPG"), &streamIn, &imageOut, IW::CNullStatus::Instance));
		IW_CHECK_EQ(reloaded.GetFirstPage().GetWidth(), cx);
		IW_CHECK_EQ(reloaded.GetFirstPage().GetHeight(), cy);
	}

	{
		// And the downscaling ladder still works: 64x48 into 8x8 takes 1/4.
		IW::StreamConstBlob streamIn(data);
		IW::Image reloaded;
		ThumbnailStream imageOut(reloaded, 8, 8);

		IW_CHECK(loader.Read(_T("JPG"), &streamIn, &imageOut, IW::CNullStatus::Instance));
		IW_CHECK_EQ(reloaded.GetFirstPage().GetWidth(), 16);
		IW_CHECK_EQ(reloaded.GetFirstPage().GetHeight(), 12);
	}
}

// Adobe writes CMYK samples inverted, and libjpeg hands them over as they are.
// Read with the textbook formula they come out as a photographic negative. The
// assertion is against the same picture encoded as RGB rather than against a
// colour literal, so it says nothing about which byte of a page is which.
IW_TEST(AnAdobeCmykJpegDecodesToTheColoursItStores)
{
	const int cx = 32;
	const int cy = 32;

	std::vector<BYTE> cmyk(cx * cy * 4);
	std::vector<BYTE> rgb(cx * cy * 3);

	for (int i = 0; i < cx * cy; i++)
	{
		const BYTE a = static_cast<BYTE>(((i % cx) / 4) * 32);
		const BYTE b = static_cast<BYTE>(((i / cx) / 4) * 32);
		const BYTE c = 0x60;

		rgb[i * 3 + 0] = a;
		rgb[i * 3 + 1] = b;
		rgb[i * 3 + 2] = c;

		// K carries no ink, so an inverted store is the colour itself.
		cmyk[i * 4 + 0] = a;
		cmyk[i * 4 + 1] = b;
		cmyk[i * 4 + 2] = c;
		cmyk[i * 4 + 3] = 0xFF;
	}

	IW::SimpleBlob cmykData;
	EncodeRawJpeg(cmykData, cx, cy, JCS_CMYK, &cmyk[0], nullptr);

	IW::SimpleBlob rgbData;
	EncodeRawJpeg(rgbData, cx, cy, JCS_RGB, &rgb[0], nullptr);

	const IW::Image decoded = DecodeJpeg(cmykData);
	const IW::Image control = DecodeJpeg(rgbData);

	IW_CHECK(!decoded.IsEmpty());
	IW_CHECK(!control.IsEmpty());
	IW_CHECK_EQ(decoded.GetFirstPage().GetWidth(), cx);

	int nWorst = 0;

	for (int y = 0; y < cy; y++)
	{
		for (int x = 0; x < cx; x++)
		{
			const COLORREF want = PixelAt(control, x, y);
			const COLORREF got = PixelAt(decoded, x, y);

			nWorst = IW::Max(nWorst, abs(static_cast<int>(IW::GetR(want)) - static_cast<int>(IW::GetR(got))));
			nWorst = IW::Max(nWorst, abs(static_cast<int>(IW::GetG(want)) - static_cast<int>(IW::GetG(got))));
			nWorst = IW::Max(nWorst, abs(static_cast<int>(IW::GetB(want)) - static_cast<int>(IW::GetB(got))));
		}
	}

	IW_CHECK(nWorst <= 12);
}

// A grayscale JPEG decodes to one channel. Re-encoding it as three wrote a file
// three times the size that still only contained grey.
IW_TEST(AGrayscaleJpegStaysGrayscaleWhenSaved)
{
	const int cx = 64;
	const int cy = 48;

	std::vector<BYTE> samples(cx * cy);

	for (int i = 0; i < cx * cy; i++)
		samples[i] = static_cast<BYTE>((i / 4) & 0xFF);

	IW::SimpleBlob data;
	EncodeRawJpeg(data, cx, cy, JCS_GRAYSCALE, &samples[0], nullptr);

	IW::Image decoded = DecodeJpeg(data);
	IW_CHECK(decoded.GetFirstPage().GetPixelFormat() == IW::PixelFormat::PF8GrayScale);

	// Drop the raw stream, or the save copies the original through untouched and
	// the encoder under test never runs.
	decoded.Blobs.clear();

	CLoadJpg loader;
	IW::SimpleBlob resaved;
	IW::StreamBlob<IW::SimpleBlob> streamOut(resaved);

	IW_CHECK(loader.Write(_T("JPG"), &streamOut, decoded, IW::CodecSettings(), IW::CNullStatus::Instance));

	const IW::Image reloaded = DecodeJpeg(resaved);
	IW_CHECK(reloaded.GetFirstPage().GetPixelFormat() == IW::PixelFormat::PF8GrayScale);
	IW_CHECK_EQ(reloaded.GetFirstPage().GetWidth(), cx);
}

// A loss-less rotate rewrote only the four metadata profiles it recognises, so
// comments, APP12 and everything else in the source was dropped on the floor.
IW_TEST(ALosslessTransformKeepsMarkersItDoesNotUnderstand)
{
	const int cx = 32;
	const int cy = 32;
	const char *szComment = "IWTESTCOMMENT";

	std::vector<BYTE> samples(cx * cy * 3, 0x80);

	IW::SimpleBlob data;
	EncodeRawJpeg(data, cx, cy, JCS_RGB, &samples[0], szComment);
	IW_CHECK(Contains(data, szComment));

	const IW::Image image = DecodeJpeg(data);
	IW_CHECK(image.HasMetaData(IW::MetaDataTypes::JPEG_IMAGE));

	IW::SimpleBlob rotated;
	IW::StreamBlob<IW::SimpleBlob> streamOut(rotated);

	CJpegTransformation trans(&streamOut, image, IW::CNullStatus::Instance);
	image.IterateMetaData(&trans);

	IW_CHECK(trans._bSuccess);
	IW_CHECK(Contains(rotated, szComment));
}

// FixExif wrote every tag it touches as a 16 bit value. PixelXDimension is a
// LONG in most cameras' output, and on a Motorola-order file a 16 bit write
// lands in the high half -- multiplying the recorded width by 65536.
namespace
{

std::vector<BYTE> MakeExifBlob(bool bMotorola, IW::UInt16 tag, int format, IW::UInt32 value)
{
	std::vector<BYTE> exif(32, 0);

	memcpy(&exif[0], "Exif\0\0", 6);

	exif[6] = bMotorola ? 'M' : 'I';
	exif[7] = bMotorola ? 'M' : 'I';

	struct Put
	{
		bool _bMotorola;

		void U16(BYTE *p, IW::UInt16 v) const
		{
			p[_bMotorola ? 0 : 1] = static_cast<BYTE>(v >> 8);
			p[_bMotorola ? 1 : 0] = static_cast<BYTE>(v);
		}

		void U32(BYTE *p, IW::UInt32 v) const
		{
			U16(p + (_bMotorola ? 0 : 2), static_cast<IW::UInt16>(v >> 16));
			U16(p + (_bMotorola ? 2 : 0), static_cast<IW::UInt16>(v));
		}
	};

	const Put put = { bMotorola };

	put.U16(&exif[8], 0x002a);
	put.U32(&exif[10], 8);      // IFD 0 sits right after the TIFF header
	put.U16(&exif[14], 1);      // one entry
	put.U16(&exif[16], tag);
	put.U16(&exif[18], static_cast<IW::UInt16>(format));
	put.U32(&exif[20], 1);      // one component

	// A SHORT lives in the first two bytes of the field, so writing it as a LONG
	// puts it in the far half on a Motorola-order file: the same trap FixExif had.
	if (format == 3 || format == 8)
		put.U16(&exif[24], static_cast<IW::UInt16>(value));
	else
		put.U32(&exif[24], value);

	put.U32(&exif[28], 0);      // no IFD 1

	return exif;
}

IW::UInt32 ReadExifValue(const std::vector<BYTE> &exif, bool bMotorola, int format)
{
	const BYTE *p = &exif[24];

	if (format == 3) // SHORT, stored in the first two bytes of the field
		return bMotorola ? ((p[0] << 8) | p[1]) : ((p[1] << 8) | p[0]);

	return bMotorola
		       ? ((static_cast<IW::UInt32>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3])
		       : ((static_cast<IW::UInt32>(p[3]) << 24) | (p[2] << 16) | (p[1] << 8) | p[0]);
}

} // namespace

IW_TEST(FixExifWritesEachTagAtItsDeclaredWidth)
{
	const IW::UInt16 EXIF_PIXEL_X = 0xA002;
	const IW::UInt16 EXIF_ORIENTATION = 0x0112;

	for (int nPass = 0; nPass < 2; nPass++)
	{
		const bool bMotorola = (nPass == 1);

		// A LONG must be written across all four bytes, whichever way round it is.
		std::vector<BYTE> asLong = MakeExifBlob(bMotorola, EXIF_PIXEL_X, 4, 0);
		Exif::FixExif(&asLong[0], static_cast<unsigned>(asLong.size()), 4000, 3000);
		IW_CHECK_EQ(static_cast<int>(ReadExifValue(asLong, bMotorola, 4)), 4000);

		// And a SHORT must not spill into the two bytes beyond it.
		std::vector<BYTE> asShort = MakeExifBlob(bMotorola, EXIF_PIXEL_X, 3, 0);
		Exif::FixExif(&asShort[0], static_cast<unsigned>(asShort.size()), 640, 480);
		IW_CHECK_EQ(static_cast<int>(ReadExifValue(asShort, bMotorola, 3)), 640);

		std::vector<BYTE> orientation = MakeExifBlob(bMotorola, EXIF_ORIENTATION, 3, 6);

		// FixExif fixes dimensions. It used to clear the orientation tag as well,
		// which meant every save made with EXIF auto rotate switched off wrote the
		// pixels as stored and then threw away the tag that said how they were.
		Exif::FixExif(&orientation[0], static_cast<unsigned>(orientation.size()), 640, 480);
		IW_CHECK_EQ(static_cast<int>(ReadExifValue(orientation, bMotorola, 3)),
		            static_cast<int>(IW::Orientation::RightTop));
		IW_CHECK_EQ(Exif::ReadOrientation(&orientation[0], static_cast<unsigned>(orientation.size())),
		            static_cast<int>(IW::Orientation::RightTop));

		// Code that has actually reoriented the pixels says so explicitly.
		Exif::SetOrientation(&orientation[0], static_cast<unsigned>(orientation.size()),
		                     IW::Orientation::TopLeft);
		IW_CHECK_EQ(static_cast<int>(ReadExifValue(orientation, bMotorola, 3)),
		            static_cast<int>(IW::Orientation::TopLeft));
	}
}

// A quarter turn of what the user can see is a quarter turn composed with the
// orientation the camera recorded. Clearing the tag without folding it in -- the
// old behaviour -- left the picture a quarter turn out in every viewer that
// honours the tag, and a rotate right on a phone photo went the wrong way.
IW_TEST(RotatingAJpegFoldsInTheOrientationItWasStoredWith)
{
	// Stored a quarter turn clockwise from upright (RightTop, the usual portrait
	// phone photo). Turning the displayed picture right must leave the stored
	// pixels a half turn from upright, and turning it left must leave them upright.
	IW_CHECK_EQ(static_cast<int>(jtransform_compose_exif_orientation(JXFORM_ROT_90, IW::Orientation::RightTop)),
	            static_cast<int>(JXFORM_ROT_180));
	IW_CHECK_EQ(static_cast<int>(jtransform_compose_exif_orientation(JXFORM_ROT_270, IW::Orientation::RightTop)),
	            static_cast<int>(JXFORM_NONE));

	// The other way round, for a picture stored a quarter turn anticlockwise.
	IW_CHECK_EQ(static_cast<int>(jtransform_compose_exif_orientation(JXFORM_ROT_90, IW::Orientation::LeftBottom)),
	            static_cast<int>(JXFORM_NONE));
	IW_CHECK_EQ(static_cast<int>(jtransform_compose_exif_orientation(JXFORM_ROT_270, IW::Orientation::LeftBottom)),
	            static_cast<int>(JXFORM_ROT_180));

	// An upright file is left exactly as asked.
	IW_CHECK_EQ(static_cast<int>(jtransform_compose_exif_orientation(JXFORM_ROT_90, IW::Orientation::TopLeft)),
	            static_cast<int>(JXFORM_ROT_90));

	// A mirrored one has to come out mirrored the other way, not rotated: the
	// group is not commutative and getting the order wrong is silent.
	IW_CHECK_EQ(static_cast<int>(jtransform_compose_exif_orientation(JXFORM_ROT_90, IW::Orientation::TopRight)),
	            static_cast<int>(JXFORM_TRANSVERSE));
	IW_CHECK_EQ(static_cast<int>(jtransform_compose_exif_orientation(JXFORM_FLIP_H, IW::Orientation::TopRight)),
	            static_cast<int>(JXFORM_NONE));

	// Every orientation composed with no turn at all is just that orientation,
	// which is what makes "normalise the file" the same operation.
	const int orientations[] =
	{
		IW::Orientation::TopLeft, IW::Orientation::TopRight, IW::Orientation::BottomRight,
		IW::Orientation::BottomLeft, IW::Orientation::LeftTop, IW::Orientation::RightTop,
		IW::Orientation::RightBottom, IW::Orientation::LeftBottom
	};

	for (int o : orientations)
	{
		// Composing twice with the inverse turn gets back where it started.
		const JXFORM_CODE once = jtransform_compose_exif_orientation(JXFORM_NONE, o);
		IW_CHECK(once == JXFORM_NONE ? o == IW::Orientation::TopLeft : o != IW::Orientation::TopLeft);
	}
}

// The dialog has always said a run of #'s is the number and ? is the original
// name. What it did was copy one character of the original per ?, from the same
// index in the template -- so the template "?" renamed every file to one letter.
IW_TEST(ARenameTemplateNumbersAndKeepsTheOriginalName)
{
	IW_CHECK(FormatRenameName(_T("Holiday ###"), _T("DSC00214"), 7) == CString(_T("Holiday 007")));
	IW_CHECK(FormatRenameName(_T("Holiday #"), _T("DSC00214"), 7) == CString(_T("Holiday 7")));

	// A number too big for its run keeps all its digits. Truncating would make
	// two files ask for the same name.
	IW_CHECK(FormatRenameName(_T("#"), _T("x"), 123) == CString(_T("123")));

	IW_CHECK(FormatRenameName(_T("?"), _T("DSC00214"), 1) == CString(_T("DSC00214")));
	IW_CHECK(FormatRenameName(_T("? (##)"), _T("Fred"), 3) == CString(_T("Fred (03)")));
	IW_CHECK(FormatRenameName(_T("plain"), _T("Fred"), 3) == CString(_T("plain")));
}

namespace
{

RENAMEPLAN MakeRenamePlan(LPCTSTR szA, LPCTSTR szB)
{
	// A folder that is not there, so the "already on disk" test cannot fire and
	// what is left under test is the template and the clash detection.
	const CString strFolder = _T("C:\\iw-no-such-folder\\");
	const LPCTSTR names[] = { szA, szB };

	RENAMEPLAN plan;

	for (LPCTSTR sz : names)
	{
		RenameItem item;
		item.strFolder = strFolder;
		item.strBase = sz;
		item.strExt = _T(".jpg");
		item.strName = item.strBase + item.strExt;
		item.strPath = strFolder + item.strName;
		plan.push_back(item);
	}

	return plan;
}

}

IW_TEST(TheRenamePreviewRefusesNamesThatWouldCollide)
{
	// Two files, one name. Renaming one and skipping the other is not a decision
	// to take on the user's behalf, so it stops the run instead -- and BOTH rows
	// say so, because the clean-looking one is where the user would go to look.
	RENAMEPLAN same = MakeRenamePlan(_T("a"), _T("b"));
	BuildRenamePlan(same, _T("fixed"), 1);
	IW_CHECK(!same[0].strProblem.IsEmpty());
	IW_CHECK(!same[1].strProblem.IsEmpty());

	// A character the file system will not take.
	RENAMEPLAN bad = MakeRenamePlan(_T("a"), _T("b"));
	BuildRenamePlan(bad, _T("a:b#"), 1);
	IW_CHECK(!bad[0].strProblem.IsEmpty());

	// Shifting the numbering means every new name is one that another file in the
	// run holds right now. That is fine -- the rename goes through temporary
	// names -- and calling it a clash would break the commonest use there is.
	RENAMEPLAN shifted = MakeRenamePlan(_T("1"), _T("2"));
	BuildRenamePlan(shifted, _T("#"), 2);
	IW_CHECK(shifted[0].strNewName == CString(_T("2.jpg")));
	IW_CHECK(shifted[1].strNewName == CString(_T("3.jpg")));
	IW_CHECK(shifted[0].strProblem.IsEmpty());
	IW_CHECK(shifted[1].strProblem.IsEmpty());
}

// The name that is already taken is the only rule in the plan that looks at the
// disk, and it is what stops a rename landing on somebody else's file.
IW_TEST(TheRenamePreviewSeesANameThatIsAlreadyTaken)
{
	TCHAR szTempDir[MAX_PATH] = { 0 };
	::GetTempPath(MAX_PATH, szTempDir);

	const CString strFolder = IW::Path::Combine(szTempDir, _T("iw-rename-test"));
	::CreateDirectory(strFolder, nullptr);

	const CString strTaken = IW::Path::Combine(strFolder, _T("taken.jpg"));

	{
		IW::CFile f;
		IW_CHECK(f.OpenForWrite(strTaken));
	}

	RENAMEPLAN plan;
	RenameItem item;
	item.strFolder = strFolder;
	item.strBase = _T("b");
	item.strExt = _T(".jpg");
	item.strName = _T("b.jpg");
	item.strPath = IW::Path::Combine(strFolder, item.strName);
	plan.push_back(item);

	BuildRenamePlan(plan, _T("taken"), 1);
	IW_CHECK(plan[0].strNewName == CString(_T("taken.jpg")));
	IW_CHECK(!plan[0].strProblem.IsEmpty());

	// And free again the moment nothing holds it.
	::DeleteFile(strTaken);
	BuildRenamePlan(plan, _T("taken"), 1);
	IW_CHECK(plan[0].strProblem.IsEmpty());

	::RemoveDirectory(strFolder);
}

// The Convert tool hands whatever the decoder produced straight to the encoder,
// so every saver has to cope with a page it did not choose the format of: a JPEG
// decodes to PF24, a BMP to PF24 or PF32, a PNG with alpha to PF32Alpha and a GIF
// to PF8. An encoder that cannot is what turns a conversion into a blank file.
namespace
{

IW::Image MakeTrueColourImage(int cx, int cy, IW::PixelFormat pf)
{
	IW::Image image;
	IW::Page &page = image.CreatePage(cx, cy, pf);
	IW::IImageSurfaceLockPtr pLock = page.GetSurfaceLock();

	std::vector<COLORREF> line(cx);

	for (int y = 0; y < cy; y++)
	{
		// Broad bands rather than a per-pixel gradient: a quantiser only has 256
		// entries, so a tolerance test needs colours it can actually represent.
		for (int x = 0; x < cx; x++)
			line[x] = IW::RGBA((x / 4) * 16, (y / 4) * 16, ((x + y) / 8) * 32);

		pLock->SetLine(&line[0], y, 0, cx);
	}

	return image;
}

IW::Image MakePalettedImage(int cx, int cy)
{
	IW::Image image;
	IW::Page &page = image.CreatePage(cx, cy, IW::PixelFormat::PF8);
	LPCOLORREF pPalette = page.GetPalette();

	for (int i = 0; i < 256; i++)
		pPalette[i] = IW::RGBA((i & 0x0F) * 16, (i >> 4) * 16, 120);

	IW::IImageSurfaceLockPtr pLock = page.GetSurfaceLock();

	// The same flat blocks as the true colour image: a lossy encoder has to be
	// able to reproduce this, or its tolerance below stops meaning anything.
	for (int y = 0; y < cy; y++)
		for (int x = 0; x < cx; x++)
			pLock->SetPixel(x, y, ((x / 4) + (y / 4) * 16) & 0xFF);

	return image;
}

// Encode, decode, and report the worst channel error against the source. A blank,
// white or transparent result reads as an error near 255, which is the whole point
// -- these formats are not all lossless, so an exact comparison cannot be used.
// The label carries the format and the source page format into the failure line,
// because one shared assertion for twenty cases otherwise names none of them.
template<class TLoader>
void CheckConversion(LPCTSTR szType, const char *szLabel, const IW::Image &image,
                     int nWorstTolerance, int nMeanTolerance)
{
	char text[256];

	TLoader loader;
	IW::SimpleBlob data;
	IW::StreamBlob<IW::SimpleBlob> streamOut(data);

	if (!loader.Write(szType, &streamOut, image, IW::CodecSettings(), IW::CNullStatus::Instance) ||
		data.GetDataSize() == 0)
	{
		sprintf_s(text, "%s: encoder wrote nothing", szLabel);
		::IW::Test::Fail(text, __FILE__, __LINE__);
		return;
	}

	IW::StreamConstBlob streamIn(data);
	IW::Image reloaded;
	IW::ImageStream<IW::IImageStream> imageOut(reloaded);

	if (!loader.Read(szType, &streamIn, &imageOut, IW::CNullStatus::Instance) ||
		reloaded.IsEmpty())
	{
		sprintf_s(text, "%s: decoder read nothing back", szLabel);
		::IW::Test::Fail(text, __FILE__, __LINE__);
		return;
	}

	const int cx = image.GetFirstPage().GetWidth();
	const int cy = image.GetFirstPage().GetHeight();

	if (reloaded.GetFirstPage().GetWidth() != cx ||
		reloaded.GetFirstPage().GetHeight() != cy)
	{
		sprintf_s(text, "%s: came back %dx%d, wanted %dx%d", szLabel,
		          reloaded.GetFirstPage().GetWidth(), reloaded.GetFirstPage().GetHeight(), cx, cy);
		::IW::Test::Fail(text, __FILE__, __LINE__);
		return;
	}

	int nWorst = 0;
	int nTotal = 0;

	for (int y = 0; y < cy; y++)
	{
		for (int x = 0; x < cx; x++)
		{
			const COLORREF want = PixelAt(image, x, y);
			const COLORREF got = PixelAt(reloaded, x, y);

			const int nR = abs(static_cast<int>(IW::GetR(want)) - static_cast<int>(IW::GetR(got)));
			const int nG = abs(static_cast<int>(IW::GetG(want)) - static_cast<int>(IW::GetG(got)));
			const int nB = abs(static_cast<int>(IW::GetB(want)) - static_cast<int>(IW::GetB(got)));

			nWorst = IW::Max(nWorst, IW::Max(nR, IW::Max(nG, nB)));
			nTotal += nR + nG + nB;
		}
	}

	const int nMean = nTotal / (cx * cy * 3);

	if (nWorst > nWorstTolerance || nMean > nMeanTolerance)
	{
		sprintf_s(text, "%s: worst %d (max %d), mean %d (max %d)",
		          szLabel, nWorst, nWorstTolerance, nMean, nMeanTolerance);
		::IW::Test::Fail(text, __FILE__, __LINE__);
	}
}

// Encode and decode through one loader, so a test can assert against what the
// app would really have on screen rather than against the page it started with.
template<class TLoader>
IW::Image ReloadThrough(LPCTSTR szType, const IW::Image &image)
{
	TLoader loader;
	IW::SimpleBlob data;
	IW::StreamBlob<IW::SimpleBlob> streamOut(data);

	IW::Image reloaded;

	if (loader.Write(szType, &streamOut, image, IW::CodecSettings(), IW::CNullStatus::Instance))
	{
		IW::StreamConstBlob streamIn(data);
		IW::ImageStream<IW::IImageStream> imageOut(reloaded);
		loader.Read(szType, &streamIn, &imageOut, IW::CNullStatus::Instance);
	}

	return reloaded;
}

// Only the entries the picture actually references: a colour map may be shorter
// than 256, and the rest of the table is whatever the loader left there.
void CheckPaletteIsOpaque(const IW::Image &image, const char *szLabel)
{
	char text[256];

	if (image.IsEmpty())
	{
		sprintf_s(text, "%s: nothing came back", szLabel);
		::IW::Test::Fail(text, __FILE__, __LINE__);
		return;
	}

	IW::Page page = image.GetFirstPage();

	if (!page.GetPixelFormat().HasPalette())
	{
		sprintf_s(text, "%s: came back as %d bpp, wanted a palette",
		          szLabel, page.GetPixelFormat().ToBpp());
		::IW::Test::Fail(text, __FILE__, __LINE__);
		return;
	}

	IW::LPCCOLORREF pPalette = page.GetPalette();
	IW::ConstIImageSurfaceLockPtr pLock = page.GetSurfaceLock();

	for (int y = 0; y < page.GetHeight(); y++)
	{
		for (int x = 0; x < page.GetWidth(); x++)
		{
			const int nIndex = static_cast<int>(pLock->GetPixel(x, y) & 0x00FFFFFF);

			if (IW::GetA(pPalette[nIndex]) != 0xFF)
			{
				sprintf_s(text, "%s: palette entry %d has alpha %d at (%d,%d)",
				          szLabel, nIndex, IW::GetA(pPalette[nIndex]), x, y);
				::IW::Test::Fail(text, __FILE__, __LINE__);
				return;
			}
		}
	}
}

} // namespace

// GIF is the only 8bpp target, so it is the only encoder that has to quantise,
// and "convert a photo to GIF" is the path that exercises it.
IW_TEST(GifWriteQuantisesATrueColourImage)
{
	CheckConversion<CLoadGif>(_T("GIF"), "GIF from PF24 64x48",
	                          MakeTrueColourImage(64, 48, IW::PixelFormat::PF24), 24, 4);
	CheckConversion<CLoadGif>(_T("GIF"), "GIF from PF32 37x19",
	                          MakeTrueColourImage(37, 19, IW::PixelFormat::PF32), 24, 4);
	CheckConversion<CLoadGif>(_T("GIF"), "GIF from PF32Alpha 37x19",
	                          MakeAlphaImage(37, 19, 0xFF), 24, 4);
}

// Wu's quantiser accumulates its moments in blue/green/red order, so the palette
// it builds is the easiest thing in the codebase to get channel-swapped -- and a
// swapped palette still looks like a picture, which is why this checks colours.
IW_TEST(QuantizeKeepsTheColoursOfATrueColourImage)
{
	const IW::Image image = MakeTrueColourImage(64, 48, IW::PixelFormat::PF24);

	IW::Image quantized;
	IW_CHECK(IW::Quantize(image, quantized, IW::CNullStatus::Instance));

	IW::Page pageOut = quantized.GetFirstPage();
	IW_CHECK(pageOut.GetPixelFormat() == IW::PixelFormat::PF8);
	IW_CHECK_EQ(pageOut.GetWidth(), 64);
	IW_CHECK_EQ(pageOut.GetHeight(), 48);

	int nWorst = 0;

	for (int y = 0; y < 48; y++)
	{
		for (int x = 0; x < 64; x++)
		{
			const COLORREF want = PixelAt(image, x, y);
			const COLORREF got = PixelAt(quantized, x, y);

			nWorst = IW::Max(nWorst, abs(static_cast<int>(IW::GetR(want)) - static_cast<int>(IW::GetR(got))));
			nWorst = IW::Max(nWorst, abs(static_cast<int>(IW::GetG(want)) - static_cast<int>(IW::GetG(got))));
			nWorst = IW::Max(nWorst, abs(static_cast<int>(IW::GetB(want)) - static_cast<int>(IW::GetB(got))));
		}
	}

	IW_CHECK(nWorst <= 24);
}

// The fourth byte of a palette entry is alpha to everything downstream -- the
// render's alpha-weighted downscale among them -- so a palette producer that
// leaves it zero draws the picture as nothing at all. That is what made a JPEG
// converted to GIF load blank: the quantiser built the colour table with a zero
// there and the GIF reader put one back on the way in. Neither the round trip
// tests nor a single-pixel read can see it, because both go through code that
// resolves the palette itself.
IW_TEST(EveryPaletteProducerLeavesItOpaque)
{
	const IW::Image paletted = MakePalettedImage(64, 48);
	const IW::Image photo = MakeTrueColourImage(64, 48, IW::PixelFormat::PF24);

	IW::Image quantized;
	IW_CHECK(IW::Quantize(photo, quantized, IW::CNullStatus::Instance));
	CheckPaletteIsOpaque(quantized, "quantiser");

	CheckPaletteIsOpaque(ReloadThrough<CLoadGif>(_T("GIF"), paletted), "GIF decoder, from PF8");
	CheckPaletteIsOpaque(ReloadThrough<CLoadGif>(_T("GIF"), photo), "GIF decoder, from PF24");
	CheckPaletteIsOpaque(ReloadThrough<CLoadPng>(_T("PNG"), paletted), "PNG decoder, from PF8");
	CheckPaletteIsOpaque(ReloadThrough<CLoadBmp>(_T("BMP"), paletted), "BMP decoder, from PF8");
}

// One case per encoder the Convert tool offers, against each page format a
// decoder in this build can hand it.
IW_TEST(EveryEncoderTakesAnyDecodedPage)
{
	const IW::Image trueColour = MakeTrueColourImage(64, 48, IW::PixelFormat::PF24);
	const IW::Image packed = MakeTrueColourImage(64, 48, IW::PixelFormat::PF32);
	const IW::Image alpha = MakeAlphaImage(64, 48, 0xFF);
	const IW::Image paletted = MakePalettedImage(64, 48);

	CheckConversion<CLoadBmp>(_T("BMP"), "BMP from PF24", trueColour, 0, 0);
	CheckConversion<CLoadBmp>(_T("BMP"), "BMP from PF32", packed, 0, 0);
	CheckConversion<CLoadBmp>(_T("BMP"), "BMP from PF32Alpha", alpha, 0, 0);
	CheckConversion<CLoadBmp>(_T("BMP"), "BMP from PF8", paletted, 0, 0);

	CheckConversion<CLoadPng>(_T("PNG"), "PNG from PF24", trueColour, 0, 0);
	CheckConversion<CLoadPng>(_T("PNG"), "PNG from PF32", packed, 0, 0);
	CheckConversion<CLoadPng>(_T("PNG"), "PNG from PF32Alpha", alpha, 0, 0);
	CheckConversion<CLoadPng>(_T("PNG"), "PNG from PF8", paletted, 0, 0);

	CheckConversion<CLoadGif>(_T("GIF"), "GIF from PF24", trueColour, 24, 4);
	CheckConversion<CLoadGif>(_T("GIF"), "GIF from PF32", packed, 24, 4);
	CheckConversion<CLoadGif>(_T("GIF"), "GIF from PF32Alpha", alpha, 24, 4);
	CheckConversion<CLoadGif>(_T("GIF"), "GIF from PF8", paletted, 0, 0);

	// JPEG is lossy and subsamples chroma, so the worst pixel sits on a block edge
	// and says nothing. The mean over the picture is what distinguishes "the same
	// photo" from "a blank rectangle".
	CheckConversion<CLoadJpg>(_T("JPG"), "JPG from PF24", trueColour, 255, 8);
	CheckConversion<CLoadJpg>(_T("JPG"), "JPG from PF32", packed, 255, 8);
	CheckConversion<CLoadJpg>(_T("JPG"), "JPG from PF32Alpha", alpha, 255, 8);
	CheckConversion<CLoadJpg>(_T("JPG"), "JPG from PF8", paletted, 255, 8);

	CheckConversion<CLoadTiff>(_T("TIF"), "TIF from PF24", trueColour, 0, 0);
	CheckConversion<CLoadTiff>(_T("TIF"), "TIF from PF32", packed, 0, 0);
	CheckConversion<CLoadTiff>(_T("TIF"), "TIF from PF32Alpha", alpha, 0, 0);
	CheckConversion<CLoadTiff>(_T("TIF"), "TIF from PF8", paletted, 0, 0);
}

// The PNG loader stored its raw stream under JPEG_IMAGE, so every loaded PNG
// looked to the JPEG encoder like a JPEG it could copy through untouched.
IW_TEST(ALoadedPngIsNotMistakenForRawJpegData)
{
	const IW::Image png = ReloadThrough<CLoadPng>(_T("PNG"), MakeTrueColourImage(64, 48, IW::PixelFormat::PF24));

	IW_CHECK(!png.IsEmpty());
	IW_CHECK(!png.HasMetaData(IW::MetaDataTypes::JPEG_IMAGE));

	CheckConversion<CLoadJpg>(_T("JPG"), "JPG from a decoded PNG", png, 255, 8);
}

// The loss-less branch ignored its own result, so a transform that could not run
// returned success over an empty stream -- and the caller had already truncated
// the file it was saving over.
IW_TEST(ALosslessSaveThatCannotRunIsReportedAsAFailure)
{
	IW::Image image = MakeTrueColourImage(16, 16, IW::PixelFormat::PF24);

	const BYTE junk[64] = { 0 };
	image.SetMetaData(IW::MetaData(IW::MetaDataTypes::JPEG_IMAGE, junk, sizeof(junk)));

	CLoadJpg loader;
	IW::SimpleBlob data;
	IW::StreamBlob<IW::SimpleBlob> streamOut(data);

	IW_CHECK(!loader.Write(_T("JPG"), &streamOut, image, IW::CodecSettings(), IW::CNullStatus::Instance));
}

// An ICC profile bigger than one APP2 marker is split into numbered chunks. The
// reader kept only the last chunk -- each one replaced the blob before it -- and
// the writer refused anything it could not fit in a single marker.
IW_TEST(ALargeIccProfileSurvivesAJpegRoundTrip)
{
	std::vector<BYTE> profile(150000);

	for (size_t i = 0; i < profile.size(); i++)
		profile[i] = static_cast<BYTE>((i * 7) & 0xFF);

	IW::Image image = MakeTrueColourImage(32, 32, IW::PixelFormat::PF24);
	image.SetMetaData(IW::MetaData(IW::MetaDataTypes::PROFILE_ICC, &profile[0],
	                               static_cast<DWORD>(profile.size())));

	CLoadJpg loader;
	IW::SimpleBlob data;
	IW::StreamBlob<IW::SimpleBlob> streamOut(data);

	IW_CHECK(loader.Write(_T("JPG"), &streamOut, image, IW::CodecSettings(), IW::CNullStatus::Instance));

	const IW::Image reloaded = DecodeJpeg(data);
	IW_CHECK(reloaded.HasMetaData(IW::MetaDataTypes::PROFILE_ICC));

	const IW::MetaData icc = reloaded.GetMetaData(IW::MetaDataTypes::PROFILE_ICC);
	IW_CHECK_EQ(static_cast<int>(icc.GetDataSize()), static_cast<int>(profile.size()));

	if (icc.GetDataSize() == profile.size())
		IW_CHECK(memcmp(icc.GetData(), &profile[0], profile.size()) == 0);
}

// The Optimize option was only ever applied to loss-less transforms, so the
// checkbox did nothing to any file the encoder actually produced.
IW_TEST(OptimizeCodingReachesThePixelEncoder)
{
	const IW::Image image = MakeTrueColourImage(96, 96, IW::PixelFormat::PF24);

	IW::SimpleBlob plain;
	IW::SimpleBlob optimized;

	{
		CLoadJpg loader;
		IW::CodecSettings settings;
		settings.JpegOptimize = false;
		IW::StreamBlob<IW::SimpleBlob> streamOut(plain);
		IW_CHECK(loader.Write(_T("JPG"), &streamOut, image, settings, IW::CNullStatus::Instance));
	}

	{
		CLoadJpg loader;
		IW::CodecSettings settings;
		settings.JpegOptimize = true;
		IW::StreamBlob<IW::SimpleBlob> streamOut(optimized);
		IW_CHECK(loader.Write(_T("JPG"), &streamOut, image, settings, IW::CNullStatus::Instance));
	}

	IW_CHECK(plain.GetDataSize() > 0);
	IW_CHECK(optimized.GetDataSize() > 0);
	IW_CHECK(optimized.GetDataSize() < plain.GetDataSize());
}

// There is no PCX encoder, so the only way to test the decoder is to build a
// file by hand and check it against a BMP carrying the same palette and the same
// indices. Comparing against another loader rather than against the decoder's own
// arithmetic is what makes the palette channel order part of the assertion.
namespace
{

const int PCX_CX = 37; // deliberately odd, so bytes_per_line is padded
const int PCX_CY = 5;

int TestIndexAt(int x, int y)
{
	// A long run of one index on every row, so the RLE path is exercised, and a
	// span of literals either side of it.
	return (x >= 8 && x < 24) ? 0xC5 : (x * 3 + y * 5) & 0xff;
}

void TestPcxPalette(BYTE rgb[768])
{
	for (int i = 0; i < 256; i++)
	{
		rgb[i * 3 + 0] = static_cast<BYTE>((i * 13 + 7) & 0xff); // red
		rgb[i * 3 + 1] = static_cast<BYTE>((i * 7) & 0xff);      // green
		rgb[i * 3 + 2] = static_cast<BYTE>(i);                   // blue
	}
}

void EncodePcxRow(std::vector<BYTE> &out, const BYTE *pRow, int nCount)
{
	int x = 0;

	while (x < nCount)
	{
		int nRun = 1;
		while (x + nRun < nCount && nRun < 63 && pRow[x + nRun] == pRow[x]) nRun++;

		// A lone byte of 0xC0 or above still has to be escaped, or the decoder
		// reads it as a run count.
		if (nRun > 1 || pRow[x] >= 0xc0)
		{
			out.push_back(static_cast<BYTE>(0xc0 | nRun));
			out.push_back(pRow[x]);
		}
		else
		{
			out.push_back(pRow[x]);
		}

		x += nRun;
	}
}

std::vector<BYTE> MakeTestPcx()
{
	const int nBytesPerLine = (PCX_CX + 1) & ~1; // PCX requires an even count

	std::vector<BYTE> file(128, 0);
	file[0] = 0x0a;
	file[1] = 5;
	file[2] = 1;
	file[3] = 8;
	*reinterpret_cast<short *>(&file[8]) = static_cast<short>(PCX_CX - 1);  // xmax
	*reinterpret_cast<short *>(&file[10]) = static_cast<short>(PCX_CY - 1); // ymax
	file[65] = 1;                                                           // color_planes
	*reinterpret_cast<short *>(&file[66]) = static_cast<short>(nBytesPerLine);

	std::vector<BYTE> row(nBytesPerLine, 0);

	for (int y = 0; y < PCX_CY; y++)
	{
		for (int x = 0; x < PCX_CX; x++)
			row[x] = static_cast<BYTE>(TestIndexAt(x, y));

		EncodePcxRow(file, &row[0], nBytesPerLine);
	}

	BYTE rgb[768];
	TestPcxPalette(rgb);

	file.push_back(0x0c);
	file.insert(file.end(), rgb, rgb + sizeof(rgb));

	return file;
}

std::vector<BYTE> MakeEquivalentBmp()
{
	const int nStride = (PCX_CX + 3) & ~3;
	const int nOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD);

	std::vector<BYTE> file(nOffBits + nStride * PCX_CY, 0);

	auto pFile = reinterpret_cast<BITMAPFILEHEADER *>(&file[0]);
	pFile->bfType = 0x4d42;
	pFile->bfSize = static_cast<DWORD>(file.size());
	pFile->bfOffBits = nOffBits;

	auto pInfo = reinterpret_cast<BITMAPINFOHEADER *>(&file[sizeof(BITMAPFILEHEADER)]);
	pInfo->biSize = sizeof(BITMAPINFOHEADER);
	pInfo->biWidth = PCX_CX;
	pInfo->biHeight = -PCX_CY; // top-down, so the rows match the PCX order
	pInfo->biPlanes = 1;
	pInfo->biBitCount = 8;
	pInfo->biCompression = BI_RGB;
	pInfo->biClrUsed = 256;

	BYTE rgb[768];
	TestPcxPalette(rgb);

	auto pPalette = reinterpret_cast<RGBQUAD *>(&file[sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)]);

	for (int i = 0; i < 256; i++)
	{
		pPalette[i].rgbRed = rgb[i * 3 + 0];
		pPalette[i].rgbGreen = rgb[i * 3 + 1];
		pPalette[i].rgbBlue = rgb[i * 3 + 2];
		pPalette[i].rgbReserved = 0;
	}

	for (int y = 0; y < PCX_CY; y++)
		for (int x = 0; x < PCX_CX; x++)
			file[nOffBits + y * nStride + x] = static_cast<BYTE>(TestIndexAt(x, y));

	return file;
}

} // namespace

IW_TEST(PcxDecodesToTheSamePixelsAsAnEquivalentBmp)
{
	const std::vector<BYTE> pcx = MakeTestPcx();
	const std::vector<BYTE> bmp = MakeEquivalentBmp();

	IW::Image fromPcx;
	IW::ImageStream<IW::IImageStream> pcxOut(fromPcx);
	IW::StreamConstBlob pcxIn(&pcx[0], pcx.size());
	CLoadPcx pcxLoader;
	IW_CHECK(pcxLoader.Read(_T("PCX"), &pcxIn, &pcxOut, IW::CNullStatus::Instance));

	IW::Image fromBmp;
	IW::ImageStream<IW::IImageStream> bmpOut(fromBmp);
	IW::StreamConstBlob bmpIn(&bmp[0], bmp.size());
	CLoadBmp bmpLoader;
	IW_CHECK(bmpLoader.Read(_T("BMP"), &bmpIn, &bmpOut, IW::CNullStatus::Instance));

	IW_CHECK_EQ(fromPcx.GetFirstPage().GetWidth(), PCX_CX);
	IW_CHECK_EQ(fromPcx.GetFirstPage().GetHeight(), PCX_CY);
	IW_CHECK_EQ(fromBmp.GetFirstPage().GetWidth(), PCX_CX);

	for (int y = 0; y < PCX_CY; y++)
		for (int x = 0; x < PCX_CX; x++)
			IW_CHECK_EQ(PixelAt(fromPcx, x, y) & 0xFFFFFF, PixelAt(fromBmp, x, y) & 0xFFFFFF);
}

// A truncated run must stop the decoder, not walk off the mapped file.
IW_TEST(PcxLoaderBoundsATruncatedFile)
{
	const std::vector<BYTE> good = MakeTestPcx();

	for (size_t nSize = 129; nSize < good.size(); nSize += 17)
	{
		const std::vector<BYTE> truncated(good.begin(), good.begin() + nSize);

		IW::Image image;
		IW::ImageStream<IW::IImageStream> imageOut(image);
		IW::StreamConstBlob streamIn(&truncated[0], truncated.size());
		CLoadPcx loader;

		// Either answer is fine; not crashing and not hanging is the assertion.
		loader.Read(_T("PCX"), &streamIn, &imageOut, IW::CNullStatus::Instance);
	}
}

// The palette of an 8 bit PCX lives after the pixels, behind a 0x0c marker. A
// file that carries no such block leaves the reader pointed at the header's
// 48-byte palette16 field, which holds sixteen entries, not 256.
IW_TEST(APcxWithNoVgaPaletteStaysInsideItsHeader)
{
	std::vector<BYTE> file = MakeTestPcx();

	// Drop the trailing marker and palette. What is left is a legal 8 bit PCX
	// with no palette of its own.
	file.resize(file.size() - 769);

	IW::Image image;
	IW::ImageStream<IW::IImageStream> imageOut(image);
	IW::StreamConstBlob streamIn(&file[0], file.size());
	CLoadPcx loader;

	IW_CHECK(loader.Read(_T("PCX"), &streamIn, &imageOut, IW::CNullStatus::Instance));
	IW_CHECK_EQ(image.GetFirstPage().GetWidth(), PCX_CX);

	// Everything the file did not describe still has to be opaque, or the
	// alpha-weighted downscale weights every sample by zero.
	IW::Page page = image.GetFirstPage();
	IW::LPCCOLORREF pPalette = page.GetPalette();

	for (int i = 0; i < 256; i++)
		IW_CHECK_EQ(IW::GetA(pPalette[i]), 255u);
}

namespace
{

void WriteMSBShort(std::vector<BYTE> &v, unsigned n)
{
	v.push_back(static_cast<BYTE>((n >> 8) & 0xFF));
	v.push_back(static_cast<BYTE>(n & 0xFF));
}

void WriteMSBLong(std::vector<BYTE> &v, unsigned n)
{
	WriteMSBShort(v, (n >> 16) & 0xFFFF);
	WriteMSBShort(v, n & 0xFFFF);
}

// A PSD header in the given colour mode, followed by a colour-map block of
// nColourMapBytes. PSD only carries a colour map for indexed and duotone
// images, but the field is present for every mode and the file chooses it.
std::vector<BYTE> MakePsdWithColourMap(unsigned nMode, unsigned nChannels,
                                       unsigned cx, unsigned cy, unsigned nColourMapBytes)
{
	std::vector<BYTE> file;

	file.push_back('8'); file.push_back('B'); file.push_back('P'); file.push_back('S');
	WriteMSBShort(file, 1);         // version
	for (int i = 0; i < 6; i++) file.push_back(0);
	WriteMSBShort(file, nChannels);
	WriteMSBLong(file, cy);         // rows
	WriteMSBLong(file, cx);         // columns
	WriteMSBShort(file, 8);         // depth
	WriteMSBShort(file, nMode);

	WriteMSBLong(file, nColourMapBytes);

	for (unsigned i = 0; i < nColourMapBytes; i++)
		file.push_back(static_cast<BYTE>(i));

	WriteMSBLong(file, 0);          // IPTC resource length
	WriteMSBLong(file, 0);          // layer and mask block length
	WriteMSBShort(file, 0);         // compression: none

	// Raw channel data, so the decode has something to read.
	for (unsigned i = 0; i < cx * cy * nChannels; i++)
		file.push_back(static_cast<BYTE>(i));

	return file;
}

} // namespace

// The colour map was written into page.GetPalette() whatever the mode, and for
// an RGB page -- which has no palette -- GetPalette() points at the first pixel.
// A 768 byte map over an 8x8 RGB image is a kilobyte written into 192 bytes.
IW_TEST(APsdColourMapCannotOverrunAPageWithNoPalette)
{
	const unsigned modes[] = {2 /*Indexed*/, 3 /*RGB*/, 4 /*CMYK*/, 8 /*Duotone*/, 9 /*Lab*/};

	for (int m = 0; m < countof(modes); m++)
	{
		// Small enough that a 256 entry palette is far larger than the pixels.
		const std::vector<BYTE> file = MakePsdWithColourMap(modes[m], 3, 8, 8, 768);

		IW::Image image;
		IW::ImageStream<IW::IImageStream> imageOut(image);
		IW::StreamConstBlob streamIn(&file[0], file.size());
		CLoadPsd loader;

		// Whether it decodes is not the point; not corrupting the heap is.
		loader.Read(_T("PSD"), &streamIn, &imageOut, IW::CNullStatus::Instance);
	}

	// A colour-map length that is negative when stored in an int sized the
	// scratch buffer to nothing and then asked the stream for 4 GB into it.
	{
		std::vector<BYTE> file = MakePsdWithColourMap(2, 1, 4, 4, 0);
		file[26] = 0xFF; file[27] = 0xFF; file[28] = 0xFF; file[29] = 0xFF;

		IW::Image image;
		IW::ImageStream<IW::IImageStream> imageOut(image);
		IW::StreamConstBlob streamIn(&file[0], file.size());
		CLoadPsd loader;

		IW_CHECK(!loader.Read(_T("PSD"), &streamIn, &imageOut, IW::CNullStatus::Instance));
	}
}

// "Only shrink, never enlarge" was read from the check box, written to the ini
// and never looked at again, so a batch resize upsampled every small file in a
// mixed folder and wrote the blurry result out as a new picture.
IW_TEST(ScaleDownOnlyLeavesASmallImageAlone)
{
	const IW::Image imageSmall = MakeTestImage(64, 48);
	const IW::Image imageLarge = MakeTestImage(400, 300);

	{
		CFilterResize filter;
		filter.m_nWidth = 200;
		filter.m_nHeight = 150;
		filter.m_bScaleDown = true;

		IW::Image out;
		IW_CHECK(filter.ApplyFilter(imageSmall, out, IW::CNullStatus::Instance));
		IW_CHECK_EQ(out.GetFirstPage().GetWidth(), 64);
		IW_CHECK_EQ(out.GetFirstPage().GetHeight(), 48);

		// The same setting must still shrink anything bigger than the target.
		IW::Image outLarge;
		IW_CHECK(filter.ApplyFilter(imageLarge, outLarge, IW::CNullStatus::Instance));
		IW_CHECK_EQ(outLarge.GetFirstPage().GetWidth(), 200);
	}

	{
		CFilterResize filter;
		filter.m_nWidth = 200;
		filter.m_nHeight = 150;
		filter.m_bScaleDown = false;

		IW::Image out;
		IW_CHECK(filter.ApplyFilter(imageSmall, out, IW::CNullStatus::Instance));
		IW_CHECK_EQ(out.GetFirstPage().GetWidth(), 200);
	}
}

// The BMP header declares 32 bits, so the alpha byte is part of the file.
// RenderLine composites it away and returns zero, which wrote a fully
// transparent picture. The reader maps 32bpp to PF32, so a round trip cannot
// see this -- the assertion has to be against the bytes that reach the file.
IW_TEST(A32BitBmpIsWrittenWithItsAlphaChannel)
{
	const int cx = 16;
	const int cy = 12;

	IW::Image image;
	IW::Page &page = image.CreatePage(cx, cy, IW::PixelFormat::PF32Alpha);

	{
		IW::IImageSurfaceLockPtr pLock = page.GetSurfaceLock();
		std::vector<COLORREF> line(cx);

		for (int y = 0; y < cy; y++)
		{
			for (int x = 0; x < cx; x++)
				line[x] = IW::RGBA(10 + x, 20 + y, 30, (x * 16) & 0xFF);

			pLock->SetLine(&line[0], y, 0, cx);
		}
	}

	CLoadBmp loader;
	IW::SimpleBlob data;
	IW::StreamBlob<IW::SimpleBlob> streamOut(data);

	IW_CHECK(loader.Write(_T("BMP"), &streamOut, image, IW::CodecSettings(), IW::CNullStatus::Instance));

	LPCBYTE pFile = data.GetData();
	const DWORD nOffBits = reinterpret_cast<const BITMAPFILEHEADER *>(pFile)->bfOffBits;

	IW_CHECK_EQ(static_cast<int>(reinterpret_cast<const BITMAPINFOHEADER *>(
		pFile + sizeof(BITMAPFILEHEADER))->biBitCount), 32);
	IW_CHECK(data.GetDataSize() >= nOffBits + (cx * cy * 4));

	// Rows go out bottom up, so file row 0 is image row cy - 1.
	for (int y = 0; y < cy; y++)
	{
		auto pRow = reinterpret_cast<const COLORREF *>(pFile + nOffBits + (y * cx * 4));

		for (int x = 0; x < cx; x++)
			IW_CHECK_EQ(IW::GetA(pRow[x]), (x * 16) & 0xFF);
	}
}

IW_TEST(BmpRoundTripIsLossless)
{
	const IW::Image image = MakeTestImage(33, 17);

	CLoadBmp loader;
	IW::SimpleBlob data;
	IW::StreamBlob<IW::SimpleBlob> streamOut(data);

	IW_CHECK(loader.Write(_T("BMP"), &streamOut, image, IW::CodecSettings(), IW::CNullStatus::Instance));
	IW_CHECK(data.GetDataSize() > 0);

	IW::StreamConstBlob streamIn(data);
	IW::Image reloaded;
	IW::ImageStream<IW::IImageStream> imageOut(reloaded);

	IW_CHECK(loader.Read(_T("BMP"), &streamIn, &imageOut, IW::CNullStatus::Instance));
	IW_CHECK_EQ(reloaded.GetFirstPage().GetWidth(), 33);
	IW_CHECK_EQ(reloaded.GetFirstPage().GetHeight(), 17);
	IW_CHECK_EQ(PixelAt(reloaded, 3, 5) & 0xFFFFFF, PixelAt(image, 3, 5) & 0xFFFFFF);
}

// Every field of a BITMAPINFOHEADER comes from the file. biClrUsed drives
// RGBQUAD* arithmetic, and the remaining-bytes check that was supposed to catch
// the result was narrowed to int before its sign was tested -- so a difference
// of about -2^32 came out small and positive.
IW_TEST(BmpLoaderBoundsAMalformedHeader)
{
	const IW::Image image = MakeTestImage(9, 5);

	CLoadBmp loader;
	IW::SimpleBlob data;
	IW::StreamBlob<IW::SimpleBlob> streamOut(data);
	IW_CHECK(loader.Write(_T("BMP"), &streamOut, image, IW::CodecSettings(), IW::CNullStatus::Instance));

	const std::vector<BYTE> good(data.GetData(), data.GetData() + data.GetDataSize());
	IW_CHECK(good.size() > sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER));

	// Too short to hold a BITMAPINFOHEADER, which is read before anything else
	{
		IW::SimpleBlob blob(&good[0], static_cast<int>(sizeof(BITMAPFILEHEADER) + 8));
		IW::StreamConstBlob streamIn(blob);
		IW::Image reloaded;
		IW::ImageStream<IW::IImageStream> imageOut(reloaded);

		IW_CHECK(!loader.Read(_T("BMP"), &streamIn, &imageOut, IW::CNullStatus::Instance));
	}

	// biClrUsed of 0x40000000 moves an RGBQUAD* exactly 4 GiB past the mapping.
	// A 32bpp DIB has no palette, so it must clamp back to none and still load.
	{
		std::vector<BYTE> bytes(good);
		auto pInfo = reinterpret_cast<BITMAPINFOHEADER*>(&bytes[sizeof(BITMAPFILEHEADER)]);
		pInfo->biClrUsed = 0x40000000;

		IW::SimpleBlob blob(&bytes[0], static_cast<int>(bytes.size()));
		IW::StreamConstBlob streamIn(blob);
		IW::Image reloaded;
		IW::ImageStream<IW::IImageStream> imageOut(reloaded);

		IW_CHECK(loader.Read(_T("BMP"), &streamIn, &imageOut, IW::CNullStatus::Instance));
		IW_CHECK_EQ(reloaded.GetFirstPage().GetWidth(), 9);
		IW_CHECK_EQ(reloaded.GetFirstPage().GetHeight(), 5);
		IW_CHECK_EQ(PixelAt(reloaded, 2, 3) & 0xFFFFFF, PixelAt(image, 2, 3) & 0xFFFFFF);
	}
}

namespace
{

void WriteLE16(std::vector<BYTE> &v, unsigned n)
{
	v.push_back(static_cast<BYTE>(n & 0xFF));
	v.push_back(static_cast<BYTE>((n >> 8) & 0xFF));
}

void WriteLE32(std::vector<BYTE> &v, unsigned n)
{
	WriteLE16(v, n & 0xFFFF);
	WriteLE16(v, (n >> 16) & 0xFFFF);
}

// A WPG level 1 file whose colour palette record claims nEntries entries from
// nStart. Both are 16 bit fields the file chooses. ReadBlobByte throws at end
// of stream, so the payload has to be long enough for the loop to actually run
// -- a short file stops it before it leaves the array, which is what made the
// first version of this test pass with the bug restored.
std::vector<BYTE> MakeWpgWithPaletteRecord(unsigned nStart, unsigned nEntries)
{
	std::vector<BYTE> file;

	WriteLE32(file, 0x435057FF); // FileId
	WriteLE32(file, 18);         // DataOffset -- the first record
	WriteLE16(file, 0x1600);     // ProductType, >> 8 must be 0x16
	WriteLE16(file, 1);          // FileType -- WPG level 1
	file.push_back(1);           // MajorVersion
	file.push_back(0);           // MinorVersion
	WriteLE16(file, 0);          // EncryptKey -- must be zero
	WriteLE16(file, 0);          // Reserved

	file.push_back(0x0E);  // colour palette
	file.push_back(0x40);  // RecordLength, one byte, under 0xFF

	WriteLE16(file, nStart);
	WriteLE16(file, nEntries);

	for (unsigned i = 0; i < nEntries * 4 + 16; i++)
		file.push_back(static_cast<BYTE>(i));

	return file;
}

} // namespace

// Both palette records take a start index and an entry count straight from the
// file and wrote them into a 256 entry array on the decoder's stack. A count of
// 65535 is a quarter of a megabyte of file-controlled bytes over the return
// address.
IW_TEST(AWpgPaletteRecordCannotOverrunItsPalette)
{
	const unsigned counts[] = {0, 1, 256, 257, 4096, 0xFFFF};
	const unsigned starts[] = {0, 255, 256, 0xFFFF};

	for (int c = 0; c < countof(counts); c++)
	{
		for (int s = 0; s < countof(starts); s++)
		{
			const std::vector<BYTE> file = MakeWpgWithPaletteRecord(starts[s], counts[c]);

			IW::Image image;
			IW::ImageStream<IW::IImageStream> imageOut(image);
			IW::StreamConstBlob streamIn(&file[0], file.size());
			CLoadWpg loader;

			// The file describes no raster, so the answer is false either way.
			// Not corrupting the stack is the assertion.
			IW_CHECK(!loader.Read(_T("WPG"), &streamIn, &imageOut, IW::CNullStatus::Instance));
		}
	}
}

// Neutral edits must be a pass-through, or every preview rebuild would soften
// an image nobody has touched yet.
IW_TEST(NeutralEditsLeaveEveryPixelAlone)
{
	const IW::Image image = MakeTestImage(50, 30);

	ImageEdits edits;
	IW_CHECK(edits.IsEmpty());

	IW::Image result;
	IW_CHECK(IW::ApplyEdits(image, result, edits, IW::CNullStatus::Instance));

	IW_CHECK_EQ(result.GetFirstPage().GetWidth(), 50);
	IW_CHECK_EQ(result.GetFirstPage().GetHeight(), 30);
	IW_CHECK_EQ(PixelAt(result, 12, 9) & 0xFFFFFF, PixelAt(image, 12, 9) & 0xFFFFFF);
}

// The tone controls run through one LUT; a full negative brightness has to
// bottom out rather than wrap, which is what an unclamped int would do.
IW_TEST(BrightnessMovesEveryChannelAndClamps)
{
	const IW::Image image = MakeTestImage(50, 30);

	ImageEdits up;
	up._brightness = 50;

	IW::Image brighter;
	IW_CHECK(IW::ApplyEdits(image, brighter, up, IW::CNullStatus::Instance));

	const COLORREF original = PixelAt(image, 12, 9);
	const COLORREF lifted = PixelAt(brighter, 12, 9);

	IW_CHECK(IW::GetR(lifted) > IW::GetR(original));
	IW_CHECK(IW::GetG(lifted) > IW::GetG(original));
	IW_CHECK(IW::GetB(lifted) > IW::GetB(original));

	ImageEdits down;
	down._brightness = ImageEdits::ColorMin;

	IW::Image darker;
	IW_CHECK(IW::ApplyEdits(image, darker, down, IW::CNullStatus::Instance));

	const COLORREF floored = PixelAt(darker, 12, 9);

	IW_CHECK_EQ(IW::GetR(floored), 0);
	IW_CHECK_EQ(IW::GetG(floored), 0);
	IW_CHECK_EQ(IW::GetB(floored), 0);
}

// The curve stretches about mid grey and adds brightness after it, so the
// offset that carries the picture's midpoint up to mid grey has to be scaled by
// the contrast gain. Without that factor auto colour under-corrected by exactly
// the gain, and an underexposed picture came back underexposed.
IW_TEST(AutoColorLiftsAnUnderexposedPicture)
{
	IW::Image image;
	IW::Page &page = image.CreatePage(64, 64, IW::PixelFormat::PF24);

	{
		IW::IImageSurfaceLockPtr pLock = page.GetSurfaceLock();
		std::vector<COLORREF> line(64);

		for (int y = 0; y < 64; y++)
		{
			for (int x = 0; x < 64; x++)
			{
				// A flat ramp from 10 to 155: dark, and short of the top of the
				// scale by enough that the contrast the fix asks for is inside
				// what the slider can give.
				const BYTE c = static_cast<BYTE>(10 + (x * 145) / 63);
				line[x] = IW::RGBA(c, c, c);
			}

			pLock->SetLine(&line[0], y, 0, 64);
		}
	}

	ImageEdits edits;
	IW::AutoColor(image, edits);

	IW_CHECK(edits._contrast > 0 && edits._contrast < ImageEdits::ColorMax);
	IW_CHECK(edits._brightness > 0);

	IW::Image out;
	IW_CHECK(IW::ApplyEdits(image, out, edits, IW::CNullStatus::Instance));

	double mean = 0;

	for (int y = 0; y < 64; y++)
		for (int x = 0; x < 64; x++)
			mean += IW::GetG(PixelAt(out, x, y));

	mean /= 64 * 64;

	// Mid grey, give or take. Without the gain the mean landed around 93.
	IW_CHECK(mean > 110.0 && mean < 145.0);
}

// Saturation at -100 collapses to luminance, so all three channels agree.
IW_TEST(FullDesaturationLeavesAGreyImage)
{
	const IW::Image image = MakeTestImage(50, 30);

	ImageEdits edits;
	edits._saturation = ImageEdits::ColorMin;

	IW::Image grey;
	IW_CHECK(IW::ApplyEdits(image, grey, edits, IW::CNullStatus::Instance));

	const COLORREF c = PixelAt(grey, 12, 9);

	IW_CHECK_EQ(IW::GetR(c), IW::GetG(c));
	IW_CHECK_EQ(IW::GetG(c), IW::GetB(c));
}

// A crop is the only part of the stack that changes the extent, and the
// rectangle it takes is in transformed -- not source -- coordinates.
IW_TEST(CropEditProducesTheRequestedExtent)
{
	const IW::Image image = MakeTestImage(50, 30);

	ImageEdits edits;
	edits._crop = CRect(10, 5, 30, 20);

	IW_CHECK(edits.HasCrop());
	IW_CHECK(edits.HasGeometry());

	IW::Image cropped;
	IW_CHECK(IW::ApplyEdits(image, cropped, edits, IW::CNullStatus::Instance));

	IW_CHECK_EQ(cropped.GetFirstPage().GetWidth(), 20);
	IW_CHECK_EQ(cropped.GetFirstPage().GetHeight(), 15);
	IW_CHECK_EQ(PixelAt(cropped, 0, 0) & 0xFFFFFF, PixelAt(image, 10, 5) & 0xFFFFFF);
}

// A quarter turn swaps the extent and is exact -- no resampling is involved,
// so the corner pixel has to survive the trip byte for byte.
IW_TEST(RotateEditSwapsTheExtentWithoutResampling)
{
	const IW::Image image = MakeTestImage(50, 30);

	ImageEdits edits;
	edits._rotate = 1;

	IW_CHECK_EQ(edits.TransformedSize(CSize(50, 30)).cx, 30);
	IW_CHECK_EQ(edits.TransformedSize(CSize(50, 30)).cy, 50);

	IW::Image rotated;
	IW_CHECK(IW::ApplyEdits(image, rotated, edits, IW::CNullStatus::Instance));

	IW_CHECK_EQ(rotated.GetFirstPage().GetWidth(), 30);
	IW_CHECK_EQ(rotated.GetFirstPage().GetHeight(), 50);

	// Four quarter turns is the identity, so the pixels must be untouched.
	ImageEdits back;
	back._rotate = 3;

	IW::Image restored;
	IW_CHECK(IW::ApplyEdits(rotated, restored, back, IW::CNullStatus::Instance));

	IW_CHECK_EQ(restored.GetFirstPage().GetWidth(), 50);
	IW_CHECK_EQ(restored.GetFirstPage().GetHeight(), 30);
	IW_CHECK_EQ(PixelAt(restored, 12, 9) & 0xFFFFFF, PixelAt(image, 12, 9) & 0xFFFFFF);
}

// A straighten goes down the projective path; the frame it was given is what
// comes back, since only the crop changes the extent.
IW_TEST(AWarpKeepsTheFrameItWasGiven)
{
	const IW::Image image = MakeTestImage(64, 64);

	ImageEdits edits;
	IW_CHECK(!edits.HasWarp());

	edits._straighten = 50;	// five degrees
	IW_CHECK(edits.HasWarp());

	IW::Image warped;
	IW_CHECK(IW::ApplyEdits(image, warped, edits, IW::CNullStatus::Instance));

	IW_CHECK_EQ(warped.GetFirstPage().GetWidth(), 64);
	IW_CHECK_EQ(warped.GetFirstPage().GetHeight(), 64);

	// The centre is the fixed point of a rotation about the centre, so the warp
	// must not move it. Within a count, not exactly: the fixed point is the middle
	// of the picture, which for an even width is half a pixel off (32, 32), and the
	// gradient is one count per pixel. This used to be an exact comparison that
	// passed only because RenderLine's composite divided by 256 instead of 255 and
	// took the one count back off again.
	IW_CHECK(abs(static_cast<int>(IW::GetR(PixelAt(warped, 32, 32))) -
	             static_cast<int>(IW::GetR(PixelAt(image, 32, 32)))) <= 1);
}

// The panel writes to the ini on every change and reads it back on the next
// activation, so a value that does not round-trip silently resets itself.
// PF555 and PF565 go through the paired-pixel loops in ImagingLock.cpp, which used to
// be skipped entirely on x64 because their end pointer was rebuilt from a
// truncated address. Odd widths are the interesting case: the pair loop
// consumes two pixels per test.
IW_TEST(SixteenBitConversionKeepsEveryPixel)
{
	const int widths[] = { 16, 17, 31, 32 };

	for (int i = 0; i < ARRAYSIZE(widths); i++)
	{
		const int cx = widths[i];
		const IW::Image original = MakeTestImage(cx, 5);

		IW::Image converted = original;
		converted.ConvertTo(IW::PixelFormat::PF555);

		IW_CHECK_EQ(converted.GetFirstPage().GetWidth(), cx);

		for (int y = 0; y < 5; y++)
		{
			for (int x = 0; x < cx; x++)
			{
				const COLORREF want = PixelAt(original, x, y);
				const COLORREF got = PixelAt(converted, x, y);

				// 555 drops three bits per channel.
				IW_CHECK(abs(IW::GetR(want) - IW::GetR(got)) < 8);
				IW_CHECK(abs(IW::GetG(want) - IW::GetG(got)) < 8);
				IW_CHECK(abs(IW::GetB(want) - IW::GetB(got)) < 8);
			}
		}
	}
}

IW_TEST(SixteenBit565ConversionKeepsEveryPixel)
{
	const int cx = 33;
	const IW::Image original = MakeTestImage(cx, 4);

	IW::Image converted = original;
	converted.ConvertTo(IW::PixelFormat::PF565);

	IW_CHECK_EQ(converted.GetFirstPage().GetWidth(), cx);

	for (int y = 0; y < 4; y++)
	{
		for (int x = 0; x < cx; x++)
		{
			const COLORREF want = PixelAt(original, x, y);
			const COLORREF got = PixelAt(converted, x, y);

			IW_CHECK(abs(IW::GetR(want) - IW::GetR(got)) < 8);
			IW_CHECK(abs(IW::GetG(want) - IW::GetG(got)) < 4);
			IW_CHECK(abs(IW::GetB(want) - IW::GetB(got)) < 8);
		}
	}
}

IW_TEST(FilePathSplitsAndRebuilds)
{
	IW::CFilePath path(CString(_T("c:\\pictures\\holiday.jpeg")));

	IW_CHECK(path.GetFileName() == CString(_T("holiday")));

	IW::CFilePath name(path);
	name.StripToFilenameAndExtension();
	IW_CHECK(name.ToString() == CString(_T("holiday.jpeg")));

	IW::CFilePath extension(path);
	extension.StripToExtension();
	IW_CHECK(extension.ToString() == CString(_T(".jpeg")));

	path.SetExtension(_T(".png"));
	IW_CHECK(path.ToString() == CString(_T("c:\\pictures\\holiday.png")));
}

IW_TEST(FilePathTerminatesFoldersOnce)
{
	IW::CFilePath path(CString(_T("c:\\pictures")));

	IW_CHECK(!path.IsTerminated());
	path.TerminateFolderPath();
	IW_CHECK(path.IsTerminated());

	const CString once = path.ToString();
	path.TerminateFolderPath();
	IW_CHECK(path.ToString() == once);

	path += CString(_T("holiday.jpg"));
	IW_CHECK(path.ToString() == CString(_T("c:\\pictures\\holiday.jpg")));
}

IW_TEST(FilePathComparisonIgnoresCaseAndSeparators)
{
	IW::CFilePath left(CString(_T("C:/Pictures/Holiday.jpg")));
	IW::CFilePath right(CString(_T("c:\\pictures\\holiday.jpg")));

	IW_CHECK(left == right);

	IW::CFilePath other(CString(_T("c:\\pictures\\other.jpg")));
	IW_CHECK(!(left == other));
}

IW_TEST(SimpleMatchMatchesWholeNamesNotPrefixes)
{
	// A bare word is wrapped in stars, so it matches anywhere
	IW_CHECK(IW::SimpleMatch(_T("holiday"), _T("my holiday snap.jpg")));
	IW_CHECK(!IW::SimpleMatch(_T("holiday"), _T("beach.jpg")));

	// An explicit pattern has to match the whole name
	IW_CHECK(IW::SimpleMatch(_T("*.jpg"), _T("photo.jpg")));
	IW_CHECK(!IW::SimpleMatch(_T("*.jpg"), _T("photo.jpgx")));
	IW_CHECK(!IW::SimpleMatch(_T("*.jp"), _T("photo.jpg")));

	IW_CHECK(IW::SimpleMatch(_T("photo?.jpg"), _T("photo1.jpg")));
	IW_CHECK(!IW::SimpleMatch(_T("photo?.jpg"), _T("photo12.jpg")));

	// Semicolon separated alternatives, and a pattern that made the
	// recursive form exponential
	IW_CHECK(IW::SimpleMatch(_T("*.gif;*.png"), _T("logo.png")));
	IW_CHECK(!IW::SimpleMatch(_T("*.gif;*.png"), _T("logo.bmp")));
	IW_CHECK(IW::SimpleMatch(_T("*a*a*a*a*"), _T("aaaaaaaaaaaaaaaaaaaaaaaaaaaab")));
}

// ITEMLIST holds a counted reference, so an insertion site that also AddRefs
// leaves the item at two and DeleteAllThumbs releasing twice frees it while the
// vector still points at it. The counts below are the whole point: reintroduce
// either half of the old two-per-slot convention and one of them is wrong.
IW_TEST(FolderItemsAreOwnedOnceByTheItemList)
{
	IW::FolderItemPtr pItem = IW::FolderItem::CreateTestItem();
	IW_CHECK(pItem->GetRefCount() == 1);

	{
		IW::Folder folder;

		folder.InsertThumb(pItem);
		IW_CHECK(folder.GetSize() == 1);
		IW_CHECK(folder.GetItem(0) == pItem.p);

		// Ours plus the vector's, and nothing else
		IW_CHECK(pItem->GetRefCount() == 2);

		folder.DeleteAllThumbs();
		IW_CHECK(folder.GetSize() == 0);
		IW_CHECK(pItem->GetRefCount() == 1);
	}

	// A folder destroyed with items still in it must not over-release either
	{
		IW::Folder folder;
		folder.InsertThumb(pItem);
		IW_CHECK(pItem->GetRefCount() == 2);
	}

	IW_CHECK(pItem->GetRefCount() == 1);
	IW_CHECK(pItem->IsImage());
}

IW_TEST(FilePathConvertsSeparators)
{
	IW::CFilePath path(CString(_T("c:\\a\\b\\c.txt")));

	path.MakeUnixPath();
	IW_CHECK(path.ToString() == CString(_T("c:/a/b/c.txt")));

	path.MakeDosPath();
	IW_CHECK(path.ToString() == CString(_T("c:\\a\\b\\c.txt")));
}

namespace
{

class TestThrowingThread : public IW::Thread
{
public:

	CEvent _eventThrew;

	TestThrowingThread() : _eventThrew(TRUE, FALSE) {}
	~TestThrowingThread() { StopThread(); }

	bool IsJoined() const { return _hThread == nullptr; }

	void Process()
	{
		_eventThrew.Set();
		throw std::exception("thrown from a worker");
	}
};

class TestBlockingThread : public IW::Thread
{
public:

	volatile bool _bRunning;

	TestBlockingThread() : _bRunning(false) {}
	~TestBlockingThread() { StopThread(); }

	void Process()
	{
		_bRunning = true;

		while (!_bExit)
			WaitForSingleObject(_eventThreadExit, INFINITE);

		_bRunning = false;
	}
};

// Throws on the first item of every pass and counts the ones it survives to
// finish. The shape of every loader-driven worker in the tree: a wait, a body
// that can throw, and a caller that keeps asking.
class TestRecoveringThread : public IW::Thread
{
public:

	CEvent _eventWork;
	volatile long _nCompleted;

	TestRecoveringThread() : _eventWork(FALSE, FALSE), _nCompleted(0) {}
	~TestRecoveringThread() { StopThread(); }

	void PostWork() { _eventWork.Set(); }

	void Process()
	{
		HANDLE objects[2];
		objects[0] = _eventWork;
		objects[1] = _eventThreadExit;

		while (!_bExit)
		{
			if (WaitForMultipleObjects(2, objects, FALSE, INFINITE) != WAIT_OBJECT_0)
				return;

			if (_bExit)
				return;

			try
			{
				throw std::exception("thrown from one item of a pass");
			}
			catch (const std::exception &)
			{
			}

			::InterlockedIncrement(&_nCompleted);
		}
	}
};

} // namespace

// An image loader throwing out of a worker used to reach the CRT and terminate
// the process, so this test dies rather than fails if the handler goes away.
IW_TEST(AWorkerThatThrowsDoesNotTakeTheProcessDown)
{
	TestThrowingThread thread;
	thread.StartThread();

	IW_CHECK(WaitForSingleObject(thread._eventThrew, 10000) == WAIT_OBJECT_0);

	thread.StopThread();
	IW_CHECK(thread.IsJoined());
}

// The test above is satisfied by a worker that dies on its first throw, which is
// exactly the bug: the handler used to sit around Process() rather than inside
// its loop, so one unreadable file ended background loading for the session.
// This one fails unless the worker is still serving requests afterwards.
//
// Move TestRecoveringThread's try/catch outside the while loop and this drops to
// one completion instead of three.
IW_TEST(AWorkerThatThrowsOnOneItemStillServesTheNext)
{
	TestRecoveringThread thread;
	thread.StartThread();

	for (int nPass = 1; nPass <= 3; nPass++)
	{
		thread.PostWork();

		for (int i = 0; i < 500 && ::InterlockedCompareExchange(&thread._nCompleted, 0, 0) < nPass; i++)
			Sleep(10);

		IW_CHECK_EQ(::InterlockedCompareExchange(&thread._nCompleted, 0, 0), (long)nPass);
	}

	thread.StopThread();
}

// A worker that cannot be restarted silently accepts work and drops it, which
// looks identical to "still loading". Fails if StartThread stops resetting
// _bExit and _eventThreadExit.
IW_TEST(AWorkerRestartedAfterStopStillDoesWork)
{
	TestRecoveringThread thread;

	thread.StartThread();
	thread.PostWork();

	for (int i = 0; i < 500 && ::InterlockedCompareExchange(&thread._nCompleted, 0, 0) < 1; i++)
		Sleep(10);

	IW_CHECK_EQ(::InterlockedCompareExchange(&thread._nCompleted, 0, 0), 1L);

	thread.StopThread();
	thread.StartThread();
	thread.PostWork();

	for (int i = 0; i < 500 && ::InterlockedCompareExchange(&thread._nCompleted, 0, 0) < 2; i++)
		Sleep(10);

	IW_CHECK_EQ(::InterlockedCompareExchange(&thread._nCompleted, 0, 0), 2L);

	thread.StopThread();
}

// StopThread has to wake a worker parked on an infinite wait, and must not
// return while it is still using the object.
IW_TEST(StopThreadWakesAndJoinsABlockedWorker)
{
	TestBlockingThread thread;
	thread.StartThread();

	for (int i = 0; i < 500 && !thread._bRunning; i++)
		Sleep(10);

	IW_CHECK(thread._bRunning);

	thread.StopThread();
	IW_CHECK(!thread._bRunning);
}

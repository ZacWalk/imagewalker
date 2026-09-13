#include "StdAfx.h"

#include "FixedPoint.h"
#include "Selection.h"
#include "Jobs.h"
#include "LoadAny.h"
#include "LoadJpg.h"
#include "LoadPng.h"
#include "LoadGif.h"

#include "iw/commontests.h"
#include "iw/channelaveragetests.h"
#include "iw/exceptionunwindtests.h"

// ArtMate 1.00 -- run with /test.

IW_TEST(FixedPointConvertsBothWays)
{
	IW_CHECK_EQ(INT_TO_FIXED(1), 65536);
	IW_CHECK_EQ(FIXED_TO_INT(INT_TO_FIXED(37)), 37);
	IW_CHECK_EQ(DOUBLE_TO_FIXED(0.5), 32768);
	IW_CHECK(FIXED_TO_DOUBLE(DOUBLE_TO_FIXED(0.25)) == 0.25);
}

IW_TEST(GifPackedPaletteMasksEachFiveBitChannel)
{
	struct PaletteReader : CLoadGif
	{
		void ReadPalette(WORD packed, RGBQUAD &color)
		{
			BYTE data[] = {static_cast<BYTE>(packed & 0xff), static_cast<BYTE>(packed >> 8)};
			m_pPointer = data;
			m_pEndData = data + sizeof(data);
			IW_CHECK(LoadPalette(&color, 1, 16));
		}
	} reader;
	for (unsigned value = 0; value <= 0xffff; ++value)
	{
		RGBQUAD color{};
		reader.ReadPalette(static_cast<WORD>(value), color);
		IW_CHECK_EQ(color.rgbRed, ((value >> 10) & 31) << 3);
		IW_CHECK_EQ(color.rgbGreen, ((value >> 5) & 31) << 3);
		IW_CHECK_EQ(color.rgbBlue, (value & 31) << 3);
	}
}

IW_TEST(FixedPointRoundsToNearest)
{
	IW_CHECK_EQ(ROUND_FIXED_TO_INT(DOUBLE_TO_FIXED(2.4)), 2);
	IW_CHECK_EQ(ROUND_FIXED_TO_INT(DOUBLE_TO_FIXED(2.5)), 3);
	IW_CHECK_EQ(ROUND_FIXED_TO_INT(DOUBLE_TO_FIXED(2.6)), 3);
}

// These replaced 32-bit inline assembly, so the 64-bit arithmetic is worth
// pinning down.
IW_TEST(FixedPointMultipliesAndDivides)
{
	IW_CHECK_EQ(FixedMul(INT_TO_FIXED(6), INT_TO_FIXED(7)), INT_TO_FIXED(42));
	IW_CHECK_EQ(FixedMul(DOUBLE_TO_FIXED(0.5), INT_TO_FIXED(9)), DOUBLE_TO_FIXED(4.5));

	int product = 0;
	FIXED_MUL(DOUBLE_TO_FIXED(1.5), DOUBLE_TO_FIXED(1.5), product);
	IW_CHECK_EQ(product, DOUBLE_TO_FIXED(2.25));

	int quotient = 0;
	FIXED_DIV(INT_TO_FIXED(9), INT_TO_FIXED(2), quotient);
	IW_CHECK_EQ(quotient, DOUBLE_TO_FIXED(4.5));

	// A product beyond 32 bits before the shift back down is the case the
	// original assembly handled with edx:eax.
	IW_CHECK_EQ(FixedMul(INT_TO_FIXED(4000), INT_TO_FIXED(4000)), INT_TO_FIXED(16000000));
}

// A thumbnail job used to be built with no job number at all, which makes
// Exit() permanently false: the decode could not be cancelled and the browser's
// destructor waited for it to finish.
IW_TEST(ThumbnailJobsAreCancellable)
{
	LONG nJobNo = 0;
	CJobThumb job(NULL, _T("test.jpg"), 1234, &nJobNo);

	IW_CHECK(!job.Exit());

	::InterlockedIncrement(&nJobNo);

	IW_CHECK(job.Exit());
}

// The scale job used to hold the address of a CDib the image pane owns, and the
// UI thread reallocates that surface on the next load - while the worker is
// reading it.
IW_TEST(ScaleJobOwnsItsSource)
{
	LONG nJobNo = 0;

	CDib dib;
	IW_CHECK(dib.Create(4, 4, 24, FALSE));
	dib.GetBitmap(0)[0] = 0x5A;

	CJobScale job(NULL, dib, CSize(2, 2), &nJobNo, "Scaling %d%%", FALSE);

	IW_CHECK(dib.Create(64, 64, 24, FALSE));

	IW_CHECK_EQ(job.m_dibSrc.Width(), 4u);
	IW_CHECK_EQ(job.m_dibSrc.Height(), 4u);
	IW_CHECK_EQ(job.m_dibSrc.GetBitmap(0)[0], 0x5A);
}

// The selection is the only thing that says what the image pane shows, so its
// Explorer semantics are worth pinning down.

IW_TEST(SelectionStartsEmptyAndUnfocused)
{
	CSelection selection;
	selection.Reset(10);

	IW_CHECK_EQ(selection.Size(), 10);
	IW_CHECK_EQ(selection.Count(), 0);
	IW_CHECK_EQ(selection.Focus(), -1);
}

IW_TEST(SelectionPlainClickReplaces)
{
	CSelection selection;
	selection.Reset(10);

	selection.Click(3, false, false);
	selection.Click(7, false, false);

	IW_CHECK_EQ(selection.Count(), 1);
	IW_CHECK(selection.Contains(7));
	IW_CHECK(!selection.Contains(3));
	IW_CHECK_EQ(selection.Focus(), 7);
}

IW_TEST(SelectionControlClickToggles)
{
	CSelection selection;
	selection.Reset(10);

	selection.Click(2, false, false);
	selection.Click(5, true, false);

	IW_CHECK_EQ(selection.Count(), 2);
	IW_CHECK_EQ(selection.Focus(), 5);

	selection.Click(5, true, false);

	IW_CHECK_EQ(selection.Count(), 1);
	IW_CHECK(selection.Contains(2));

	// Focus stays on the item that was just clicked even though it is no longer
	// selected, which is what Explorer does.
	IW_CHECK_EQ(selection.Focus(), 5);
}

// Dragging the far end of a shift range back has to shrink it again; the
// original replaced this with a growing union.
IW_TEST(SelectionShiftRangeCanShrink)
{
	CSelection selection;
	selection.Reset(10);

	selection.Click(2, false, false);
	selection.Click(8, false, true);
	IW_CHECK_EQ(selection.Count(), 7);

	selection.Click(4, false, true);
	IW_CHECK_EQ(selection.Count(), 3);
	IW_CHECK(selection.Contains(2) && selection.Contains(3) && selection.Contains(4));
	IW_CHECK(!selection.Contains(5));
	IW_CHECK_EQ(selection.Focus(), 4);
}

IW_TEST(SelectionSelectAllAndInvert)
{
	CSelection selection;
	selection.Reset(5);

	selection.SelectAll();
	IW_CHECK_EQ(selection.Count(), 5);

	selection.Invert();
	IW_CHECK_EQ(selection.Count(), 0);

	selection.Click(1, false, false);
	selection.Invert();
	IW_CHECK_EQ(selection.Count(), 4);
	IW_CHECK(!selection.Contains(1));
}

IW_TEST(SelectionIgnoresOutOfRange)
{
	CSelection selection;
	selection.Reset(3);

	selection.Click(-1, false, false);
	selection.Click(3, false, false);

	IW_CHECK_EQ(selection.Count(), 0);
	IW_CHECK_EQ(selection.Focus(), -1);
	IW_CHECK(!selection.Contains(3));
}

namespace
{
	// All fixtures stay in owned memory: tests need no sample downloads, files
	// beside the executable, or cleanup after a failed assertion.
	const BYTE loaderColors[4][3] =
	{
		{ 229, 31, 67 }, { 17, 193, 83 }, { 43, 71, 211 }, { 241, 179, 23 }
	};

	void Append16(std::vector<BYTE>& bytes, unsigned value, bool bigEndian = false)
	{
		bytes.push_back(static_cast<BYTE>(bigEndian ? value >> 8 : value));
		bytes.push_back(static_cast<BYTE>(bigEndian ? value : value >> 8));
	}

	void Append32(std::vector<BYTE>& bytes, unsigned value, bool bigEndian = false)
	{
		Append16(bytes, bigEndian ? value >> 16 : value, bigEndian);
		Append16(bytes, bigEndian ? value : value >> 16, bigEndian);
	}

	std::vector<BYTE> PalettePixels(const std::vector<BYTE>& indices, bool alpha = false)
	{
		std::vector<BYTE> pixels;
		for (BYTE index : indices)
		{
			pixels.insert(pixels.end(), loaderColors[index], loaderColors[index] + 3);
			pixels.push_back(alpha ? static_cast<BYTE>(index * 85) : 255);
		}
		return pixels;
	}

	void CheckDecode(CLoadAny& loader, std::vector<BYTE>& bytes, unsigned width, unsigned height,
		unsigned bpp, const std::vector<BYTE>& expected, int tolerance = 0, BOOL thumbnail = FALSE)
	{
		IW_CHECK(!bytes.empty());
		if (bytes.empty())
			return;

		CDib dib;
		const BOOL loaded = loader.Load(&dib, bytes.data(), static_cast<DWORD>(bytes.size()), nullptr, thumbnail);
		IW_CHECK(loaded);
		IW_CHECK(dib.IsOpen());
		if (!loaded || !dib.IsOpen())
			return;

		IW_CHECK_EQ(dib.Width(), width);
		IW_CHECK_EQ(dib.Height(), height);
		IW_CHECK_EQ(dib.Bpp(), bpp);
		IW_CHECK_EQ(expected.size(), static_cast<size_t>(width) * height * 4);
		if (dib.Width() != width || dib.Height() != height || dib.Bpp() != bpp ||
			expected.size() != static_cast<size_t>(width) * height * 4)
			return;

		for (unsigned y = 0; y < height; ++y)
		{
			const BYTE* row = dib.GetBitmap(y);
			for (unsigned x = 0; x < width; ++x)
			{
				RGBQUAD pixel = {};
				if (bpp == 8)
					pixel = dib.GetColor()[row[x]];
				else
				{
					pixel.rgbBlue = row[x * (bpp / 8)];
					pixel.rgbGreen = row[x * (bpp / 8) + 1];
					pixel.rgbRed = row[x * (bpp / 8) + 2];
					pixel.rgbReserved = bpp == 32 ? row[x * 4 + 3] : 255;
				}
				const BYTE* want = expected.data() + (y * width + x) * 4;
				IW_CHECK(abs(static_cast<int>(pixel.rgbRed) - want[0]) <= tolerance);
				IW_CHECK(abs(static_cast<int>(pixel.rgbGreen) - want[1]) <= tolerance);
				IW_CHECK(abs(static_cast<int>(pixel.rgbBlue) - want[2]) <= tolerance);
				IW_CHECK_EQ(pixel.rgbReserved, want[3]);
			}
		}
	}

	void CheckRejected(CLoadAny& loader, std::vector<BYTE> bytes)
	{
		CDib dib;
		IW_CHECK(!loader.Load(&dib, bytes.data(), static_cast<DWORD>(bytes.size()), nullptr, FALSE));
	}

	std::vector<BYTE> MakeBmp(unsigned width, unsigned height, const std::vector<BYTE>& indices, bool indexed)
	{
		const unsigned bpp = indexed ? 8 : 24;
		const unsigned stride = ((width * (bpp / 8)) + 3) & ~3u;
		const unsigned offset = 54 + (indexed ? 256 * 4 : 0);
		std::vector<BYTE> bytes = { 'B', 'M' };
		Append32(bytes, offset + stride * height);
		Append32(bytes, 0);
		Append32(bytes, offset);
		Append32(bytes, 40);
		Append32(bytes, width);
		Append32(bytes, height);
		Append16(bytes, 1);
		Append16(bytes, bpp);
		Append32(bytes, BI_RGB);
		Append32(bytes, stride * height);
		bytes.resize(54, 0);
		if (indexed)
		{
			bytes.resize(offset, 0);
			for (unsigned i = 0; i < 4; ++i)
			{
				bytes[54 + i * 4] = loaderColors[i][2];
				bytes[54 + i * 4 + 1] = loaderColors[i][1];
				bytes[54 + i * 4 + 2] = loaderColors[i][0];
			}
		}
		// Nonzero padding catches a decoder treating the DIB stride as pixels.
		bytes.resize(offset + stride * height, 0xAD);
		for (unsigned y = 0; y < height; ++y)
			for (unsigned x = 0; x < width; ++x)
			{
				const BYTE index = indices[y * width + x];
				const unsigned pos = offset + (height - 1 - y) * stride + x * (bpp / 8);
				if (indexed)
					bytes[pos] = index;
				else
					for (unsigned channel = 0; channel < 3; ++channel)
						bytes[pos + channel] = loaderColors[index][2 - channel];
			}
		return bytes;
	}

	std::vector<BYTE> MakeGif(const std::vector<BYTE>& indices, bool interlaced)
	{
		std::vector<BYTE> bytes = { 'G', 'I', 'F', '8', '9', 'a', 3, 0, 5, 0, 0x81, 0, 0 };
		for (const auto& color : loaderColors)
			bytes.insert(bytes.end(), color, color + 3);
		const BYTE control[] = { 0x21, 0xF9, 4, 1, 0, 0, 2, 0 };
		bytes.insert(bytes.end(), control, control + sizeof(control));
		bytes.push_back(0x2C);
		Append16(bytes, 0);
		Append16(bytes, 0);
		Append16(bytes, 3);
		Append16(bytes, 5);
		bytes.push_back(interlaced ? 0x40 : 0);
		bytes.push_back(2);

		std::vector<BYTE> raster;
		unsigned bits = 0;
		const auto emit = [&](unsigned code)
		{
			for (unsigned bit = 0; bit < 3; ++bit, ++bits)
			{
				if (bits % 8 == 0)
					raster.push_back(0);
				raster.back() |= static_cast<BYTE>(((code >> bit) & 1) << (bits % 8));
			}
		};
		const unsigned interlaceRows[] = { 0, 4, 2, 1, 3 };
		for (unsigned row = 0; row < 5; ++row)
			for (unsigned x = 0; x < 3; ++x)
			{
				// Clear between literals keeps the independently generated LZW
				// stream at three bits per code, without an encoder dependency.
				emit(4);
				emit(indices[(interlaced ? interlaceRows[row] : row) * 3 + x]);
			}
		emit(5);
		bytes.push_back(static_cast<BYTE>(raster.size()));
		bytes.insert(bytes.end(), raster.begin(), raster.end());
		bytes.push_back(0);
		bytes.push_back(0x3B);
		return bytes;
	}

	std::vector<BYTE> MakePcx(const std::vector<BYTE>& indices)
	{
		std::vector<BYTE> bytes(128, 0);
		bytes[0] = 0x0A;
		bytes[1] = 5;
		bytes[2] = 1;
		bytes[3] = 8;
		bytes[8] = 3;
		bytes[10] = 2;
		bytes[65] = 1;
		bytes[66] = 4;
		bytes[68] = 1;
		for (size_t i = 0; i < indices.size();)
		{
			size_t count = 1;
			while (count < 4 - (i % 4) && indices[i + count] == indices[i])
				++count;
			const BYTE value = indices[i] == 3 ? 0xE3 : indices[i];
			if (count > 1 || value >= 0xC0)
				bytes.push_back(static_cast<BYTE>(0xC0 | count));
			bytes.push_back(value);
			i += count;
		}
		bytes.push_back(0x0C);
		const size_t palette = bytes.size();
		bytes.resize(palette + 256 * 3, 0);
		for (unsigned i = 0; i < 4; ++i)
			memcpy(bytes.data() + palette + (i == 3 ? 0xE3 : i) * 3, loaderColors[i], 3);
		return bytes;
	}

	std::vector<BYTE> MakePng(unsigned width, unsigned height, const std::vector<BYTE>& rgba)
	{
		png_image image = {};
		image.version = PNG_IMAGE_VERSION;
		image.width = width;
		image.height = height;
		image.format = PNG_FORMAT_RGBA;
		png_alloc_size_t size = 0;
		const bool measured = png_image_write_to_memory(&image, nullptr, &size, 0, rgba.data(), 0, nullptr) != 0;
		IW_CHECK(measured);
		std::vector<BYTE> bytes(measured ? static_cast<size_t>(size) : 0);
		if (measured)
		{
			const bool written = png_image_write_to_memory(&image, bytes.data(), &size, 0, rgba.data(), 0, nullptr) != 0;
			IW_CHECK(written);
			bytes.resize(written ? static_cast<size_t>(size) : 0);
		}
		png_image_free(&image);
		return bytes;
	}

	std::vector<BYTE> MakeTiff(const std::vector<BYTE>& rgba, bool bigEndian)
	{
		// Classic uncompressed RGB TIFF, one strip, with an explicit top-left
		// orientation. Both byte orders use the same top-down pixel payload.
		std::vector<BYTE> bytes(2, bigEndian ? 'M' : 'I');
		Append16(bytes, 42, bigEndian);
		Append32(bytes, 8, bigEndian);
		Append16(bytes, 11, bigEndian);
		const unsigned bitsOffset = 8 + 2 + 11 * 12 + 4;
		const unsigned pixelsOffset = bitsOffset + 6;
		const auto entry = [&](unsigned tag, unsigned type, unsigned count, unsigned value)
		{
			Append16(bytes, tag, bigEndian);
			Append16(bytes, type, bigEndian);
			Append32(bytes, count, bigEndian);
			if (type == 3 && count == 1)
			{
				Append16(bytes, value, bigEndian);
				Append16(bytes, 0, bigEndian);
			}
			else
				Append32(bytes, value, bigEndian);
		};
		entry(256, 4, 1, 3);
		entry(257, 4, 1, 2);
		entry(258, 3, 3, bitsOffset);
		entry(259, 3, 1, 1);
		entry(262, 3, 1, 2);
		entry(273, 4, 1, pixelsOffset);
		entry(274, 3, 1, 1);
		entry(277, 3, 1, 3);
		entry(278, 4, 1, 2);
		entry(279, 4, 1, 18);
		entry(284, 3, 1, 1);
		Append32(bytes, 0, bigEndian);
		for (unsigned i = 0; i < 3; ++i)
			Append16(bytes, 8, bigEndian);
		for (size_t i = 0; i < rgba.size(); i += 4)
			bytes.insert(bytes.end(), rgba.begin() + i, rgba.begin() + i + 3);
		return bytes;
	}

	struct JpegFixtureWriter
	{
		struct Error
		{
			jpeg_error_mgr base;
			jmp_buf jump;
		} error = {};
		jpeg_compress_struct info = {};
		unsigned char* bytes = nullptr;
		unsigned long size = 0;

		~JpegFixtureWriter()
		{
			jpeg_destroy_compress(&info);
			free(bytes);
		}

		static void OnError(j_common_ptr info)
		{
			auto* error = reinterpret_cast<Error*>(info->err);
			longjmp(error->jump, 1);
		}

		bool Write(unsigned width, unsigned height, std::vector<BYTE>& pixels, bool grayscale)
		{
			info.err = jpeg_std_error(&error.base);
			error.base.error_exit = OnError;
			// No automatic objects requiring destruction may cross this jump.
#pragma warning(suppress: 4611)
			if (setjmp(error.jump))
				return false;
			jpeg_create_compress(&info);
			jpeg_mem_dest(&info, &bytes, &size);
			info.image_width = width;
			info.image_height = height;
			info.input_components = grayscale ? 1 : 3;
			info.in_color_space = grayscale ? JCS_GRAYSCALE : JCS_RGB;
			jpeg_set_defaults(&info);
			for (int i = 0; i < info.num_components; ++i)
				info.comp_info[i].h_samp_factor = info.comp_info[i].v_samp_factor = 1;
			jpeg_set_quality(&info, 100, TRUE);
			jpeg_start_compress(&info, TRUE);
			while (info.next_scanline < info.image_height)
			{
				JSAMPROW row = pixels.data() + static_cast<size_t>(info.next_scanline) * width * info.input_components;
				jpeg_write_scanlines(&info, &row, 1);
			}
			jpeg_finish_compress(&info);
			return true;
		}
	};
}

IW_TEST(BmpLoaderDecodesBottomUpRowsPaddingAndPalette)
{
	const std::vector<BYTE> indices = { 0, 1, 2, 3, 2, 1 };
	const auto rgba = PalettePixels(indices);
	CLoadAny loader;
	for (bool indexed : { false, true })
	{
		auto bytes = MakeBmp(3, 2, indices, indexed);
		for (BOOL thumbnail : { FALSE, TRUE })
			CheckDecode(loader, bytes, 3, 2, indexed ? 8 : 24, rgba, 0, thumbnail);
		CheckRejected(loader, std::vector<BYTE>(bytes.begin(), bytes.begin() + 53));
		auto truncated = bytes;
		truncated.pop_back();
		CheckRejected(loader, truncated);
		auto badOffset = bytes;
		memset(badOffset.data() + 10, 0xFF, 4);
		CheckRejected(loader, badOffset);
		CheckDecode(loader, bytes, 3, 2, indexed ? 8 : 24, rgba);
	}
}

IW_TEST(BmpLoaderThumbnailKeepsColorsAndAspectRatio)
{
	std::vector<BYTE> indices(160 * 80);
	std::vector<BYTE> thumbnailIndices(80 * 40);
	for (unsigned y = 0; y < 80; ++y)
		for (unsigned x = 0; x < 160; ++x)
			indices[y * 160 + x] = static_cast<BYTE>((x / 80) + 2 * (y / 40));
	for (unsigned y = 0; y < 40; ++y)
		for (unsigned x = 0; x < 80; ++x)
			thumbnailIndices[y * 80 + x] = static_cast<BYTE>((x / 40) + 2 * (y / 20));
	auto bytes = MakeBmp(160, 80, indices, false);
	CLoadAny loader;
	CheckDecode(loader, bytes, 80, 40, 32, PalettePixels(thumbnailIndices), 0, TRUE);
}

IW_TEST(GifLoaderDecodesInterlacedRowsAndTransparency)
{
	const std::vector<BYTE> indices = { 0, 1, 2, 3, 2, 1, 1, 3, 0, 2, 0, 3, 3, 1, 2 };
	auto rgba = PalettePixels(indices);
	for (size_t i = 0; i < indices.size(); ++i)
		if (indices[i] == 2)
			rgba[i * 4 + 3] = 0;
	CLoadAny loader;
	for (bool interlaced : { false, true })
	{
		auto bytes = MakeGif(indices, interlaced);
		for (BOOL thumbnail : { FALSE, TRUE })
			CheckDecode(loader, bytes, 3, 5, 8, rgba, 0, thumbnail);
		auto badVersion = bytes;
		badVersion[3] = '0';
		CheckRejected(loader, badVersion);
		CheckRejected(loader, std::vector<BYTE>(bytes.begin(), bytes.begin() + 12));
		CheckRejected(loader, std::vector<BYTE>(bytes.begin(), bytes.begin() + 20));
		CheckDecode(loader, bytes, 3, 5, 8, rgba);
	}
}

IW_TEST(GifLiteralAndStackOutputPreserveTheHighestByteIndices)
{
	std::vector<BYTE> bytes = {'G', 'I', 'F', '8', '9', 'a', 2, 0, 1, 0, 0x87, 0, 0};
	for (unsigned i = 0; i < 256; ++i)
		for (unsigned channel = 0; channel < 3; ++channel)
			bytes.push_back(static_cast<BYTE>(i));
	const BYTE descriptor[] = {0x2c, 0, 0, 0, 0, 2, 0, 1, 0, 0, 8};
	bytes.insert(bytes.end(), descriptor, descriptor + sizeof(descriptor));
	std::vector<BYTE> raster;
	unsigned bits = 0;
	for (unsigned code : {256u, 255u, 254u, 257u})
		for (unsigned bit = 0; bit < 9; ++bit, ++bits)
		{
			if (bits % 8 == 0) raster.push_back(0);
			raster.back() |= static_cast<BYTE>(((code >> bit) & 1) << (bits % 8));
		}
	bytes.push_back(static_cast<BYTE>(raster.size()));
	bytes.insert(bytes.end(), raster.begin(), raster.end());
	bytes.push_back(0);
	bytes.push_back(0x3b);
	CLoadAny loader;
	CheckDecode(loader, bytes, 2, 1, 8, {255, 255, 255, 255, 254, 254, 254, 255});
}

IW_TEST(PcxLoaderDecodesRleAndHighPaletteIndices)
{
	const std::vector<BYTE> indices = { 0, 0, 3, 3, 3, 3, 1, 1, 2, 2, 2, 3 };
	const auto rgba = PalettePixels(indices);
	auto bytes = MakePcx(indices);
	CLoadAny loader;
	for (BOOL thumbnail : { FALSE, TRUE })
		CheckDecode(loader, bytes, 4, 3, 8, rgba, 0, thumbnail);
	CheckRejected(loader, std::vector<BYTE>(bytes.begin(), bytes.begin() + 127));
	auto missingPalette = bytes;
	missingPalette.pop_back();
	CheckRejected(loader, missingPalette);
	auto zeroRun = bytes;
	zeroRun[128] = 0xC0;
	CheckRejected(loader, zeroRun);
	auto badEncoding = bytes;
	badEncoding[2] = 0;
	CheckRejected(loader, badEncoding);
	auto tooLarge = bytes;
	tooLarge[8] = tooLarge[10] = 0xFF;
	tooLarge[9] = tooLarge[11] = 0x7F;
	CheckRejected(loader, tooLarge);
	CheckDecode(loader, bytes, 4, 3, 8, rgba);
}

IW_TEST(PngLoaderPreservesRgbaRatherThanCompositingIt)
{
	const auto rgba = PalettePixels({ 0, 1, 2, 3, 2, 1 }, true);
	auto bytes = MakePng(3, 2, rgba);
	IW_CHECK(bytes.size() >= 33);
	if (bytes.size() < 33)
		return;
	CLoadAny loader;
	for (BOOL thumbnail : { FALSE, TRUE })
		CheckDecode(loader, bytes, 3, 2, 32, rgba, 0, thumbnail);
	CheckRejected(loader, std::vector<BYTE>(bytes.begin(), bytes.begin() + 8));
	CheckRejected(loader, std::vector<BYTE>(bytes.begin(), bytes.begin() + 32));
	auto badCrc = bytes;
	badCrc[29] ^= 1;
	CheckRejected(loader, badCrc);
	CheckDecode(loader, bytes, 3, 2, 32, rgba);
}

IW_TEST(TiffLoaderDecodesBothByteOrdersAndTopLeftRgb)
{
	const auto rgba = PalettePixels({ 0, 1, 2, 3, 2, 1 });
	CLoadAny loader;
	for (bool bigEndian : { false, true })
	{
		auto bytes = MakeTiff(rgba, bigEndian);
		for (BOOL thumbnail : { FALSE, TRUE })
			CheckDecode(loader, bytes, 3, 2, 32, rgba, 0, thumbnail);
		CheckRejected(loader, std::vector<BYTE>(bytes.begin(), bytes.begin() + 7));
		CheckRejected(loader, std::vector<BYTE>(bytes.begin(), bytes.begin() + 8));
		auto badStrip = bytes;
		memset(badStrip.data() + 10 + 5 * 12 + 8, 0xFF, 4);
		CheckRejected(loader, badStrip);
		CheckDecode(loader, bytes, 3, 2, 32, rgba);
	}
}

IW_TEST(JpegLoaderDecodesColorAndGrayscaleAndRecoversAfterBadHeader)
{
	CLoadAny loader;
	for (bool grayscale : { false, true })
	{
		std::vector<BYTE> pixels;
		std::vector<BYTE> rgba;
		// Constant, MCU-aligned blocks avoid edge ringing. A small tolerance
		// accounts for JPEG's lossy color conversion and ArtMate's fast IDCT.
		for (unsigned y = 0; y < 16; ++y)
			for (unsigned x = 0; x < 16; ++x)
			{
				const unsigned index = (x / 8) + 2 * (y / 8);
				if (grayscale)
				{
					const BYTE value = static_cast<BYTE>(32 + index * 64);
					pixels.push_back(value);
					rgba.insert(rgba.end(), 3, value);
				}
				else
				{
					pixels.insert(pixels.end(), loaderColors[index], loaderColors[index] + 3);
					rgba.insert(rgba.end(), loaderColors[index], loaderColors[index] + 3);
				}
				rgba.push_back(255);
			}
		JpegFixtureWriter writer;
		const bool written = writer.Write(16, 16, pixels, grayscale);
		IW_CHECK(written);
		if (!written)
			return;
		std::vector<BYTE> bytes(writer.bytes, writer.bytes + writer.size);
		for (BOOL thumbnail : { FALSE, TRUE })
			CheckDecode(loader, bytes, 16, 16, grayscale ? 8 : 24, rgba, 3, thumbnail);
		CheckRejected(loader, { 0xFF, 0xD8, 0xFF, 0xD9 });
		CheckRejected(loader, { 0xFF, 0xD8, 0xFF, 0xE0, 0, 1 });
		CheckDecode(loader, bytes, 16, 16, grayscale ? 8 : 24, rgba, 3);
	}
}

IW_TEST(ImageLoaderRejectsEmptyAndUnknownSignatures)
{
	CLoadAny loader;
	CDib dib;
	IW_CHECK(!loader.Load(&dib, static_cast<LPBYTE>(nullptr), 0, nullptr, FALSE));
	CheckRejected(loader, { 'B' });
	CheckRejected(loader, std::vector<BYTE>(16, 0xAA));
}

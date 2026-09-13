#pragma once

#include "channelaveragetests.h"
#include "exceptionunwindtests.h"
#include "jfifdensity.h"
#include "printrange.h"

void CheckMainFrameCommandDispatchForTests();
void CheckFullScreenLayoutForTests();

IW_TEST(MainFrameMessageMapDispatchesToTheViewThenTheFrame)
{
	CheckMainFrameCommandDispatchForTests();
}

IW_TEST(FullScreenStaysOnItsMonitorAndRestoresWindowedLayout)
{
	CheckFullScreenLayoutForTests();
}

IW_TEST(ThumbnailChannelAveragesRoundAlphaWithItsOwnSampleCount)
{
	const IW::RGBSUM sums[] = {
		{510, 0, 510, 2, 510, 4},
		{0, 0, 0, 0, 0, 4},
		{255, 128, 1, 1, 255, 1},
		{0xffffffffu, 0xffffffffu, 0xffffffffu, 16843009u, 0xffffffffu, 16843009u}
	};
	BYTE result[16]{};
	IW::SumLineTo32(result, sums, 4);
	const BYTE expected[] = {255, 0, 255, 128, 0, 0, 0, 0, 255, 128, 1, 255, 255, 255, 255, 255};
	IW_CHECK(memcmp(result, expected, sizeof(expected)) == 0);
}

IW_TEST(LabConversionOnlyNarrowsClampedChannels)
{
	for (BYTE l : {BYTE{0}, BYTE{100}, BYTE{255}})
		for (BYTE a : {BYTE{0}, BYTE{128}, BYTE{255}})
			for (BYTE b : {BYTE{0}, BYTE{128}, BYTE{255}})
			{
				const BYTE input[] = {l, a, b};
				BYTE output[3]{};
				int red = 0, green = 0, blue = 0;
				IW::LABtoRGB(l, a, b, red, green, blue);
				IW_CHECK(red >= 0 && red <= 255 && green >= 0 && green <= 255 && blue >= 0 && blue <= 255);
				IW::ConvertCIELABtoBGR(output, input, 1);
				IW_CHECK_EQ(output[0], blue);
				IW_CHECK_EQ(output[1], green);
				IW_CHECK_EQ(output[2], red);
			}
}

IW_TEST(HslExtremaAndPaletteChannelsRetainTheByteRange)
{
	for (const int red : {0, 1, 127, 254, 255})
		for (const int green : {0, 1, 127, 254, 255})
			for (const int blue : {0, 1, 127, 254, 255})
			{
				int hue = 0, saturation = 0, lightness = 0;
				IW::RGBtoHSL(red, green, blue, hue, saturation, lightness);
				IW_CHECK(hue >= 0 && hue <= 255);
				IW_CHECK(saturation >= 0 && saturation <= 255);
				IW_CHECK(lightness >= 0 && lightness <= 255);
				const COLORREF color = RGB(red, green, blue);
				const PALETTEENTRY entry{GetRValue(color), GetGValue(color), GetBValue(color), 0};
				IW_CHECK_EQ(entry.peRed, IW::GetR(color));
				IW_CHECK_EQ(entry.peGreen, IW::GetG(color));
				IW_CHECK_EQ(entry.peBlue, IW::GetB(color));
			}
}

IW_TEST(PsdEightBitChannelsKeepEveryPossibleSample)
{
	std::vector<BYTE> bytes(40 + 4 * 256, 0);
	memcpy(bytes.data(), "8BPS", 4);
	bytes[5] = 1;   // PSD version.
	bytes[13] = 4;  // RGBA planes.
	bytes[17] = 1;  // One row.
	bytes[20] = 1;  // 256 columns, big endian.
	bytes[23] = 8;
	bytes[25] = 3;  // RGB, uncompressed, no optional blocks.
	for (unsigned channel = 0; channel < 4; ++channel)
		for (unsigned value = 0; value < 256; ++value)
			bytes[40 + channel * 256 + value] = static_cast<BYTE>(value);
	IW::StreamConstBlob stream(bytes.data(), bytes.size());
	IW::Image image;
	IW::ImageStream<IW::IImageStream> output(image);
	CLoadPsd loader;
	const bool loaded = loader.Read(_T("PSD"), &stream, &output, IW::CNullStatus::Instance);
	IW_CHECK(loaded && !image.IsEmpty());
	if (!loaded || image.IsEmpty()) return;
	IW_CHECK_EQ(image.GetFirstPage().GetWidth(), 256);
	std::vector<COLORREF> pixels(256);
	image.GetFirstPage().GetSurfaceLock()->GetLine(pixels.data(), 0, 0, 256);
	for (unsigned value = 0; value < 256; ++value)
	{
		IW_CHECK_EQ(IW::GetR(pixels[value]), value);
		IW_CHECK_EQ(IW::GetG(pixels[value]), value);
		IW_CHECK_EQ(IW::GetB(pixels[value]), value);
		IW_CHECK_EQ(IW::GetA(pixels[value]), value);
	}
}

IW_TEST(QuantizerRetainsDimensionsBeyondSixteenBits)
{
	for (const CSize size : {CSize(65536, 1), CSize(1, 65536)})
	{
		IW::Image input;
		auto &page = input.CreatePage(size.cx, size.cy, IW::PixelFormat::PF24);
		auto lock = page.GetSurfaceLock();
		std::vector<COLORREF> line(static_cast<size_t>(size.cx));
		for (int y = 0; y < size.cy; ++y)
		{
			for (int x = 0; x < size.cx; ++x)
				line[x] = ((x + y) & 1) ? 0xffffffffu : 0xff000000u;
			lock->SetLine(line.data(), y, 0, size.cx);
		}
		IW::Image output;
		IW_CHECK(IW::Quantize(input, output, IW::CNullStatus::Instance));
		IW_CHECK(!output.IsEmpty());
		if (output.IsEmpty()) continue;
		const auto &result = output.GetFirstPage();
		IW_CHECK_EQ(result.GetWidth(), size.cx);
		IW_CHECK_EQ(result.GetHeight(), size.cy);
		if (result.GetWidth() != size.cx || result.GetHeight() != size.cy) continue;
		auto resultLock = result.GetSurfaceLock();
		bool exact = true;
		for (int y = 0; y < size.cy; ++y)
		{
			// Opaque indexed output: compare palette colours, not the stored indices.
			resultLock->RenderLine(line.data(), y, 0, size.cx);
			for (int x = 0; x < size.cx; ++x)
				exact = exact && line[x] == (((x + y) & 1) ? 0xffffffffu : 0xff000000u);
		}
		IW_CHECK(exact);
	}
}

IW_TEST(QuantizerKeepsChannelSumsBeyondSignedThirtyTwoBits)
{
	constexpr int width = 65536, height = 129;
	static_assert(sizeof(IW::Int64) == 8);
	static_assert(static_cast<IW::Int64>(width) * height * 255 > 0x7fffffff);
	IW::Image input;
	auto &page = input.CreatePage(width, height, IW::PixelFormat::PF24);
	auto lock = page.GetSurfaceLock();
	std::vector<COLORREF> line(width);
	for (const bool split : {false, true})
	{
		for (int x = 0; x < width; ++x)
			line[x] = split && x >= width / 2 ? IW::RGBA(255, 240, 255) : 0xffffffffu;
		for (int y = 0; y < height; ++y)
			lock->SetLine(line.data(), y, 0, width);
		IW::Image output;
		const bool quantized = IW::Quantize(input, output, IW::CNullStatus::Instance);
		IW_CHECK(quantized && !output.IsEmpty());
		if (!quantized || output.IsEmpty()) return;
		const auto &result = output.GetFirstPage();
		IW_CHECK_EQ(result.GetWidth(), width);
		IW_CHECK_EQ(result.GetHeight(), height);
		if (result.GetWidth() != width || result.GetHeight() != height) continue;
		auto resultLock = result.GetSurfaceLock();
		bool exact = true;
		for (int y = 0; y < height; ++y)
		{
			resultLock->RenderLine(line.data(), y, 0, width);
			for (int x = 0; x < width; ++x)
				if (line[x] != (split && x >= width / 2 ? IW::RGBA(255, 240, 255) : 0xffffffffu))
					exact = false;
		}
		IW_CHECK(exact);
	}
}

IW_TEST(JfifDensitySaturatesInsteadOfWrappingInBothSavePaths)
{
	IW_CHECK_EQ(IW::JfifDensity(0), 1);
	IW_CHECK_EQ(IW::JfifDensity(2834), 72);
	IW_CHECK_EQ(IW::JfifDensity(3937), 100);
	IW_CHECK_EQ(IW::JfifDensity(6553500, true), 65535);
	IW_CHECK_EQ(IW::JfifDensity(6553600, true), 65535);
	IW_CHECK_EQ(IW::JfifDensity(0xffffffffu), 65535);
	IW_CHECK_EQ(IW::JfifDensity(0xffffffffu, true), 65535);

	const auto checkHeader = [](const IW::SimpleBlob &bytes, BYTE unit)
	{
		IW_CHECK(bytes.GetDataSize() >= 18);
		if (bytes.GetDataSize() < 18) return;
		const BYTE *p = bytes.GetData();
		IW_CHECK(memcmp(p + 6, "JFIF\0", 5) == 0);
		IW_CHECK_EQ(p[13], unit);
		IW_CHECK_EQ((p[14] << 8) | p[15], 65535);
		IW_CHECK_EQ((p[16] << 8) | p[17], 1);
	};
	auto image = WorkflowSolidImage(IW::RGBA(255, 0, 0));
	image.SetXPelsPerMeter(0xffffffffu);
	image.SetYPelsPerMeter(0);
	IW::SimpleBlob encoded;
	IW::StreamBlob<IW::SimpleBlob> stream(encoded);
	CLoadJpg encoder;
	const bool saved = encoder.Write(_T("JPG"), &stream, image, IW::CodecSettings{}, IW::CNullStatus::Instance);
	IW_CHECK(saved);
	if (!saved) return;
	checkHeader(encoded, 1);

	IW::StreamConstBlob input(encoded);
	IW::SimpleBlob transformed;
	IW::StreamBlob<IW::SimpleBlob> output(transformed);
	CLoadJpg lossless;
	const bool copied = lossless.Write(&output, &input, image, IW::CodecSettings{}, IW::CNullStatus::Instance);
	IW_CHECK(copied);
	if (copied) checkHeader(transformed, 2);
}

IW_TEST(PrintDialogNarrowsOnlyItsSelectableRangeNotTheWholeJob)
{
	PRINTDLG dialog{};
	unsigned long first = 0, last = 0;
	for (const int count : {0, 1, 65535, 65536, 1000000})
	{
		IW::SetPrintDialogPageRange(dialog, count);
		IW_CHECK_EQ(dialog.nMinPage, 1);
		IW_CHECK_EQ(dialog.nFromPage, 1);
		IW_CHECK_EQ(dialog.nMaxPage, count < 1 ? 1 : count > 65535 ? 65535 : count);
		IW_CHECK_EQ(dialog.nToPage, dialog.nMaxPage);
		IW_CHECK_EQ(IW::GetPrintJobPageRange(dialog, count, first, last), count > 0);
		if (count > 0)
		{
			IW_CHECK_EQ(first, 1u);
			IW_CHECK_EQ(last, static_cast<unsigned long>(count));
		}
	}
	dialog.Flags = PD_PAGENUMS;
	dialog.nFromPage = 2;
	dialog.nToPage = 3;
	IW_CHECK(IW::GetPrintJobPageRange(dialog, 5, first, last));
	IW_CHECK_EQ(first, 2u);
	IW_CHECK_EQ(last, 3u);
	IW_CHECK(!IW::GetPrintJobPageRange(dialog, 2, first, last));
	dialog.nFromPage = 0;
	IW_CHECK(!IW::GetPrintJobPageRange(dialog, 5, first, last));
	dialog.nFromPage = 4;
	IW_CHECK(!IW::GetPrintJobPageRange(dialog, 5, first, last));
}

IW_TEST(CommandImageTableRejectsOutOfRangeIdsRatherThanAliasingThem)
{
	struct CommandBar : ImageWalkerCommandBarCtrl
	{
		using ImageWalkerCommandBarCtrl::m_arrCommand;
	} bar;
	bar.m_mapCommand[1] = 0;
	bar.m_mapCommand[65535] = 1;
	bar.m_mapCommand[65536] = 2;
	bar.m_mapCommand[-1] = 3;
	bar.m_mapCommand[0] = 4;
	bar.m_mapCommand[42] = -1;
	bar.UpdateCommands();
	IW_CHECK_EQ(bar.m_arrCommand.GetSize(), 2);
	if (bar.m_arrCommand.GetSize() == 2)
	{
		IW_CHECK_EQ(bar.m_arrCommand[0], 1);
		IW_CHECK_EQ(bar.m_arrCommand[1], 65535);
	}
}

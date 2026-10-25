// ImageWalker by Zac Walker
//
// Purpose: EXIF through libfiles - the camera settings and orientation a
//          loader reads, the numbers a lossless turn rewrites, and the
//          property reader: every IFD plus maker notes, with each tag name,
//          title and description, as libfiles lists them.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "LegacyPch.h"
#include "Metadata.h"
#include "MetadataExif.h"

#include <libfiles/exif.h>
#include <libfiles/exif_listing.h>
#include <libfiles/scan.h>

#include <cmath>

namespace
{
	// The TIFF stream after the APP1 segment's "Exif\0\0", or nothing.
	std::span<std::uint8_t> TiffOf(unsigned char* d, const unsigned int size)
	{
		static constexpr unsigned char signature[] = {'E', 'x', 'i', 'f', 0, 0};

		if (d == nullptr || size < 14 || memcmp(d, signature, sizeof signature) != 0)
			return {};

		return {d + sizeof signature, size - sizeof signature};
	}

	// value as a fraction over denominator, or false when the numerator would not fit.
	bool ToRational(const double value, const IW::UInt32 denominator, IW::Rational& out)
	{
		const double numerator = value * denominator;
		if (!(numerator >= 0.0 && numerator <= 4294967295.0))
			return false;

		out.numerator = static_cast<IW::UInt32>(std::llround(numerator));
		out.denominator = denominator;
		return true;
	}
}

void Exif::Parse(IW::CameraSettings* pSettings, const unsigned char* d, const unsigned int size)
{
	const auto tiff = TiffOf(const_cast<unsigned char*>(d), size);
	if (tiff.empty())
		return;

	const auto exif = libfiles::read_exif(tiff);

	if (exif.orientation)
		pSettings->Orientation = *exif.orientation;

	if (exif.f_number >= 1.0f)
		ToRational(2.0 * std::log2(exif.f_number), 10000, pSettings->Aperture);

	// Under a second as 1/n, which FormatExposureTime shows as it shows a camera's.
	if (exif.exposure_time > 0.0f && exif.exposure_time < 1.0f)
	{
		IW::Rational time;
		if (ToRational(1.0 / exif.exposure_time, 1, time))
		{
			time.denominator = time.numerator;
			time.numerator = 1;
			pSettings->ExposureTime = time;
		}
	}
	else if (exif.exposure_time >= 1.0f)
	{
		ToRational(exif.exposure_time, 1000, pSettings->ExposureTime);
	}

	if (exif.iso_speed != 0)
		pSettings->IsoSpeed = exif.iso_speed;

	if (exif.focal_length > 0.0f)
		ToRational(exif.focal_length, 1000, pSettings->FocalLength);

	if (exif.focal_length_35mm != 0)
		pSettings->FocalLength35mmEquivalent = exif.focal_length_35mm;

	if (!exif.original.text.empty())
		pSettings->DateTaken.ParseExifDate(exif.original.text.c_str());
}

int Exif::ReadOrientation(const unsigned char* d, const unsigned int size)
{
	const auto tiff = TiffOf(const_cast<unsigned char*>(d), size);
	const auto orientation = tiff.empty() ? std::nullopt : libfiles::summarize_exif(tiff).orientation;

	if (!orientation || *orientation < IW::Orientation::TopLeft || *orientation > IW::Orientation::LeftBottom)
		return IW::Orientation::TopLeft;

	return *orientation;
}

void Exif::FixExif(unsigned char* d, const unsigned int size, const int cx, const int cy)
{
	const libfiles::exif_number numbers[] = {
		{libfiles::exif_directory::exif, 0xa002, static_cast<std::uint32_t>(cx)}, // PixelXDimension
		{libfiles::exif_directory::exif, 0xa003, static_cast<std::uint32_t>(cy)}, // PixelYDimension
	};

	libfiles::set_exif_numbers(TiffOf(d, size), numbers);
}

void Exif::SetOrientation(unsigned char* d, const unsigned int size, const int nOrientation)
{
	if (nOrientation < IW::Orientation::TopLeft || nOrientation > IW::Orientation::LeftBottom)
		return;

	const libfiles::exif_number numbers[] = {
		{libfiles::exif_directory::primary, 0x0112, static_cast<std::uint32_t>(nOrientation)},
		{libfiles::exif_directory::thumbnail, 0x0112, static_cast<std::uint32_t>(nOrientation)},
	};

	libfiles::set_exif_numbers(TiffOf(d, size), numbers);
}

// libexif's defaults, which the property pages always listed with: a tag it does not
// name in its directory is left out, and so is a maker note value it does not know.
MetadataExif::MetadataExif(const IW::MetaData& data) : _listing(std::make_unique<libfiles::exif_listing>())
{
	if (!data.IsEmpty())
		*_listing = libfiles::list_exif({data.GetData(), data.GetDataSize()}, {.ignore_unknown_tags = true});
}

MetadataExif::~MetadataExif() = default;

// The listing's text, converted as the property pages always converted libexif's.
static CString Text(const std::optional<std::string>& text)
{
	return text ? CString(text->c_str()) : CString();
}

static LPCTSTR SectionName(const libfiles::exif_directory directory)
{
	switch (directory)
	{
	case libfiles::exif_directory::primary: return g_szIFD0;
	case libfiles::exif_directory::thumbnail: return g_szIFD1;
	case libfiles::exif_directory::exif: return g_szIFD;
	case libfiles::exif_directory::gps: return g_szIFDGPS;
	case libfiles::exif_directory::interoperability: return g_szIFDInteroperability;
	}

	return g_szEmptyString;
}

void MetadataExif::Load(IW::MetadataProperties& propertiesOut) const
{
	for (const auto& directory : _listing->directories)
	{
		propertiesOut.Begin(SectionName(directory.directory));

		for (const auto& e : directory.entries)
		{
			propertiesOut.Add(Text(e.name), Text(e.title), Text(e.description),
			                  e.bytes.empty() ? CString() : CString(e.value.c_str()));
		}
	}

	if (_listing->maker_note.empty())
		return;

	propertiesOut.Begin(_T("Maker Notes"));

	for (const auto& v : _listing->maker_note)
	{
		if (!v.name || v.name->empty())
			continue;

		propertiesOut.Add(Text(v.name), Text(v.title), Text(v.description), CString(v.value.c_str()));
	}
}

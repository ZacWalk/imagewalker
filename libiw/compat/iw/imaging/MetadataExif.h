// ImageWalker by Zac Walker
//
// Purpose: EXIF through libfiles: the camera settings and orientation a
//          loader reads, the numbers a lossless turn rewrites, and the
//          listing the property page shows.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include <memory>

namespace IW
{
	struct MetadataProperties;
}

namespace libfiles
{
	struct exif_listing;
}

// What a loader and a lossless turn need of an EXIF block. Each takes the APP1
// segment's payload, "Exif\0\0" first, and leaves a block that does not begin
// so, or that holds no TIFF stream, as it is. libfiles reads and writes the
// block, by the rules Diffractor's are.
class Exif
{
public:
	// What the block states of the camera: the orientation by libfiles' one
	// rule, the aperture as APEX (FormatAperture turns it back into an
	// f-number), the exposure time, ISO speed, focal lengths and date taken.
	// A setting the block does not state is left as it was.
	static void Parse(IW::CameraSettings *pSettings, const unsigned char *d, unsigned int size);

	// The picture's orientation, TopLeft when the block does not say.
	static int ReadOrientation(const unsigned char *d, unsigned int size);

	// The pixel dimensions, where the block holds them. Orientation is not
	// this function's: it describes how the pixels are stored, and only code
	// that has reoriented them may touch it.
	static void FixExif(unsigned char *d, unsigned int size, int cx, int cy);

	// The orientation of the picture and its thumbnail, where the block holds
	// them.
	static void SetOrientation(unsigned char *d, unsigned int size, int nOrientation);
};

// The property view's reader. This lists through libfiles rather than the
// parser above, because it wants every tag with its name, title and
// description rather than the handful the loader cares about.
class MetadataExif
{
private:
	std::unique_ptr<libfiles::exif_listing> _listing;

	MetadataExif(const MetadataExif &other);
	void operator=(const MetadataExif &other);

public:
	explicit MetadataExif(const IW::MetaData &data);
	~MetadataExif();

	void Load(IW::MetadataProperties &propertiesOut) const;
};

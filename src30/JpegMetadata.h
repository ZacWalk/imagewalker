// ImageWalker by Zac Walker
// Carries a JPEG's metadata segments into a re-encoded JPEG without re-serialising them.

#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace iw::jpeg
{
	// The marker segments before the first scan, as SOI followed by complete segments. Empty when
	// the file is not a JPEG or its header is malformed or implausibly large.
	std::vector<std::uint8_t> read_header(const std::filesystem::path& path);

	// Returns `encoded` with the descriptive segments of `source` in place of its own: EXIF, XMP,
	// IPTC, comments, vendor segments and an RGB ICC profile. They are copied byte for byte apart
	// from what re-encoding invalidated - the EXIF and XMP orientation becomes 1 because the new
	// pixels are upright, the EXIF pixel dimensions become width x height, and the EXIF and JFIF
	// thumbnails are dropped. Segments describing the old encoding (Adobe colour transform,
	// tables) or pointing into the old file (a multi-picture index) are dropped, and so is an
	// EXIF block too malformed to correct. Empty when either input is not a well-formed JPEG.
	std::vector<std::uint8_t> carry_metadata(std::span<const std::uint8_t> source,
	                                         std::span<const std::uint8_t> encoded, int width, int height);
}

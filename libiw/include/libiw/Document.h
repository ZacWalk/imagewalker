#pragma once

#include <libiw/Codec.h>
#include <libfiles/document.h>

namespace iw::imaging
{
	// Rows of stored pixels as ImageWalker edits them: BGRA and top-down, through libfiles' copy_bgra.
	// Throws std::invalid_argument for rows that do not hold what they describe.
	PixelImage pixels_from_rows(const libfiles::pixel_rows& rows);

	// A decoded image: libfiles' document - its frames and metadata, owned, so a copy stands apart from
	// it - with what ImageWalker's loaders add to it.
	struct DocumentResult : OperationResult
	{
		libfiles::decoded_document document;
		// EXIF's 1 to 8, as the loader read it.
		int orientation{1};
		// The stored size a thumbnail was scaled from, or 0 when the frame is the stored size.
		int originalWidth{};
		int originalHeight{};
		// The loader's account of the file, and the name of the loader that read it.
		std::wstring description;
		std::wstring formatKey;
	};

	// A frame as ImageWalker edits it, with the stored size the loader read.
	PixelImage frame_pixels(const DocumentResult& result, std::size_t index = 0);

	DocumentResult decode_document(std::span<const std::uint8_t> bytes, std::wstring_view extension = {},
	                               const DecodeOptions& options = {});
	DocumentResult decode_document(const std::filesystem::path& path, const DecodeOptions& options = {});
}
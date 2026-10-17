// ImageWalker by Zac Walker
// The app-owned WebP codec. Encoded bytes must be published through Files' temporary output.

#pragma once

#include "Files.h"
#include <span>

namespace iw::webp
{
	// Static WebP only: animation is refused instead of silently saving its first frame.
	// A decode retains originalWidth/Height when maximum bounds request a thumbnail.
	bool probe(std::span<const std::uint8_t> bytes, int& width, int& height);
	bool probe(const std::filesystem::path& path, int& width, int& height);
	files::DecodedImage decode(std::span<const std::uint8_t> bytes, int maximumWidth = 0, int maximumHeight = 0);
	files::DecodedImage load(const std::filesystem::path& path, int maximumWidth = 0, int maximumHeight = 0);

	// Empty on invalid input or codec failure. No filesystem writes occur here, even on success.
	// The caller writes these bytes to its owned temporary sibling and commits explicitly.
	std::vector<std::uint8_t> encode(const files::DecodedImage& image, const files::SaveOptions& options = {});
}

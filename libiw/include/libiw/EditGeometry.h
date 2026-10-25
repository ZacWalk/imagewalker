// ImageWalker by Zac Walker
// Source-normalized perspective geometry and bounded crop manipulation.

#pragma once

#include <libiw/Codec.h>
#include <core/util_geometry.h>

#include <array>
#include <optional>

namespace iw::edits
{
	struct Corner
	{
		double x{};
		double y{};
		bool operator==(const Corner&) const = default;
	};

	// Clockwise in image coordinates: top-left, top-right, bottom-right, bottom-left.
	// Coordinates refer to the outer pixel centres, not the edges of the pixel cells.
	using Quadrilateral = std::array<Corner, 4>;
	inline constexpr Quadrilateral whole_picture{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};

	bool valid_quadrilateral(const Quadrilateral& corners);
	// Refuse more than 128M output pixels or fourfold source-area amplification.
	// Rendering returns an empty image on invalid geometry or allocation failure.
	sizei perspective_size(sizei source, const Quadrilateral& corners);
	imaging::PixelImage correct_perspective(const imaging::PixelImage& source,
		const Quadrilateral& corners);

	// A conservative closed-page detector. An absent result is not a whole-picture fallback.
	// Works with bright or dark pages against a contrasting surrounding background.
	std::optional<Quadrilateral> detect_document(const imaging::PixelImage& source);

}

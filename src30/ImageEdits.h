// ImageWalker by Zac Walker
// Declares the non-destructive photo edit stack and the pixel passes that realise it.

#pragma once

#include "Files.h"
#include "EditGeometry.h"
#include "util_geometry.h"

namespace iw::edits
{
	// Every value is neutral at zero, so a default-constructed stack is the identity.
	struct ImageEdits
	{
		std::optional<Quadrilateral> perspective; // source-normalized, before rotate/straighten/crop
		int rotation{}; // quarter turns clockwise, 0..3
		int straighten{}; // tenths of a degree, -100..100
		recti crop; // in transformed pixels; an empty rect means the whole picture

		int brightness{};
		int contrast{};
		int darks{};
		int midtones{};
		int lights{};
		int saturation{};
		int vibrance{};
		int temperature{};
		int tint{};

		bool has_rotation() const { return (rotation % 4) != 0; }
		bool has_warp() const { return straighten != 0; }
		bool has_crop() const { return crop.width > 0 && crop.height > 0; }
		bool has_geometry() const { return perspective.has_value() || has_rotation() || has_warp() || has_crop(); }
		bool operator==(const ImageEdits&) const = default;

		bool has_color() const
		{
			return brightness || contrast || darks || midtones || lights || saturation || vibrance ||
				temperature || tint;
		}

		bool empty() const { return !has_geometry() && !has_color(); }

		// True when saving over the source cannot be undone from the file itself.
		bool is_irreversible() const { return has_geometry() || has_color(); }

		void reset_geometry()
		{
			perspective.reset();
			rotation = 0;
			straighten = 0;
			crop = {};
		}

		void reset_color()
		{
			brightness = contrast = darks = midtones = lights = 0;
			saturation = vibrance = temperature = tint = 0;
		}
	};

	// Draft snapshots only; saving establishes a new baseline and clears this history.
	class EditHistory
	{
	public:
		void record(const ImageEdits& before, const ImageEdits& after, int group = 0);
		bool undo(ImageEdits& current);
		bool redo(ImageEdits& current);
		bool can_undo() const { return !undo_.empty(); }
		bool can_redo() const { return !redo_.empty(); }
		void clear() { undo_.clear(); redo_.clear(); group_ = 0; }

	private:
		std::vector<ImageEdits> undo_;
		std::vector<ImageEdits> redo_;
		int group_{};
	};

	// The decoded source is retained by the caller; reset after replacing its pixels.
	class PreviewSource
	{
	public:
		bool update(const files::DecodedImage& source, sizei pane);
		void reset() { image_ = {}; pane_ = {}; }
		const files::DecodedImage& image() const { return image_; }

	private:
		files::DecodedImage image_;
		sizei pane_;
	};

	// Crop coordinates belong to the full transformed image, not the scaled preview.
	ImageEdits scaled_edits(const ImageEdits& value, sizei source, sizei preview);

	// The frame a warp keeps: quarter turns swap the sides, and straightening turns the picture
	// inside the frame it was given rather than growing it.
	sizei transformed_size(sizei source, const ImageEdits& value);

	// The largest upright rectangle that still lands entirely on the straightened picture.
	// Straightening empties four corner wedges, and the crop must stay off them.
	recti crop_bounds(sizei source, const ImageEdits& value);

	// The reviewed crop pulled inside crop_bounds, or the bounds themselves when none was set.
	recti effective_crop(sizei source, const ImageEdits& value);

	// Perspective, rotate, straighten, crop, then colour. An empty stack is the identity.
	// `applyCrop` false keeps the whole straightened frame, corners and all, which is what the
	// editing presentation shows while Preview is off.
	files::DecodedImage apply(const files::DecodedImage& source, const ImageEdits& value,
	                          bool applyCrop = true);

	// The colour pass alone, exposed so its neutrality and clamping can be asserted directly.
	void apply_color(std::vector<std::uint32_t>& pixels, const ImageEdits& value);

	// Sets contrast, brightness and temperature from the picture's own histogram: the middle 98%
	// of the luminance range is stretched to fill the scale and a grey-world white balance is
	// applied. False means the picture carries no usable histogram.
	bool auto_color(const files::DecodedImage& source, ImageEdits& value);

	// Recovers a tilt of up to five degrees by finding the angle that packs the picture's strong
	// edge points into the fewest bands. False means too few edges to decide, and nothing is set.
	bool auto_straighten(const files::DecodedImage& source, ImageEdits& value);
}

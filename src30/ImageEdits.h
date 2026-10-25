#pragma once
#include "Files.h"
#include "EditGeometry.h"
#include <libiw/ImageEdits.h>
namespace iw::edits
{
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

}

#include "ImageEdits.h"
namespace iw::edits
{
	void EditHistory::record(const ImageEdits& before, const ImageEdits& after, const int group)
	{
		if (before == after) return;
		if (group == 0 || group != group_ || undo_.empty())
		{
			if (undo_.size() == 100) undo_.erase(undo_.begin());
			undo_.push_back(before);
		}
		redo_.clear();
		group_ = group;
	}

	bool EditHistory::undo(ImageEdits& current)
	{
		if (undo_.empty()) return false;
		redo_.push_back(current);
		current = undo_.back();
		undo_.pop_back();
		group_ = 0;
		return true;
	}

	bool EditHistory::redo(ImageEdits& current)
	{
		if (redo_.empty()) return false;
		undo_.push_back(current);
		current = redo_.back();
		redo_.pop_back();
		group_ = 0;
		return true;
	}

	bool PreviewSource::update(const files::DecodedImage& source, const sizei pane)
	{
		if (!imaging::valid_pixels(source.width, source.height, source.pixels.size()) ||
			pane.width <= 0 || pane.height <= 0)
		{
			reset();
			return false;
		}
		if (!image_.pixels.empty() && pane_ == pane) return false;
		auto image = make_preview(source, pane);
		if (image.pixels.empty()) return false;
		image_ = std::move(image);
		pane_ = pane;
		return true;
	}
}

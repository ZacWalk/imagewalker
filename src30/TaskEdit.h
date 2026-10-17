// ImageWalker by Zac Walker
// Declares the Photo Edit task view.

#pragma once

#include "ImageEdits.h"
#include "TaskView.h"

namespace iw
{
	namespace tests { struct EditAccess; struct PhotoGeometryAccess; }

	// The name Save as proposes: "photo-edit.jpg", then "photo-edit 2.jpg", then the next free
	// number. Never an existing file.
	std::filesystem::path proposed_edit_path(const std::filesystem::path& source);

	class TaskEdit final : public TaskView
	{
	public:
		std::wstring title() const override { return L"Edit"; }
		void set_photo(const std::filesystem::path& path);
		bool request_close() override;
		bool can_undo_draft() const override
		{ return !loading_ && !saving_ && (content_dragging() || history_.can_undo()); }
		bool undo_draft() override
		{
			if (!can_undo_draft()) return false;
			undo(false);
			return true;
		}

	protected:
		bool uses_plan() const override { return false; }
		void build_controls() override;
		std::vector<Column> review_columns() const override { return {}; }
		tasks::TaskRunner::AnalyzeFunction make_analyzer() override;
		tasks::RunOptions build_run_options() override { return {}; }
		std::vector<platform::ToolbarItem> toolbar_items() override;
		void draw_content(const ui::CanvasRenderer& renderer, recti bounds) override;
		bool content_mouse(platform::MouseMessage message, const platform::MouseInput& input) override;
		bool content_key(const platform::KeyInput& input) override;
		bool content_dragging() const override { return cropDrag_ != edits::CropHandle::none || cornerDrag_ >= 0; }
		void controls_changed() override;
		void control_activated(ui::Control& control) override;

	private:
		friend struct tests::EditAccess;
		friend struct tests::PhotoGeometryAccess;
		enum class Leaving { save, discard, cancel };

		void load_photo();
		void rebuild_preview(sizei pane);
		void read_controls();
		void write_controls();
		void persist_options();
		bool save_to(const std::filesystem::path& destination, bool inPlace);
		void report_save_error();
		bool save_as_jpeg();
		void saved_as(const std::filesystem::path& destination);
		bool save();
		bool save_as();
		Leaving ask_about_draft();
		bool step_photo(int direction, bool saveFirst);
		void set_status_for_photo();
		void undo(bool redo);
		void finish_drag(bool cancel = false);
		sizei original_size() const;
		recti displayed_crop() const;
		bool dirty() const { return !edits_.empty(); }

		std::filesystem::path path_;
		size_t index_{};
		files::DecodedImage source_;
		std::optional<files::FileSnapshot> sourceSnapshot_;
		edits::PreviewSource previewSource_;
		std::uint64_t loadGeneration_{};
		std::wstring saveError_;
		bool showResult_{};
		bool loading_{};
		bool saving_{};
		bool closingAfterSave_{};
		bool backupOnOverwrite_{true};
		int jpegQuality_{90};
		int webpQuality_{90};
		bool webpLossless_{};
		edits::ImageEdits edits_;
		edits::EditHistory history_;
		int controlGroup_{};
		bool perspectiveHandles_{};
		edits::CropHandle cropDrag_{edits::CropHandle::none};
		int cornerDrag_{-1};
		pointi dragStart_;
		recti dragCrop_;
		edits::ImageEdits beforeDrag_;
		recti imageBounds_;
		recti photoBounds_;
		recti stripBounds_;
	};
}

// ImageWalker by Zac Walker
// Declares the Photo Edit task view.

#pragma once

#include "ImageEdits.h"
#include "TaskView.h"

namespace iw
{
	namespace tests
	{
		struct EditAccess;
		struct PhotoGeometryAccess;
	}

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
		{
			return !loading_ && !saving_ && (content_dragging() || history_.can_undo());
		}
		bool undo_draft() override
		{
			if (!can_undo_draft()) return false;
			undo(false);
			return true;
		}

	protected:
		bool uses_plan() const override { return false; }
		int tool_panel_width() const override { return 354; }
		void build_controls() override;
		std::vector<Column> review_columns() const override { return {}; }
		tasks::TaskRunner::AnalyzeFunction make_analyzer() override;
		tasks::RunOptions build_run_options() override { return {}; }
		std::vector<platform::ToolbarItem> toolbar_items() override;
		void draw_content(const ui::CanvasRenderer& renderer, app::recti bounds) override;
		bool content_mouse(platform::MouseMessage message, const platform::MouseInput& input) override;
		bool content_key(const platform::KeyInput& input) override;
		bool content_dragging() const override
		{
			return draggingCompare_ || cropDrag_ != edits::CropHandle::none || cornerDrag_ >= 0;
		}
		void controls_changed() override;
		void control_activated(ui::Control& control) override;
		void layout_hosted_controls() override;
		void release_hosted_controls() override;

	private:
		friend struct tests::EditAccess;
		friend struct tests::PhotoGeometryAccess;
		enum class Leaving
		{
			save,
			discard,
			cancel
		};

		void load_photo();
		void rebuild_preview(app::sizei pane);
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
		void open_adjustment(ui::Control& control);
		void dismiss_adjustment(bool accept);
		void draw_histogram(const ui::CanvasRenderer& renderer, app::recti bounds) const;
		bool tone_mouse(platform::MouseMessage message, app::recti bounds, app::pointi point);
		void update_histogram(const files::DecodedImage& image);
		app::sizei original_size() const;
		app::recti displayed_crop() const;
		bool dirty() const { return !edits_.empty(); }

		std::filesystem::path path_;
		size_t index_{};
		files::DecodedImage source_;
		std::optional<files::FileSnapshot> sourceSnapshot_;
		std::optional<std::array<unsigned char, 32>> sourceDigest_;
		edits::PreviewSource previewSource_;
		files::DecodedImage renderedPreview_;
		edits::ImageEdits renderedEdits_;
		app::sizei renderedSourceSize_;
		bool renderedCropped_{};
		std::uint64_t loadGeneration_{};
		std::wstring saveError_;
		bool showResult_{};
		bool comparing_{};
		bool draggingCompare_{};
		bool holdingCompare_{};
		double comparisonPosition_{0.5};
		double beforeComparisonDrag_{0.5};
		bool loading_{};
		bool saving_{};
		bool closingAfterSave_{};
		int jpegQuality_{90};
		int webpQuality_{90};
		bool webpLossless_{};
		edits::ImageEdits edits_;
		edits::EditHistory history_;
		int controlGroup_{};
		int adjustmentId_{};
		int toneHandle_{-1};
		edits::ImageEdits beforeTone_;
		std::array<std::array<unsigned, 64>, 3> histogram_{};
		platform::TextInputPtr adjustmentInput_;
		bool perspectiveHandles_{};
		edits::CropHandle cropDrag_{edits::CropHandle::none};
		int cornerDrag_{-1};
		app::pointi dragStart_;
		app::recti dragCrop_;
		edits::ImageEdits beforeDrag_;
		app::recti imageBounds_;
		app::recti photoBounds_;
		app::recti compareHandleBounds_;
		app::recti stripBounds_;
	};
}

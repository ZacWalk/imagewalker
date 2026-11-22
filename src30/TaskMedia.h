// ImageWalker by Zac Walker
// Audio/video transport and its optional packet-copy editor.

#pragma once

#include "PlatformMedia.h"
#include "MediaEdit.h"
#include "TaskView.h"

namespace iw
{
	class TaskMedia final : public TaskView
	{
	public:
		~TaskMedia() override;
		void set_media(std::filesystem::path path, bool edit = false);
		std::wstring title() const override { return editing_ ? L"Edit media" : L"Play media"; }
		bool request_close() override;
		bool closing() const override { return closing_ || closeAfterSave_; }
		std::vector<platform::ToolbarItem> toolbar_items() override;

	protected:
		bool uses_plan() const override { return false; }
		void build_controls() override;
		std::vector<Column> review_columns() const override { return {}; }
		tasks::TaskRunner::AnalyzeFunction make_analyzer() override { return {}; }
		tasks::RunOptions build_run_options() override { return {}; }
		void draw_content(const ui::CanvasRenderer& renderer, app::recti bounds) override;
		void layout_hosted_controls() override;
		void release_hosted_controls() override { player_.reset(); }
		int tool_panel_width() const override { return editing_ ? 340 : 300; }
		void control_activated(ui::Control& control) override;
		bool content_key(const platform::KeyInput& input) override;
		bool content_key_first(const platform::KeyInput& input) const override
		{
			return input.key == platform::KeyCode::space && !input.control && !input.alt && !input.shift;
		}
		bool content_mouse(platform::MouseMessage message, const platform::MouseInput& input) override;
		bool content_dragging() const override { return drag_ != Drag::none; }
		bool can_undo_draft() const override { return editing_ && !saving_ && (content_dragging() || !undo_.empty()); }
		bool undo_draft() override;

	private:
		enum class Drag
		{
			none,
			playhead,
			first,
			last
		};

		void start_playback();
		void update_playback(const media::PlaybackStatus& status);
		void inspect_media();
		void update_edit();
		void set_cut(bool first, std::int64_t where);
		void change_draft(media::edit::Draft draft);
		void push_undo(const media::edit::Draft& previous);
		void draw_timeline(const ui::CanvasRenderer& renderer, app::recti bounds);
		void finish_drag(bool cancel);
		void toggle_play();
		void apply_volume();
		bool save_edit(bool saveAs, bool closeAfter = false);
		bool start_save(const std::filesystem::path& destination, bool inPlace, bool closeAfter);
		bool can_copy() const;
		void set_edit_status();
		void seek(std::int64_t where);
		std::int64_t timeline_position() const;
		app::recti video_bounds(app::recti bounds) const;
		std::filesystem::path path_;
		std::unique_ptr<media::Player> player_;
		media::PlaybackStatus playback_;
		std::stop_source metadataStop_;
		std::stop_source saveStop_;
		std::optional<files::FileSnapshot> source_;
		std::optional<std::array<unsigned char, 32>> digest_;
		media::edit::Info editInfo_;
		media::edit::Draft draft_, beforeDrag_;
		std::vector<media::edit::Draft> undo_;
		app::recti timeline_, scrubber_;
		Drag drag_{};
		std::int64_t position_{}, dragPosition_{};
		std::wstring displayedStatus_;
		int unmutedVolume_{100};
		bool editing_{};
		bool inspecting_{};
		bool saving_{};
		bool closeAfterSave_{};
		bool resumeAfterDrag_{};
		bool initialized_{};
		bool closing_{};
	};
}

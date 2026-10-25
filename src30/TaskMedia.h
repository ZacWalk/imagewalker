// ImageWalker by Zac Walker
// Read-only audio/video playback task.

#pragma once

#include "PlatformMedia.h"
#include "TaskView.h"

namespace iw
{
	class TaskMedia final : public TaskView
	{
	public:
		~TaskMedia() override;
		void set_media(std::filesystem::path path);
		std::wstring title() const override { return L"Play media"; }
		bool request_close() override;
		bool closing() const override { return closing_; }
		std::vector<platform::ToolbarItem> toolbar_items() override;

	protected:
		bool uses_plan() const override { return false; }
		void build_controls() override;
		std::vector<Column> review_columns() const override { return {}; }
		tasks::TaskRunner::AnalyzeFunction make_analyzer() override { return {}; }
		tasks::RunOptions build_run_options() override { return {}; }
		void draw_content(const ui::CanvasRenderer& renderer, recti bounds) override;
		void layout_hosted_controls() override;
		void control_activated(ui::Control& control) override;
		bool content_key(const platform::KeyInput& input) override;

	private:
		void start_playback();
		void update_playback(const media::PlaybackStatus& status);
		std::filesystem::path path_;
		std::unique_ptr<media::Player> player_;
		media::PlaybackStatus playback_;
		std::stop_source metadataStop_;
		std::wstring displayedStatus_;
		bool initialized_{};
		bool closing_{};
	};
}

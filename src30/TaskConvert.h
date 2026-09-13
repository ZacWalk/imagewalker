// ImageWalker by Zac Walker
// Declares the Convert or Resize task view.

#pragma once

#include "TaskView.h"

namespace iw
{
	namespace convert
	{
		struct Settings
		{
			std::filesystem::path destination;
			files::ImageSaveFormat format{files::ImageSaveFormat::jpeg};
			int quality{85};
			bool lossless{};
			bool limitEnabled{};
			int maximumDimension{1920};
			tasks::CollisionPolicy policy{tasks::CollisionPolicy::block};
		};

		std::wstring settings_problem(const Settings& settings);
		tasks::TaskPlan analyze(const std::vector<std::filesystem::path>& sources, const Settings& settings,
		                       const std::stop_token& stop = {}, const tasks::ProgressFunction& progress = {});
		std::wstring describe_changes(const tasks::TaskRow& row, const Settings& settings);
		tasks::RunOptions run_options(const Settings& settings,
		                             const std::vector<std::filesystem::path>& originals);
	}

	// Preserves aspect ratio and only ever scales down: a photo already inside the limit is
	// returned unchanged. A limit of zero means no limit.
	sizei limited_output_size(int width, int height, int limit);

	class TaskConvert final : public TaskView
	{
	public:
		std::wstring title() const override { return L"Convert or Resize"; }
		void set_initial_destination(std::filesystem::path folder);

	protected:
		std::wstring run_label() const override { return L"Convert"; }
		void build_controls() override;
		std::vector<Column> review_columns() const override;
		tasks::TaskRunner::AnalyzeFunction make_analyzer() override;
		tasks::RunOptions build_run_options() override;
		std::wstring review_cell(const tasks::TaskRow& row, size_t column) const override;
		std::wstring run_block_reason() const override;
		void control_activated(ui::Control& control) override;
		void layout_hosted_controls() override;
		void work_state_changed(bool busy) override;

	private:
		convert::Settings read_settings() const;
		// Quality and the dimension limit only redraw the Changes column; format, destination and
		// policy need a fresh filesystem analysis.
		void refresh_changes();

		std::filesystem::path destination_;
		std::vector<files::ImageSaveFormat> formats_;
		platform::TextInputPtr limitInput_;
	};
}

// ImageWalker by Zac Walker
// Declares the Batch Rename task view and the in-place staging its run depends on.

#pragma once

#include "TaskView.h"

#include <memory>

namespace iw::rename
{
	tasks::TaskPlan analyze(const std::vector<std::filesystem::path>& sources, std::wstring_view templateText,
	                       int start, tasks::CollisionPolicy policy, const std::stop_token& stop = {},
	                       const tasks::ProgressFunction& progress = {});
	tasks::RunOptions run_options(const tasks::TaskPlan& plan);
	tasks::TaskPlan direct_plan(const std::filesystem::path& source, std::wstring_view filename);

	// "photo.jpg.xmp" keeps the whole name; "photo.xmp" keeps only the stem.
	std::filesystem::path sidecar_destination(const std::filesystem::path& sidecar,
	                                          const std::filesystem::path& source,
	                                          const std::filesystem::path& destination);

	// Shared by every row of one run. Recovery never overwrites a completed row or an unrelated file.
	class Staging
	{
	public:
		Staging() = default;
		~Staging();
		Staging(const Staging&) = delete;
		Staging& operator=(const Staging&) = delete;

		// Where the item is now, which is a temporary path if it was moved out of the way.
		std::filesystem::path actual(const std::filesystem::path& source) const;
		void register_bundle(const std::filesystem::path& primary, const std::vector<std::filesystem::path>& sidecars);
		bool stage(const std::filesystem::path& path,
			std::vector<std::filesystem::path>* mutatedPaths = nullptr);
		void settled(const std::filesystem::path& temporary);
		struct Recovery
		{
			std::filesystem::path original;
			std::filesystem::path recovered;
			bool failed{};
		};
		std::vector<Recovery> restore() noexcept;
		size_t pending() const { return staged_.size(); }

	private:
		struct Entry
		{
			std::filesystem::path temporary;
			std::filesystem::path original;
		};

		std::vector<Entry> staged_;
		std::vector<std::vector<std::filesystem::path>> bundles_;
		bool restorationAttempted_{};
		bool stage_one(const std::filesystem::path& path,
			std::vector<std::filesystem::path>* mutatedPaths);
	};

	// Performs one reviewed row, moving its sidecars with it. A destination held by another row of
	// the same run is staged aside rather than refused; anything else that appeared since Review is
	// skipped. plannedSource includes companions as well as primaries. Failures are reported on
	// the row or thrown; run_options also registers complete bundles for cycle recovery.
	// On success, mutatedPaths contains original (not staging) names actually changed by this
	// row, including companions displaced while staging a different participant.
	void rename_row(tasks::TaskRow& row, Staging& staging,
	                const std::function<bool(const std::filesystem::path&)>& plannedSource,
	                std::vector<std::filesystem::path>* mutatedPaths = nullptr);
}

namespace iw
{
	class TaskRename final : public TaskView
	{
	public:
		std::wstring title() const override { return L"Batch Rename"; }

	protected:
		std::wstring run_label() const override { return L"Rename files"; }
		void build_controls() override;
		std::vector<Column> review_columns() const override;
		tasks::TaskRunner::AnalyzeFunction make_analyzer() override;
		tasks::RunOptions build_run_options() override;
		std::wstring review_cell(const tasks::TaskRow& row, size_t column) const override;
		void control_activated(ui::Control& control) override;
		void layout_hosted_controls() override;
		void work_state_changed(bool busy) override;

	private:
		std::wstring template_text() const;
		void set_template(std::wstring text);

		platform::TextInputPtr templateInput_;
		platform::TextInputPtr startInput_;
		bool templateNeedsFocus_{true};
	};
}

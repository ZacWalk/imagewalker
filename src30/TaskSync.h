// ImageWalker by Zac Walker
// Declares the Synchronize task view.

#pragma once

#include "TaskView.h"

#include <map>

namespace iw::sync
{
	enum Action { ignore, copyToRemote, copyToLocal, deleteLocal, deleteRemote };

	struct Options
	{
		bool wholeCollection{true};
		std::filesystem::path local;
		std::filesystem::path remote;
		std::vector<std::filesystem::path> collection;
		bool toRemote{true};
		bool toLocal{};
		bool deleteLocal{};
		bool deleteRemote{};
	};

	using FileState = files::FileSnapshot;

	struct RowSnapshot
	{
		tasks::TaskRow row;
		FileState local;
		FileState remote;
		std::filesystem::path localRoot;
		std::filesystem::path remoteRoot;
		files::FileSnapshot localRootState;
		files::FileSnapshot remoteRootState;
		std::filesystem::path localParent;
		std::filesystem::path remoteParent;
		files::FileSnapshot localParentState;
		files::FileSnapshot remoteParentState;
	};

	struct Review
	{
		tasks::TaskPlan plan;
		std::map<std::wstring, RowSnapshot, tasks::detail::PathLess> snapshots;
	};

	Options load_options(std::wstring_view section = L"Synchronize");
	void save_options(const Options& options, std::wstring_view section = L"Synchronize");
	std::wstring configuration_error(const Options& options);
	Review analyze(const Options& options, const std::stop_token& stop = {},
	               const tasks::ProgressFunction& progress = {});
	tasks::RunOptions run_options(std::shared_ptr<const Review> review);
	std::wstring summarize(const tasks::TaskPlan& plan);
	platform::ChoiceDefinition deletion_confirmation(const tasks::TaskPlan& plan);
	bool confirm_deletions(const tasks::TaskPlan& plan,
	                      const std::function<int(const platform::ChoiceDefinition&)>& confirm);
	std::vector<std::filesystem::path> affected_folders(const tasks::TaskPlan& plan);
}

namespace iw
{
	class TaskSync final : public TaskView
	{
	public:
		explicit TaskSync(std::wstring settingsSection = L"Synchronize");
		std::wstring title() const override { return L"Synchronize"; }
		void set_local_folder(std::filesystem::path folder);
		void set_collection_folders(std::vector<std::filesystem::path> folders);
		void add_collection_folder(std::filesystem::path folder);
		void remove_collection_folder(size_t index);
		void set_options(sync::Options options);
		const sync::Options& options() const { return options_; }
		void start_run() override;

	protected:
		std::wstring run_label() const override { return L"Synchronize"; }
		std::wstring refresh_label() const override { return L"Analyze"; }
		// Scanning both trees may be substantial work, so it is never implicit.
		bool auto_analyze() const override { return false; }
		void build_controls() override;
		std::vector<Column> review_columns() const override;
		tasks::TaskRunner::AnalyzeFunction make_analyzer() override;
		tasks::RunOptions build_run_options() override;
		std::wstring review_cell(const tasks::TaskRow& row, size_t column) const override;
		void control_activated(ui::Control& control) override;
		void controls_changed() override;
		std::wstring analysis_block_reason() const override { return sync::configuration_error(options_); }
		std::wstring analysis_summary() const override { return sync::summarize(plan_); }

	private:
		sync::Options options_;
		std::wstring settingsSection_;
		std::shared_ptr<sync::Review> review_;
		bool confirming_{};
		size_t collectionSelection_{};
	};
}

// ImageWalker by Zac Walker
// Declares the cancellable analyse-and-run engine shared by every task view.

#pragma once

#include "TaskModel.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>

namespace iw::undo { class History; }

namespace iw::tasks
{
	using ProgressFunction = std::function<void(size_t complete, size_t total)>;
	using RowProgressFunction = std::function<void(size_t index, const TaskRow& row)>;

	struct RunOptions
	{
		// Runs on the worker immediately before a row acts. False refuses that row, so a row whose
		// disk state changed since Review is never written.
		std::function<bool(TaskRow&)> revalidate;

		// Performs one row. Throwing fails that row and nothing else. The row may set its own
		// state, which is then left alone.
		std::function<void(TaskRow&)> act;

		ProgressFunction progress;
		RowProgressFunction rowProgress;
		// Completes worker-owned staging before results are published, including on cancellation.
		// Recovery failures should classify their affected rows here.
		std::function<void(TaskPlan&)> finish;

		// Mutable paths include absent destinations. Rename protects the whole plan before
		// staging so a cycle cannot destroy an original before its own row is reached.
		std::wstring undoLabel;
		std::function<std::vector<std::filesystem::path>(const TaskRow&)> undoPaths;
		// Optional exact mutations of the just-completed row. Omission means every undoPath
		// was changed; staged operations supply this to avoid accepting edits to older outputs.
		std::function<std::vector<std::filesystem::path>(const TaskRow&)> undoMutatedPaths;
		bool undoAllBeforeRun{};
		// Null uses explicitly initialized application history (or the test's ScopedHistory).
		// It never implicitly opens the user's live journal.
		undo::History* undoHistory{};
	};

	// Acts on every ready row in order. Rows not reached before a stop are left notRun, so a
	// cancelled run can never be mistaken for a complete one.
	void run_plan(TaskPlan& plan, const std::stop_token& stop, const RunOptions& options);

	class TaskRunner
	{
	public:
		enum class Phase { idle, analyzing, running };

		// Both members default to the platform queues. Tests replace them to control ordering.
		struct Dispatcher
		{
			std::function<bool(std::function<void()>)> background;
			std::function<bool(std::function<void()>)> ui;
		};

		using AnalyzeFunction = std::function<TaskPlan(const std::stop_token&, const ProgressFunction&)>;

		TaskRunner();
		explicit TaskRunner(Dispatcher dispatcher);
		~TaskRunner();
		TaskRunner(const TaskRunner&) = delete;
		TaskRunner& operator=(const TaskRunner&) = delete;

		std::function<void(Phase, size_t complete, size_t total)> progress;
		RowProgressFunction rowProgress;
		std::function<void(TaskPlan)> analyzed;
		std::function<void(TaskPlan)> finished;

		// Discards whatever is in flight and returns the generation later work must carry.
		std::uint64_t invalidate();

		bool analyze(AnalyzeFunction work);
		bool run(TaskPlan plan, RunOptions options);

		// Stops future work without discarding the result, so the partial outcome is still reported.
		void cancel();

		Phase phase() const { return phase_; }
		bool busy() const { return phase_ != Phase::idle; }
		std::uint64_t generation() const { return generation_.load(); }

	private:
		struct Job
		{
			std::stop_source stop;
			std::uint64_t generation{};
			Phase phase{Phase::idle};
			// Held by the job, not read through the runner, so a worker never dereferences it.
			std::function<bool(std::function<void()>)> postUi;
		};

		// Lets a completion posted from a worker discover that its runner has gone.
		using Guard = std::shared_ptr<TaskRunner*>;

		std::shared_ptr<Job> begin(Phase phase, std::uint64_t generation);
		bool post_background(std::function<void()> work) const;
		bool accepts(const std::shared_ptr<Job>& job) const;
		void complete(const std::shared_ptr<Job>& job);

		Dispatcher dispatcher_;
		Guard alive_;
		std::shared_ptr<Job> job_;
		std::atomic<std::uint64_t> generation_{1};
		Phase phase_{Phase::idle};
	};
}

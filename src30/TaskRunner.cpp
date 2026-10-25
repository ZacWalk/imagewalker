// ImageWalker by Zac Walker
// Implements per-row isolation, cancellation, progress, and the generation guard for task work.

#include "TaskRunner.h"

#include "Platform.h"
#include "Undo.h"

#include <exception>
#include <utility>

namespace iw::tasks
{
	namespace
	{
		// Exception text is diagnostic, so a byte-wise widening is enough and costs no dependency.
		std::wstring widen(const char* text)
		{
			std::wstring result;
			for (; text && *text; ++text) result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*text)));
			return result;
		}

		template<typename Callback, typename... Args>
		void report_safely(const Callback& callback, Args&&... args)
		{
			// Reporting is observational: a failed progress sink must not abandon the remaining rows.
			try { if (callback) callback(std::forward<Args>(args)...); }
			catch (...) {}
		}
	}

	void run_plan(TaskPlan& plan, const std::stop_token& stop, const RunOptions& options)
	{
		std::shared_ptr<undo::Batch> undoBatch;
		bool undoProtected{};
		std::wstring protectionFailure;
		size_t total = 0;
		for (const auto& row : plan.rows) if (row.state == RowState::ready) ++total;
		report_safely(options.progress, size_t{0}, total);

		size_t complete = 0;
		for (size_t index = 0; index < plan.rows.size(); ++index)
		{
			auto& row = plan.rows[index];
			if (row.state == RowState::pending || row.state == RowState::running) row.state = RowState::notRun;
			if (row.state == RowState::blocked)
			{
				row.state = RowState::failed;
				if (row.detail.empty()) row.detail = row.change.empty() ? L"The item was blocked in Review." : row.change;
			}
			if (row.state != RowState::ready) continue;
			if (stop.stop_requested())
			{
				row.state = RowState::notRun;
				report_safely(options.rowProgress, index, row);
				continue;
			}

			row.state = RowState::running;
			report_safely(options.rowProgress, index, row);
			// The handler is inside the loop: one malformed item fails one row, not the run.
			try
			{
				if (options.revalidate && !options.revalidate(row))
				{
					row.state = RowState::failed;
					if (row.detail.empty()) row.detail = L"The file changed after it was reviewed.";
				}
				else
				{
					if (!options.act)
					{
						row.state = RowState::failed;
						row.detail = L"No operation was supplied for this item.";
					}
					else
					{
						bool protectedRow = true;
						if (options.undoPaths)
						{
							if (!undoBatch)
								undoBatch = (options.undoHistory ? *options.undoHistory : undo::history()).begin(options.undoLabel);
							std::vector<std::filesystem::path> protectedPaths;
							if (options.undoAllBeforeRun && !undoProtected)
							{
								for (const auto& item : plan.rows)
								{
									if (item.state != RowState::ready && item.state != RowState::running) continue;
									const auto paths = options.undoPaths(item);
									protectedPaths.insert(protectedPaths.end(), paths.begin(), paths.end());
								}
							}
							else if (!options.undoAllBeforeRun) protectedPaths = options.undoPaths(row);
							protectedRow = protectionFailure.empty() &&
								undoBatch->protect(protectedPaths, row.detail);
							if (options.undoAllBeforeRun)
							{
								undoProtected = true;
								if (!protectedRow && protectionFailure.empty()) protectionFailure = row.detail;
								if (!protectionFailure.empty()) row.detail = protectionFailure;
							}
						}
						if (!protectedRow) row.state = RowState::failed;
						else options.act(row);
						if (row.state == RowState::running) row.state = RowState::success;
						else if (row.state != RowState::success && row.state != RowState::failed &&
							row.state != RowState::skipped && row.state != RowState::notRun)
						{
							row.state = RowState::failed;
							if (row.detail.empty()) row.detail = L"The operation did not report a completed outcome.";
						}
						if (row.state == RowState::success && undoBatch)
						{
							std::wstring error;
							const auto bundle = options.undoPaths(row);
							const auto mutated = options.undoMutatedPaths ? options.undoMutatedPaths(row) : bundle;
							if (!undoBatch->completed(bundle, mutated, error))
							{
								row.detail = error;
								plan.completionError = error;
							}
						}
					}
				}
			}
			catch (const std::exception& error)
			{
				row.state = RowState::failed;
				row.detail = widen(error.what());
			}
			catch (...)
			{
				row.state = RowState::failed;
				row.detail = L"The item failed for an unknown reason.";
			}

			++complete;
			report_safely(options.rowProgress, index, row);
			report_safely(options.progress, complete, total);
		}
		try
		{
			if (options.finish) options.finish(plan);
		}
		catch (const std::exception& error)
		{
			plan.completionError = L"Finishing the operation failed: " + widen(error.what());
		}
		catch (...)
		{
			plan.completionError = L"Finishing the operation failed for an unknown reason.";
		}
		if (undoBatch)
		{
			std::wstring error;
			if (!undoBatch->finish(error))
			{
				if (!plan.completionError.empty()) plan.completionError += L" ";
				plan.completionError += error;
			}
		}
	}

	TaskRunner::TaskRunner()
		: TaskRunner(Dispatcher{})
	{
	}

	TaskRunner::TaskRunner(Dispatcher dispatcher)
		: dispatcher_(std::move(dispatcher)), alive_(std::make_shared<TaskRunner*>(this))
	{
	}

	TaskRunner::~TaskRunner()
	{
		if (job_) job_->stop.request_stop();
		*alive_ = nullptr;
	}

	bool TaskRunner::post_background(std::function<void()> work) const
	{
		try
		{
			if (dispatcher_.background) return dispatcher_.background(std::move(work));
			return platform::queue_work(platform::WorkQueue::task, std::move(work));
		}
		catch (...) { return false; }
	}

	std::shared_ptr<TaskRunner::Job> TaskRunner::begin(const Phase phase, const std::uint64_t generation)
	{
		auto job = std::make_shared<Job>();
		job->generation = generation;
		job->phase = phase;
		job->postUi = dispatcher_.ui
			              ? dispatcher_.ui
			              : std::function<bool(std::function<void()>)>(
				              [](std::function<void()> work) { return platform::queue_ui(std::move(work)); });
		job_ = job;
		phase_ = phase;
		return job;
	}

	bool TaskRunner::accepts(const std::shared_ptr<Job>& job) const
	{
		return job && job == job_ && job->generation == generation_.load();
	}

	void TaskRunner::complete(const std::shared_ptr<Job>& job)
	{
		if (!accepts(job)) return;
		job_.reset();
		phase_ = Phase::idle;
	}

	std::uint64_t TaskRunner::invalidate()
	{
		// The abandoned job stays owned until it completes; the generation is what drops its result.
		if (job_) job_->stop.request_stop();
		job_.reset();
		phase_ = Phase::idle;
		return generation_.fetch_add(1) + 1;
	}

	bool TaskRunner::analyze(AnalyzeFunction work)
	{
		if (!work || busy()) return false;
		invalidate();

		const auto job = begin(Phase::analyzing, generation_.load());
		const std::weak_ptr guard = alive_;
		const auto stop = job->stop.get_token();
		const auto report = [guard, job](const size_t complete, const size_t total)
		{
			job->postUi([guard, job, complete, total]
			{
				const auto target = guard.lock();
				if (!target || !*target) return;
				TaskRunner& runner = **target;
				if (!runner.accepts(job) || !runner.progress) return;
				const auto callback = runner.progress;
				report_safely(callback, job->phase, complete, total);
			});
		};

		const bool queued = post_background([job, stop, work = std::move(work), report, guard]
		{
			TaskPlan plan;
			try
			{
				if (!stop.stop_requested()) plan = work(stop, report);
			}
			catch (const std::exception& error) { plan.blockReason = L"Analysis failed: " + widen(error.what()); }
			catch (...) { plan.blockReason = L"Analysis failed."; }
			plan.generation = job->generation;
			job->postUi([guard, job, plan = std::move(plan)]() mutable
			{
				const auto target = guard.lock();
				if (!target || !*target) return;
				TaskRunner& runner = **target;
				if (!runner.accepts(job)) return;
				if (job->stop.stop_requested()) plan.blockReason = L"Analysis cancelled. Analyze again before running.";
				else if (plan.blockReason.empty())
				{
					if (plan.count(RowState::blocked)) plan.blockReason = L"Resolve the blocked items before running.";
					else if (!plan.count(RowState::ready)) plan.blockReason = L"Nothing to do.";
				}
				auto callback = runner.analyzed;
				runner.complete(job);
				if (callback) callback(std::move(plan));
			});
		});

		if (!queued) complete(job);
		return queued;
	}

	bool TaskRunner::run(TaskPlan plan, RunOptions options)
	{
		if (busy() || !plan.can_run() || plan.count(RowState::ready) == 0) return false;
		const auto generation = plan.generation;
		if (generation != generation_.load()) return false;
		if (job_) job_->stop.request_stop();

		const auto job = begin(Phase::running, generation);
		const std::weak_ptr guard = alive_;
		const auto stop = job->stop.get_token();
		options.progress = [guard, job, observer = std::move(options.progress)](const size_t complete, const size_t total)
		{
			report_safely(observer, complete, total);
			job->postUi([guard, job, complete, total]
			{
				const auto target = guard.lock();
				if (!target || !*target) return;
				TaskRunner& runner = **target;
				if (!runner.accepts(job) || !runner.progress) return;
				const auto callback = runner.progress;
				report_safely(callback, job->phase, complete, total);
			});
		};
		options.rowProgress = [guard, job, observer = std::move(options.rowProgress)](const size_t index, const TaskRow& row)
		{
			report_safely(observer, index, row);
			job->postUi([guard, job, index, row]
			{
				const auto target = guard.lock();
				if (!target || !*target) return;
				TaskRunner& runner = **target;
				if (!runner.accepts(job)) return;
				const auto callback = runner.rowProgress;
				report_safely(callback, index, row);
			});
		};

		const bool queued = post_background(
			[job, stop, guard, plan = std::move(plan), options = std::move(options)]() mutable
			{
				run_plan(plan, stop, options);
				job->postUi([guard, job, plan = std::move(plan)]() mutable
				{
					const auto target = guard.lock();
					if (!target || !*target) return;
					TaskRunner& runner = **target;
					if (!runner.accepts(job)) return;
					auto callback = runner.finished;
					runner.complete(job);
					if (callback) callback(std::move(plan));
				});
			});

		if (!queued) complete(job);
		return queued;
	}

	void TaskRunner::cancel()
	{
		// Cancel is partial completion, not rollback: the generation stands so the result arrives.
		if (job_) job_->stop.request_stop();
	}
}

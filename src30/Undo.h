// ImageWalker by Zac Walker
// Session undo with durable, never automatically evicted recovery copies.

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace iw::undo
{
	struct Report
	{
		size_t restored{};
		size_t conflicts{};
		size_t failed{};
		std::vector<std::wstring> details;
		std::vector<std::filesystem::path> folders;
		std::wstring summary() const;
	};

	class Batch;

	class History
	{
	public:
		// The root is application-owned, not a temporary folder. Existing history is never removed.
		explicit History(std::filesystem::path root);
		~History();
		History(const History&) = delete;
		History& operator=(const History&) = delete;

		std::shared_ptr<Batch> begin(std::wstring description);
		bool can_undo() const;
		std::wstring description() const;
		Report undo();
		const std::filesystem::path& root() const;

	private:
		struct State;
		std::shared_ptr<State> state_;
		friend class Batch;
	};

	class Batch
	{
	public:
		~Batch();
		Batch(const Batch&) = delete;
		Batch& operator=(const Batch&) = delete;

		// Protect EVERY mutable path before the first mutation of an overlapping operation.
		// Missing paths are recorded too. A false result forbids the operation.
		bool protect(const std::vector<std::filesystem::path>& paths, std::wstring& error);
		// Records only an empty-directory creation/removal, never ownership of a merged tree.
		// Complete directory nodes separately from file bundles. Undo removes only empty nodes.
		bool protect_directory(const std::filesystem::path& path, std::wstring& error);
		bool remove_directory(const std::filesystem::path& path, std::wstring& error);
		bool validate_before(const std::vector<std::filesystem::path>& paths, std::wstring& error) const;
		bool validate_completed(const std::vector<std::filesystem::path>& paths, std::wstring& error) const;
		// Records a successful bundle whose paths were ALL mutated by this step.
		bool completed(const std::vector<std::filesystem::path>& paths, std::wstring& error);
		// Staged/overlapping work must distinguish logical bundle membership from the paths
		// actually changed by THIS step. Untouched paths retain their last recorded state.
		// Mutated paths are also included in the bundle and must have been protected.
		bool completed(const std::vector<std::filesystem::path>& bundlePaths,
			const std::vector<std::filesystem::path>& mutatedPaths, std::wstring& error);
		// Called after cancellation/rollback/staging recovery. Failed/unreached bundles are omitted.
		// A false result leaves recovery copies intact and gives an actionable error.
		bool finish(std::wstring& error);
		std::filesystem::path recovery_folder() const;

	private:
		struct State;
		std::unique_ptr<State> state_;
		bool protect_impl(const std::vector<std::filesystem::path>& paths, bool directoryOnly, std::wstring& error);
		bool validate(const std::vector<std::filesystem::path>& paths, bool completed, std::wstring& error) const;
		explicit Batch(std::shared_ptr<History::State> history, std::wstring description);
		friend class History;
	};

	struct TransferResult
	{
		std::error_code error;
		std::wstring detail;
		bool destinationCompleted{};
		bool sourceRemoved{};
	};

	// Uses the existing transactional writers. Cross-volume source deletion is a separate,
	// guarded step, so a committed destination remains undoable if deleting its source fails.
	// An optional source-deletion guard requests the same two-step path on any volume.
	TransferResult transfer_file(Batch& batch, const std::filesystem::path& source,
		const std::filesystem::path& destination, bool move, bool overwrite,
		const std::function<bool()>& beforeCommit = {},
		const std::function<bool()>& beforeSourceDelete = {});

	// Automatic Undo is deliberately current-session only. After restart/crash the flushed
	// manifests and originals remain available for manual recovery; they are never replayed.
	// Call once on interactive startup, AFTER handling /test. Merely calling history() never
	// constructs a live journal; without explicit initialization it refuses the operation.
	void initialize_live_history();
	History& history();

	// Startup/test dependency injection, not a runtime setting. The supplied History must
	// outlive this scope, and all operations using it must finish before the scope is destroyed.
	// Tests own and clean their unique fixture root after this scope; production never evicts.
	class ScopedHistory
	{
	public:
		explicit ScopedHistory(History& value) noexcept;
		~ScopedHistory();
		ScopedHistory(const ScopedHistory&) = delete;
		ScopedHistory& operator=(const ScopedHistory&) = delete;

	private:
		History* previous_{};
		History* installed_{};
	};
}

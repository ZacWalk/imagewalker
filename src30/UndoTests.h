// ImageWalker by Zac Walker
// Real filesystem fixtures for guarded undo, cancellation, cycles and retained recovery.

#pragma once

#include "Undo.h"
#include "Files.h"
#include "TaskRename.h"
#include "TaskRunner.h"
#include "TaskEdit.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <windows.h>

namespace iw::tests
{
	template<typename Reporter, typename EditorAccess = EditAccess>
	void test_undo(Reporter& reporter)
	{
		reporter.section(L"Application-wide session undo and retained originals");
		GUID id{};
		wchar_t text[40]{};
		if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, text, static_cast<int>(std::size(text))))
		{
			reporter.check(false, L"the undo fixture obtains a unique name");
			return;
		}
		const auto root = std::filesystem::current_path() / (std::wstring(L"undo-test-fixture-") + text);
		if (!std::filesystem::create_directory(root))
		{
			reporter.check(false, L"the undo fixture is created in the project-owned working directory");
			return;
		}
		struct Cleanup
		{
			std::filesystem::path path;
			~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
		} cleanup{root};
		const auto write = [](const std::filesystem::path& path, const std::string& bytes)
		{
			std::ofstream stream(path, std::ios::binary | std::ios::trunc);
			stream << bytes;
			stream.close();
			if (!stream) throw std::runtime_error("Cannot write an isolated undo fixture.");
		};
		const auto read = [](const std::filesystem::path& path)
		{
			std::ifstream stream(path, std::ios::binary);
			return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
		};
		const auto replace = [&](undo::History& history, const std::filesystem::path& path,
			const std::string& bytes)
		{
			std::wstring error;
			const auto input = root / L"new-content.bin";
			write(input, bytes);
			const auto batch = history.begin(L"Replace fixture");
			if (!batch->protect({path}, error)) throw std::runtime_error("Undo fixture protection failed.");
			if (files::copy_file_to(input, path, true)) throw std::runtime_error("Undo fixture replacement failed.");
			reporter.check(batch->completed({path}, error) && batch->finish(error),
				L"a successful replacement is recorded in the shared history");
		};

		const auto original = root / L"original.bin";
		write(original, "original");
		const auto originalTime = std::filesystem::last_write_time(original);
		undo::History history(root / L"history");
		replace(history, original, "first edit");
		replace(history, original, "second edit");
		reporter.check(history.can_undo() && history.description() == L"Replace fixture",
			L"the application exposes the latest completed operation");
		auto report = history.undo();
		reporter.check(report.restored == 1 && !report.failed && read(original) == "first edit",
			L"Undo restores the most recent replacement from its secured original");
		report = history.undo();
		reporter.check(report.restored == 1 && !report.failed && read(original) == "original" &&
			std::filesystem::last_write_time(original) == originalTime,
			L"repeated Undo safely rebases copied identities and restores the original timestamp");
		reporter.check(!history.can_undo() && !std::filesystem::is_empty(root / L"history"),
			L"completed history leaves its recovery copies on disk rather than evicting them");

		std::wstring error;
		auto deletion = history.begin(L"Delete fixture");
		reporter.check(deletion->protect({original}, error), L"a deletion secures its original first");
		std::filesystem::remove(original);
		reporter.check(deletion->completed({original}, error) && deletion->finish(error),
			L"a successfully deleted path is recorded as absent");
		report = history.undo();
		reporter.check(report.restored == 1 && read(original) == "original",
			L"Undo restores a permanently deleted original");

		replace(history, original, "successful");
		const auto countBeforeFailedBatch = std::distance(std::filesystem::directory_iterator(root / L"history"),
			std::filesystem::directory_iterator());
		{
			const auto failed = history.begin(L"Failed operation");
			reporter.check(failed->protect({original}, error) && failed->finish(error),
				L"a refused operation can finish without publishing a successful row");
		}
		reporter.check(history.description() == L"Replace fixture",
			L"a failed operation does not hide the preceding successful Undo entry");
		report = history.undo();
		reporter.check(report.restored == 1 && read(original) == "original" &&
			std::distance(std::filesystem::directory_iterator(root / L"history"),
				std::filesystem::directory_iterator()) > countBeforeFailedBatch,
			L"failed operation recovery data is retained without replacing the Undo history");

		replace(history, original, "recorded");
		const auto recordedTime = std::filesystem::last_write_time(original);
		write(original, "modified");
		std::filesystem::last_write_time(original, recordedTime);
		report = history.undo();
		reporter.check(report.conflicts == 1 && !report.restored && read(original) == "modified" &&
			history.can_undo(), L"equal size and timestamp do not disguise a content edit; conflicts remain retryable");

		undo::History identityHistory(root / L"identity-history");
		const auto identityPath = root / L"identity.bin";
		write(identityPath, "before");
		replace(identityHistory, identityPath, "after");
		const auto occupied = root / L"other-identity.bin";
		write(occupied, "after");
		const auto identityTime = std::filesystem::last_write_time(identityPath);
		reporter.check(!files::move_file_to(occupied, identityPath, true), L"the identity fixture is replaced");
		std::filesystem::last_write_time(identityPath, identityTime);
		report = identityHistory.undo();
		reporter.check(report.conflicts == 1 && !report.restored && read(identityPath) == "after",
			L"an unrelated replacement with identical bytes is not overwritten by Undo");

		undo::History absentHistory(root / L"absent-history");
		const auto absent = root / L"absent.bin";
		write(absent, "deleted");
		const auto removed = absentHistory.begin(L"Delete");
		reporter.check(removed->protect({absent}, error), L"the absent-path fixture is protected");
		std::filesystem::remove(absent);
		reporter.check(removed->completed({absent}, error) && removed->finish(error),
			L"the absent-path fixture records its deletion");
		write(absent, "new arrival");
		report = absentHistory.undo();
		reporter.check(report.conflicts == 1 && read(absent) == "new arrival",
			L"a newly appeared destination always wins against deletion undo");

		undo::History partialHistory(root / L"partial-history");
		tasks::TaskPlan partial;
		const auto input = root / L"batch-input.bin";
		write(input, "new");
		for (int i = 0; i < 3; ++i)
		{
			tasks::TaskRow row;
			row.destination = root / (L"partial-" + std::to_wstring(i) + L".bin");
			row.state = tasks::RowState::ready;
			write(row.destination, "old");
			partial.rows.push_back(row);
		}
		std::stop_source stop;
		tasks::RunOptions options;
		options.undoHistory = &partialHistory;
		options.undoLabel = L"Partial batch";
		options.undoPaths = [](const tasks::TaskRow& row)
		{ return std::vector<std::filesystem::path>{row.destination}; };
		int acted = 0;
		options.act = [&](tasks::TaskRow& row)
		{
			if (++acted == 2)
			{
				stop.request_stop();
				throw std::runtime_error("Injected independent row failure.");
			}
			if (files::copy_file_to(input, row.destination, true))
				throw std::runtime_error("Fixture copy failed.");
		};
		tasks::run_plan(partial, stop.get_token(), options);
		reporter.check(partial.count(tasks::RowState::success) == 1 &&
			partial.count(tasks::RowState::failed) == 1 && partial.count(tasks::RowState::notRun) == 1,
			L"cancellation keeps the completed, failed and unreached rows distinct");
		report = partialHistory.undo();
		reporter.check(report.restored == 1 && read(partial.rows[0].destination) == "old" &&
			read(partial.rows[1].destination) == "old" && read(partial.rows[2].destination) == "old",
			L"Undo changes only a cancelled batch's successful row");

		const auto blockedRoot = root / L"not-a-directory";
		write(blockedRoot, "occupied");
		undo::History unavailable(blockedRoot);
		auto refusedPlan = partial;
		for (auto& row : refusedPlan.rows) row.state = tasks::RowState::ready;
		options.undoHistory = &unavailable;
		acted = 0;
		tasks::run_plan(refusedPlan, {}, options);
		reporter.check(!acted && refusedPlan.count(tasks::RowState::failed) == 3 && !unavailable.can_undo(),
			L"an unavailable backup location refuses every destructive action before it starts");

		undo::History scopedHistory(root / L"scoped-history");
		{
			undo::ScopedHistory selected(scopedHistory);
			tasks::TaskPlan scopedPlan;
			auto row = partial.rows.front();
			row.state = tasks::RowState::ready;
			scopedPlan.rows.push_back(row);
			auto scopedOptions = options;
			scopedOptions.undoHistory = nullptr;
			scopedOptions.act = [&](tasks::TaskRow& item)
			{
				if (files::copy_file_to(input, item.destination, true))
					throw std::runtime_error("Scoped undo fixture copy failed.");
			};
			tasks::run_plan(scopedPlan, {}, scopedOptions);
			reporter.check(scopedPlan.count(tasks::RowState::success) == 1 &&
				undo::history().root() == scopedHistory.root() && scopedHistory.can_undo(),
				L"default task history is explicitly injected into a uniquely owned fixture, never live storage");
			report = scopedHistory.undo();
			reporter.check(report.restored == 1 && read(row.destination) == "old",
				L"the fixture-injected default history is real undo, not a disabled test fallback");
		}

		undo::History renameHistory(root / L"rename-history");
		const auto one = root / L"1.bin";
		const auto two = root / L"2.bin";
		const auto oneSidecar = root / L"1.bin.xmp";
		const auto twoSidecar = root / L"2.bin.xmp";
		write(one, "one"); write(two, "two");
		write(oneSidecar, "one metadata"); write(twoSidecar, "two metadata");
		auto cycle = rename::analyze({two, one}, L"#", 1, tasks::CollisionPolicy::replace);
		auto renameOptions = rename::run_options(cycle);
		renameOptions.undoHistory = &renameHistory;
		tasks::run_plan(cycle, {}, renameOptions);
		reporter.check(cycle.count(tasks::RowState::success) == 2 && read(one) == "two" &&
			read(oneSidecar) == "two metadata", L"the rename fixture completes a real cycle with companions");
		report = renameHistory.undo();
		reporter.check(report.restored == 4 && !report.conflicts && !report.failed &&
			read(one) == "one" && read(two) == "two" && read(oneSidecar) == "one metadata" &&
			read(twoSidecar) == "two metadata", L"Undo restores an entire rename cycle and its sidecars");

		const auto editedCycleRoot = root / L"externally-edited-cycle";
		std::filesystem::create_directory(editedCycleRoot);
		const auto editedOne = editedCycleRoot / L"1.bin";
		const auto editedTwo = editedCycleRoot / L"2.bin";
		const auto editedOneSidecar = editedCycleRoot / L"1.bin.xmp";
		const auto editedTwoSidecar = editedCycleRoot / L"2.bin.xmp";
		write(editedOne, "one"); write(editedTwo, "two");
		write(editedOneSidecar, "one metadata"); write(editedTwoSidecar, "two metadata");
		undo::History editedCycleHistory(root / L"externally-edited-cycle-history");
		auto editedCycle = rename::analyze({editedTwo, editedOne}, L"#", 1, tasks::CollisionPolicy::replace);
		auto editedCycleOptions = rename::run_options(editedCycle);
		editedCycleOptions.undoHistory = &editedCycleHistory;
		int injectedEdits = 0;
		editedCycleOptions.progress = [&](const size_t complete, size_t)
		{
			if (complete != 1) return;
			++injectedEdits;
			const auto outputTime = std::filesystem::last_write_time(editedOne);
			const auto sidecarTime = std::filesystem::last_write_time(editedOneSidecar);
			write(editedOne, "own");
			write(editedOneSidecar, "own metadata");
			std::filesystem::last_write_time(editedOne, outputTime);
			std::filesystem::last_write_time(editedOneSidecar, sidecarTime);
		};
		tasks::run_plan(editedCycle, {}, editedCycleOptions);
		reporter.check(injectedEdits == 1 && editedCycle.count(tasks::RowState::success) == 2 &&
			read(editedTwo) == "one" && read(editedTwoSidecar) == "one metadata",
			L"the second cycle row consumes staged originals without touching the first row's edited outputs");
		report = editedCycleHistory.undo();
		reporter.check(report.conflicts == 1 && !report.restored && !report.failed &&
			read(editedOne) == "own" && read(editedOneSidecar) == "own metadata" &&
			read(editedTwo) == "one" && read(editedTwoSidecar) == "one metadata",
			L"cycle connectivity never refreshes untouched fingerprints or authorizes undo over an intervening edit");

		undo::History cancelledRenameHistory(root / L"cancelled-rename-history");
		auto cancelledCycle = rename::analyze({two, one}, L"#", 1, tasks::CollisionPolicy::replace);
		auto cancelledOptions = rename::run_options(cancelledCycle);
		cancelledOptions.undoHistory = &cancelledRenameHistory;
		std::stop_source cancelRename;
		cancelledOptions.progress = [&](const size_t complete, size_t)
		{ if (complete == 1) cancelRename.request_stop(); };
		tasks::run_plan(cancelledCycle, cancelRename.get_token(), cancelledOptions);
		reporter.check(cancelledCycle.count(tasks::RowState::success) == 1 &&
			cancelledCycle.count(tasks::RowState::notRun) == 1,
			L"cancellation leaves an unfinished cycle row unreached while staging recovery completes");
		report = cancelledRenameHistory.undo();
		reporter.check(report.restored == 4 && !report.failed && !report.conflicts &&
			read(one) == "one" && read(two) == "two" && read(oneSidecar) == "one metadata" &&
			read(twoSidecar) == "two metadata",
			L"Undo reverses the completed cycle row without consuming unfinished-row recovery copies");

		undo::History caseHistory(root / L"case-history");
		const auto casePath = root / L"case.bin";
		write(casePath, "case content");
		auto casePlan = rename::analyze({casePath}, L"CASE", 1, tasks::CollisionPolicy::replace);
		auto caseOptions = rename::run_options(casePlan);
		caseOptions.undoHistory = &caseHistory;
		tasks::run_plan(casePlan, {}, caseOptions);
		report = caseHistory.undo();
		WIN32_FIND_DATAW foundName{};
		const HANDLE foundCase = FindFirstFileW(casePath.c_str(), &foundName);
		if (foundCase != INVALID_HANDLE_VALUE) FindClose(foundCase);
		reporter.check(report.restored == 1 && foundCase != INVALID_HANDLE_VALUE &&
			std::wstring(foundName.cFileName) == L"case.bin",
			L"a case-only rename is not mistaken for an unchanged file and restores its original spelling");

		undo::History bundleHistory(root / L"bundle-history");
		const auto bundle = bundleHistory.begin(L"Atomic bundle");
		reporter.check(bundle->protect({one, oneSidecar}, error), L"both bundle members are protected");
		write(one, "new one"); write(oneSidecar, "new metadata");
		reporter.check(bundle->completed({one, oneSidecar}, error) && bundle->finish(error),
			L"bundle completion is recorded together");
		write(oneSidecar, "user metadata");
		report = bundleHistory.undo();
		reporter.check(report.conflicts == 1 && !report.restored && read(one) == "new one" &&
			read(oneSidecar) == "user metadata", L"a sidecar conflict refuses its entire bundle without partial undo");

		undo::History folderHistory(root / L"folder-history");
		const auto oldFolder = root / L"folder-before";
		const auto newFolder = root / L"folder-after";
		std::filesystem::create_directory(oldFolder);
		write(oldFolder / L"child.bin", "child");
		const auto folder = folderHistory.begin(L"Rename folder");
		const bool securedFolder = folder->protect({oldFolder, files::native_path(oldFolder), newFolder}, error);
		reporter.check(securedFolder, (L"a folder rename protects the whole tree: " + error).c_str());
		if (securedFolder)
		{
			reporter.check(!files::move_file_to(oldFolder, newFolder, false), L"the folder fixture is renamed");
			const bool recordedFolder = folder->completed(
				{files::native_path(oldFolder), files::native_path(newFolder)}, error) && folder->finish(error);
			reporter.check(recordedFolder, (L"folder rename completion is recorded: " + error).c_str());
			report = folderHistory.undo();
			reporter.check(report.restored == 2 && !report.failed &&
				read(oldFolder / L"child.bin") == "child" && !std::filesystem::exists(newFolder),
				(L"Undo restores a renamed folder and its contents without overwriting another tree: " +
					report.summary()).c_str());
		}

		undo::History storageGuard(root / L"storage-self-guard");
		const auto unsafe = storageGuard.begin(L"Reject own storage");
		reporter.check(!unsafe->protect({files::native_path(storageGuard.root())}, error) &&
			!storageGuard.can_undo(), L"an extended-path alias cannot capture the undo storage itself");
		reporter.check(unsafe->finish(error), L"a refused storage operation releases the batch");

		for (int editWhen = 0; editWhen < 4; ++editWhen)
		{
			const auto chainRoot = root / (L"descendant-chain-" + std::to_wstring(editWhen));
			const auto parent = chainRoot / L"P";
			const auto renamedParent = chainRoot / L"Q";
			std::filesystem::create_directories(parent / L"nested");
			const auto oldChild = parent / L"a.bin";
			const auto newChild = parent / L"b.bin";
			const auto oldNested = parent / L"nested" / L"a.bin";
			const auto newNested = parent / L"nested" / L"b.bin";
			write(oldChild, "one");
			write(oldNested, "nested");
			undo::History chainHistory(chainRoot / L"history");
			auto childPlan = rename::analyze({oldChild, oldNested}, L"b", 1, tasks::CollisionPolicy::replace);
			auto childOptions = rename::run_options(childPlan);
			childOptions.undoHistory = &chainHistory;
			tasks::run_plan(childPlan, {}, childOptions);
			reporter.check(childPlan.count(tasks::RowState::success) == 2,
				L"the chained fixture renames files in the parent and a nested folder");
			const auto editChild = [&]
			{
				const auto time = std::filesystem::last_write_time(newChild);
				write(newChild, "own");
				std::filesystem::last_write_time(newChild, time);
			};
			if (editWhen == 1) editChild();
			if (editWhen == 3)
			{
				const auto time = std::filesystem::last_write_time(newChild);
				const auto replacement = chainRoot / L"other-identity.bin";
				write(replacement, "one");
				reporter.check(!files::move_file_to(replacement, newChild, true),
					L"the descendant fixture replaces a completed file with an unrelated identical-content file");
				std::filesystem::last_write_time(newChild, time);
			}
			auto parentPlan = rename::analyze({parent}, L"Q", 1, tasks::CollisionPolicy::replace);
			auto parentOptions = rename::run_options(parentPlan);
			parentOptions.undoHistory = &chainHistory;
			tasks::run_plan(parentPlan, {}, parentOptions);
			reporter.check(parentPlan.count(tasks::RowState::success) == 1 &&
				std::filesystem::exists(renamedParent / L"nested" / L"b.bin"),
				(L"a later operation renames the entire parent tree: " + tasks::summarize_results(parentPlan)).c_str());
			report = chainHistory.undo();
			reporter.check(report.restored == 2 && !report.failed && !report.conflicts &&
				!std::filesystem::exists(renamedParent),
				L"Undo recreates the parent tree before attempting the earlier file renames");
			if (editWhen == 2) editChild();
			report = chainHistory.undo();
			if (!editWhen)
				reporter.check(report.restored == 4 && !report.failed && !report.conflicts &&
					read(oldChild) == "one" && read(oldNested) == "nested" &&
					!std::filesystem::exists(newChild) && !std::filesystem::exists(newNested),
					L"verified descendants and their immediate parents inherit restored identities for chained Undo");
			else
				reporter.check(report.restored == 2 && report.conflicts == 1 && !report.failed &&
					!std::filesystem::exists(oldChild) && read(newChild) == (editWhen == 3 ? "one" : "own") &&
					read(oldNested) == "nested" && !std::filesystem::exists(newNested),
					L"descendant rebasing never blesses edits or unrelated replacements; independent rows still restore");
		}

		undo::History directHistory(root / L"direct-rename-history");
		const auto directSource = root / L"direct.bin";
		const auto directSidecar = root / L"direct.bin.xmp";
		write(directSource, "direct");
		write(directSidecar, "direct metadata");
		auto directPlan = rename::direct_plan(directSource, L"literal# {name}.bin");
		auto directOptions = rename::run_options(directPlan);
		directOptions.undoHistory = &directHistory;
		tasks::run_plan(directPlan, {}, directOptions);
		reporter.check(directPlan.count(tasks::RowState::success) == 1 &&
			read(root / L"literal# {name}.bin") == "direct" &&
			read(root / L"literal# {name}.bin.xmp") == "direct metadata",
			L"direct Rename treats the entered filename literally and moves companions through the reviewed runner");
		report = directHistory.undo();
		reporter.check(report.restored == 4 && !report.failed &&
			read(directSource) == "direct" && read(directSidecar) == "direct metadata",
			L"direct Rename uses the same application-wide bundle undo");

		undo::History transferHistory(root / L"partial-transfer-history");
		const auto transferSource = root / L"transfer.bin";
		const auto transferSidecar = root / L"transfer.bin.xmp";
		const auto transferTarget = root / L"transfer-copy.bin";
		const auto transferTargetSidecar = root / L"transfer-copy.bin.xmp";
		write(transferSource, "source");
		write(transferSidecar, "metadata");
		write(transferTarget, "previous target");
		write(transferTargetSidecar, "previous metadata");
		const auto partialTransfer = transferHistory.begin(L"Partial Copy");
		reporter.check(partialTransfer->protect({transferTarget, transferTargetSidecar}, error),
			L"Copy protects all destination originals before transferring a sidecar bundle");
		const auto copiedPrimary = undo::transfer_file(*partialTransfer, transferSource, transferTarget, false, true);
		write(transferTargetSidecar, "user sidecar edit");
		const auto failedSidecar = undo::transfer_file(*partialTransfer, transferSidecar, transferTargetSidecar,
			false, true, [] { return false; });
		reporter.check(!copiedPrimary.error && copiedPrimary.destinationCompleted && failedSidecar.error &&
			!failedSidecar.destinationCompleted, L"partial Copy reports its committed primary and refused sidecar separately");
		reporter.check(partialTransfer->completed({transferTarget}, {}, error) && partialTransfer->finish(error),
			L"a partially transferred row connects only its successful paths without refreshing their fingerprints");
		report = transferHistory.undo();
		reporter.check(report.restored == 1 && !report.failed && read(transferTarget) == "previous target" &&
			read(transferTargetSidecar) == "user sidecar edit" && read(transferSource) == "source",
			L"Undo of partial Copy restores the replaced primary and never authorizes an untouched sidecar edit");

		undo::History twoStepHistory(root / L"two-step-move-history");
		const auto twoStepSource = root / L"two-step-source.bin";
		const auto twoStepTarget = root / L"two-step-target.bin";
		write(twoStepSource, "source retained");
		write(twoStepTarget, "old destination");
		const auto sourceIdentity = files::snapshot_file(twoStepSource);
		const auto twoStep = twoStepHistory.begin(L"Move with refused source removal");
		const auto partlyMoved = undo::transfer_file(*twoStep, twoStepSource, twoStepTarget, true, true,
			{}, [] { return false; });
		reporter.check(partlyMoved.error && partlyMoved.destinationCompleted && !partlyMoved.sourceRemoved &&
			read(twoStepSource) == "source retained" && read(twoStepTarget) == "source retained",
			L"a two-step Move records a committed copy even when source deletion is refused");
		reporter.check(twoStep->finish(error), L"the partial Move is published to undo history");
		report = twoStepHistory.undo();
		reporter.check(report.restored == 1 && !report.failed && read(twoStepTarget) == "old destination" &&
			sourceIdentity == files::snapshot_file(twoStepSource),
			L"partial Move undo restores only the destination and keeps the untouched source identity");

		undo::History removalHistory(root / L"directory-removal-history");
		const auto removalPath = root / L"directory-removal";
		std::filesystem::create_directory(removalPath);
		const auto removal = removalHistory.begin(L"Guard directory removal");
		reporter.check(removal->protect_directory(removalPath, error), L"a source directory node is protected before removal");
		write(removalPath / L"arrival.bin", "new child");
		reporter.check(!removal->remove_directory(removalPath, error) &&
			read(removalPath / L"arrival.bin") == "new child",
			L"source directory removal atomically refuses a newly arrived child");
		std::filesystem::rename(removalPath, root / L"retained-directory");
		write(removalPath, "new file at the old directory name");
		reporter.check(!removal->remove_directory(removalPath, error) &&
			read(removalPath) == "new file at the old directory name",
			L"source directory removal never deletes a replacement regular file at the same pathname");
		reporter.check(removal->finish(error) && !removalHistory.can_undo(),
			L"refused directory removals do not invent successful mutations");

		for (const bool unrelatedChild : {false, true})
		{
			const auto mergeRoot = root / (unrelatedChild ? L"merge-with-arrival" : L"merge");
			const auto from = mergeRoot / L"source";
			const auto to = mergeRoot / L"destination";
			std::filesystem::create_directories(from / L"nested");
			std::filesystem::create_directories(to);
			write(from / L"photo.bin", "photo");
			write(from / L"photo.bin.xmp", "photo metadata");
			write(from / L"nested" / L"child.bin", "child");
			write(to / L"photo.bin", "old photo");
			write(to / L"keep.bin", "unrelated original");
			undo::History mergeHistory(mergeRoot / L"history");
			const auto merge = mergeHistory.begin(L"Move into existing directory");
			reporter.check(merge->protect_directory(from, error) &&
				merge->protect_directory(from / L"nested", error) &&
				merge->protect_directory(to / L"nested", error),
				L"directory merges protect source nodes and newly created nodes, not the existing destination tree");
			std::filesystem::create_directory(to / L"nested");
			reporter.check(merge->completed({to / L"nested"}, error), L"a new empty destination node is recorded immediately");
			std::vector<std::filesystem::path> completedPaths;
			for (const auto& relative : {std::filesystem::path(L"photo.bin"), std::filesystem::path(L"photo.bin.xmp"),
				std::filesystem::path(L"nested") / L"child.bin"})
			{
				const auto result = undo::transfer_file(*merge, from / relative, to / relative, true,
					std::filesystem::exists(to / relative));
				reporter.check(!result.error && result.destinationCompleted && result.sourceRemoved,
					L"each successful file mutation of a directory Move is recorded independently");
				completedPaths.push_back(from / relative);
				completedPaths.push_back(to / relative);
			}
			write(to / L"keep.bin", "unrelated user edit");
			if (unrelatedChild) write(to / L"nested" / L"arrival.bin", "new user file");
			reporter.check(merge->remove_directory(from / L"nested", error) && merge->remove_directory(from, error),
				L"Move removes verified empty source directories by handle and records only successful removals");
			reporter.check(merge->completed(completedPaths, {}, error) && merge->finish(error),
				L"finishing a merged transfer never captures untouched destination-tree contents");
			report = mergeHistory.undo();
			reporter.check(!report.conflicts && read(from / L"photo.bin") == "photo" &&
				read(from / L"photo.bin.xmp") == "photo metadata" &&
				read(from / L"nested" / L"child.bin") == "child" &&
				read(to / L"photo.bin") == "old photo" && read(to / L"keep.bin") == "unrelated user edit",
				(L"merged Move undo restores parents before files and preserves unrelated edits: " + report.summary()).c_str());
			if (unrelatedChild)
				reporter.check(report.failed == 1 && read(to / L"nested" / L"arrival.bin") == "new user file" &&
					!std::filesystem::exists(to / L"nested" / L"child.bin"),
					L"created-directory cleanup refuses a newly arrived child instead of moving or deleting it");
			else
				reporter.check(!report.failed && !std::filesystem::exists(to / L"nested"),
					L"created destination directories are removed only after their owned files have been undone");
		}

		for (const bool userArrival : {false, true})
		{
			const auto caseRoot = root / (userArrival ? L"copy-rename-user-child" : L"copy-rename");
			std::filesystem::create_directory(caseRoot);
			const auto source = caseRoot / L"source.bin";
			const auto copiedFolder = caseRoot / L"copied";
			const auto renamedFolder = caseRoot / L"renamed";
			write(source, "copied bytes");
			undo::History copiedHistory(caseRoot / L"history");
			const auto copy = copiedHistory.begin(L"Copy folder fixture");
			const bool protectedNode = copy->protect_directory(copiedFolder, error);
			reporter.check(protectedNode, L"the copied directory's absent node is protected");
			if (!protectedNode) return;
			std::filesystem::create_directory(copiedFolder);
			reporter.check(copy->completed({copiedFolder}, error),
				L"directory creation records only the node before files are added");
			const auto transferred = undo::transfer_file(*copy, source, copiedFolder / L"item.bin", false, false);
			reporter.check(!transferred.error && copy->finish(error), L"a copied file and its created directory are recorded");
			const auto renameFolder = copiedHistory.begin(L"Rename copied folder");
			const bool protectedRename = renameFolder->protect({copiedFolder, renamedFolder}, error);
			reporter.check(protectedRename, L"the copied directory can subsequently be renamed");
			if (!protectedRename) return;
			reporter.check(!files::move_file_to(copiedFolder, renamedFolder, false) &&
				renameFolder->completed({copiedFolder, renamedFolder}, error) && renameFolder->finish(error),
				L"the later folder rename is recorded");
			report = copiedHistory.undo();
			reporter.check(!report.failed && !report.conflicts && read(copiedFolder / L"item.bin") == "copied bytes",
				L"Undo Rename restores the copied directory and rebases its node identity");
			if (userArrival) write(copiedFolder / L"user.bin", "user bytes");
			report = copiedHistory.undo();
			reporter.check(!report.conflicts && !std::filesystem::exists(copiedFolder / L"item.bin"),
				L"Undo Copy can remove its files after the directory was restored by Undo Rename");
			if (userArrival)
				reporter.check(report.failed == 1 && read(copiedFolder / L"user.bin") == "user bytes",
					L"rebasing a created directory never grants ownership of a newly arrived child");
			else
				reporter.check(!report.failed && !std::filesystem::exists(copiedFolder) && !copiedHistory.can_undo(),
					L"Undo Copy removes its empty created node and leaves no false conflict blocking history");
		}

		files::DecodedImage editImage;
		editImage.width = editImage.originalWidth = 4;
		editImage.height = editImage.originalHeight = 4;
		editImage.pixels.assign(16, 0xff405060u);
		const auto editSource = root / L"undo-edit.png";
		reporter.check(files::save_image(editImage, editSource, files::ImageSaveFormat::png, {}, false),
			L"the real Photo Edit undo fixture is encoded");
		const auto originalEditBytes = read(editSource);
		undo::History editHistory(root / L"photo-edit-history");
		{
			undo::ScopedHistory selected(editHistory);
			TaskEdit editor;
			EditorAccess::prepare(editor, editSource, editImage);
			reporter.check(EditorAccess::write(editor, editSource, true) && editHistory.can_undo(),
				L"Photo Edit in-place Save protects the original and records its actual successful encoding");
		}
		report = editHistory.undo();
		reporter.check(report.restored == 1 && !report.failed && read(editSource) == originalEditBytes,
			L"application-wide Undo restores the exact original bytes after Photo Edit Save");
		const auto editCopy = root / L"undo-edit-copy.png";
		{
			undo::ScopedHistory selected(editHistory);
			TaskEdit editor;
			EditorAccess::prepare(editor, editSource, editImage);
			reporter.check(EditorAccess::write(editor, editCopy, false) && editHistory.can_undo(),
				L"Photo Edit Save as records its new output without claiming the source");
			EditorAccess::lose_decode(editor);
			reporter.check(!EditorAccess::write(editor, editCopy, false) && editHistory.can_undo(),
				L"a failed Photo Edit save does not replace the preceding successful history entry");
		}
		report = editHistory.undo();
		reporter.check(report.restored == 1 && !report.failed && !std::filesystem::exists(editCopy) &&
			read(editSource) == originalEditBytes, L"Undo of Photo Edit Save as removes only its verified output");

		const auto retained = root / L"retained-history";
		{
			undo::History session(retained);
			replace(session, original, "session edit");
		}
		undo::History restarted(retained);
		reporter.check(!restarted.can_undo() && !std::filesystem::is_empty(retained),
			L"restart never replays stale operations or deletes their retained recovery originals");
	}
}

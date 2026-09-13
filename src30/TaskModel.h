// ImageWalker by Zac Walker
// Declares the reviewed plan every task view analyses, shows, and runs.

#pragma once

#include "Paths.h"

#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace iw::tasks
{
	enum class CollisionPolicy { block, skip, autoRename, replace };

	enum class RowState { pending, ready, blocked, running, success, failed, skipped, notRun };

	struct TaskRow
	{
		std::filesystem::path source;
		std::filesystem::path destination;
		std::vector<std::filesystem::path> sidecars;
		std::wstring change; // Review column, or Status after a run
		std::wstring detail; // first actionable error for this row
		RowState state{RowState::pending};
		// Reviewed and approved for overwrite. Nothing else may overwrite anything.
		bool replace{};
		// The view's own classification of this row. The model never interprets it.
		int tag{};
		std::shared_ptr<const void> context; // immutable workflow-specific reviewed snapshot
	};

	// A plan is immutable once analysis completes: any control change bumps the generation and
	// discards it, and a completion carrying a stale generation is dropped rather than merged.
	struct TaskPlan
	{
		std::uint64_t generation{};
		std::vector<TaskRow> rows;
		std::wstring blockReason; // empty means Run is available
		size_t collisions{};
		size_t replacements{};
		size_t skips{};
		std::wstring completionError;

		size_t count(const RowState state) const
		{
			size_t result = 0;
			for (const auto& row : rows) if (row.state == state) ++result;
			return result;
		}

		bool can_run() const { return blockReason.empty(); }
	};

	// True when the row intends to write, and so owns its destination for the whole run.
	inline bool row_writes(const TaskRow& row)
	{
		return !row.destination.empty() && row.state != RowState::skipped && row.state != RowState::blocked &&
			row.state != RowState::notRun;
	}

	namespace detail
	{
		struct PathLess
		{
			bool operator()(const std::wstring& left, const std::wstring& right) const
			{
				return paths::icompare(left, right) < 0;
			}
		};

		using PathSet = std::set<std::wstring, PathLess>;
	}

	// The first destination claimed by two writing rows, or an empty path.
	inline std::filesystem::path duplicate_destination(const TaskPlan& plan)
	{
		detail::PathSet claimed;
		for (const auto& row : plan.rows)
		{
			if (!row_writes(row)) continue;
			if (!claimed.insert(row.destination.lexically_normal().native()).second) return row.destination;
		}
		return {};
	}

	// No plan may merge two sources into one destination, whatever policy produced it. This is a
	// property of the finished plan, so it is checked after collision resolution, not instead of it.
	inline bool block_duplicate_destinations(TaskPlan& plan)
	{
		const auto duplicate = duplicate_destination(plan);
		if (duplicate.empty()) return false;
		plan.blockReason = std::format(L"Two items would be written to {}.", duplicate.filename().wstring());
		return true;
	}

	using ExistsFunction = std::function<bool(const std::filesystem::path&)>;

	// Chooses a free destination near the proposed one. `alsoTaken` reports names this plan has
	// already claimed but that do not exist on disk yet. Returns an empty path when bounded
	// searching finds nothing.
	using UniqueFunction = std::function<std::filesystem::path(const std::filesystem::path& proposed,
	                                                           const ExistsFunction& alsoTaken)>;

	// Applies the collision policy to every pending row and leaves each one ready, skipped or
	// blocked. `occupied` answers whether a destination is already on disk.
	inline void resolve_collisions(TaskPlan& plan, const CollisionPolicy policy,
	                               const ExistsFunction& occupied, const UniqueFunction& unique)
	{
		plan.collisions = 0;
		plan.replacements = 0;
		plan.skips = 0;
		plan.blockReason.clear();

		detail::PathSet claimed;
		const ExistsFunction claimedHere = [&claimed](const std::filesystem::path& path)
		{
			return claimed.contains(path.lexically_normal().native());
		};
		// Auto-rename must avoid both what is on disk and what this same plan has already claimed.
		const ExistsFunction anythingTaken = [&occupied, &claimedHere](const std::filesystem::path& path)
		{
			return (occupied && occupied(path)) || claimedHere(path);
		};

		size_t blocked = 0;
		for (auto& row : plan.rows)
		{
			if (row.state == RowState::blocked)
			{
				++blocked;
				continue;
			}
			if (row.state == RowState::skipped)
			{
				++plan.skips;
				continue;
			}
			row.detail.clear();
			row.replace = false;
			if (row.destination.empty())
			{
				row.state = RowState::blocked;
				row.change = L"No destination";
				++blocked;
				continue;
			}

			const bool onDisk = occupied && occupied(row.destination);
			const bool taken = claimedHere(row.destination);
			if (!onDisk && !taken)
			{
				row.state = RowState::ready;
				claimed.insert(row.destination.lexically_normal().native());
				continue;
			}

			++plan.collisions;
			switch (policy)
			{
			case CollisionPolicy::skip:
				row.state = RowState::skipped;
				row.change = taken ? L"Skip - another item claims this name" : L"Skip - destination exists";
				++plan.skips;
				break;

			case CollisionPolicy::autoRename:
			{
				const auto free = unique ? unique(row.destination, anythingTaken) : std::filesystem::path{};
				if (free.empty() || anythingTaken(free))
				{
					row.state = RowState::blocked;
					row.change = L"No free name";
					++blocked;
					break;
				}
				row.destination = free;
				row.state = RowState::ready;
				row.change = std::format(L"Renamed to {}", free.filename().wstring());
				claimed.insert(free.lexically_normal().native());
				break;
			}

			case CollisionPolicy::replace:
				// Replacing a file this same run is about to create is a merge, not a replacement.
				if (taken)
				{
					row.state = RowState::blocked;
					row.change = L"Two items claim this name";
					++blocked;
					break;
				}
				row.state = RowState::ready;
				row.change = L"Replace";
				row.replace = true;
				++plan.replacements;
				claimed.insert(row.destination.lexically_normal().native());
				break;

			case CollisionPolicy::block:
			default:
				row.state = RowState::blocked;
				row.change = L"File exists";
				++blocked;
				break;
			}
		}

		if (blocked)
			plan.blockReason = std::format(L"{} destination{} cannot be written. Choose another destination or "
			                               L"collision policy.", blocked, blocked == 1 ? L"" : L"s");
		else if (!block_duplicate_destinations(plan))
		{
			const auto ready = plan.count(RowState::ready);
			if (plan.rows.empty()) plan.blockReason = L"Nothing to do.";
			else if (ready == 0) plan.blockReason = L"Every item would be skipped.";
		}
	}

	inline std::wstring describe_collisions(const TaskPlan& plan)
	{
		if (!plan.collisions) return {};
		std::wstring result = std::format(L"{} collision{}", plan.collisions, plan.collisions == 1 ? L"" : L"s");
		if (plan.replacements) result += std::format(L", {} replaced", plan.replacements);
		if (plan.skips) result += std::format(L", {} skipped", plan.skips);
		return result;
	}

	// A finished run never reports partial completion as success: every row keeps an outcome.
	inline std::wstring summarize_results(const TaskPlan& plan)
	{
		const auto success = plan.count(RowState::success);
		const auto failed = plan.count(RowState::failed);
		const auto skipped = plan.count(RowState::skipped);
		const auto notRun = plan.count(RowState::notRun);
		std::wstring result = std::format(L"{} succeeded", success);
		if (failed) result += std::format(L", {} failed", failed);
		if (skipped) result += std::format(L", {} skipped", skipped);
		if (notRun) result += std::format(L", {} not run", notRun);
		bool describedError = false;
		for (const auto& row : plan.rows)
		{
			if (row.state != RowState::failed || row.detail.empty()) continue;
			result += L" - " + row.detail;
			describedError = true;
			break;
		}
		if (!describedError)
		{
			for (const auto& row : plan.rows)
			{
				if (row.state != RowState::notRun || row.detail.empty()) continue;
				result += L" - " + row.detail;
				break;
			}
		}
		if (!plan.completionError.empty()) result += L" - " + plan.completionError;
		return result;
	}
}

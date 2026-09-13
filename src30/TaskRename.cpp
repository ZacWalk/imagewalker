// ImageWalker by Zac Walker
// Implements Batch Rename, including cycle staging and revalidation of the reviewed snapshot.

#include "TaskRename.h"

#include "Paths.h"

#include <algorithm>
#include <format>
#include <memory>
#include <map>
#include <limits>
#include <set>
#include <stdexcept>
#include <windows.h>

namespace iw
{
	namespace
	{
		enum ControlId
		{
			idDescription = 1,
			idTemplateHeading,
			idTemplate,
			idStart,
			idSuggestFile,
			idSuggestCreated,
			idSuggestYearMonth,
			idSuggestName,
			idHelp,
			idCollisionHeading,
			idCollision
		};

		tasks::CollisionPolicy policy_from_choice(const int value)
		{
			switch (value)
			{
			case 1: return tasks::CollisionPolicy::skip;
			case 2: return tasks::CollisionPolicy::autoRename;
			case 3: return tasks::CollisionPolicy::replace;
			default: return tasks::CollisionPolicy::block;
			}
		}

	}
}

namespace iw::rename
{
	namespace
	{
		using PathSet = std::set<std::wstring, tasks::detail::PathLess>;

		struct Snapshot
		{
			bool valid{};
			bool exists{};
			DWORD attributes{};
			DWORD volume{};
			DWORD indexHigh{};
			DWORD indexLow{};
			std::uint64_t size{};
			std::uint64_t modified{};

			static Snapshot read(const std::filesystem::path& path)
			{
				Snapshot result;
				const HANDLE handle = CreateFileW(files::native_path(path).c_str(), FILE_READ_ATTRIBUTES,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
					FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
				if (handle == INVALID_HANDLE_VALUE)
				{
					const auto error = GetLastError();
					result.valid = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
					return result;
				}
				BY_HANDLE_FILE_INFORMATION info{};
				result.valid = GetFileInformationByHandle(handle, &info) != FALSE;
				CloseHandle(handle);
				result.exists = true;
				result.attributes = info.dwFileAttributes;
				result.volume = info.dwVolumeSerialNumber;
				result.indexHigh = info.nFileIndexHigh;
				result.indexLow = info.nFileIndexLow;
				result.size = static_cast<std::uint64_t>(info.nFileSizeHigh) << 32 | info.nFileSizeLow;
				result.modified = static_cast<std::uint64_t>(info.ftLastWriteTime.dwHighDateTime) << 32 |
					info.ftLastWriteTime.dwLowDateTime;
				return result;
			}

			bool same(const Snapshot& current) const
			{
				return valid && current.valid && exists == current.exists && (!exists ||
					(attributes == current.attributes && volume == current.volume &&
					 indexHigh == current.indexHigh && indexLow == current.indexLow &&
					 size == current.size && modified == current.modified));
			}

			bool writable_item() const
			{
				return valid && exists && !(attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_REPARSE_POINT));
			}
		};

		struct Reviewed
		{
			std::vector<std::filesystem::path> sources;
			std::vector<std::filesystem::path> destinations;
			std::vector<Snapshot> sourceState;
			std::vector<Snapshot> destinationState;
			std::filesystem::path parent;
		};

		std::filesystem::path canonical_parent(const std::filesystem::path& path)
		{
			std::error_code error;
			const auto result = std::filesystem::canonical(files::native_path(path.parent_path()), error);
			return error ? std::filesystem::path{} : result;
		}

		std::vector<std::filesystem::path> bundle(const tasks::TaskRow& row, const std::filesystem::path& destination)
		{
			std::vector<std::filesystem::path> result{destination};
			for (const auto& sidecar : row.sidecars)
				result.push_back(sidecar_destination(sidecar, row.source, destination));
			return result;
		}

		bool unchanged(const tasks::TaskRow& row)
		{
			return row.source.native() == row.destination.native();
		}

		std::wstring expand_name(std::wstring_view text, const files::Metadata& metadata,
		                         const std::filesystem::path& source, const int sequence, const bool directory)
		{
			if (!directory) return files::expand_tokens(text, metadata, source, sequence);
			std::wstring result;
			// Folder dots belong to the name, not an extension; expand {name} without dropping them.
			while (!text.empty())
			{
				const auto open = text.find(L'{');
				if (open == std::wstring_view::npos)
				{
					result += files::expand_tokens(text, metadata, source, sequence);
					break;
				}
				result += files::expand_tokens(text.substr(0, open), metadata, source, sequence);
				text.remove_prefix(open);
				const auto close = text.find(L'}');
				if (close == std::wstring_view::npos)
				{
					result += files::expand_tokens(text, metadata, source, sequence);
					break;
				}
				result += paths::iequals(text.substr(1, close - 1), L"name") ? source.filename().wstring() :
					files::expand_tokens(text.substr(0, close + 1), metadata, source, sequence);
				text.remove_prefix(close + 1);
			}
			return result;
		}
	}

	std::filesystem::path sidecar_destination(const std::filesystem::path& sidecar,
	                                          const std::filesystem::path& source,
	                                          const std::filesystem::path& destination)
	{
		const auto name = sidecar.filename().wstring();
		const auto sourceName = source.filename().wstring();
		if (name.size() > sourceName.size() &&
			paths::iequals(std::wstring_view(name).substr(0, sourceName.size()), sourceName))
			return destination.parent_path() /
				(destination.filename().wstring() + name.substr(sourceName.size()));
		return destination.parent_path() / (destination.stem().wstring() + sidecar.extension().wstring());
	}

	Staging::~Staging()
	{
		if (!restorationAttempted_) restore();
	}

	std::vector<Staging::Recovery> Staging::restore() noexcept
	{
		restorationAttempted_ = true;
		std::vector<Recovery> recovered;
		try
		{
			for (const auto& group : bundles_)
			{
				if (group.empty()) continue;
				const auto& primary = group.front();
				if (paths::equal(actual(primary), primary)) continue;
				const auto freeBundle = [&](const std::filesystem::path& candidate)
				{
					for (size_t index = 0; index < group.size(); ++index)
					{
						if (paths::equal(actual(group[index]), group[index])) continue;
						const auto destination = index ? sidecar_destination(group[index], primary, candidate) : candidate;
						const auto state = Snapshot::read(destination);
						if (!state.valid || state.exists) return false;
					}
					return true;
				};
				auto candidate = primary;
				for (int number = 2; !freeBundle(candidate) && number <= 10000; ++number)
					candidate = primary.parent_path() /
						std::format(L"{} ({}){}", primary.stem().wstring(), number, primary.extension().wstring());
				if (!freeBundle(candidate)) continue;
				for (size_t index = 0; index < group.size(); ++index)
				{
					const auto entry = std::ranges::find_if(staged_, [&](const Entry& value)
					{
						return paths::equal(value.original, group[index]);
					});
					if (entry == staged_.end()) continue;
					const auto destination = index ? sidecar_destination(group[index], primary, candidate) : candidate;
					if (files::move_file_to(entry->temporary, destination, false)) continue;
					recovered.push_back({entry->original, destination, false});
					staged_.erase(entry);
				}
			}
			for (auto entry = staged_.begin(); entry != staged_.end();)
			{
				const auto state = Snapshot::read(entry->temporary);
				if (state.valid && !state.exists)
				{
					entry = staged_.erase(entry);
					continue;
				}
				auto target = entry->original;
				bool restored = false;
				// Bounded retries also handle a new arrival after choosing a recovery name.
				for (int attempt = 0; attempt < 8; ++attempt)
				{
					target = files::unique_destination(entry->original);
					if (target.empty()) break;
					if (!files::move_file_to(entry->temporary, target, false))
					{
						restored = true;
						break;
					}
					if (!Snapshot::read(target).exists) break;
				}
				recovered.push_back({entry->original, restored ? target : entry->temporary, !restored});
				if (restored) entry = staged_.erase(entry);
				else ++entry;
			}
		}
		catch (...) {}
		return recovered;
	}

	std::filesystem::path Staging::actual(const std::filesystem::path& source) const
	{
		for (const auto& entry : staged_)
			if (paths::equal(entry.original, source)) return entry.temporary;
		return source;
	}

	void Staging::register_bundle(const std::filesystem::path& primary,
	                             const std::vector<std::filesystem::path>& sidecars)
	{
		bundles_.push_back({primary});
		bundles_.back().insert(bundles_.back().end(), sidecars.begin(), sidecars.end());
	}

	bool Staging::stage_one(const std::filesystem::path& path,
		std::vector<std::filesystem::path>* mutatedPaths)
	{
		if (!paths::equal(actual(path), path)) return false;
		const auto temporary = files::unique_destination(path.parent_path() / L".iw30-rename-stage");
		if (temporary.empty()) return false;
		restorationAttempted_ = false;
		if (mutatedPaths) mutatedPaths->push_back(path);
		staged_.push_back({temporary, path});
		if (files::move_file_to(path, temporary, false))
		{
			staged_.pop_back();
			return false;
		}
		return true;
	}

	bool Staging::stage(const std::filesystem::path& path,
		std::vector<std::filesystem::path>* mutatedPaths)
	{
		if (!paths::equal(actual(path), path)) return false;
		for (const auto& group : bundles_)
		{
			if (!std::ranges::any_of(group, [&](const auto& member) { return paths::equal(member, path); })) continue;
			for (const auto& member : group)
				if (paths::equal(actual(member), member) && !stage_one(member, mutatedPaths)) return false;
			return true;
		}
		return stage_one(path, mutatedPaths);
	}

	void Staging::settled(const std::filesystem::path& temporary)
	{
		std::erase_if(staged_, [&temporary](const Entry& entry)
		{
			return paths::equal(entry.temporary, temporary);
		});
	}

	void rename_row(tasks::TaskRow& row, Staging& staging,
	                const std::function<bool(const std::filesystem::path&)>& plannedSource,
	                std::vector<std::filesystem::path>* mutatedPaths)
	{
		if (mutatedPaths) mutatedPaths->clear();
		if (unchanged(row))
		{
			row.state = tasks::RowState::skipped;
			row.change = L"Unchanged";
			return;
		}
		auto sources = std::vector<std::filesystem::path>{row.source};
		sources.insert(sources.end(), row.sidecars.begin(), row.sidecars.end());
		const auto destinations = bundle(row, row.destination);
		const auto reviewed = std::static_pointer_cast<const Reviewed>(row.context);
		std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moved;
		std::vector<std::filesystem::path> replaced;
		moved.reserve(sources.size());
		replaced.reserve(sources.size());
		try
		{
			// Stage all occupied members before moving any part of this logical row.
			for (size_t index = 0; index < sources.size(); ++index)
			{
				const auto& target = destinations[index];
				const auto current = Snapshot::read(target);
				if (!current.valid) throw std::runtime_error("A destination cannot be accessed.");
				if (!current.exists) continue;
				if (paths::equal(staging.actual(sources[index]), target))
				{
					if (sources[index].native() == target.native()) continue;
					if (!staging.stage(sources[index], mutatedPaths)) throw std::runtime_error("The case-only rename could not be staged.");
					continue;
				}
				if (plannedSource && plannedSource(target) && paths::equal(staging.actual(target), target))
				{
					if (!staging.stage(target, mutatedPaths)) throw std::runtime_error("A name exchange could not be staged.");
					continue;
				}
				const bool approved = reviewed ? reviewed->destinationState[index].exists && row.replace :
					index == 0 && row.replace;
				if (approved && reviewed && !reviewed->destinationState[index].same(current))
					throw std::runtime_error("The approved replacement changed after Review.");
				if (!approved)
				{
					if (index == 0)
					{
						row.state = tasks::RowState::skipped;
						row.change = L"Skipped - the name was taken after review";
						return;
					}
					throw std::runtime_error("A companion destination was taken after review.");
				}
				if (!current.writable_item() || (current.attributes & FILE_ATTRIBUTE_DIRECTORY) ||
					!staging.stage(target, mutatedPaths))
					throw std::runtime_error("The approved replacement could not be staged.");
				replaced.push_back(staging.actual(target));
			}
			for (size_t index = 0; index < sources.size(); ++index)
			{
				const auto source = staging.actual(sources[index]);
				if (source.native() == destinations[index].native()) continue;
				// Allocate recovery bookkeeping before modifying the filesystem.
				moved.emplace_back(destinations[index], source);
				if (mutatedPaths)
				{
					if (paths::equal(source, sources[index])) mutatedPaths->push_back(sources[index]);
					mutatedPaths->push_back(destinations[index]);
				}
				if (const auto failure = files::move_file_to(source, destinations[index], false))
				{
					moved.pop_back();
					throw std::runtime_error(index ? "A companion file could not be renamed." : failure.message());
				}
			}
		}
		catch (...)
		{
			for (auto item = moved.rbegin(); item != moved.rend(); ++item)
			{
				if (files::move_file_to(item->first, item->second, false))
					row.detail += std::format(L" Recovery required: {} remains at {}.", item->second.wstring(), item->first.wstring());
			}
			if (!row.detail.empty())
			{
				row.state = tasks::RowState::failed;
				row.detail = L"The rename failed." + row.detail;
				return;
			}
			throw;
		}
		for (const auto& [destination, source] : moved) staging.settled(source);
		for (const auto& backup : replaced)
		{
			std::error_code error;
			if (std::filesystem::remove(files::native_path(backup), error)) staging.settled(backup);
			else
			{
				staging.settled(backup);
				row.detail += std::format(L" Previous destination retained at {}.", backup.wstring());
			}
		}
		row.change = row.destination.filename().wstring();
	}

	tasks::TaskPlan analyze(const std::vector<std::filesystem::path>& sources, const std::wstring_view templateText,
	                       const int start, const tasks::CollisionPolicy policy, const std::stop_token& stop,
	                       const tasks::ProgressFunction& progress)
	{
		tasks::TaskPlan plan;
		if (templateText.empty())
		{
			plan.blockReason = L"Enter a name template.";
			return plan;
		}
		if (start < 0 || sources.size() > static_cast<size_t>((std::numeric_limits<int>::max)() - start))
		{
			plan.blockReason = L"The sequence is outside the supported range.";
			return plan;
		}
		PathSet allSources;
		std::vector<std::filesystem::path> proposed;
		for (size_t index = 0; index < sources.size(); ++index)
		{
			if (stop.stop_requested()) break;
			tasks::TaskRow row;
			const auto parent = canonical_parent(sources[index]);
			row.source = parent.empty() ? sources[index] : parent / sources[index].filename();
			const auto sourceState = Snapshot::read(row.source);
			const bool directory = (sourceState.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
			if (!directory) row.sidecars = files::sidecar_paths(row.source);
			auto review = std::make_shared<Reviewed>();
			review->parent = parent;
			review->sources.push_back(row.source);
			review->sources.insert(review->sources.end(), row.sidecars.begin(), row.sidecars.end());
			for (const auto& source : review->sources)
			{
				review->sourceState.push_back(Snapshot::read(source));
				if (!allSources.insert(source.native()).second)
				{
					row.state = tasks::RowState::blocked;
					row.change = L"A selected item or companion belongs to more than one row.";
				}
				if (!review->sourceState.back().writable_item() || parent.empty())
				{
					row.state = tasks::RowState::blocked;
					row.change = L"A source or companion is missing, read-only, or not a local regular item.";
				}
			}
			const auto metadata = files::read_metadata(row.source);
			const auto name = expand_name(templateText, metadata, row.source, start + static_cast<int>(index), directory);
			const auto finalName = name + (directory ? L"" : row.source.extension().wstring());
			if (const auto problem = paths::validate_file_name(finalName);
				name.empty() || finalName.size() > 255 || problem != paths::NameProblem::none)
			{
				row.state = tasks::RowState::blocked;
				row.change = name.empty() ? L"The template produces an empty name." :
					finalName.size() > 255 ? L"The new name exceeds 255 characters." : paths::describe(problem);
			}
			else row.destination = row.source.parent_path() / finalName;
			if (row.state != tasks::RowState::blocked && unchanged(row))
			{
				row.state = tasks::RowState::skipped;
				row.change = L"Unchanged";
			}
			row.context = std::move(review);
			proposed.push_back(row.destination);
			plan.rows.push_back(std::move(row));
			if (progress) progress(index + 1, sources.size());
		}

		for (auto& row : plan.rows)
			for (const auto& other : plan.rows)
				if (!paths::equal(row.source, other.source) && paths::contains(row.source, other.source))
				{
					row.state = tasks::RowState::blocked;
					row.change = L"Rename a selected folder separately from items inside it.";
				}

		// Removing a skipped source can turn an apparent cycle into an ordinary collision. Iterate
		// until every row still advertised as moving really vacates its source.
		for (size_t pass = 0; pass <= plan.rows.size(); ++pass)
		{
			PathSet active;
			for (const auto& row : plan.rows)
			{
				if (row.state == tasks::RowState::blocked || row.state == tasks::RowState::skipped) continue;
				active.insert(row.source.native());
				for (const auto& sidecar : row.sidecars) active.insert(sidecar.native());
			}
			PathSet claimed;
			bool removed = false;
			for (size_t index = 0; index < plan.rows.size(); ++index)
			{
				auto& row = plan.rows[index];
				if (row.state == tasks::RowState::blocked || row.state == tasks::RowState::skipped) continue;
				row.destination = proposed[index];
				row.replace = false;
				row.change.clear();
				bool disk = false, duplicate = false, protectedSource = false, inaccessible = false;
				const auto collision = [&](const std::filesystem::path& candidate)
				{
					disk = duplicate = protectedSource = inaccessible = false;
					PathSet members;
					for (const auto& target : bundle(row, candidate))
					{
						const auto state = Snapshot::read(target);
						inaccessible |= !state.valid;
						duplicate |= claimed.contains(target.native()) || !members.insert(target.native()).second;
						if (!active.contains(target.native()) && state.exists)
						{
							disk = true;
							protectedSource |= allSources.contains(target.native());
							inaccessible |= !state.writable_item() || (state.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
						}
					}
					return disk || duplicate || inaccessible;
				};
				if (collision(row.destination))
				{
					if (policy == tasks::CollisionPolicy::autoRename)
					{
						const auto sourceState = Snapshot::read(row.source);
						const bool directory = (sourceState.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
						const auto stem = directory ? proposed[index].filename().wstring() : proposed[index].stem().wstring();
						const auto extension = directory ? L"" : proposed[index].extension().wstring();
						bool found = false;
						for (int suffix = 2; suffix <= 10000; ++suffix)
						{
							if (stop.stop_requested()) break;
							auto candidate = proposed[index].parent_path() /
								std::format(L"{} ({}){}", stem, suffix, extension);
							if (candidate.filename().native().size() > 255) break;
							if (collision(candidate)) continue;
							row.destination = std::move(candidate);
							row.change = L"Auto-renamed";
							found = true;
							break;
						}
						if (!found)
						{
							row.state = tasks::RowState::blocked;
							row.change = L"No free name for this item and all its companions.";
							removed = true;
							continue;
						}
					}
					else if (policy == tasks::CollisionPolicy::skip)
					{
						row.state = tasks::RowState::skipped;
						row.change = L"Skip - the item or a companion destination is occupied.";
						removed = true;
						continue;
					}
					else if (policy == tasks::CollisionPolicy::replace && !duplicate && !protectedSource && !inaccessible)
					{
						row.replace = true;
						row.change = L"Replace existing item or companion";
					}
					else
					{
						row.state = tasks::RowState::blocked;
						row.change = duplicate ? L"Two items or companions claim this name." :
							protectedSource ? L"The name belongs to an original that is not being moved." :
							inaccessible ? L"A destination is inaccessible, read-only, or a folder." : L"File or companion exists.";
						removed = true;
						continue;
					}
				}
				row.state = tasks::RowState::ready;
				for (const auto& target : bundle(row, row.destination)) claimed.insert(target.native());
			}
			if (!removed) break;
		}

		for (auto& row : plan.rows)
		{
			auto review = std::make_shared<Reviewed>(*std::static_pointer_cast<const Reviewed>(row.context));
			if (!row.destination.empty())
			{
				review->destinations = bundle(row, row.destination);
				for (const auto& destination : review->destinations)
					review->destinationState.push_back(Snapshot::read(destination));
			}
			row.context = std::move(review);
			if (row.state == tasks::RowState::blocked && plan.blockReason.empty()) plan.blockReason = row.change;
			if (row.state == tasks::RowState::skipped) ++plan.skips;
			if (row.replace) ++plan.replacements;
			if (row.replace || row.change.starts_with(L"Auto-renamed") || row.change.starts_with(L"Skip -") ||
				(row.state == tasks::RowState::blocked && !row.destination.empty())) ++plan.collisions;
		}
		if (plan.blockReason.empty() && !plan.count(tasks::RowState::ready))
			plan.blockReason = plan.rows.empty() ? L"Nothing to rename." : L"Every item is unchanged or would be skipped.";
		return plan;
	}

	tasks::TaskPlan direct_plan(const std::filesystem::path& source, const std::wstring_view filename)
	{
		auto plan = analyze({source}, L"{name}", 1, tasks::CollisionPolicy::block);
		if (plan.rows.size() != 1 || plan.rows.front().state == tasks::RowState::blocked) return plan;
		auto& row = plan.rows.front();
		if (const auto problem = paths::validate_file_name(filename);
			problem != paths::NameProblem::none || filename.size() > 255)
		{
			plan.blockReason = problem == paths::NameProblem::none ?
				L"The new name exceeds 255 characters." : paths::describe(problem);
			row.state = tasks::RowState::blocked;
			return plan;
		}
		row.destination = row.source.parent_path() / filename;
		auto review = std::make_shared<Reviewed>(*std::static_pointer_cast<const Reviewed>(row.context));
		review->destinations = bundle(row, row.destination);
		review->destinationState.clear();
		plan.blockReason.clear();
		row.state = unchanged(row) ? tasks::RowState::skipped : tasks::RowState::ready;
		row.change = row.state == tasks::RowState::skipped ? L"Unchanged" : row.destination.filename().wstring();
		PathSet claimed;
		for (size_t index = 0; index < review->destinations.size(); ++index)
		{
			const auto& target = review->destinations[index];
			const auto current = Snapshot::read(target);
			review->destinationState.push_back(current);
			if (!current.valid || !claimed.insert(target.native()).second ||
				(current.exists && !paths::equal(target, review->sources[index])))
			{
				row.state = tasks::RowState::blocked;
				plan.blockReason = L"The new filename or a companion destination is occupied or cannot be inspected.";
			}
		}
		row.context = std::move(review);
		return plan;
	}

	tasks::RunOptions run_options(const tasks::TaskPlan& plan)
	{
		struct RunState
		{
			Staging staging;
			std::vector<tasks::TaskRow> rows;
			PathSet active;
			std::map<std::wstring, Snapshot, tasks::detail::PathLess> originalState;
			std::vector<std::filesystem::path> mutatedPaths;
			bool checked{};
			bool aborted{};
		};
		auto state = std::make_shared<RunState>();
		state->rows = plan.rows;
		state->aborted = !plan.can_run();
		for (const auto& row : plan.rows)
		{
			if (const auto review = std::static_pointer_cast<const Reviewed>(row.context))
				for (size_t index = 0; index < review->sources.size(); ++index)
					state->originalState.emplace(review->sources[index].native(), review->sourceState[index]);
			if (row.state != tasks::RowState::ready) continue;
			state->staging.register_bundle(row.source, row.sidecars);
			state->active.insert(row.source.native());
			for (const auto& sidecar : row.sidecars) state->active.insert(sidecar.native());
		}
		tasks::RunOptions options;
		options.undoLabel = L"Batch Rename";
		options.undoAllBeforeRun = true;
		options.undoMutatedPaths = [state](const tasks::TaskRow&) { return state->mutatedPaths; };
		options.undoPaths = [](const tasks::TaskRow& row)
		{
			auto result = bundle(row, row.source);
			const auto destinations = bundle(row, row.destination);
			result.insert(result.end(), destinations.begin(), destinations.end());
			return result;
		};
		options.revalidate = [state](tasks::TaskRow& row)
		{
			if (!state->checked)
			{
				state->checked = true;
				for (const auto& item : state->rows)
				{
					const auto reviewed = std::static_pointer_cast<const Reviewed>(item.context);
					if (!reviewed || !paths::equal(reviewed->parent, canonical_parent(item.source)))
					{
						state->aborted = true;
						break;
					}
					for (size_t index = 0; index < reviewed->sources.size(); ++index)
						if (!reviewed->sourceState[index].same(Snapshot::read(reviewed->sources[index])))
							state->aborted = true;
					PathSet companions;
					if (!(reviewed->sourceState.front().attributes & FILE_ATTRIBUTE_DIRECTORY))
						for (const auto& sidecar : files::sidecar_paths(item.source)) companions.insert(sidecar.native());
					PathSet expected;
					for (const auto& sidecar : item.sidecars) expected.insert(sidecar.native());
					if (companions != expected) state->aborted = true;
					if (state->aborted) break;
				}
			}
			if (state->aborted)
			{
				row.detail = L"Files or companions changed after Review. Analyze again.";
				return false;
			}
			const auto reviewed = std::static_pointer_cast<const Reviewed>(row.context);
			bool valid = reviewed && paths::equal(reviewed->parent, canonical_parent(row.source));
			if (valid)
			{
				for (size_t index = 0; index < reviewed->sources.size(); ++index)
					if (!reviewed->sourceState[index].same(Snapshot::read(state->staging.actual(reviewed->sources[index]))))
						valid = false;
				if (!(reviewed->sourceState.front().attributes & FILE_ATTRIBUTE_DIRECTORY) &&
					paths::equal(state->staging.actual(row.source), row.source))
				{
					PathSet current, expected;
					for (const auto& sidecar : files::sidecar_paths(row.source)) current.insert(sidecar.native());
					for (const auto& sidecar : row.sidecars) expected.insert(sidecar.native());
					if (current != expected) valid = false;
				}
				for (const auto& target : reviewed->destinations)
				{
					// Ordinary destinations are checked by rename_row so a newly occupied primary
					// skips only its row, while a newly occupied companion fails that row.
					if (!state->active.contains(target.native())) continue;
					// A participant may already be staged; its original name must then be free.
					if (!paths::equal(state->staging.actual(target), target) && Snapshot::read(target).exists)
						valid = false;
					const auto expected = state->originalState.find(target.native());
					if (expected == state->originalState.end() ||
						!expected->second.same(Snapshot::read(state->staging.actual(target))))
						valid = false;
				}
			}
			if (!valid)
			{
				row.detail = L"The item or a companion changed after Review. Analyze again.";
				state->active.erase(row.source.native());
				for (const auto& sidecar : row.sidecars) state->active.erase(sidecar.native());
			}
			return valid;
		};
		options.act = [state](tasks::TaskRow& row)
		{
			const auto release = [&]
			{
				state->active.erase(row.source.native());
				for (const auto& sidecar : row.sidecars) state->active.erase(sidecar.native());
			};
			try
			{
				rename_row(row, state->staging, [state](const auto& path) { return state->active.contains(path.native()); },
					&state->mutatedPaths);
			}
			catch (...)
			{
				release();
				throw;
			}
			release();
		};
		options.finish = [state](tasks::TaskPlan& result)
		{
			for (const auto& recovery : state->staging.restore())
			{
				auto owner = std::ranges::find_if(result.rows, [&](const tasks::TaskRow& row)
				{
					return paths::equal(row.source, recovery.original) ||
						std::ranges::any_of(row.sidecars, [&](const auto& path) { return paths::equal(path, recovery.original); });
				});
				if (owner == result.rows.end())
					owner = std::ranges::find_if(result.rows, [&](const tasks::TaskRow& row)
					{
						const auto reviewed = std::static_pointer_cast<const Reviewed>(row.context);
						return reviewed && std::ranges::any_of(reviewed->destinations,
							[&](const auto& path) { return paths::equal(path, recovery.original); });
					});
				if (owner == result.rows.end()) continue;
				if (!paths::equal(recovery.original, recovery.recovered) || recovery.failed)
					owner->detail += std::format(L" {} at {}.", recovery.failed ? L"Recovery required; item retained" :
						L"Unfinished item recovered", recovery.recovered.wstring());
				if (recovery.failed) owner->state = tasks::RowState::failed;
			}
		};
		return options;
	}
}

namespace iw
{
	std::wstring TaskRename::template_text() const
	{
		if (templateInput_) return templateInput_->text();
		const auto* control = panel_.find(idTemplate);
		return control ? control->text : std::wstring{};
	}

	void TaskRename::set_template(std::wstring text)
	{
		if (auto* control = panel_.find(idTemplate)) control->text = text;
		if (templateInput_) templateInput_->set_text(text);
		controls_changed();
	}

	void TaskRename::build_controls()
	{
		const auto changed = [this] { controls_changed(); };

		ui::Control description;
		description.id = idDescription;
		description.kind = ui::ControlKind::label;
		description.label =
			L"Every selected item is renamed in place. Review the new names before you run.";
		panel_.add(std::move(description));

		ui::Control heading;
		heading.id = idTemplateHeading;
		heading.kind = ui::ControlKind::heading;
		heading.label = L"Template";
		panel_.add(std::move(heading));

		ui::Control templateControl;
		templateControl.id = idTemplate;
		templateControl.kind = ui::ControlKind::text;
		templateControl.label = L"Name template: # is the number, {name} is the old name";
		templateControl.text = L"file-###";
		panel_.add(std::move(templateControl));

		ui::Control start;
		start.id = idStart;
		start.kind = ui::ControlKind::text;
		start.label = L"Start at";
		start.minimum = 0;
		start.maximum = 999999;
		start.value = 1;
		start.text = L"1";
		start.changed = changed;
		panel_.add(std::move(start));

		const auto suggestion = [this](const int id, const wchar_t* text)
		{
			ui::Control control;
			control.id = id;
			control.kind = ui::ControlKind::button;
			control.label = text;
			panel_.add(std::move(control));
		};
		suggestion(idSuggestFile, L"file-###");
		suggestion(idSuggestCreated, L"{created}-###");
		suggestion(idSuggestYearMonth, L"{year}-{month}-###");
		suggestion(idSuggestName, L"{name}-###");

		ui::Control help;
		help.id = idHelp;
		help.kind = ui::ControlKind::link;
		help.label = L"More about name templates";
		panel_.add(std::move(help));

		ui::Control collisionHeading;
		collisionHeading.id = idCollisionHeading;
		collisionHeading.kind = ui::ControlKind::heading;
		collisionHeading.label = L"If the new name already exists";
		panel_.add(std::move(collisionHeading));

		ui::Control collision;
		collision.id = idCollision;
		collision.kind = ui::ControlKind::choice;
		collision.label = L"Existing names";
		collision.choices = {L"Block run", L"Skip", L"Auto-rename", L"Replace"};
		collision.changed = changed;
		panel_.add(std::move(collision));

		if (!startInput_ && frame_)
		{
			platform::TextInputOptions options;
			options.text = L"1";
			options.cueBanner = L"Starting sequence number";
			options.changed = [this](const std::wstring& text)
			{
				if (auto* control = panel_.find(idStart)) control->text = text;
				controls_changed();
			};
			bind_text_input(idStart, options);
			startInput_ = platform::create_text_input(frame_, std::move(options));
			if (startInput_) startInput_->set_font(platform::create_message_font(dpi_));
		}
		if (!templateInput_ && frame_)
		{
			platform::TextInputOptions options;
			options.text = L"file-###";
			options.cueBanner = L"file-###";
			options.changed = [this](const std::wstring& text)
			{
				if (auto* control = panel_.find(idTemplate)) control->text = text;
				controls_changed();
			};
			bind_text_input(idTemplate, options);
			templateInput_ = platform::create_text_input(frame_, std::move(options));
			if (templateInput_)
			{
				templateInput_->set_font(platform::create_message_font(dpi_));
				templateInput_->show(true);
			}
		}
	}

	void TaskRename::control_activated(ui::Control& control)
	{
		switch (control.id)
		{
		case idTemplate: if (templateInput_) templateInput_->set_focus(); return;
		case idStart: if (startInput_) startInput_->set_focus(); return;
		case idSuggestFile: set_template(L"file-###"); return;
		case idSuggestCreated: set_template(L"{created}-###"); return;
		case idSuggestYearMonth: set_template(L"{year}-{month}-###"); return;
		case idSuggestName: set_template(L"{name}-###"); return;
		case idHelp:
		{
			platform::ChoiceDefinition help;
			help.title = L"Rename templates";
			help.heading = L"Build a name from text, numbers, and metadata";
			help.message = L"Use # for a sequence number; ### pads it to at least three digits.\n\n"
				L"{name} keeps the old name without its file extension. File extensions are kept automatically; "
				L"a folder's dots remain part of its name.\n\n"
				L"Date tokens: {created}, {year}, {month}, {day}, {time}.\n"
				L"Other tokens: {folder}, {camera}, {iso}, {aperture}, {exposure}, {focal-length}.\n\n"
				L"Missing metadata and unknown tokens expand to empty text. Always review the exact names before Rename files.";
			help.buttons = {{1, L"OK"}};
			help.defaultButton = 1;
			platform::show_choice(frame_, help);
			return;
		}
		default: break;
		}
		TaskView::control_activated(control);
	}

	void TaskRename::layout_hosted_controls()
	{
		const auto layout = [this](const platform::TextInputPtr& input, const int id)
		{
			if (!input) return;
			const auto bounds = panel_.text_bounds(id);
			const bool visible = bounds.width > 0 && bounds.height > 0 && panel_.interactive();
			input->show(visible);
			if (visible)
			{
				input->set_bounds(bounds);
				if (id == idTemplate && templateNeedsFocus_)
				{
					input->set_focus();
					templateNeedsFocus_ = !input->has_focus();
				}
			}
		};
		layout(templateInput_, idTemplate);
		layout(startInput_, idStart);
	}

	void TaskRename::work_state_changed(const bool busy)
	{
		if (templateInput_) templateInput_->show(!busy);
		if (startInput_) startInput_->show(!busy);
		if (!busy) layout_hosted_controls();
	}

	std::vector<TaskView::Column> TaskRename::review_columns() const
	{
		return {{L"Old name", 4}, {L"", 1}, {L"New name", 4}};
	}

	std::wstring TaskRename::review_cell(const tasks::TaskRow& row, const size_t column) const
	{
		switch (column)
		{
		case 0: return row.source.filename().wstring();
		case 1:
			// A no-op row has no direction arrow.
			return row.source.native() == row.destination.native() || row.destination.empty() ? std::wstring{} : L"\u2192";
		default:
			if (finished_ || (row.state != tasks::RowState::ready && row.state != tasks::RowState::blocked &&
				row.state != tasks::RowState::skipped && row.state != tasks::RowState::pending))
				return row.destination.filename().wstring() + L" - " + TaskView::review_cell(row, 2);
			if (row.destination.empty()) return row.change;
			return row.destination.filename().wstring() + (row.change.empty() ? L"" : L" - " + row.change);
		}
	}

	tasks::TaskRunner::AnalyzeFunction TaskRename::make_analyzer()
	{
		std::wstring templateText = template_text();
		int start = 1;
		if (const auto* control = panel_.find(idStart))
		{
			const auto text = startInput_ ? startInput_->text() : control->text;
			start = text.empty() ? -1 : 0;
			for (const auto digit : text)
			{
				if (digit < L'0' || digit > L'9' ||
					start > ((std::numeric_limits<int>::max)() - (digit - L'0')) / 10)
				{
					start = -1;
					break;
				}
				start = start * 10 + digit - L'0';
			}
		}
		auto policy = tasks::CollisionPolicy::block;
		if (const auto* control = panel_.find(idCollision)) policy = policy_from_choice(control->value);
		auto sources = targets();

		return [templateText, start, policy, sources = std::move(sources)](
			const std::stop_token& stop, const tasks::ProgressFunction& progress)
		{
			return rename::analyze(sources, templateText, start, policy, stop, progress);
		};
	}

	tasks::RunOptions TaskRename::build_run_options()
	{
		return rename::run_options(plan_);
	}
}

// ImageWalker by Zac Walker
// Implements Synchronize: explicit analysis, immutable reviewed evidence, and permanent deletion.

#include "TaskSync.h"

#include "Paths.h"

#include <algorithm>
#include <array>
#include <format>
#include <stdexcept>
#include <utility>

namespace iw::sync
{
	namespace
	{
		using EntryMap = std::map<std::wstring, FileState, tasks::detail::PathLess>;

		struct Tree
		{
			EntryMap files;
			tasks::detail::PathSet directories;
			std::wstring error;
		};

		bool missing(const std::error_code& error)
		{
			return error == std::errc::no_such_file_or_directory;
		}

		bool ordinary_path(const std::filesystem::path& path, const bool allowMissing)
		{
			// Check every ancestor, not just the leaf: a directory junction redirects ordinary files too.
			for (auto current = path; !current.empty();)
			{
				const auto attributes = platform::read_file_attributes(current);
				if (!attributes.known)
				{
					std::error_code error;
					const auto status = std::filesystem::symlink_status(files::native_path(current), error);
					if (!allowMissing || (!missing(error) && (error || std::filesystem::exists(status)))) return false;
				}
				else if (attributes.reparse) return false;
				const auto parent = current.parent_path();
				if (parent == current) break;
				current = parent;
			}
			return true;
		}

		bool read_state(const std::filesystem::path& path, FileState& state)
		{
			state = {};
			if (!ordinary_path(path, true)) return false;
			const auto snapshot = files::snapshot_file(path);
			if (!snapshot || snapshot->directory) return false;
			state = *snapshot;
			return true;
		}

		Tree walk(const std::filesystem::path& root, const std::stop_token& stop,
		          const tasks::ProgressFunction& progress, size_t& scanned)
		{
			Tree result;
			std::vector<std::filesystem::path> pending{root};
			while (!pending.empty() && !stop.stop_requested())
			{
				const auto folder = pending.back();
				pending.pop_back();
				if (!ordinary_path(folder, false))
				{
					result.error = std::format(L"Cannot safely read {}: unreadable folder or reparse point.", folder.wstring());
					return result;
				}
				std::error_code error;
				std::filesystem::directory_iterator iterator(files::native_path(folder), error);
				if (error)
				{
					result.error = std::format(L"Could not read folder {}.", folder.wstring());
					return result;
				}
				const std::filesystem::directory_iterator end;
				while (iterator != end && !stop.stop_requested())
				{
					const auto path = folder / iterator->path().filename();
					const auto relative = path.lexically_relative(root).wstring();
					const auto attributes = platform::read_file_attributes(path);
					if (!attributes.known || attributes.reparse)
					{
						result.error = std::format(L"Cannot safely read {}: unreadable entry or reparse point.", path.wstring());
						return result;
					}
					++scanned;
					if (attributes.directory)
					{
						if (!result.directories.insert(relative).second || result.files.contains(relative))
						{
							result.error = L"Folder names map ambiguously when compared without case.";
							return result;
						}
						pending.push_back(path);
					}
					else
					{
						FileState record;
						if (!read_state(path, record) || !record.exists)
						{
							result.error = std::format(L"Could not read file metadata for {}.", path.wstring());
							return result;
						}
						if (!result.files.emplace(relative, record).second || result.directories.contains(relative))
						{
							result.error = L"File names map ambiguously when compared without case.";
							return result;
						}
					}
					if (progress) progress(scanned, 0);
					iterator.increment(error);
					if (error)
					{
						result.error = std::format(L"Could not finish reading folder {}.", folder.wstring());
						return result;
					}
				}
			}
			return result;
		}

		std::filesystem::path normalized(const std::filesystem::path& path)
		{
			auto result = path.lexically_normal();
			if (!result.has_filename() && result != result.root_path()) result = result.parent_path();
			return result;
		}

		std::vector<std::filesystem::path> roots(const Options& options)
		{
			auto result = options.wholeCollection ? options.collection : std::vector{options.local};
			for (auto& root : result) root = normalized(root);
			return result;
		}

		std::filesystem::path mapping_name(const std::filesystem::path& root)
		{
			if (!root.filename().empty()) return root.filename();
			auto name = root.root_name().wstring();
			if (name.size() == 2 && name.back() == L':') name.pop_back();
			return name;
		}

		bool overlap(const std::filesystem::path& left, const std::filesystem::path& right)
		{
			return paths::equal(left, right) || paths::contains(left, right) || paths::contains(right, left);
		}

		bool physical_overlap(const std::filesystem::path& left, const std::filesystem::path& right)
		{
			const auto containsSameDirectory = [](std::filesystem::path child, const std::filesystem::path& parent)
			{
				for (;;)
				{
					std::error_code error;
					if (std::filesystem::equivalent(files::native_path(child), files::native_path(parent), error) && !error)
						return true;
					const auto next = child.parent_path();
					if (next.empty() || next == child) return false;
					child = next;
				}
			};
			return containsSameDirectory(left, right) || containsSameDirectory(right, left);
		}

		bool readable_directory(const std::filesystem::path& root, const files::FileSnapshot& expected)
		{
			if (!ordinary_path(root, false)) return false;
			const auto current = files::snapshot_file(root);
			if (!current || !current->exists || !current->directory ||
				current->identity != expected.identity || current->volume != expected.volume) return false;
			std::error_code error;
			if (!std::filesystem::is_directory(files::native_path(root), error) || error) return false;
			const std::filesystem::directory_iterator iterator(files::native_path(root), error);
			return !error;
		}

		std::pair<std::filesystem::path, FileState> existing_parent(
			const std::filesystem::path& path, const std::filesystem::path& root)
		{
			for (auto current = path.parent_path();;)
			{
				const auto state = files::snapshot_file(current);
				if (!state) return {};
				if (state->exists) return state->directory
					                          ? std::pair{current, *state}
					                          : std::pair<std::filesystem::path, FileState>{};
				if (paths::equal(current, root)) return {};
				const auto parent = current.parent_path();
				if (parent.empty() || parent == current ||
					(!paths::equal(parent, root) && !paths::contains(root, parent))) return {};
				current = parent;
			}
		}

		const wchar_t* action_text(const int action, const bool replace)
		{
			switch (action)
			{
			case copyToRemote: return replace ? L"Copy to remote (Replace)" : L"Copy to remote";
			case copyToLocal: return replace ? L"Copy to local (Replace)" : L"Copy to local";
			case deleteLocal: return L"Delete local";
			case deleteRemote: return L"Delete remote";
			default: return L"Ignore";
			}
		}
	}

	Options load_options(const std::wstring_view section)
	{
		Options options;
		if (section.empty()) return options;
		options.wholeCollection = platform::read_integer_setting(section, L"WholeCollection", 1) != 0;
		options.local = platform::read_text_setting(section, L"Local");
		options.remote = platform::read_text_setting(section, L"Remote");
		options.toRemote = platform::read_integer_setting(section, L"CopyToRemote", 1) != 0;
		options.toLocal = platform::read_integer_setting(section, L"CopyToLocal", 0) != 0;
		options.deleteLocal = platform::read_integer_setting(section, L"DeleteLocal", 0) != 0;
		options.deleteRemote = platform::read_integer_setting(section, L"DeleteRemote", 0) != 0;
		const int count = (std::max)(0, platform::read_integer_setting(section, L"CollectionCount", 0));
		for (int index = 0; index < count; ++index)
		{
			options.collection.emplace_back(platform::read_text_setting(section, std::format(L"Collection{}", index)));
			// Empty entries are retained as invalid roots. In particular, one damaged entry must
			// not silently discard later configured roots and reduce deletion authority.
		}
		return options;
	}

	void save_options(const Options& options, const std::wstring_view section)
	{
		if (section.empty()) return;
		const std::array integers{
			platform::IntegerSetting{L"WholeCollection", options.wholeCollection ? 1 : 0},
			platform::IntegerSetting{L"CopyToRemote", options.toRemote ? 1 : 0},
			platform::IntegerSetting{L"CopyToLocal", options.toLocal ? 1 : 0},
			platform::IntegerSetting{L"DeleteLocal", options.deleteLocal ? 1 : 0},
			platform::IntegerSetting{L"DeleteRemote", options.deleteRemote ? 1 : 0},
			platform::IntegerSetting{L"CollectionCount", static_cast<int>(options.collection.size())}};
		std::vector texts{
			platform::TextSetting{L"Local", options.local.wstring()},
			platform::TextSetting{L"Remote", options.remote.wstring()}};
		const int previousCount = (std::max)(0, platform::read_integer_setting(section, L"CollectionCount", 0));
		for (size_t index = 0; index < options.collection.size(); ++index)
			texts.push_back({std::format(L"Collection{}", index), options.collection[index].wstring()});
		for (size_t index = options.collection.size(); index < static_cast<size_t>(previousCount); ++index)
		{
			const auto key = std::format(L"Collection{}", index);
			texts.push_back({key, {}});
		}
		platform::write_text_settings(section, texts);
		platform::write_integer_settings(section, integers);
	}

	std::wstring configuration_error(const Options& options)
	{
		const auto localRoots = roots(options);
		if (options.remote.empty()) return L"Choose a remote folder.";
		if (options.wholeCollection && localRoots.empty())
			return L"Add at least one folder to the saved Sync collection.";
		if (!options.wholeCollection && options.local.empty())
			return L"Choose a valid local folder scope.";
		if (!options.remote.is_absolute()) return L"The remote folder must be an absolute path.";
		const auto remote = normalized(options.remote);
		tasks::detail::PathSet mapped;
		for (size_t index = 0; index < localRoots.size(); ++index)
		{
			const auto& root = localRoots[index];
			if (root.empty() || !root.is_absolute()) return L"Every local folder must be an absolute path.";
			if (overlap(root, remote)) return L"The local and remote folders overlap. Choose separate trees.";
			for (size_t previous = 0; previous < index; ++previous)
				if (overlap(root, localRoots[previous]))
					return L"The local collection roots overlap and map ambiguously. Choose separate roots.";
			if (options.wholeCollection)
			{
				const auto mappedName = mapping_name(root).wstring();
				if (paths::validate_file_name(mappedName) != paths::NameProblem::none)
					return L"A collection root has no safe remote folder name. Select a named local folder.";
				if (!mapped.insert(mappedName).second)
					return L"A remote path maps ambiguously to more than one local root. Use distinct collection folder names.";
			}
			if (!ordinary_path(root, false))
				return std::format(L"The local folder is unreadable or contains a reparse point: {}.", root.wstring());
			std::error_code error;
			if (!std::filesystem::is_directory(files::native_path(root), error) || error)
				return std::format(L"The local folder could not be read: {}.", root.wstring());
			if (physical_overlap(root, remote))
				return L"The local and remote folders overlap through a filesystem alias. Choose separate trees.";
			for (size_t previous = 0; previous < index; ++previous)
				if (physical_overlap(root, localRoots[previous]))
					return L"The local collection roots overlap through a filesystem alias and map ambiguously.";
		}
		if (!ordinary_path(remote, false)) return L"The remote folder is unreadable or contains a reparse point.";
		std::error_code error;
		if (!std::filesystem::is_directory(files::native_path(remote), error) || error)
			return L"The remote folder could not be read.";
		if (!options.toRemote && !options.toLocal && !options.deleteLocal && !options.deleteRemote)
			return L"Choose at least one copy direction or deletion.";
		return {};
	}

	Review analyze(const Options& options, const std::stop_token& stop, const tasks::ProgressFunction& progress)
	{
		Review review;
		auto& plan = review.plan;
		const auto cancel = [&]
		{
			plan = {};
			plan.blockReason = L"Analysis was cancelled.";
			review.snapshots.clear();
		};
		if (stop.stop_requested()) { cancel(); return review; }
		plan.blockReason = configuration_error(options);
		if (!plan.blockReason.empty()) return review;

		size_t scanned = 0;
		const auto remoteRoot = normalized(options.remote);
		const auto remoteRootState = files::snapshot_file(remoteRoot);
		if (!remoteRootState || !remoteRootState->exists || !remoteRootState->directory)
		{
			plan.blockReason = L"The remote folder identity could not be read.";
			return review;
		}
		const auto remoteTree = walk(remoteRoot, stop, progress, scanned);
		if (stop.stop_requested()) { cancel(); return review; }
		if (!remoteTree.error.empty()) { plan.blockReason = remoteTree.error; return review; }

		tasks::detail::PathSet comparedRemote;
		for (const auto& localRoot : roots(options))
		{
			const auto localRootState = files::snapshot_file(localRoot);
			if (!localRootState || !localRootState->exists || !localRootState->directory)
			{
				plan.rows.clear();
				review.snapshots.clear();
				plan.blockReason = L"A local folder identity could not be read.";
				return review;
			}
			const auto localTree = walk(localRoot, stop, progress, scanned);
			if (stop.stop_requested()) { cancel(); return review; }
			if (!localTree.error.empty())
			{
				plan.rows.clear();
				review.snapshots.clear();
				plan.blockReason = localTree.error;
				return review;
			}
			const auto prefix = options.wholeCollection ? mapping_name(localRoot) : std::filesystem::path{};
			if (!prefix.empty() && remoteTree.files.contains(prefix.wstring()))
			{
				plan.blockReason = L"A remote file occupies a collection folder name. Choose another remote folder.";
				return review;
			}
			EntryMap remoteFiles;
			for (const auto& [name, state] : remoteTree.files)
			{
				const std::filesystem::path relative{name};
				if (prefix.empty()) remoteFiles.emplace(name, state);
				else if (paths::contains(prefix, relative))
				{
					std::filesystem::path withinRoot;
					auto component = relative.begin();
					for (++component; component != relative.end(); ++component) withinRoot /= *component;
					remoteFiles.emplace(withinRoot.wstring(), state);
				}
			}
			tasks::detail::PathSet names;
			for (const auto& [name, state] : localTree.files) names.insert(name);
			for (const auto& [name, state] : remoteFiles) names.insert(name);
			for (const auto& name : names)
			{
				if (stop.stop_requested()) { cancel(); return review; }
				const auto remoteName = (prefix / name).wstring();
				comparedRemote.insert(remoteName);
				const auto left = localTree.files.find(name);
				const auto right = remoteFiles.find(name);
				const FileState local = left == localTree.files.end() ? FileState{} : left->second;
				const FileState remote = right == remoteFiles.end() ? FileState{} : right->second;
				tasks::TaskRow row;
				row.source = localRoot / name;
				row.destination = remoteRoot / remoteName;
				row.tag = ignore;
				if (local.exists && !remote.exists)
					row.tag = options.toRemote ? copyToRemote : options.deleteLocal ? deleteLocal : ignore;
				else if (!local.exists && remote.exists)
					row.tag = options.toLocal ? copyToLocal : options.deleteRemote ? deleteRemote : ignore;
				else if (local.modified == remote.modified)
				{
					if (local.size != remote.size && options.toRemote != options.toLocal)
						row.tag = options.toRemote ? copyToRemote : copyToLocal;
				}
				else if (local.modified > remote.modified && options.toRemote) row.tag = copyToRemote;
				else if (remote.modified > local.modified && options.toLocal) row.tag = copyToLocal;
				row.replace = (row.tag == copyToRemote || row.tag == copyToLocal) && local.exists && remote.exists;
				row.state = row.tag == ignore ? tasks::RowState::skipped : tasks::RowState::ready;
				row.change = action_text(row.tag, row.replace);
				// A directory is not an absent file: even a delete requires a truly absent counterpart.
				if (localTree.directories.contains(name) || remoteTree.directories.contains(remoteName))
				{
					row.state = tasks::RowState::blocked;
					row.change = L"File/folder conflict";
					row.detail = L"A file maps to a folder on the other side. Choose separate destinations.";
					plan.blockReason = row.detail;
				}
				const bool readOnlyTarget =
					(row.tag == copyToRemote && row.replace && remote.readOnly) ||
					(row.tag == copyToLocal && row.replace && local.readOnly);
				if (readOnlyTarget)
				{
					row.state = tasks::RowState::blocked;
					row.change = L"Read-only target";
					row.detail = L"A reviewed replacement target is read-only.";
					plan.blockReason = row.detail;
				}
				if (row.replace) ++plan.replacements;
				const auto [localParent, localParentState] = existing_parent(row.source, localRoot);
				const auto [remoteParent, remoteParentState] = existing_parent(row.destination, remoteRoot);
				if (localParent.empty() || remoteParent.empty())
				{
					row.state = tasks::RowState::blocked;
					row.change = L"Unreadable folder";
					row.detail = L"A source or destination parent folder could not be reviewed safely.";
					plan.blockReason = row.detail;
				}
				review.snapshots.emplace(row.source.wstring(),
					RowSnapshot{row, local, remote, localRoot, remoteRoot, *localRootState, *remoteRootState,
						localParent, remoteParent, localParentState, remoteParentState});
				plan.rows.push_back(std::move(row));
			}
		}
		// Collection backup folders with no local root have no unambiguous destination or delete authority.
		for (const auto& [name, state] : remoteTree.files)
		{
			if (stop.stop_requested()) { cancel(); return review; }
			if (comparedRemote.contains(name)) continue;
			tasks::TaskRow row;
			row.destination = remoteRoot / name;
			row.state = tasks::RowState::skipped;
			row.change = L"Ignore - outside collection roots";
			plan.rows.push_back(std::move(row));
		}
		plan.skips = plan.count(tasks::RowState::skipped);
		if (plan.blockReason.empty() && plan.count(tasks::RowState::ready) == 0)
			plan.blockReason = L"Nothing to synchronize with these options.";
		if (progress) progress(plan.rows.size(), plan.rows.size());
		if (stop.stop_requested()) cancel();
		return review;
	}

	tasks::RunOptions run_options(std::shared_ptr<const Review> review)
	{
		tasks::RunOptions options;
		options.undoLabel = L"Synchronize";
		options.undoPaths = [](const tasks::TaskRow& row)
		{
			return std::vector<std::filesystem::path>{
				row.tag == copyToLocal || row.tag == deleteLocal ? row.source : row.destination};
		};
		options.revalidate = [review](tasks::TaskRow& row)
		{
			const auto refuse = [&row]
			{
				row.detail = L"Files changed after analysis. Analyze again before synchronizing this row.";
				return false;
			};
			if (!review || !review->plan.can_run()) return refuse();
			const auto found = review->snapshots.find(row.source.wstring());
			if (found == review->snapshots.end()) return refuse();
			const auto& snapshot = found->second;
			if (!paths::equal(row.source, snapshot.row.source) ||
				row.tag != snapshot.row.tag || row.replace != snapshot.row.replace ||
				!paths::equal(row.destination, snapshot.row.destination)) return refuse();
			if (!readable_directory(snapshot.localRoot, snapshot.localRootState) ||
				!readable_directory(snapshot.remoteRoot, snapshot.remoteRootState) ||
				!readable_directory(snapshot.localParent, snapshot.localParentState) ||
				!readable_directory(snapshot.remoteParent, snapshot.remoteParentState)) return refuse();
			FileState local, remote;
			if (!read_state(row.source, local) || !read_state(row.destination, remote) ||
				local != snapshot.local || remote != snapshot.remote) return refuse();
			return true;
		};
		const auto revalidate = options.revalidate;
		options.act = [revalidate](tasks::TaskRow& row)
		{
			if (!revalidate(row)) { row.state = tasks::RowState::failed; return; }
			switch (row.tag)
			{
			case copyToRemote:
			case copyToLocal:
			{
				const auto& source = row.tag == copyToRemote ? row.source : row.destination;
				const auto& target = row.tag == copyToRemote ? row.destination : row.source;
				if (const auto failure = files::copy_file_to(source, target, row.replace,
					[&] { return revalidate(row); }))
				{
					if (row.detail.empty()) throw std::runtime_error(failure.message());
					row.state = tasks::RowState::failed;
					return;
				}
				row.change = row.tag == copyToRemote ? L"Copied to remote" : L"Copied to local";
				return;
			}
			case deleteLocal:
			case deleteRemote:
			{
				const std::array targets{row.tag == deleteLocal ? row.source : row.destination};
				std::error_code error;
				if (!platform::delete_permanently(targets, error))
					throw std::runtime_error(error ? error.message() : "The file could not be deleted.");
				row.change = row.tag == deleteLocal ? L"Deleted local" : L"Deleted remote";
				return;
			}
			default: row.state = tasks::RowState::skipped;
			}
		};
		return options;
	}

	std::wstring summarize(const tasks::TaskPlan& plan)
	{
		std::array<size_t, 5> counts{};
		for (const auto& row : plan.rows)
			if (row.tag >= ignore && row.tag <= deleteRemote) ++counts[static_cast<size_t>(row.tag)];
		return std::format(L"{} copy to local, {} copy to remote, {} delete local, {} delete remote, {} ignore, {} replacements",
			counts[copyToLocal], counts[copyToRemote], counts[deleteLocal], counts[deleteRemote],
			counts[ignore], plan.replacements);
	}

	platform::ChoiceDefinition deletion_confirmation(const tasks::TaskPlan& plan)
	{
		size_t local = 0, remote = 0;
		for (const auto& row : plan.rows)
		{
			if (row.state != tasks::RowState::ready) continue;
			if (row.tag == deleteLocal) ++local;
			else if (row.tag == deleteRemote) ++remote;
		}
		platform::ChoiceDefinition definition;
		if (!local && !remote) return definition;
		definition.title = L"Synchronize";
		const auto total = local + remote;
		definition.heading = std::format(L"Synchronization will permanently delete {} file{}.",
			total, total == 1 ? L"" : L"s");
		definition.message = std::format(
			L"These files cannot be recovered from the Recycle Bin.\n\n{} local file{} and {} remote file{} will be deleted."
			L"\n\nImageWalker secures recovery copies first. After closing this task, Undo can restore completed changes during this session.",
			local, local == 1 ? L"" : L"s", remote, remote == 1 ? L"" : L"s");
		definition.buttons = {{1, L"Synchronize"}, {2, L"Cancel"}};
		definition.defaultButton = 2;
		definition.warning = true;
		return definition;
	}

	bool confirm_deletions(const tasks::TaskPlan& plan,
	                       const std::function<int(const platform::ChoiceDefinition&)>& confirm)
	{
		if (!plan.can_run() || plan.count(tasks::RowState::ready) == 0) return false;
		const auto definition = deletion_confirmation(plan);
		return definition.buttons.empty() || (confirm && confirm(definition) == 1);
	}

	std::vector<std::filesystem::path> affected_folders(const tasks::TaskPlan& plan)
	{
		tasks::detail::PathSet seen;
		std::vector<std::filesystem::path> folders;
		for (const auto& row : plan.rows)
		{
			if (row.state != tasks::RowState::success) continue;
			if (row.tag != copyToLocal && row.tag != deleteLocal) continue;
			const auto folder = row.source.parent_path();
			if (seen.insert(folder.wstring()).second) folders.push_back(folder);
		}
		return folders;
	}
}

namespace iw
{
	namespace
	{
		enum ControlId
		{
			idDescription = 1, idScopeHeading, idScopeTree, idScopeFolder, idLocal, idRemote,
			idOptionsHeading, idCopyToRemote, idCopyToLocal, idDeleteLocal, idDeleteRemote,
			idCollectionFolders, idAddCollectionFolder, idRemoveCollectionFolder
		};
		constexpr int scopeGroup = 1;
	}

	TaskSync::TaskSync(std::wstring settingsSection)
		: options_(sync::load_options(settingsSection)), settingsSection_(std::move(settingsSection))
	{
	}

	void TaskSync::set_local_folder(std::filesystem::path folder)
	{
		if (!options_.local.empty()) return;
		auto options = options_;
		options.local = std::move(folder);
		set_options(std::move(options));
	}

	void TaskSync::set_collection_folders(std::vector<std::filesystem::path> folders)
	{
		auto options = options_;
		options.collection = std::move(folders);
		set_options(std::move(options));
	}

	void TaskSync::add_collection_folder(std::filesystem::path folder)
	{
		if (folder.empty()) return;
		auto folders = options_.collection;
		collectionSelection_ = folders.size();
		folders.push_back(std::move(folder));
		set_collection_folders(std::move(folders));
	}

	void TaskSync::remove_collection_folder(const size_t index)
	{
		if (index >= options_.collection.size()) return;
		auto folders = options_.collection;
		folders.erase(folders.begin() + static_cast<std::ptrdiff_t>(index));
		set_collection_folders(std::move(folders));
	}

	void TaskSync::set_options(sync::Options options)
	{
		options_ = std::move(options);
		collectionSelection_ = options_.collection.empty() ? 0 :
			(std::min)(collectionSelection_, options_.collection.size() - 1);
		sync::save_options(options_, settingsSection_);
		if (frame_) rebuild_controls();
		review_.reset();
		TaskView::controls_changed();
	}

	void TaskSync::controls_changed()
	{
		const auto checked = [this](const int id)
		{
			const auto* control = panel_.find(id);
			return control && control->checked;
		};
		options_.wholeCollection = checked(idScopeTree);
		options_.toRemote = checked(idCopyToRemote);
		options_.toLocal = checked(idCopyToLocal);
		options_.deleteLocal = checked(idDeleteLocal);
		options_.deleteRemote = checked(idDeleteRemote);
		if (const auto* control = panel_.find(idLocal); control && control->kind == ui::ControlKind::folder)
			options_.local = control->text;
		if (const auto* control = panel_.find(idRemote)) options_.remote = control->text;
		sync::save_options(options_, settingsSection_);
		review_.reset();
		rebuild_controls();
		TaskView::controls_changed();
	}

	void TaskSync::build_controls()
	{
		const auto changed = [this] { controls_changed(); };
		ui::Control description;
		description.id = idDescription;
		description.kind = ui::ControlKind::label;
		description.label = L"Analyze first. Review every copy and deletion before running. Deletions bypass the Recycle Bin; recovery copies are secured for session Undo.";
		panel_.add(std::move(description));
		ui::Control heading;
		heading.id = idScopeHeading;
		heading.kind = ui::ControlKind::heading;
		heading.label = L"Folders";
		panel_.add(std::move(heading));
		ui::Control tree;
		tree.id = idScopeTree;
		tree.kind = ui::ControlKind::radio;
		tree.label = L"Synchronize whole collection";
		tree.group = scopeGroup;
		tree.checked = options_.wholeCollection;
		tree.changed = changed;
		panel_.add(std::move(tree));
		ui::Control single;
		single.id = idScopeFolder;
		single.kind = ui::ControlKind::radio;
		single.label = L"Synchronize only one folder";
		single.group = scopeGroup;
		single.checked = !options_.wholeCollection;
		single.changed = changed;
		panel_.add(std::move(single));
		ui::Control local;
		local.id = idLocal;
		local.kind = options_.wholeCollection ? ui::ControlKind::label : ui::ControlKind::folder;
		local.label = options_.wholeCollection
			? L"Saved Sync collection folders (including all subfolders):"
			: L"Local folder";
		local.text = options_.local.wstring();
		if (options_.wholeCollection)
		{
			for (const auto& folder : options_.collection)
				local.label += std::format(L"\n{} \u2192 Remote\\{}", folder.wstring(),
					sync::mapping_name(sync::normalized(folder)).wstring());
			if (options_.collection.empty()) local.label += L"\nUse Add collection folder to choose the folders to synchronize.";
			else local.label += L"\nRemoving a folder from this list does not delete any files.";
		}
		local.changed = changed;
		panel_.add(std::move(local));
		if (options_.wholeCollection)
		{
			ui::Control add;
			add.id = idAddCollectionFolder;
			add.kind = ui::ControlKind::button;
			add.label = L"Add collection folder...";
			panel_.add(std::move(add));
			ui::Control folders;
			folders.id = idCollectionFolders;
			folders.kind = ui::ControlKind::choice;
			folders.label = L"Collection folder to remove";
			for (const auto& folder : options_.collection) folders.choices.push_back(folder.wstring());
			folders.value = static_cast<int>(collectionSelection_);
			folders.enabled = !options_.collection.empty();
			folders.changed = [this]
			{
				if (const auto* control = panel_.find(idCollectionFolders))
					collectionSelection_ = static_cast<size_t>((std::max)(0, control->value));
			};
			panel_.add(std::move(folders));
			ui::Control remove;
			remove.id = idRemoveCollectionFolder;
			remove.kind = ui::ControlKind::button;
			remove.label = L"Remove selected folder from collection";
			remove.enabled = !options_.collection.empty();
			panel_.add(std::move(remove));
		}
		ui::Control remote;
		remote.id = idRemote;
		remote.kind = ui::ControlKind::folder;
		remote.label = L"Remote folder";
		remote.text = options_.remote.wstring();
		remote.changed = changed;
		panel_.add(std::move(remote));
		ui::Control optionsHeading;
		optionsHeading.id = idOptionsHeading;
		optionsHeading.kind = ui::ControlKind::heading;
		optionsHeading.label = L"What to do";
		panel_.add(std::move(optionsHeading));
		const auto option = [this, &changed](const int id, const wchar_t* text, const bool checked)
		{
			ui::Control control;
			control.id = id;
			control.kind = ui::ControlKind::checkBox;
			control.label = text;
			control.checked = checked;
			control.changed = changed;
			panel_.add(std::move(control));
		};
		option(idCopyToRemote, L"Copy newer local files to remote folders", options_.toRemote);
		option(idCopyToLocal, L"Copy newer remote files to local folders", options_.toLocal);
		option(idDeleteLocal, L"Delete local files that do not exist remotely", options_.deleteLocal);
		option(idDeleteRemote, L"Delete remote files that do not exist locally", options_.deleteRemote);
	}

	void TaskSync::control_activated(ui::Control& control)
	{
		if (control.id == idAddCollectionFolder)
		{
			const auto folder = choose_folder(L"Add a folder to the saved Sync collection");
			if (!folder.empty()) add_collection_folder(folder);
		}
		else if (control.id == idRemoveCollectionFolder) remove_collection_folder(collectionSelection_);
		else TaskView::control_activated(control);
	}

	std::vector<TaskView::Column> TaskSync::review_columns() const
	{
		return {{L"Action", 2}, {L"Local", 3}, {L"", 1}, {L"Remote", 3}};
	}

	std::wstring TaskSync::review_cell(const tasks::TaskRow& row, const size_t column) const
	{
		switch (column)
		{
		case 0:
			if (finished_ || row.state == tasks::RowState::running || row.state == tasks::RowState::success ||
				row.state == tasks::RowState::failed || row.state == tasks::RowState::notRun)
				return TaskView::review_cell(row, 2);
			return row.change;
		case 1: return row.source.wstring();
		case 2:
			switch (row.tag)
			{
			case sync::copyToRemote: return L"\u2192";
			case sync::copyToLocal: return L"\u2190";
			case sync::deleteLocal:
			case sync::deleteRemote: return L"\u2717";
			default: return L"=";
			}
		default: return row.destination.wstring();
		}
	}

	tasks::TaskRunner::AnalyzeFunction TaskSync::make_analyzer()
	{
		review_ = std::make_shared<sync::Review>();
		return [options = options_, review = review_](const std::stop_token& stop,
		                                             const tasks::ProgressFunction& progress)
		{
			*review = sync::analyze(options, stop, progress);
			return review->plan;
		};
	}

	void TaskSync::start_run()
	{
		if (busy() || finished_ || confirming_ || !plan_.can_run() || !review_) return;
		const auto keepAlive = shared_from_this();
		const auto reviewed = review_;
		confirming_ = true;
		const auto confirmed = sync::confirm_deletions(plan_, [this](const platform::ChoiceDefinition& definition)
		{
			return platform::show_choice(frame_, definition);
		});
		confirming_ = false;
		if (!confirmed || reviewed != review_) return;
		TaskView::start_run();
	}

	tasks::RunOptions TaskSync::build_run_options()
	{
		return sync::run_options(review_);
	}
}

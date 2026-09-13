// ImageWalker by Zac Walker
// Implements main-window file dialogs, clipboard commands, and dropped-file UI handling.

#include "Platform.h"
#include "MainWindow.h"

#include "Format.h"
#include "Paths.h"
#include "TaskRename.h"
#include "Undo.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <format>
#include <limits>
#include <map>
#include <set>
#include <system_error>

namespace iw
{
	namespace
	{
		constexpr platform::DialogFieldId renameValue = 1;
		bool has_transfer_report(const platform::WindowFramePtr& owner);
		void show_transfer_report(const platform::WindowFramePtr& owner);
	}

	void MainWindow::open_folder_picker()
	{
		if (task_active())
		{
			platform::show_error(L"Close the current task before opening another folder.", L"Open folder", frame_);
			return;
		}
		if (const auto path = platform::choose_folder(frame_, L"Open folder in ImageWalker"))
			open_folder(*path);
	}

	std::vector<std::filesystem::path> MainWindow::selected_paths() const { return canvas_.selected_paths(); }

	void MainWindow::show_file_menu(const pointi screenPoint)
	{
		CommandContext context = build_command_context();
		// The canvas context menu always targets the canvas selection, whatever holds focus.
		context.focus = FocusTarget::canvas;
		std::vector<platform::PopupItem> items;
		const auto append = [&](std::wstring label, std::function<void()> invoke,
		                         std::function<bool()> enabled = {})
		{
			auto action = std::make_shared<platform::Command>();
			action->invoke = std::move(invoke);
			action->enabled = std::move(enabled);
			items.push_back(platform::PopupItem::action(std::move(action), std::move(label)));
		};
		append(L"Open", [this]
		{
			const int item = canvas_.focused_index();
			if (item >= 0 && static_cast<size_t>(item) < files_.size()) activate_item(files_[item]);
		}, [context] { return context.selectionCount == 1; });
		items.push_back(platform::PopupItem::separator_item());
		append(L"Copy", [this] { copy_selected(false); }, [context] { return context.selectionCount > 0; });
		append(L"Cut", [this] { copy_selected(true); }, [context] { return context.selectionCount > 0; });
		append(L"Paste", [this] { paste_files(); }, [context]
		{
			return context.hasFolder && (context.clipboardHasFiles || context.clipboardHasImage);
		});
		items.push_back(platform::PopupItem::separator_item());
		append(L"Rename", [this] { rename_selected(); }, [context] { return context.selectionCount == 1; });
		append(L"Delete", [this] { delete_selected(); }, [context] { return context.selectionCount > 0; });
		if (has_transfer_report(frame_))
		{
			items.push_back(platform::PopupItem::separator_item());
			append(L"Last Copy/Move results...", [this] { show_transfer_report(frame_); });
		}
		if (fullscreen_)
		{
			items.push_back(platform::PopupItem::separator_item());
			auto properties = std::make_shared<platform::Command>();
			properties->invoke = [this]
			{
				canvas_.set_file_properties_visible(!canvas_.file_properties_visible());
				update_fullscreen_menu_checks();
			};
			properties->checked = [this] { return canvas_.file_properties_visible(); };
			items.push_back(platform::PopupItem::action(std::move(properties), L"File properties"));
			auto thumbnails = std::make_shared<platform::Command>();
			thumbnails->invoke = [this]
			{
				canvas_.set_thumbnail_selector_visible(!canvas_.thumbnail_selector_visible());
				update_fullscreen_menu_checks();
			};
			thumbnails->checked = [this] { return canvas_.thumbnail_selector_visible(); };
			items.push_back(platform::PopupItem::action(std::move(thumbnails), L"Thumbnail selector"));
		}
		platform::show_popup_menu(frame_, screenPoint, items);
	}

	void MainWindow::rename_selected() try
	{
		const auto paths = selected_paths();
		if (paths.size() != 1) return;
		platform::DialogField name;
		name.id = renameValue;
		name.label = L"&New name:";
		name.value = paths.front().filename().wstring();
		name.selectionEnd = static_cast<int>(paths.front().stem().wstring().size());
		platform::DialogDefinition definition;
		definition.title = L"Rename Image";
		definition.fields.push_back(std::move(name));
		definition.validate = [](const platform::DialogValues& values) -> std::optional<std::wstring>
		{
			const auto found = values.find(renameValue);
			const auto* value = found == values.end() ? nullptr : std::get_if<std::wstring>(&found->second);
			const std::filesystem::path path(value ? *value : L"");
			return value && !path.empty() && !path.has_parent_path()
				       ? std::nullopt
				       : std::optional<std::wstring>(L"Enter a file name without a folder path.");
		};
		const auto values = platform::show_modal_dialog(frame_, std::move(definition));
		if (!values) return;
		const auto found = values->find(renameValue);
		if (found == values->end()) return;
		const auto* value = std::get_if<std::wstring>(&found->second);
		if (!value) return;
		const std::filesystem::path newName(*value);
		const auto renamed = paths.front().parent_path() / newName;
		auto plan = rename::direct_plan(paths.front(), *value);
		if (!plan.can_run())
		{
			platform::show_error(plan.blockReason, L"Rename failed", frame_);
			return;
		}
		if (!plan.count(tasks::RowState::ready)) return;
		const auto keepAlive = weak_from_this().lock();
		auto options = rename::run_options(plan);
		options.undoLabel = L"Rename";
		const bool processed = platform::run_with_status(frame_, L"Rename", L"Securing originals and renaming...", [&]
		{
			tasks::run_plan(plan, {}, options);
			return true;
		});
		if (!processed || plan.count(tasks::RowState::success) != 1)
		{
			platform::show_error(processed ? tasks::summarize_results(plan) : L"The rename could not start.",
				L"Rename failed", frame_);
			return;
		}
		if (!plan.completionError.empty()) platform::show_error(plan.completionError, L"Rename recovery", frame_);
		const bool displayed = canvas_.path() == paths.front();
		open_folder(folder_, false);
		if (displayed) open_image(renamed);
	}
	catch (const std::exception& error)
	{
		const std::string text = error.what();
		platform::show_error(std::wstring(text.begin(), text.end()), L"Rename failed", frame_);
	}

	void MainWindow::delete_selected()
	{
		const auto paths = selected_paths();
		if (platform::perform_file_operation(frame_, platform::FileOperation::recycle, paths))
			open_folder(folder_, false);
	}

	void MainWindow::copy_selected(const bool cut)
	{
		const auto paths = selected_paths();
		platform::set_clipboard_files(frame_, paths, cut);
	}

	void MainWindow::paste_files()
	{
		auto content = platform::read_clipboard(frame_);
		if (!content.files.empty())
		{
			if (platform::perform_file_operation(frame_, platform::FileOperation::copy, content.files, folder_))
				open_folder(folder_, false);
			return;
		}
		files::DecodedImage image;
		image.width = image.originalWidth = content.image.size.width;
		image.height = image.originalHeight = content.image.size.height;
		image.pixels = std::move(content.image.pixels);
		if (image.pixels.empty()) return;
		const auto path = files::next_image_path(folder_, pasteImageFormat_);
		if (!path.empty() && files::save_image(image, path, pasteImageFormat_, {}, false))
		{
			open_folder(folder_, false);
			open_image(path);
		}
		else platform::show_error(L"The clipboard image could not be saved.", L"Paste failed", frame_);
	}

	void MainWindow::import_dropped_files(const std::vector<std::filesystem::path>& paths)
	{
		if (paths.empty()) return;
		if (task_active())
		{
			platform::show_error(L"Close the current task before importing dropped files.", L"Import files", frame_);
			return;
		}
		if (platform::perform_file_operation(frame_, platform::FileOperation::copy, paths, folder_))
			open_folder(folder_, false);
	}

	namespace
	{
		constexpr platform::DialogFieldId transferDestination = 1;
		constexpr platform::DialogFieldId transferBrowse = 2;
		constexpr platform::DialogFieldId transferOpenAfter = 3;
		constexpr platform::DialogFieldId openWithChoice = 1;

		std::wstring widen_message(const std::string& text)
		{
			return {text.begin(), text.end()};
		}

		using TransferStamp = files::FileSnapshot;

		struct TransferPart
		{
			std::filesystem::path source;
			std::filesystem::path destination;
			TransferStamp sourceStamp;
			TransferStamp destinationStamp;
		};

		struct TransferRow
		{
			std::filesystem::path source;
			std::filesystem::path destination;
			std::vector<std::filesystem::path> sidecars;
			std::vector<std::filesystem::path> selected;
			std::vector<TransferPart> parts;
		};

		struct TransferPathLess
		{
			bool operator()(const std::filesystem::path& left, const std::filesystem::path& right) const
			{ return paths::icompare(left.native(), right.native()) < 0; }
		};

		using TransferParents = std::map<std::filesystem::path, TransferStamp, TransferPathLess>;

		struct TransferReport
		{
			std::weak_ptr<platform::WindowFrame> owner;
			std::wstring title;
			std::wstring summary;
			std::vector<std::wstring> results;
		};

		std::vector<TransferReport> transferReports;

		bool has_transfer_report(const platform::WindowFramePtr& owner)
		{
			return std::ranges::any_of(transferReports, [&owner](const TransferReport& report)
				{ return report.owner.lock() == owner; });
		}

		void show_transfer_report(const platform::WindowFramePtr& owner)
		{
			const auto report = std::ranges::find_if(transferReports, [&owner](const TransferReport& value)
				{ return value.owner.lock() == owner; });
			if (report == transferReports.end()) return;
			platform::DialogField resultList;
			resultList.id = 1;
			resultList.kind = platform::DialogFieldKind::choice;
			resultList.label = L"&Per-item results:";
			for (size_t index = 0; index < report->results.size(); ++index)
				resultList.choices.push_back({report->results[index], static_cast<std::int64_t>(index)});
			platform::DialogDefinition dialog;
			dialog.title = report->title + L" results";
			dialog.message = report->summary;
			dialog.fields.push_back(std::move(resultList));
			dialog.acceptText = L"Close";
			dialog.cancelText.clear();
			platform::show_modal_dialog(owner, std::move(dialog));
		}

		TransferStamp transfer_stamp(const std::filesystem::path& path, std::error_code& error)
		{
			error.clear();
			const auto native = files::native_path(path);
			const auto status = std::filesystem::symlink_status(native, error);
			if (error == std::errc::no_such_file_or_directory || status.type() == std::filesystem::file_type::not_found)
			{
				error.clear();
				return {};
			}
			if (error) return {};
			const auto attributes = platform::read_file_attributes(path);
			if (!attributes.known || attributes.reparse ||
				(!std::filesystem::is_directory(status) && !std::filesystem::is_regular_file(status)))
			{
				error = std::make_error_code(std::errc::operation_not_supported);
				return {};
			}
			const auto stamp = files::snapshot_file(path);
			if (!stamp || !stamp->exists)
			{
				error = std::make_error_code(std::errc::io_error);
				return {};
			}
			return *stamp;
		}

		std::error_code transfer_parent_check(std::filesystem::path path)
		{
			while (!path.empty())
			{
				std::error_code error;
				const auto stamp = transfer_stamp(path, error);
				if (error) return error;
				if (stamp.exists && !stamp.directory) return std::make_error_code(std::errc::not_a_directory);
				const auto parent = path.parent_path();
				if (parent == path) break;
				path = parent;
			}
			return {};
		}

		bool remember_transfer_parents(std::filesystem::path path, TransferParents& parents)
		{
			while (!path.empty())
			{
				std::error_code error;
				const auto stamp = transfer_stamp(path, error);
				if (error || (stamp.exists && !stamp.directory)) return false;
				parents.try_emplace(path, stamp);
				const auto parent = path.parent_path();
				if (parent == path) break;
				path = parent;
			}
			return true;
		}

		bool transfer_parents_unchanged(const TransferParents& parents, const bool allowContentChanges)
		{
			for (const auto& [path, expected] : parents)
			{
				std::error_code error;
				auto current = transfer_stamp(path, error);
				if (error) return false;
				if (allowContentChanges)
				{
					current.modified = expected.modified;
					current.size = expected.size;
				}
				if (current != expected) return false;
			}
			return true;
		}

		bool transfer_parent_chain_unchanged(std::filesystem::path path, const TransferParents& parents)
		{
			while (!path.empty())
			{
				const auto expected = parents.find(path);
				if (expected == parents.end()) return false;
				std::error_code error;
				auto current = transfer_stamp(path, error);
				if (error) return false;
				// Our own file writes change directory timestamps, but never its identity.
				current.modified = expected->second.modified;
				current.size = expected->second.size;
				if (current != expected->second) return false;
				const auto parent = path.parent_path();
				if (parent == path) break;
				path = parent;
			}
			return true;
		}

		bool qualified_transfer_folder(const std::filesystem::path& path)
		{
			if (!path.is_absolute() || path.native().find(L'\0') != std::wstring::npos) return false;
			for (const auto& component : path.relative_path())
				if (!component.empty() && component != L"." && component != L".." &&
					paths::validate_file_name(component.wstring()) != paths::NameProblem::none) return false;
			return true;
		}

		std::filesystem::path transfer_companion_target(const std::filesystem::path& source,
			const std::filesystem::path& sidecar, const std::filesystem::path& destination)
		{
			const auto name = sidecar.filename().wstring();
			const auto primaryName = source.filename().wstring();
			const bool fullName = name.size() > primaryName.size() &&
				paths::iequals(name.substr(0, primaryName.size()), primaryName) && name[primaryName.size()] == L'.';
			return destination.parent_path() / ((fullName ? destination.filename().wstring() :
				destination.stem().wstring()) + sidecar.extension().wstring());
		}

		std::error_code collect_transfer_parts(TransferRow& row, const bool move)
		{
			std::vector<std::filesystem::path> pending{row.source};
			pending.insert(pending.end(), row.sidecars.begin(), row.sidecars.end());
			while (!pending.empty())
			{
				const auto source = pending.back();
				pending.pop_back();
				std::error_code error;
				const auto stamp = transfer_stamp(source, error);
				if (error) return error;
				if (!stamp.exists) return std::make_error_code(std::errc::no_such_file_or_directory);
				if (move && stamp.readOnly && !stamp.directory)
					return std::make_error_code(std::errc::permission_denied);
				if (error) return error;
				row.parts.push_back({source, {}, stamp, {}});
				if (!stamp.directory) continue;
				std::filesystem::directory_iterator iterator(files::native_path(source), error), end;
				while (!error && iterator != end)
				{
					pending.push_back(source / iterator->path().filename());
					iterator.increment(error);
				}
				if (error) return error;
			}
			std::ranges::stable_sort(row.parts, [](const TransferPart& left, const TransferPart& right)
			{
				return left.sourceStamp.directory && !right.sourceStamp.directory;
			});
			return {};
		}

		void set_transfer_destinations(TransferRow& row, const std::filesystem::path& destination)
		{
			row.destination = destination;
			for (auto& part : row.parts)
			{
				if (paths::equal(part.source, row.source)) part.destination = destination;
				else if (paths::contains(row.source, part.source))
					part.destination = destination / part.source.lexically_relative(row.source);
				else part.destination = transfer_companion_target(row.source, part.source, destination);
			}
		}

		bool path_in(const std::vector<std::filesystem::path>& pathsToCheck, const std::filesystem::path& path)
		{
			return std::ranges::any_of(pathsToCheck, [&path](const auto& value) { return paths::equal(value, path); });
		}

		std::vector<std::filesystem::path> open_with_selection(std::vector<std::filesystem::path> selected,
			const std::filesystem::path& focused, std::wstring& failure, const bool requireSameType = false)
		{
			if (selected.empty()) { failure = L"Select a file first."; return {}; }
			const auto extension = paths::lowercase_extension(selected.front());
			for (const auto& path : selected)
			{
				std::error_code error;
				if (!path.is_absolute() || !std::filesystem::is_regular_file(files::native_path(path), error) ||
					error || (requireSameType && !paths::iequals(paths::lowercase_extension(path), extension)))
				{
					failure = requireSameType ? L"Select local files of one type for a registered application."
						: L"Select local files to open with an application or configured tool.";
					return {};
				}
			}
			const auto focus = std::ranges::find_if(selected, [&focused](const auto& path) { return paths::equal(path, focused); });
			if (focus != selected.end()) std::rotate(selected.begin(), focus, focus + 1);
			return selected;
		}

		std::wstring open_with_key(const std::wstring_view identity)
		{
			std::uint64_t hash = 14695981039346656037ull;
			for (const auto character : identity)
			{
				hash ^= static_cast<std::uint64_t>(towlower(character));
				hash *= 1099511628211ull;
			}
			return L"entry-" + std::to_wstring(hash);
		}

		void record_open_with_use(const std::wstring& key)
		{
			const auto count = (std::max)(0, platform::read_integer_setting(L"OpenWith", key, 0));
			const std::array uses{platform::IntegerSetting{key, count == (std::numeric_limits<int>::max)() ? count : count + 1}};
			platform::write_integer_settings(L"OpenWith", uses);
		}

		bool launch_configured_tool(const tools::InstalledTool& tool, const std::vector<std::filesystem::path>& selected,
			const platform::WindowFramePtr& owner)
		{
			if (selected.empty()) return false;
			std::error_code error;
			const auto attributes = platform::read_file_attributes(tool.executable);
			const auto items = tools::accepts_multiple_items(tool)
				? std::span<const std::filesystem::path>(selected)
				: std::span<const std::filesystem::path>(selected.data(), 1);
			const auto arguments = tools::build_arguments(tool, items);
			return tool.executable.is_absolute() && attributes.known && !attributes.reparse &&
				std::filesystem::is_regular_file(files::native_path(tool.executable), error) && !error &&
				std::ranges::all_of(items, [&error](const auto& item)
				{ return std::filesystem::is_regular_file(files::native_path(item), error) && !error; }) &&
				tools::declaration_is_usable({tool.executable.filename().wstring(), tool.invoke}) &&
				arguments.size() + tool.executable.native().size() + 4 < 32767 &&
				platform::launch_process(tool.executable, arguments, owner);
		}
	}

	void MainWindow::copy_or_move_to_folder(const bool move) try
	{
		const std::wstring title = move ? L"Move to folder" : L"Copy to folder";
		if (task_active())
		{
			platform::show_error(L"Close the current task before transferring items.", title, frame_);
			return;
		}
		auto selected = selected_paths();
		const int previousFocus = canvas_.focused_index();
		const auto sourceFolder = folder_;
		if (selected.empty())
		{
			platform::show_error(L"Select the items to transfer first.", title, frame_);
			return;
		}
		for (auto& path : selected)
		{
			path = path.lexically_normal();
			if (const auto error = transfer_parent_check(path.parent_path()))
			{
				platform::show_error(path.wstring() + L": " + widen_message(error.message()), title, frame_);
				return;
			}
		}
		std::vector<TransferRow> rows;
		TransferParents sourceParents;
		std::vector<std::filesystem::path> companions;
		for (const auto& path : selected)
		{
			std::error_code error;
			const auto stamp = transfer_stamp(path, error);
			if (!path.is_absolute() || error || !stamp.exists || transfer_parent_check(path.parent_path()))
			{
				platform::show_error(path.filename().wstring() + L" cannot be transferred. Nothing was changed.", title, frame_);
				return;
			}
			if (!remember_transfer_parents(path.parent_path(), sourceParents))
			{
				platform::show_error(L"A source folder could not be inspected. Nothing was changed.", title, frame_);
				return;
			}
			for (const auto& other : selected)
				if (stamp.directory && paths::contains(path, other))
				{
					platform::show_error(L"Select a folder or its contents, not both. Nothing was changed.", title, frame_);
					return;
				}
			if (stamp.directory) continue;
			for (const auto& companion : files::sidecar_paths(path))
			{
				if (path_in(companions, companion))
				{
					platform::show_error(L"Two selected files share a sidecar. Transfer them separately.", title, frame_);
					return;
				}
				companions.push_back(companion);
			}
		}
		std::uintmax_t totalSize = 0;
		for (const auto& path : selected)
		{
			if (path_in(companions, path)) continue;
			TransferRow row;
			row.source = path;
			row.selected.push_back(path);
			std::error_code statusError;
			if (!std::filesystem::is_directory(files::native_path(path), statusError))
				row.sidecars = files::sidecar_paths(path);
			for (const auto& sidecar : row.sidecars)
				if (path_in(selected, sidecar)) row.selected.push_back(sidecar);
			if (const auto error = collect_transfer_parts(row, move))
			{
				platform::show_error(path.filename().wstring() + L": " + widen_message(error.message()) +
					L". Nothing was changed.", title, frame_);
				return;
			}
			for (const auto& part : row.parts)
			{
				if (!part.sourceStamp.directory)
					totalSize += (std::min)(static_cast<std::uintmax_t>(part.sourceStamp.size),
						(std::numeric_limits<std::uintmax_t>::max)() - totalSize);
				else sourceParents.try_emplace(part.source, part.sourceStamp);
			}
			rows.push_back(std::move(row));
		}

		std::filesystem::path destination(lastTransferFolder_);
		const std::wstring seeded = destination.wstring();
		platform::DialogField destinationField;
		destinationField.id = transferDestination;
		destinationField.label = L"&Destination folder:";
		destinationField.value = seeded;
		destinationField.folderCompletion = true;
		platform::DialogField browse;
		browse.id = transferBrowse;
		browse.kind = platform::DialogFieldKind::action;
		browse.label = L"&Choose a folder...";
		browse.editValues = [this](platform::DialogValues& values)
		{
			if (const auto chosen = platform::choose_folder(frame_, L"Choose the destination folder"))
				values[transferDestination] = chosen->wstring();
		};
		platform::DialogField openAfter;
		openAfter.id = transferOpenAfter;
		openAfter.kind = platform::DialogFieldKind::checkBox;
		openAfter.label = L"&Open destination folder afterwards";
		openAfter.value = openDestinationAfterTransfer_;

		platform::DialogDefinition definition;
		definition.title = title;
		definition.message = std::format(L"{} and {} of data.",
		                                 selected.size() == 1
			                                 ? selected.front().filename().wstring()
			                                 : std::format(L"{} items", selected.size()),
		                                 format::size(totalSize));
		for (size_t index = 0; index < (std::min)(selected.size(), size_t{5}); ++index)
		{
			const auto& path = selected[index];
			const auto thumbnail = files::load_thumbnail(path, 64, 64);
			if (!thumbnail.pixels.empty())
				definition.previews.push_back({{thumbnail.width, thumbnail.height}, thumbnail.pixels, true});
			else
			{
				const auto attributes = platform::read_file_attributes(path);
				definition.previews.push_back(platform::load_bitmap_resource(attributes.directory
					? platform::BitmapAsset::fileFolder : platform::BitmapAsset::fileOther));
			}
		}
		definition.fields.push_back(std::move(destinationField));
		definition.fields.push_back(std::move(browse));
		definition.fields.push_back(std::move(openAfter));
		definition.acceptText = move ? L"Move" : L"Copy";
		definition.validate = [](const platform::DialogValues& values) -> std::optional<std::wstring>
		{
			const auto found = values.find(transferDestination);
			const auto* value = found == values.end() ? nullptr : std::get_if<std::wstring>(&found->second);
			const std::filesystem::path path(value ? *value : L"");
			return qualified_transfer_folder(path)
				       ? std::nullopt
				       : std::optional<std::wstring>(L"Enter a full destination folder path.");
		};
		const auto values = platform::show_modal_dialog(frame_, std::move(definition));
		if (!values) return;
		std::wstring typed = seeded;
		if (const auto found = values->find(transferDestination); found != values->end())
			if (const auto* value = std::get_if<std::wstring>(&found->second)) typed = *value;
		destination = std::filesystem::path(typed).lexically_normal();
		if (const auto found = values->find(transferOpenAfter); found != values->end())
			if (const auto* value = std::get_if<bool>(&found->second)) openDestinationAfterTransfer_ = *value;
		if (!qualified_transfer_folder(destination))
		{
			platform::show_error(L"Enter a full destination folder path.", title, frame_);
			return;
		}
		if (const auto error = transfer_parent_check(destination))
		{
			platform::show_error(L"The destination is not a usable folder: " + widen_message(error.message()), title, frame_);
			return;
		}
		for (const auto& row : rows)
		{
			std::error_code error;
			const auto sourceIdentity = std::filesystem::weakly_canonical(files::native_path(row.source), error);
			if (error) { platform::show_error(widen_message(error.message()), title, frame_); return; }
			const auto destinationIdentity = std::filesystem::weakly_canonical(files::native_path(destination), error);
			if (error) { platform::show_error(widen_message(error.message()), title, frame_); return; }
			if (std::filesystem::is_directory(files::native_path(row.source), error) &&
				(paths::equal(sourceIdentity, destinationIdentity) || paths::contains(sourceIdentity, destinationIdentity)))
			{
				platform::show_error(L"A folder cannot be transferred into itself or one of its children.", title, frame_);
				return;
			}
		}

		// The survey runs before the folder is created, so cancelling here leaves nothing behind.
		TransferParents destinationParents;
		if (!remember_transfer_parents(destination, destinationParents))
		{
			platform::show_error(L"The destination folder could not be inspected. Nothing was changed.", title, frame_);
			return;
		}
		std::vector<std::filesystem::path> collisions;
		std::set<std::filesystem::path, TransferPathLess> claimed;
		for (auto& row : rows)
		{
			set_transfer_destinations(row, destination / row.source.filename());
			std::filesystem::path collision;
			std::vector<std::filesystem::path> nonDirectoryTargets;
			for (auto& part : row.parts)
			{
				std::error_code error;
				if (std::ranges::any_of(nonDirectoryTargets, [&part](const auto& path)
					{ return paths::contains(path, part.destination); })) part.destinationStamp = {};
				else part.destinationStamp = transfer_stamp(part.destination, error);
				if (error)
				{
					platform::show_error(part.destination.wstring() + L": " + widen_message(error.message()), title, frame_);
					return;
				}
				if (part.sourceStamp.directory && part.destinationStamp.exists && !part.destinationStamp.directory)
					nonDirectoryTargets.push_back(part.destination);
				if (collision.empty() && (part.destinationStamp.exists || claimed.contains(part.destination)))
					collision = part.destination;
				claimed.insert(part.destination);
			}
			if (!collision.empty()) collisions.push_back(collision);
		}

		bool replace = false;
		if (!collisions.empty())
		{
			platform::ChoiceDefinition choice;
			choice.title = title;
			choice.heading = collisions.size() == 1
				                 ? std::format(L"{} is occupied or claimed by another selected item.",
				                               collisions.front().filename().wstring())
				                 : std::format(L"{} selected items have destination collisions.", collisions.size());
			choice.message = L"Auto-rename keeps both. Replace requires recovery copies before overwriting. "
				L"Completed changes can be undone during this session if their files have not changed.";
			choice.buttons = {{1, L"Auto-rename"}, {2, L"Replace"}};
			choice.defaultButton = 1;
			choice.warning = true;
			const int chosen = platform::show_choice(frame_, choice);
			if (!chosen) return;
			replace = chosen == 2;
		}

		claimed.clear();
		std::set<std::filesystem::path, TransferPathLess> sources;
		for (const auto& row : rows)
			for (const auto& part : row.parts) sources.insert(part.source);
		for (auto& row : rows)
		{
			if (!replace)
			{
				const auto target = files::unique_destination(row.destination, [&](const std::filesystem::path& candidate)
				{
					if (claimed.contains(candidate)) return true;
					for (const auto& sidecar : row.sidecars)
					{
						const auto companion = transfer_companion_target(row.source, sidecar, candidate);
						std::error_code error;
						if (transfer_stamp(companion, error).exists || error || claimed.contains(companion)) return true;
					}
					return false;
				});
				if (target.empty())
				{
					platform::show_error(L"No free name is available for " + row.source.filename().wstring(), title, frame_);
					return;
				}
				set_transfer_destinations(row, target);
				for (auto& part : row.parts) part.destinationStamp = {};
			}
			std::error_code identityError;
			const auto targetIdentity = std::filesystem::weakly_canonical(files::native_path(row.destination), identityError);
			if (identityError) { platform::show_error(widen_message(identityError.message()), title, frame_); return; }
			for (const auto& source : rows)
			{
				const auto sourceIdentity = std::filesystem::weakly_canonical(files::native_path(source.source), identityError);
				if (identityError) { platform::show_error(widen_message(identityError.message()), title, frame_); return; }
				if (paths::equal(targetIdentity, sourceIdentity) || paths::contains(targetIdentity, sourceIdentity))
				{
					platform::show_error(L"A destination contains a selected source. Choose Auto-rename or another folder.", title, frame_);
					return;
				}
			}
			for (const auto& part : row.parts)
			{
				if (claimed.contains(part.destination))
				{
					platform::show_error(L"Two selected items have the same destination. Choose Auto-rename.", title, frame_);
					return;
				}
				if (part.destinationStamp.exists && part.destinationStamp.directory != part.sourceStamp.directory)
				{
					platform::show_error(L"A file and folder have the same destination name. Choose Auto-rename.", title, frame_);
					return;
				}
				std::error_code sameError;
				if (sources.contains(part.destination) || (part.destinationStamp.exists &&
					std::filesystem::equivalent(files::native_path(part.source), files::native_path(part.destination), sameError)))
				{
					platform::show_error(L"A destination is one of the selected source items. Choose Auto-rename.", title, frame_);
					return;
				}
				claimed.insert(part.destination);
			}
		}

		size_t succeeded = 0;
		std::wstring firstFailure;
		std::vector<std::wstring> results;
		std::vector<std::filesystem::path> transferred;
		std::wstring preparationFailure;
		const bool processed = platform::run_with_status(frame_, title, L"Processing files...", [&]
		{
		// Recheck every reviewed object before the first write. Replace never grants permission
		// to overwrite a file which appeared or changed after the collision decision.
		if (!transfer_parents_unchanged(sourceParents, true) || !transfer_parents_unchanged(destinationParents, true))
		{
			preparationFailure = L"A source or destination folder changed. Review the transfer again.";
			return false;
		}
		for (const auto& row : rows)
		{
			std::error_code typeError;
			if (!std::filesystem::is_directory(files::native_path(row.source), typeError))
			{
				const auto companionsNow = files::sidecar_paths(row.source);
				if (companionsNow.size() != row.sidecars.size() || !std::ranges::all_of(row.sidecars,
					[&companionsNow](const auto& path) { return path_in(companionsNow, path); }))
				{
					preparationFailure = L"The sidecar set changed. Review the transfer again: " + row.source.wstring();
					return false;
				}
			}
			for (const auto& part : row.parts)
			{
				std::error_code error;
				const auto source = transfer_stamp(part.source, error);
				if (error || source != part.sourceStamp)
				{
					preparationFailure = L"A source changed. Review the transfer again: " + part.source.wstring();
					return false;
				}
				const auto target = transfer_stamp(part.destination, error);
				if (error || target != part.destinationStamp || transfer_parent_check(part.destination.parent_path()))
				{
					preparationFailure = L"A destination changed. Review the transfer again: " + part.destination.wstring();
					return false;
				}
			}
		}
		auto journal = undo::history().begin(title);
		std::error_code error;
		for (auto& [path, stamp] : destinationParents)
			if (!stamp.exists)
			{
				if (!journal->protect_directory(path, preparationFailure)) return false;
				if (!std::filesystem::create_directory(files::native_path(path), error))
				{
					preparationFailure = error ? widen_message(error.message()) :
						L"A destination folder appeared after review. Review the transfer again.";
					return false;
				}
				stamp = transfer_stamp(path, error);
				if (error || !stamp.exists || !stamp.directory)
				{
					preparationFailure = L"The destination folder changed while it was created.";
					return false;
				}
				if (!journal->completed({path}, preparationFailure)) return false;
			}

		for (auto& row : rows)
		{
			std::wstring failure;
			std::vector<std::filesystem::path> completedFiles;
			try
			{
				std::vector<std::filesystem::path> protectedFiles;
				for (const auto& part : row.parts)
				{
					if (part.sourceStamp.directory)
					{
						if (move && !journal->protect_directory(part.source, failure)) break;
						if (!part.destinationStamp.exists && !journal->protect_directory(part.destination, failure)) break;
					}
					else
					{
						protectedFiles.push_back(part.destination);
						if (move) protectedFiles.push_back(part.source);
					}
				}
				const bool protectedRow = failure.empty() && journal->protect(protectedFiles, failure);
				if (!protectedRow && failure.empty()) failure = L"Recovery copies could not be secured before transfer.";
				for (auto& part : row.parts)
				{
					if (!protectedRow || !failure.empty()) break;
					if (!transfer_parent_chain_unchanged(part.source.parent_path(), sourceParents) ||
						!transfer_parent_chain_unchanged(part.destination.parent_path(), destinationParents))
					{
						failure = L"A source or destination folder was replaced after review.";
						break;
					}
					error = transfer_parent_check(part.destination.parent_path());
					if (error) { failure = widen_message(error.message()); break; }
					const auto now = transfer_stamp(part.destination, error);
					if (error || now != part.destinationStamp)
					{
						failure = L"A destination changed after review: " + part.destination.filename().wstring();
						break;
					}
					if (part.sourceStamp.directory)
					{
						const auto sourceNow = transfer_stamp(part.source, error);
						if (error || sourceNow != part.sourceStamp)
						{
							failure = L"A source folder changed: " + part.source.filename().wstring();
							break;
						}
						if (!now.exists && !std::filesystem::create_directory(files::native_path(part.destination), error))
						{
							if (!error) error = std::make_error_code(std::errc::file_exists);
						}
						if (!error)
						{
							const auto created = transfer_stamp(part.destination, error);
							if (!error && created.exists && created.directory && (!now.exists || created == now))
							{
								destinationParents.insert_or_assign(part.destination, created);
								if (!now.exists && !journal->completed({part.destination}, failure))
									error = std::make_error_code(std::errc::io_error);
							}
							else if (!error) error = std::make_error_code(std::errc::state_not_recoverable);
						}
					}
					else
					{
						error = transfer_parent_check(part.source.parent_path());
						if (error) { failure = L"A source folder changed: " + widen_message(error.message()); break; }
						const auto sourceNow = transfer_stamp(part.source, error);
						if (error || sourceNow != part.sourceStamp)
						{
							failure = L"A source changed: " + part.source.filename().wstring();
							break;
						}
						const auto beforeCommit = [&]
						{
							std::error_code observedError;
							const auto source = transfer_stamp(part.source, observedError);
							if (observedError || source != part.sourceStamp ||
								!transfer_parent_chain_unchanged(part.source.parent_path(), sourceParents))
							{
								failure = L"A source changed before the output could be committed.";
								return false;
							}
							const auto target = transfer_stamp(part.destination, observedError);
							if (observedError || target != part.destinationStamp ||
								!transfer_parent_chain_unchanged(part.destination.parent_path(), destinationParents))
							{
								failure = L"A destination changed before the output could be committed.";
								return false;
							}
							return true;
						};
						const auto outcome = undo::transfer_file(*journal, part.source, part.destination, move, now.exists, beforeCommit);
						error = outcome.error;
						if (!outcome.detail.empty()) failure = outcome.detail;
						if (outcome.destinationCompleted)
						{
							completedFiles.push_back(part.destination);
							if (paths::equal(part.destination.parent_path(), destination) && !path_in(transferred, part.destination))
								transferred.push_back(part.destination);
						}
						if (outcome.sourceRemoved) completedFiles.push_back(part.source);
					}
					if (error)
					{
						if (failure.empty()) failure = part.source.filename().wstring() + L": " + widen_message(error.message());
						break;
					}
					if (paths::equal(part.destination.parent_path(), destination) && !path_in(transferred, part.destination))
						transferred.push_back(part.destination);
				}
				if (move && failure.empty())
					for (auto part = row.parts.rbegin(); part != row.parts.rend(); ++part)
					{
						if (!part->sourceStamp.directory) continue;
						if (!transfer_parent_chain_unchanged(part->source, sourceParents))
						{
							failure = L"A source folder changed before it could be removed.";
							break;
						}
						if (!journal->remove_directory(part->source, failure)) break;
					}
			}
			catch (const std::exception& exception) { failure = widen_message(exception.what()); }
			catch (...) { failure = L"The transfer failed unexpectedly."; }
			if (!completedFiles.empty())
			{
				std::wstring recordError;
				if (!journal->completed(completedFiles, {}, recordError))
				{
					if (!failure.empty()) failure += L" ";
					failure += recordError;
				}
			}
			if (failure.empty())
			{
				succeeded += row.selected.size();
			}
			else if (firstFailure.empty()) firstFailure = row.source.filename().wstring() + L": " + failure;
			for (const auto& path : row.selected)
			{
				const auto target = paths::equal(path, row.source) ? row.destination :
					transfer_companion_target(row.source, path, row.destination);
				results.push_back(path.filename().wstring() + (failure.empty() ? L": Success — " + target.wstring() :
					L": Failed (may be partially transferred) — " + failure));
			}
		}
		std::wstring journalError;
		if (!journal->finish(journalError))
		{
			if (firstFailure.empty()) firstFailure = journalError;
			results.push_back(L"Undo recovery: " + journalError);
		}
		return true;
		});
		if (!processed)
		{
			platform::show_error(preparationFailure.empty()
				? L"The transfer could not start. No selected item was transferred."
				: preparationFailure, title, frame_);
			return;
		}

		lastTransferFolder_ = destination.wstring();
		save_settings();
		statusText_ = firstFailure.empty()
			              ? std::format(L"{} of {} item{} {}", succeeded, selected.size(),
			                            selected.size() == 1 ? L"" : L"s", move ? L"moved" : L"copied")
			              : std::format(L"{} of {} transferred - {}", succeeded, selected.size(), firstFailure);
		update_status_surface();
		std::erase_if(transferReports, [this](const TransferReport& report)
			{ return report.owner.expired() || report.owner.lock() == frame_; });
		transferReports.push_back({frame_, title, statusText_, std::move(results)});
		show_transfer_report(frame_);

		const bool openDestination = openDestinationAfterTransfer_;
		const bool destinationIsSource = paths::equal(destination, sourceFolder);
		if (openDestination) { if (!open_folder(destination)) return; }
		else if (!open_folder(sourceFolder, false)) return;
		const auto scanGeneration = folderScanGeneration_.load();
		const auto expectedFolder = folder_;
		const auto weak = weak_from_this();
		auto desiredSelection = openDestination || (move && destinationIsSource) ? std::move(transferred) : selected;
		// The folder queue is serial: its scan posts the collection before this selection callback.
		platform::queue_work(platform::WorkQueue::folder,
			[weak, scanGeneration, expectedFolder, previousFocus, openDestination, move, destinationIsSource,
				desiredSelection = std::move(desiredSelection)]
			{
				platform::queue_ui([weak, scanGeneration, expectedFolder, previousFocus, openDestination, move,
					destinationIsSource, desiredSelection]
				{
					const auto self = weak.lock();
					if (!self || self->folderScanGeneration_ != scanGeneration || !paths::equal(self->folder_, expectedFolder))
						return;
					if (openDestination || !move || destinationIsSource)
					{
						self->canvas_.clear_selection();
						bool first = true;
						for (size_t index = 0; index < self->files_.size(); ++index)
							if (path_in(desiredSelection, self->files_[index]))
							{
								self->canvas_.select_index(static_cast<int>(index), false, !first);
								first = false;
							}
					}
					else if (!self->files_.empty())
						self->canvas_.select_index(std::clamp(previousFocus, 0, static_cast<int>(self->files_.size()) - 1), false, false);
				});
			});
	}
	catch (const std::exception& exception)
	{
		platform::show_error(widen_message(exception.what()), move ? L"Move to folder" : L"Copy to folder", frame_);
	}
	catch (...)
	{
		platform::show_error(L"The transfer could not be completed.", move ? L"Move to folder" : L"Copy to folder", frame_);
	}

	void MainWindow::show_open_with_menu(const pointi screenPoint)
	{
		if (task_active())
		{
			platform::show_error(L"Close the current task before opening selected items.", L"Open with", frame_);
			return;
		}
		std::wstring failure;
		const auto focus = canvas_.focused_index();
		const auto selected = open_with_selection(selected_paths(),
			focus >= 0 && static_cast<size_t>(focus) < files_.size() ? files_[focus] : std::filesystem::path{}, failure);
		if (selected.empty())
		{
			platform::show_error(failure, L"Open with", frame_);
			return;
		}
		const auto extension = paths::lowercase_extension(selected.front());

		std::vector<platform::PopupItem> items;
		std::vector<platform::CommandPtr> commands;
		const auto append = [&](std::wstring label, std::function<void()> invoke)
		{
			auto command = std::make_shared<platform::Command>();
			command->invoke = std::move(invoke);
			commands.push_back(command);
			items.push_back(platform::PopupItem::action(command, std::move(label)));
		};

		if (std::ranges::all_of(selected, [&extension](const auto& path)
			{ return paths::iequals(paths::lowercase_extension(path), extension); }))
		{
			for (const auto& handler : platform::registered_apps_for_extension(extension))
				append(std::format(L"Open with {}", handler->name()), [this, handler, selected]
				{
					std::wstring error;
					if (open_with_selection(selected, selected.front(), error, true).empty() || !handler->invoke(selected))
						platform::show_error(L"Failed to open selected items", L"Open with", frame_);
					else record_open_with_use(open_with_key(L"app:" + handler->name()));
				});
		}

		const auto configured = tools::tools_for(tools_.installed(), selected);
		if (!configured.empty()) items.push_back(platform::PopupItem::separator_item());
		for (const auto* tool : configured)
		{
			const auto label = selected.size() > 1 && !tools::accepts_multiple_items(*tool)
				                   ? std::format(L"Open with {} (first item only)", tool->name)
				                   : std::format(L"Open with {}", tool->name);
			append(label, [this, tool = *tool, selected]
			{
				std::wstring error;
				if (open_with_selection(selected, selected.front(), error).empty() ||
					!launch_configured_tool(tool, selected, frame_))
					platform::show_error(L"Failed to open selected items", L"Open with", frame_);
				else record_open_with_use(open_with_key(L"configured:" + tool.executable.wstring() + L"|" + tool.invoke));
			});
		}

		items.push_back(platform::PopupItem::separator_item());
		append(L"Open with...", [this] { open_with_chooser(); });
		platform::show_popup_menu(frame_, screenPoint, items);
	}

	void MainWindow::open_with_chooser()
	{
		if (task_active())
		{
			platform::show_error(L"Close the current task before opening selected items.", L"Open with", frame_);
			return;
		}
		std::wstring failure;
		const auto focus = canvas_.focused_index();
		const auto selected = open_with_selection(selected_paths(),
			focus >= 0 && static_cast<size_t>(focus) < files_.size() ? files_[focus] : std::filesystem::path{}, failure);
		if (selected.empty())
		{
			platform::show_error(failure, L"Open with", frame_);
			return;
		}
		const auto extension = paths::lowercase_extension(selected.front());

		struct Entry
		{
			std::wstring label;
			std::wstring key;
			std::function<bool()> invoke;
			int uses{};
		};
		std::vector<Entry> entries;
		if (std::ranges::all_of(selected, [&extension](const auto& path)
			{ return paths::iequals(paths::lowercase_extension(path), extension); }))
		{
			for (const auto& handler : platform::registered_apps_for_extension(extension))
				entries.push_back({
					std::format(L"{} (app)", handler->name()),
					open_with_key(L"app:" + handler->name()),
					[this, handler, selected]
					{
						std::wstring error;
						return !open_with_selection(selected, selected.front(), error, true).empty() && handler->invoke(selected);
					},
					platform::read_integer_setting(L"OpenWith", open_with_key(L"app:" + handler->name()), 0)
				});
		}
		for (const auto* tool : tools::tools_for(tools_.installed(), selected))
			entries.push_back({
				std::format(L"{} (tool){}", tool->name,
					selected.size() > 1 && !tools::accepts_multiple_items(*tool) ? L" — first item only" : L""),
				open_with_key(L"configured:" + tool->executable.wstring() + L"|" + tool->invoke),
				[this, tool = *tool, selected]
				{
					std::wstring error;
					return !open_with_selection(selected, selected.front(), error).empty() &&
						launch_configured_tool(tool, selected, frame_);
				},
				platform::read_integer_setting(L"OpenWith",
					open_with_key(L"configured:" + tool->executable.wstring() + L"|" + tool->invoke), 0)
			});
		const auto appendCommand = [&](const wchar_t* label, const wchar_t* identity, std::function<void()> command)
		{
			const auto key = open_with_key(identity);
			entries.push_back({label, key, [this, selected, command = std::move(command)]
				{
					const auto current = selected_paths();
					if (current.size() != selected.size() || !std::ranges::all_of(selected,
						[&current](const auto& path) { return path_in(current, path); })) return false;
					command();
					return true;
				},
				platform::read_integer_setting(L"OpenWith", key, 0)});
		};
		appendCommand(L"Copy to folder (tool)", L"internal:copy", [this] { copy_or_move_to_folder(false); });
		const bool writable = std::ranges::all_of(selected, [](const auto& path)
		{
			const auto stamp = files::snapshot_file(path);
			return stamp && stamp->exists && !stamp->readOnly;
		});
		if (writable)
		{
			appendCommand(L"Move to folder (tool)", L"internal:move", [this] { copy_or_move_to_folder(true); });
			appendCommand(L"Batch Rename (tool)", L"internal:rename", [this] { open_rename_task(); });
		}
		if (std::ranges::all_of(selected, [](const auto& path) { return files::is_supported_image(path); }))
			appendCommand(L"Convert or Resize (tool)", L"internal:convert", [this] { open_convert_task(); });
		if (writable && selected.size() == 1 && files::is_editable_image(selected.front()))
			appendCommand(L"Photo Edit (tool)", L"internal:edit", [this] { open_edit_task(); });
		// Frequently used entries gain ranking weight.
		std::ranges::stable_sort(entries, [](const Entry& left, const Entry& right)
		{
			return left.uses > right.uses;
		});

		platform::DialogField list;
		list.id = openWithChoice;
		list.kind = platform::DialogFieldKind::searchList;
		list.label = L"&Open with:";
		list.value = std::int64_t{0};
		for (size_t index = 0; index < entries.size(); ++index)
			list.choices.push_back({entries[index].label, static_cast<std::int64_t>(index)});

		platform::DialogDefinition definition;
		definition.title = L"Open with app or tool";
		definition.message = selected.size() == 1 ?
			std::format(L"Choose what opens {}.", selected.front().filename().wstring()) :
			std::format(L"Choose what opens {} selected files. The first item is {}.",
				selected.size(), selected.front().filename().wstring());
		definition.fields.push_back(std::move(list));
		definition.validate = [count = entries.size()](const platform::DialogValues& values) -> std::optional<std::wstring>
		{
			const auto found = values.find(openWithChoice);
			const auto* choice = found == values.end() ? nullptr : std::get_if<std::int64_t>(&found->second);
			if (!choice || *choice < 0 || static_cast<size_t>(*choice) >= count)
				return L"Choose an app or tool from the search results.";
			return {};
		};
		const auto values = platform::show_modal_dialog(frame_, std::move(definition));
		if (!values) return;
		const auto found = values->find(openWithChoice);
		if (found == values->end()) return;
		const auto* chosen = std::get_if<std::int64_t>(&found->second);
		if (!chosen || *chosen < 0 || static_cast<size_t>(*chosen) >= entries.size()) return;
		auto& entry = entries[static_cast<size_t>(*chosen)];
		if (entry.invoke()) record_open_with_use(entry.key);
		else platform::show_error(L"Failed to open selected items", L"Open with", frame_);
	}
}

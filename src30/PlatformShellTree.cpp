// ImageWalker by Zac Walker
// Shows the drawn folder tree in a frame of its own, and lists drives and folders off the UI thread.

#include "PlatformWin32.h"
#include "DesktopServices.h"
#include "Files.h"
#include "FolderTree.h"
#include "Paths.h"
#include "Platform.h"

#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>

namespace iw::platform
{
	namespace
	{
		std::vector<folders::Folder> list_drives()
		{
			std::vector<folders::Folder> result;
			const DWORD length = GetLogicalDriveStringsW(0, nullptr);
			std::wstring drives(length, L'\0');
			if (!length || !GetLogicalDriveStringsW(length, drives.data())) return result;
			for (size_t position = 0; position < drives.size();)
			{
				const auto end = drives.find(L'\0', position);
				if (end == std::wstring::npos || end == position) break;
				std::filesystem::path root(drives.substr(position, end - position));
				auto label = root.root_name().wstring();
				result.push_back({std::move(root), std::move(label), true});
				position = end + 1;
			}
			return result;
		}

		// The folders in folder the tree shows, neither hidden nor system ones, in name order regardless of case;
		// nullopt when it cannot be read.
		std::optional<std::vector<folders::Folder>> list_subfolders(const std::filesystem::path& folder,
		                                                            const std::stop_token& exiting)
		{
			const auto pattern = files::native_path(folder) / L"*";
			WIN32_FIND_DATAW entry{};
			const HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &entry,
			                                     FindExSearchLimitToDirectories, nullptr, FIND_FIRST_EX_LARGE_FETCH);
			if (find == INVALID_HANDLE_VALUE)
			{
				if (GetLastError() == ERROR_FILE_NOT_FOUND) return std::vector<folders::Folder>{};
				return std::nullopt;
			}
			std::vector<folders::Folder> result;
			DWORD error = ERROR_NO_MORE_FILES;
			for (;;)
			{
				if (exiting.stop_requested())
				{
					error = ERROR_CANCELLED;
					break;
				}
				const std::wstring_view name = entry.cFileName;
				if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
				    !(entry.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) && name != L"." &&
				    name != L"..")
					result.push_back({folder / name, std::wstring(name), false});
				if (!FindNextFileW(find, &entry))
				{
					error = GetLastError();
					break;
				}
			}
			FindClose(find);
			if (error != ERROR_NO_MORE_FILES) return std::nullopt;
			std::ranges::sort(result, [](const auto& left, const auto& right)
			{ return paths::icompare(left.label, right.label) < 0; });
			return result;
		}

		class DrawnShellTree;

		// The tree's frame reaches the tree without owning it, so the two make no cycle.
		class TreeReactor final : public FrameReactor
		{
		public:
			explicit TreeReactor(std::weak_ptr<DrawnShellTree> tree) : tree_(std::move(tree)) {}

			void paint(const WindowFramePtr& frame, DrawContext& draw) override;
			void size(const WindowFramePtr& frame, app::sizei extent, MeasureContext& measure) override;
			MessageResult files_dropped(const WindowFramePtr& frame, const FileDrop& drop) override;

		private:
			std::weak_ptr<DrawnShellTree> tree_;
		};

		class DrawnShellTree final : public ShellTree, public std::enable_shared_from_this<DrawnShellTree>
		{
		public:
			DrawnShellTree(const WindowFramePtr& owner, ShellTreeOptions options) :
				owner_(owner), options_(std::move(options))
			{
			}

			~DrawnShellTree() override
			{
				element_.reset();
				if (!frame_) return;
				frame_->set_reactor({});
				frame_->close();
			}

			DrawnShellTree(const DrawnShellTree&) = delete;
			DrawnShellTree& operator=(const DrawnShellTree&) = delete;

			bool create()
			{
				const auto owner = owner_.lock();
				if (!owner) return false;
				WindowOptions options;
				options.className = "ImageWalker30.FolderTree";
				options.child = true;
				options.visible = true;
				frame_ = owner->create_child(std::make_shared<TreeReactor>(weak_from_this()), options);
				if (!frame_) return false;
				const auto weak = weak_from_this();
				view_ = std::make_shared<folders::FolderTreeView>(
					folders::FolderTreeView::Events{[weak](const std::filesystem::path& folder)
				{
					const auto self = weak.lock();
					if (const auto changed = self ? self->options_.selectionChanged : nullptr) changed(folder);
				}, [weak](const std::filesystem::path& folder)
				{
					if (const auto self = weak.lock()) self->list(folder);
				}});
				element_ = add_frame_element(frame_, view_);
				if (!element_) return false;
				const auto client = frame_->client_rect();
				resize({client.width, client.height});
				refresh();
				return true;
			}

			void set_bounds(const app::recti bounds) override
			{
				if (frame_) frame_->move(bounds);
			}

			void show(const bool visible) override
			{
				if (frame_) frame_->show(visible);
			}

			void set_focus() override
			{
				if (view_) view_->focus();
			}

			bool has_focus() const override { return view_ && view_->has_focus(); }

			void set_current_path(const std::filesystem::path& path) override
			{
				if (view_ && !path.empty()) view_->reveal(path);
			}

			// Lists the drives again; the folder selected is shown again under them.
			void refresh() override
			{
				const auto weak = weak_from_this();
				const auto generation = ++rootsGeneration_;
				queue_work(WorkQueue::shell, [weak, generation]
				{
					auto drives = list_drives();
					queue_ui([weak, generation, drives = std::move(drives)]() mutable
					{
						const auto self = weak.lock();
						if (self && self->view_ && generation == self->rootsGeneration_)
							self->view_->set_roots(std::move(drives));
					});
				});
			}

			void resize(const app::sizei extent)
			{
				if (element_) element_->set_bounds({0, 0, extent.width, extent.height});
			}

			// Files dropped on a folder are copied there, or moved with Shift; the selected folder takes a drop
			// below the last one.
			void drop(const FileDrop& drop)
			{
				if (!view_) return;
				auto target = view_->folder_at({drop.point.x, drop.point.y});
				if (!target) target = view_->model().selected_path();
				if (!target) return;
				const auto files = paths::outside_folder(drop.files, *target);
				if (files.empty()) return;
				const bool move = ::platform::current_key_state().shift;
				perform_file_operation(owner_.lock(), move ? FileOperation::move : FileOperation::copy, files, *target);
				if (const auto changed = options_.filesChanged) changed();
			}

		private:
			void list(const std::filesystem::path& folder)
			{
				const auto weak = weak_from_this();
				queue_work(WorkQueue::shell, [weak, folder]
				{
					const auto exiting = exit_token();
					auto subfolders = list_subfolders(folder, exiting);
					if (exiting.stop_requested()) return;
					if (!subfolders)
						write_diagnostic(std::format(L"The folder tree could not list {}.\n", folder.wstring()));
					queue_ui([weak, folder,
					          subfolders = std::move(subfolders).value_or(std::vector<folders::Folder>{})]() mutable
					{
						if (const auto self = weak.lock(); self && self->view_)
							self->view_->listed(folder, std::move(subfolders));
					});
				});
			}

			std::weak_ptr<WindowFrame> owner_;
			ShellTreeOptions options_;
			WindowFramePtr frame_;
			std::shared_ptr<folders::FolderTreeView> view_;
			FrameElementPtr element_;
			std::uint64_t rootsGeneration_{};
		};

		void TreeReactor::paint(const WindowFramePtr& frame, DrawContext& draw)
		{
			draw.fill(frame->client_rect(), system_color(SystemColor::window));
		}

		void TreeReactor::size(const WindowFramePtr&, const app::sizei extent, MeasureContext&)
		{
			if (const auto tree = tree_.lock()) tree->resize(extent);
		}

		MessageResult TreeReactor::files_dropped(const WindowFramePtr&, const FileDrop& drop)
		{
			if (const auto tree = tree_.lock()) tree->drop(drop);
			return 1;
		}
	}

	ShellTreePtr create_shell_tree(const WindowFramePtr& owner, ShellTreeOptions options)
	{
		auto result = std::make_shared<DrawnShellTree>(owner, std::move(options));
		return result->create() ? result : ShellTreePtr{};
	}

	bool begin_file_drag(const WindowFramePtr&, std::span<const std::filesystem::path> paths)
	{
		if (paths.empty()) return false;
		std::vector<core::file_path> files;
		for (const auto& path : paths) files.push_back(desktop::file_path(path));
		return ::platform::begin_file_drag(nullptr, files);
	}
}

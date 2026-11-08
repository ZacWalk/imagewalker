// ImageWalker by Zac Walker
// Hosts the folder tree as an application-owned Win32 child of the shared ui frame.

#include "DesktopServices.h"
#include "Platform.h"
#include "PlatformWin32.h"

#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <vector>

namespace iw::platform
{
	namespace
	{
		class Win32ShellTree final : public ShellTree, public std::enable_shared_from_this<Win32ShellTree>
		{
		public:
			Win32ShellTree(const WindowFramePtr& owner, ShellTreeOptions options)
				: owner_(owner), options_(std::move(options))
			{
				const auto locked_owner = owner_.lock();
				const auto frame = native_ui_frame(locked_owner);
				const auto handle = frame ? frame->handle() : std::any{};
				const auto parent =
					handle.has_value() && handle.type() == typeid(HWND) ? std::any_cast<HWND>(handle) : nullptr;
				if (!parent) return;
				register_class();
				host_ = CreateWindowExW(WS_EX_CLIENTEDGE, class_name, L"",
					WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 0, 0, parent, nullptr,
					GetModuleHandleW(nullptr), this);
				if (!host_) return;
				tree_ = CreateWindowExW(0, WC_TREEVIEWW, L"",
					WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS |
						TVS_TRACKSELECT,
					0, 0, 0, 0, host_, nullptr, GetModuleHandleW(nullptr), nullptr);
				if (!tree_) return;
				SetWindowSubclass(tree_, tree_subclass_proc, 1, reinterpret_cast<DWORD_PTR>(this));
				DragAcceptFiles(host_, TRUE);
				refresh();
			}

			~Win32ShellTree() override
			{
				if (tree_) RemoveWindowSubclass(tree_, tree_subclass_proc, 1);
				if (host_) DestroyWindow(host_);
			}

			bool valid() const { return host_ && tree_; }

			void set_bounds(app::recti r) override
			{
				if (host_) SetWindowPos(host_, nullptr, r.x, r.y, r.width, r.height, SWP_NOZORDER | SWP_NOACTIVATE);
			}

			void show(bool v) override
			{
				if (host_) ShowWindow(host_, v ? SW_SHOW : SW_HIDE);
			}

			void set_focus() override
			{
				if (tree_) SetFocus(tree_);
			}

			bool has_focus() const override { return tree_ && GetFocus() == tree_; }

			void set_current_path(const std::filesystem::path& path) override
			{
				if (!tree_ || path.empty()) return;
				const bool was_suppressed = suppressSelection_;
				suppressSelection_ = true;
				const auto target = normal(path);
				HTREEITEM item = TreeView_GetRoot(tree_);
				while (item)
				{
					const auto current = node_path(item);
					if (current && equal_path(*current, target))
					{
						TreeView_SelectItem(tree_, item);
						break;
					}
					if (current && target.wstring().starts_with(current->wstring()))
					{
						expand(item);
						if (select_descendant(TreeView_GetChild(tree_, item), target)) break;
					}
					item = TreeView_GetNextSibling(tree_, item);
				}
				suppressSelection_ = was_suppressed;
			}

			void refresh() override
			{
				if (!tree_) return;
				TreeView_DeleteAllItems(tree_);
				nodes_.clear();
				DWORD length = GetLogicalDriveStringsW(0, nullptr);
				std::wstring drives(length, L'\0');
				if (length && GetLogicalDriveStringsW(length, drives.data()))
				{
					for (size_t pos = 0; pos < drives.size();)
					{
						const auto end = drives.find(L'\0', pos);
						if (end == std::wstring::npos || end == pos) break;
						insert_root(std::filesystem::path(drives.substr(pos, end - pos)));
						pos = end + 1;
					}
				}
			}

		private:
			static constexpr wchar_t class_name[] = L"ImageWalker30.ShellTreeHost";

			struct Node
			{
				std::unique_ptr<std::filesystem::path> path;
				HTREEITEM item{};
			};

			static std::filesystem::path normal(const std::filesystem::path& p) { return p.lexically_normal(); }

			static bool equal_path(const std::filesystem::path& a, const std::filesystem::path& b)
			{
				return _wcsicmp(a.c_str(), b.c_str()) == 0;
			}

			static bool visible_directory(const std::filesystem::directory_entry& entry)
			{
				std::error_code error;
				if (!entry.is_directory(error) || error) return false;
				const auto attributes = GetFileAttributesW(entry.path().c_str());
				return attributes != INVALID_FILE_ATTRIBUTES &&
					(attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) == 0;
			}

			std::filesystem::path* node_path(HTREEITEM item) const
			{
				TVITEMW value{};
				value.hItem = item;
				value.mask = TVIF_PARAM;
				if (!TreeView_GetItem(tree_, &value) || !value.lParam) return nullptr;
				return reinterpret_cast<std::filesystem::path*>(value.lParam);
			}

			bool select_descendant(HTREEITEM item, const std::filesystem::path& target)
			{
				for (; item; item = TreeView_GetNextSibling(tree_, item))
				{
					if (const auto p = node_path(item); p && equal_path(*p, target))
					{
						TreeView_SelectItem(tree_, item);
						return true;
					}
					if (TreeView_GetChild(tree_, item) && select_descendant(TreeView_GetChild(tree_, item), target))
						return true;
				}
				return false;
			}

			void insert_root(const std::filesystem::path& path) { insert_item(TVI_ROOT, path, true); }

			HTREEITEM insert_item(HTREEITEM parent, const std::filesystem::path& path, const bool root = false)
			{
				auto node = std::make_unique<Node>();
				node->path = std::make_unique<std::filesystem::path>(path);
				const auto label = root ? path.root_name().wstring() : path.filename().wstring();
				TVINSERTSTRUCTW insert{};
				insert.hParent = parent;
				insert.hInsertAfter = TVI_LAST;
				insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
				insert.item.pszText = const_cast<wchar_t*>(label.c_str());
				insert.item.lParam = reinterpret_cast<LPARAM>(node->path.get());
				insert.item.cChildren = 1;
				const auto item = TreeView_InsertItem(tree_, &insert);
				node->item = item;
				nodes_.push_back(std::move(node));
				return item;
			}

			void expand(HTREEITEM item)
			{
				if (!item || TreeView_GetChild(tree_, item) != nullptr)
				{
					if (TreeView_GetChild(tree_, item) && node_path(TreeView_GetChild(tree_, item)) == nullptr)
						remove_placeholder(item);
					else return;
				}
				const auto path = node_path(item);
				if (!path) return;
				std::vector<std::filesystem::path> folders;
				std::error_code error;
				for (std::filesystem::directory_iterator it(*path, error), end; !error && it != end;
					it.increment(error))
					if (visible_directory(*it)) folders.push_back(it->path());
				std::sort(folders.begin(), folders.end(), [](const auto& a, const auto& b)
				{
					return _wcsicmp(a.filename().c_str(), b.filename().c_str()) < 0;
				});
				for (const auto& folder : folders) insert_item(item, folder);
				TVITEMW state{};
				state.hItem = item;
				state.mask = TVIF_CHILDREN;
				state.cChildren = folders.empty() ? 0 : 1;
				TreeView_SetItem(tree_, &state);
			}

			void remove_placeholder(HTREEITEM parent)
			{
				auto child = TreeView_GetChild(tree_, parent);
				if (child && node_path(child) == nullptr) TreeView_DeleteItem(tree_, child);
			}

			static void register_class()
			{
				static const auto registered = []()
				{
					WNDCLASSW c{};
					c.hInstance = GetModuleHandleW(nullptr);
					c.lpfnWndProc = window_proc;
					c.lpszClassName = class_name;
					c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
					RegisterClassW(&c);
					return true;
				}();
				(void)registered;
			}

			static LRESULT CALLBACK tree_subclass_proc(
				HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data)
			{
				auto* self = reinterpret_cast<Win32ShellTree*>(data);
				if (self && (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) &&
					dispatch_menu_accelerator(static_cast<char32_t>(wp), ::platform::current_key_state()))
					return 0;
				return DefSubclassProc(hwnd, message, wp, lp);
			}

			static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
			{
				auto* self = reinterpret_cast<Win32ShellTree*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
				if (message == WM_NCCREATE)
				{
					auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
					self = reinterpret_cast<Win32ShellTree*>(create->lpCreateParams);
					SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
				}
				if (!self) return DefWindowProcW(hwnd, message, wp, lp);
				switch (message)
				{
				case WM_SIZE:
					if (self->tree_) MoveWindow(self->tree_, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
					break;
				case WM_NOTIFY:
				{
					auto* note = reinterpret_cast<NMHDR*>(lp);
					if (note->code == TVN_ITEMEXPANDINGW)
					{
						auto* tree = reinterpret_cast<NMTREEVIEWW*>(lp);
						if (tree->action == TVE_EXPAND) self->expand(tree->itemNew.hItem);
					}
					else if (note->code == TVN_SELCHANGEDW)
					{
						auto* tree = reinterpret_cast<NMTREEVIEWW*>(lp);
						if (const auto path = self->node_path(tree->itemNew.hItem);
							path && !self->suppressSelection_ && self->options_.selectionChanged)
							self->options_.selectionChanged(*path);
					}
					break;
				}
				case WM_DROPFILES:
				{
					auto drop = reinterpret_cast<HDROP>(wp);
					const UINT count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
					std::vector<std::filesystem::path> files;
					for (UINT i = 0; i < count; ++i)
					{
						wchar_t buffer[32768]{};
						if (DragQueryFileW(drop, i, buffer, std::size(buffer))) files.emplace_back(buffer);
					}
					DragFinish(drop);
					if (!files.empty())
					{
						const auto target = self->node_path(TreeView_GetSelection(self->tree_));
						if (target)
						{
							const bool move = GetKeyState(VK_SHIFT) < 0;
							if (const auto owner = self->owner_.lock())
								perform_file_operation(
									owner, move ? FileOperation::move : FileOperation::copy, files, *target);
							if (self->options_.filesChanged) self->options_.filesChanged();
						}
					}
					break;
				}
				default: break;
				}
				return DefWindowProcW(hwnd, message, wp, lp);
			}

			std::weak_ptr<WindowFrame> owner_;
			ShellTreeOptions options_;
			HWND host_{}, tree_{};
			std::vector<std::unique_ptr<Node>> nodes_;
			bool suppressSelection_{};
		};
	}

	ShellTreePtr create_shell_tree(const WindowFramePtr& owner, ShellTreeOptions options)
	{
		auto result = std::make_shared<Win32ShellTree>(owner, std::move(options));
		return result->valid() ? result : ShellTreePtr{};
	}

	bool begin_file_drag(const WindowFramePtr&, std::span<const std::filesystem::path> paths)
	{
		if (paths.empty()) return false;
		std::vector<core::file_path> files;
		for (const auto& path : paths) files.push_back(desktop::file_path(path));
		return ::platform::begin_file_drag(nullptr, files);
	}
}
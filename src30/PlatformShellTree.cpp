// ImageWalker by Zac Walker
// Implements the Desktop-rooted shell namespace tree and shell drag-and-drop backend.

#include "Platform.h"
#include "PlatformWin32.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <set>

namespace iw::platform
{
	namespace
	{
		using ShellId = std::shared_ptr<void>;

		struct ShellItemData
		{
			ShellId id;
			std::filesystem::path path;
			std::wstring label;
			int image{};
			int selectedImage{};
			bool hasChildren{};
		};

		bool path_equal(const std::filesystem::path& left, const std::filesystem::path& right)
		{
			return !left.empty() && !right.empty() &&
				compare_ordinal_ignore_case(left.native(), right.native()) == 0;
		}

		bool shell_ids_equal(const ShellId& left, const ShellId& right)
		{
			return left && right && ILIsEqual(static_cast<PCIDLIST_ABSOLUTE>(left.get()),
			                                    static_cast<PCIDLIST_ABSOLUTE>(right.get()));
		}

		ShellItemData describe_shell_item(IShellItem* item)
		{
			ShellItemData result;
			PIDLIST_ABSOLUTE id = nullptr;
			if (!item || FAILED(SHGetIDListFromObject(item, &id))) return result;
			result.id = ShellId(id, [](void* value) { CoTaskMemFree(value); });

			PWSTR value = nullptr;
			if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &value)))
			{
				result.label = value;
				CoTaskMemFree(value);
			}
			value = nullptr;
			if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &value)))
			{
				result.path = value;
				CoTaskMemFree(value);
			}

			SHFILEINFOW info{};
			SHGetFileInfoW(reinterpret_cast<LPCWSTR>(result.id.get()), 0, &info, sizeof(info),
			               SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
			result.image = info.iIcon;
			SHGetFileInfoW(reinterpret_cast<LPCWSTR>(result.id.get()), 0, &info, sizeof(info),
			               SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_OPENICON);
			result.selectedImage = info.iIcon;
			SFGAOF attributes = SFGAO_HASSUBFOLDER;
			result.hasChildren = SUCCEEDED(item->GetAttributes(attributes, &attributes)) &&
				(attributes & SFGAO_HASSUBFOLDER) != 0;
			return result;
		}

		DWORD drop_effect(const DWORD keyState, const DWORD allowed, const bool sameVolume)
		{
			if ((keyState & MK_CONTROL) && (allowed & DROPEFFECT_COPY)) return DROPEFFECT_COPY;
			if ((keyState & MK_SHIFT) && (allowed & DROPEFFECT_MOVE)) return DROPEFFECT_MOVE;
			if (sameVolume && (allowed & DROPEFFECT_MOVE)) return DROPEFFECT_MOVE;
			if (allowed & DROPEFFECT_COPY) return DROPEFFECT_COPY;
			if (allowed & DROPEFFECT_MOVE) return DROPEFFECT_MOVE;
			return DROPEFFECT_NONE;
		}

		class WinShellTree;

		class ShellTreeDropTarget final : public IDropTarget
		{
		public:
			explicit ShellTreeDropTarget(WinShellTree& owner) : owner_(owner)
			{
			}

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID identifier, void** object) noexcept override;
			ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++references_; }
			ULONG STDMETHODCALLTYPE Release() noexcept override;
			HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD keyState, POINTL point,
			                                    DWORD* effect) noexcept override;
			HRESULT STDMETHODCALLTYPE DragOver(DWORD keyState, POINTL point, DWORD* effect) noexcept override;
			HRESULT STDMETHODCALLTYPE DragLeave() noexcept override;
			HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD keyState, POINTL point,
			                               DWORD* effect) noexcept override;

		private:
			HRESULT update_target(DWORD keyState, POINTL point, DWORD* effect);
			static bool supports_files(IDataObject* data);
			static std::filesystem::path source_root(IDataObject* data);

			WinShellTree& owner_;
			std::atomic_ulong references_{1};
			HTREEITEM target_{};
			std::filesystem::path sourceRoot_;
			bool hasFiles_{};
		};

		class WinShellTree final : public ShellTree, public std::enable_shared_from_this<WinShellTree>
		{
		public:
			WinShellTree(const WindowFramePtr& owner, ShellTreeOptions options)
				: ownerFrame_(owner), owner_(win32::owner_window(owner)),
				  options_(std::move(options))
			{
				if (!owner_) return;
				tree_ = CreateWindowExW(
					0, WC_TREEVIEWW, nullptr,
					WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES |
					TVS_LINESATROOT | TVS_SHOWSELALWAYS,
					0, 0, 0, 0, owner_, nullptr, GetModuleHandleW(nullptr), nullptr);
				if (!tree_) return;
				SetWindowSubclass(owner_, parent_proc, 1, reinterpret_cast<DWORD_PTR>(this));
				SetWindowSubclass(tree_, tree_proc, 1, reinterpret_cast<DWORD_PTR>(this));
				const auto images = reinterpret_cast<HIMAGELIST>(win32::shell_small_image_list());
				if (images)
					TreeView_SetImageList(tree_, images, TVSIL_NORMAL);
				initialize_roots();
				dropTarget_ = new ShellTreeDropTarget(*this);
				if (FAILED(RegisterDragDrop(tree_, dropTarget_)))
				{
					dropTarget_->Release();
					dropTarget_ = nullptr;
				}
				else dropRegistered_ = true;
			}

			~WinShellTree() override
			{
				++generation_;
				if (dropRegistered_ && tree_)
				{
					RevokeDragDrop(tree_);
					dropRegistered_ = false;
				}
				// DestroyWindow makes the tree send TVN_DELETEITEM and TVN_SELCHANGED to its parent,
				// so the subclass has to go first or notify() re-enters an object being destroyed.
				if (owner_ && IsWindow(owner_)) RemoveWindowSubclass(owner_, parent_proc, 1);
				if (tree_ && IsWindow(tree_))
				{
					DestroyWindow(tree_);
				}
				if (dropTarget_) dropTarget_->Release();
			}

			void set_bounds(const recti bounds) override
			{
				if (tree_) MoveWindow(tree_, bounds.x, bounds.y, bounds.width, bounds.height, TRUE);
			}

			void show(const bool visible) override
			{
				if (tree_) ShowWindow(tree_, visible ? SW_SHOW : SW_HIDE);
			}

			void set_focus() override
			{
				if (tree_) SetFocus(tree_);
			}

			bool has_focus() const override
			{
				return tree_ && GetFocus() == tree_;
			}

			void set_current_path(const std::filesystem::path& path) override
			{
				if (path.empty() || !tree_) return;
				HTREEITEM current = TreeView_GetRoot(tree_);
				if (!current) return;
				TreeView_Expand(tree_, current, TVE_EXPAND);

				const auto root = items_.find(current);
				if (root != items_.end() && path_equal(root->second.path, path))
				{
					select_current(current);
					return;
				}

				const std::array<KNOWNFOLDERID, 5> knownFolders{
					FOLDERID_Pictures, FOLDERID_Documents, FOLDERID_Downloads,
					FOLDERID_Music, FOLDERID_Videos
				};
				for (const auto& identifier : knownFolders)
				{
					IShellItem* folder = nullptr;
					if (FAILED(SHGetKnownFolderItem(identifier, KF_FLAG_DEFAULT, nullptr,
					                                IID_PPV_ARGS(&folder)))) continue;
					const auto data = describe_shell_item(folder);
					folder->Release();
					if (!path_equal(data.path, path)) continue;
					const auto existing = find_child(current, data.id);
					select_current(existing ? existing : add_item(current, data));
					return;
				}

				IShellItem* computer = nullptr;
				if (SUCCEEDED(SHGetKnownFolderItem(FOLDERID_ComputerFolder, KF_FLAG_DEFAULT, nullptr,
				                                    IID_PPV_ARGS(&computer))))
				{
					const auto data = describe_shell_item(computer);
					const auto existing = find_child(current, data.id);
					current = existing ? existing : add_item(current, data);
					computer->Release();
				}
				if (!current) return;

				std::filesystem::path currentPath = path.root_path();
				HTREEITEM next = find_child(current, currentPath);
				if (!next) next = add_path_item(current, currentPath);
				TreeView_Expand(tree_, current, TVE_EXPAND);
				current = next;
				for (const auto& component : path.relative_path())
				{
					if (!current) break;
					currentPath /= component;
					next = find_child(current, currentPath);
					if (!next) next = add_path_item(current, currentPath);
					TreeView_Expand(tree_, current, TVE_EXPAND);
					current = next;
				}
				select_current(current);
			}

			void refresh() override
			{
				++generation_;
				if (!tree_) return;
				const auto current = currentPath_;
				TreeView_DeleteAllItems(tree_);
				items_.clear();
				populated_.clear();
				loading_.clear();
				initialize_roots();
				set_current_path(current);
			}

			HWND window() const { return tree_; }

			std::filesystem::path path_for(const HTREEITEM item) const
			{
				const auto found = items_.find(item);
				return found == items_.end() ? std::filesystem::path{} : found->second.path;
			}

			HTREEITEM hit_test(const POINTL point) const
			{
				TVHITTESTINFO hit{};
				hit.pt = {point.x, point.y};
				ScreenToClient(tree_, &hit.pt);
				return TreeView_HitTest(tree_, &hit);
			}

			void select_drop_target(const HTREEITEM item) const
			{
				TreeView_SelectDropTarget(tree_, item);
			}

			bool perform_drop(IDataObject* data, const HTREEITEM target, const DWORD effect)
			{
				const auto destination = path_for(target);
				if (destination.empty()) return false;
				FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
				STGMEDIUM medium{};
				if (FAILED(data->GetData(&format, &medium))) return false;
				const auto operation = effect == DROPEFFECT_MOVE ? FileOperation::move : FileOperation::copy;
				const bool changed = perform_file_operation(
					ownerFrame_, operation, win32::dropped_files(medium.hGlobal), destination);
				ReleaseStgMedium(&medium);
				if (changed && options_.filesChanged) options_.filesChanged();
				return changed;
			}

		private:
			static LRESULT CALLBACK parent_proc(const HWND window, const UINT message, const WPARAM wparam,
			                                    const LPARAM lparam, const UINT_PTR subclassId,
			                                    const DWORD_PTR data) noexcept
			{
				try
				{
					auto* self = reinterpret_cast<WinShellTree*>(data);
					if (message == WM_NOTIFY)
					{
						const auto* notification = reinterpret_cast<const NMHDR*>(lparam);
						if (notification->hwndFrom == self->tree_)
						{
							self->notify(*notification);
							return 0;
						}
					}
					if (message == WM_NCDESTROY)
					{
						self->owner_ = nullptr;
						RemoveWindowSubclass(window, parent_proc, subclassId);
					}
				}
				catch (...) { return 0; }
				return DefSubclassProc(window, message, wparam, lparam);
			}

			static LRESULT CALLBACK tree_proc(const HWND window, const UINT message, const WPARAM wparam,
			                                  const LPARAM lparam, const UINT_PTR subclassId,
			                                  const DWORD_PTR data) noexcept
			{
				try
				{
					auto* self = reinterpret_cast<WinShellTree*>(data);
					if (message == WM_NCDESTROY)
					{
						if (self->dropRegistered_)
						{
							RevokeDragDrop(window);
							self->dropRegistered_ = false;
						}
						self->tree_ = nullptr;
						RemoveWindowSubclass(window, tree_proc, subclassId);
					}
				}
				catch (...) { return 0; }
				return DefSubclassProc(window, message, wparam, lparam);
			}

			void initialize_roots()
			{
				IShellItem* desktop = nullptr;
				if (SUCCEEDED(SHGetKnownFolderItem(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr,
				                                    IID_PPV_ARGS(&desktop))))
				{
					add_item(TVI_ROOT, describe_shell_item(desktop));
					desktop->Release();
				}
			}

			HTREEITEM add_item(const HTREEITEM parent, const ShellItemData& data)
			{
				if (!data.id) return nullptr;
				std::wstring label = data.label.empty() ? L"Desktop" : data.label;
				TVINSERTSTRUCTW insert{};
				insert.hParent = parent;
				insert.hInsertAfter = TVI_LAST;
				insert.item.mask = TVIF_TEXT | TVIF_CHILDREN | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
				insert.item.pszText = label.data();
				insert.item.cChildren = data.hasChildren ? 1 : 0;
				insert.item.iImage = data.image;
				insert.item.iSelectedImage = data.selectedImage;
				const auto item = TreeView_InsertItem(tree_, &insert);
				if (item) items_.insert_or_assign(item, data);
				return item;
			}

			HTREEITEM add_path_item(const HTREEITEM parent, const std::filesystem::path& path)
			{
				IShellItem* shellItem = nullptr;
				if (path.empty() || FAILED(SHCreateItemFromParsingName(
					path.c_str(), nullptr, IID_PPV_ARGS(&shellItem)))) return nullptr;
				const auto item = add_item(parent, describe_shell_item(shellItem));
				shellItem->Release();
				return item;
			}

			HTREEITEM find_child(const HTREEITEM parent, const std::filesystem::path& path) const
			{
				for (auto child = TreeView_GetChild(tree_, parent); child;
				     child = TreeView_GetNextSibling(tree_, child))
				{
					const auto found = items_.find(child);
					if (found != items_.end() && path_equal(found->second.path, path)) return child;
				}
				return nullptr;
			}

			HTREEITEM find_child(const HTREEITEM parent, const ShellId& id) const
			{
				for (auto child = TreeView_GetChild(tree_, parent); child;
				     child = TreeView_GetNextSibling(tree_, child))
				{
					const auto found = items_.find(child);
					if (found != items_.end() && shell_ids_equal(found->second.id, id)) return child;
				}
				return nullptr;
			}

			void select_current(const HTREEITEM item)
			{
				if (!item) return;
				synchronizing_ = true;
				TreeView_SelectItem(tree_, item);
				TreeView_EnsureVisible(tree_, item);
				synchronizing_ = false;
				currentPath_ = path_for(item);
			}

			void populate(const HTREEITEM item)
			{
				if (!item || populated_.contains(item) || loading_.contains(item)) return;
				const auto found = items_.find(item);
				if (found == items_.end() || !found->second.id) return;
				const auto parent = found->second;
				loading_.insert(item);
				const auto weak = weak_from_this();
				const auto generation = generation_;
				const bool queued = queue_work(WorkQueue::shell, [weak, item, parent, generation]
				{
					std::vector<ShellItemData> folders;
					try
					{
						IShellItem* parentItem = nullptr;
						IEnumShellItems* children = nullptr;
						if (SUCCEEDED(SHCreateItemFromIDList(
							static_cast<PCIDLIST_ABSOLUTE>(parent.id.get()), IID_PPV_ARGS(&parentItem))))
						{
							if (SUCCEEDED(parentItem->BindToHandler(
								nullptr, BHID_EnumItems, IID_PPV_ARGS(&children))))
							{
								IShellItem* child = nullptr;
								while (children->Next(1, &child, nullptr) == S_OK)
								{
									SFGAOF attributes = SFGAO_FOLDER;
									if (SUCCEEDED(child->GetAttributes(attributes, &attributes)) &&
										(attributes & SFGAO_FOLDER))
										folders.push_back(describe_shell_item(child));
									child->Release();
								}
								children->Release();
							}
							parentItem->Release();
						}
						std::ranges::sort(folders, {}, [](const ShellItemData& data)
						{
							return data.label;
						});
					}
					catch (...) { folders.clear(); }
					queue_ui([weak, item, parent, generation, folders = std::move(folders)]
					{
						const auto self = weak.lock();
						if (!self) return;
						// The claim belongs to the job, not to its result: a node left claimed here
						// never expands again, silently, for the rest of the session.
						self->loading_.erase(item);
						if (self->generation_ != generation) return;
						const auto found = self->items_.find(item);
						if (found == self->items_.end() || !shell_ids_equal(found->second.id, parent.id)) return;
						self->populated_.insert(item);
						for (const auto& folder : folders)
							if (!self->find_child(item, folder.id)) self->add_item(item, folder);
						TreeView_SortChildren(self->tree_, item, FALSE);
						if (!TreeView_GetChild(self->tree_, item))
						{
							TVITEMW update{};
							update.mask = TVIF_CHILDREN;
							update.hItem = item;
							update.cChildren = 0;
							TreeView_SetItem(self->tree_, &update);
						}
					});
				});
				if (!queued) loading_.erase(item);
			}

			void notify(const NMHDR& notification)
			{
				if (notification.code == TVN_ITEMEXPANDINGW)
				{
					const auto& expanding = reinterpret_cast<const NMTREEVIEWW&>(notification);
					if (expanding.action == TVE_EXPAND) populate(expanding.itemNew.hItem);
				}
				else if (notification.code == TVN_SELCHANGEDW && !synchronizing_)
				{
					const auto& changed = reinterpret_cast<const NMTREEVIEWW&>(notification);
					currentPath_ = path_for(changed.itemNew.hItem);
					if (!currentPath_.empty() && options_.selectionChanged)
						options_.selectionChanged(currentPath_);
				}
			}

			WindowFramePtr ownerFrame_;
			HWND owner_{};
			HWND tree_{};
			ShellTreeOptions options_;
			ShellTreeDropTarget* dropTarget_{};
			bool dropRegistered_{};
			std::map<HTREEITEM, ShellItemData> items_;
			std::set<HTREEITEM> populated_;
			std::set<HTREEITEM> loading_;
			std::filesystem::path currentPath_;
			std::uint64_t generation_{};
			bool synchronizing_{};
		};

		HRESULT ShellTreeDropTarget::QueryInterface(REFIID identifier, void** object) noexcept
		{
			if (!object) return E_POINTER;
			*object = nullptr;
			if (identifier == IID_IUnknown || identifier == IID_IDropTarget)
			{
				*object = static_cast<IDropTarget*>(this);
				AddRef();
				return S_OK;
			}
			return E_NOINTERFACE;
		}

		ULONG ShellTreeDropTarget::Release() noexcept
		{
			const ULONG references = --references_;
			if (!references) delete this;
			return references;
		}

		bool ShellTreeDropTarget::supports_files(IDataObject* data)
		{
			if (!data) return false;
			FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
			return SUCCEEDED(data->QueryGetData(&format));
		}

		std::filesystem::path ShellTreeDropTarget::source_root(IDataObject* data)
		{
			if (!data) return {};
			FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
			STGMEDIUM medium{};
			if (FAILED(data->GetData(&format, &medium))) return {};
			const auto files = win32::dropped_files(medium.hGlobal);
			ReleaseStgMedium(&medium);
			return files.empty() ? std::filesystem::path{} : files.front().root_path();
		}

		HRESULT ShellTreeDropTarget::update_target(const DWORD keyState, const POINTL point, DWORD* effect)
		{
			if (!effect) return E_POINTER;
			target_ = owner_.hit_test(point);
			if (!hasFiles_ || owner_.path_for(target_).empty())
			{
				target_ = nullptr;
				*effect = DROPEFFECT_NONE;
			}
			else *effect = drop_effect(keyState, *effect,
				path_equal(sourceRoot_, owner_.path_for(target_).root_path()));
			owner_.select_drop_target(target_);
			return S_OK;
		}

		HRESULT ShellTreeDropTarget::DragEnter(IDataObject* data, const DWORD keyState, const POINTL point,
		                                       DWORD* effect) noexcept
		{
			try
			{
				hasFiles_ = supports_files(data);
				sourceRoot_ = hasFiles_ ? source_root(data) : std::filesystem::path{};
				return update_target(keyState, point, effect);
			}
			catch (...) { return E_UNEXPECTED; }
		}

		HRESULT ShellTreeDropTarget::DragOver(const DWORD keyState, const POINTL point, DWORD* effect) noexcept
		{
			try { return update_target(keyState, point, effect); }
			catch (...) { return E_UNEXPECTED; }
		}

		HRESULT ShellTreeDropTarget::DragLeave() noexcept
		{
			try
			{
				owner_.select_drop_target(nullptr);
				target_ = nullptr;
				sourceRoot_.clear();
				hasFiles_ = false;
				return S_OK;
			}
			catch (...) { return E_UNEXPECTED; }
		}

		HRESULT ShellTreeDropTarget::Drop(IDataObject* data, const DWORD keyState, const POINTL point,
		                                  DWORD* effect) noexcept
		{
			try
			{
				update_target(keyState, point, effect);
				const HTREEITEM target = target_;
				owner_.select_drop_target(nullptr);
				target_ = nullptr;
				if (!target || !effect || *effect == DROPEFFECT_NONE) return S_OK;
				if (!owner_.perform_drop(data, target, *effect)) *effect = DROPEFFECT_NONE;
				return S_OK;
			}
			catch (...) { return E_UNEXPECTED; }
		}

		class FileDragSource final : public IDropSource
		{
		public:
			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID identifier, void** object) override
			{
				if (!object) return E_POINTER;
				*object = nullptr;
				if (identifier == IID_IUnknown || identifier == IID_IDropSource)
				{
					*object = static_cast<IDropSource*>(this);
					AddRef();
					return S_OK;
				}
				return E_NOINTERFACE;
			}

			ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG references = --references_;
				if (!references) delete this;
				return references;
			}

			HRESULT STDMETHODCALLTYPE QueryContinueDrag(const BOOL escapePressed,
			                                            const DWORD keyState) override
			{
				if (escapePressed) return DRAGDROP_S_CANCEL;
				if (!(keyState & MK_LBUTTON)) return DRAGDROP_S_DROP;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override
			{
				return DRAGDROP_S_USEDEFAULTCURSORS;
			}

		private:
			std::atomic_ulong references_{1};
		};
	}

	ShellTreePtr create_shell_tree(const WindowFramePtr& owner, ShellTreeOptions options)
	{
		if (!owner) return {};
		auto tree = std::make_shared<WinShellTree>(owner, std::move(options));
		return tree->window() ? tree : ShellTreePtr{};
	}

	bool begin_file_drag(const WindowFramePtr&, const std::span<const std::filesystem::path> paths)
	{
		if (paths.empty()) return false;
		const auto parentPath = paths.front().parent_path();
		if (parentPath.empty() || std::ranges::any_of(paths, [&](const auto& path)
		{
			return !path_equal(parentPath, path.parent_path());
		})) return false;
		std::vector<PIDLIST_ABSOLUTE> absoluteItems;
		absoluteItems.reserve(paths.size());
		for (const auto& path : paths)
		{
			PIDLIST_ABSOLUTE item = nullptr;
			if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &item, 0, nullptr))) break;
			absoluteItems.push_back(item);
		}
		if (absoluteItems.size() != paths.size())
		{
			for (const auto item : absoluteItems) CoTaskMemFree(item);
			return false;
		}
		PIDLIST_ABSOLUTE parent = ILClone(absoluteItems.front());
		ILRemoveLastID(parent);
		std::vector<PCUITEMID_CHILD> children;
		children.reserve(absoluteItems.size());
		for (const auto item : absoluteItems) children.push_back(ILFindLastID(item));
		IDataObject* data = nullptr;
		bool moved{};
		if (SUCCEEDED(SHCreateDataObject(parent, static_cast<UINT>(children.size()), children.data(), nullptr,
			IID_PPV_ARGS(&data))))
		{
			auto* source = new FileDragSource();
			DWORD effect = DROPEFFECT_NONE;
			DoDragDrop(data, source, DROPEFFECT_COPY | DROPEFFECT_MOVE, &effect);
			moved = effect == DROPEFFECT_MOVE;
			source->Release();
			data->Release();
		}
		CoTaskMemFree(parent);
		for (const auto item : absoluteItems) CoTaskMemFree(item);
		return moved;
	}
}

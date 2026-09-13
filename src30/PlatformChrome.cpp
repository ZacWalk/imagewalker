// ImageWalker by Zac Walker
// Implements the Windows command-surface backend for menus, toolbars, breadcrumbs, and status controls.

#include "Platform.h"
#include "Paths.h"
#include "PlatformWin32.h"

#include <commctrl.h>
#include <algorithm>
#include <map>

namespace iw::platform
{
	MenuItem MenuItem::action(CommandPtr command)
	{
		MenuItem result;
		result.command = std::move(command);
		return result;
	}

	MenuItem MenuItem::submenu(std::wstring label, std::vector<MenuItem> items)
	{
		MenuItem result;
		result.kind = MenuItemKind::submenu;
		result.name = std::move(label);
		result.children = std::move(items);
		return result;
	}

	MenuItem MenuItem::separator()
	{
		MenuItem result;
		result.kind = MenuItemKind::separator;
		return result;
	}

	ToolbarItem ToolbarItem::action(CommandPtr command)
	{
		ToolbarItem result;
		result.command = std::move(command);
		return result;
	}

	ToolbarItem ToolbarItem::menu(CommandPtr command, std::vector<MenuItem> items)
	{
		ToolbarItem result;
		result.kind = ToolbarItemKind::menu;
		result.command = std::move(command);
		result.menuItems = std::move(items);
		return result;
	}

	ToolbarItem ToolbarItem::separator()
	{
		ToolbarItem result;
		result.kind = ToolbarItemKind::separator;
		return result;
	}

	namespace
	{
		using NativeCommandId = std::uint32_t;
		constexpr NativeCommandId breadcrumbCommandFirst = 60000;
		constexpr size_t maxBreadcrumbIndex = (UINT16_MAX - breadcrumbCommandFirst) / 2;

		NativeCommandId breadcrumb_folder_command(const size_t index)
		{
			return breadcrumbCommandFirst + static_cast<NativeCommandId>(index * 2);
		}

		NativeCommandId breadcrumb_menu_command(const size_t rightIndex)
		{
			return breadcrumbCommandFirst + static_cast<NativeCommandId>(rightIndex * 2 - 1);
		}

		int scale_for_dpi(const int value, const unsigned int dpi)
		{
			return MulDiv(value, static_cast<int>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI),
			              USER_DEFAULT_SCREEN_DPI);
		}

		std::wstring shortcut_text(const Shortcut shortcut)
		{
			std::wstring result;
			if (shortcut.control) result += L"Ctrl+";
			if (shortcut.shift) result += L"Shift+";
			if (shortcut.alt) result += L"Alt+";
			switch (shortcut.key)
			{
			case ShortcutKey::a: result += L'A'; break;
			case ShortcutKey::b: result += L'B'; break;
			case ShortcutKey::c: result += L'C'; break;
			case ShortcutKey::e: result += L'E'; break;
			case ShortcutKey::n: result += L'N'; break;
			case ShortcutKey::o: result += L'O'; break;
			case ShortcutKey::r: result += L'R'; break;
			case ShortcutKey::v: result += L'V'; break;
			case ShortcutKey::x: result += L'X'; break;
			case ShortcutKey::z: result += L'Z'; break;
			case ShortcutKey::deleteKey: result += L"Del"; break;
			case ShortcutKey::enterKey: result += L"Enter"; break;
			case ShortcutKey::f1: result += L"F1"; break;
			case ShortcutKey::f2: result += L"F2"; break;
			case ShortcutKey::f3: result += L"F3"; break;
			case ShortcutKey::f4: result += L"F4"; break;
			case ShortcutKey::f5: result += L"F5"; break;
			case ShortcutKey::f6: result += L"F6"; break;
			case ShortcutKey::f7: result += L"F7"; break;
			case ShortcutKey::f8: result += L"F8"; break;
			case ShortcutKey::f9: result += L"F9"; break;
			case ShortcutKey::f11: result += L"F11"; break;
			case ShortcutKey::f12: result += L"F12"; break;
			default: break;
			}
			return result;
		}

		HWND create_text_toolbar(const HWND parent)
		{
			const HWND toolbar = CreateWindowExW(
				0, TOOLBARCLASSNAMEW, nullptr,
				WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TRANSPARENT | TBSTYLE_TOOLTIPS | TBSTYLE_LIST |
				CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN,
				0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
			if (!toolbar) return nullptr;
			SendMessageW(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
			SendMessageW(toolbar, TB_SETEXTENDEDSTYLE, 0,
			             TBSTYLE_EX_DRAWDDARROWS | TBSTYLE_EX_MIXEDBUTTONS);
			return toolbar;
		}

		void clear_toolbar(const HWND toolbar)
		{
			if (!toolbar) return;
			while (SendMessageW(toolbar, TB_BUTTONCOUNT, 0, 0) > 0)
				SendMessageW(toolbar, TB_DELETEBUTTON, 0, 0);
		}

		class WinCommandSurface final : public CommandSurface,
		                                public std::enable_shared_from_this<WinCommandSurface>
		{
		public:
			explicit WinCommandSurface(const WindowFramePtr& owner)
				: ownerFrame_(owner), owner_(win32::owner_window(owner))
			{
				if (!owner_) return;
				top_ = CreateWindowExW(0, L"STATIC", nullptr,
				                       WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
				                       0, 0, 0, 0, owner_, nullptr, GetModuleHandleW(nullptr), nullptr);
				status_ = CreateWindowExW(0, L"STATIC", nullptr,
				                          WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
				                          0, 0, 0, 0, owner_, nullptr, GetModuleHandleW(nullptr), nullptr);
				topBar_ = create_text_toolbar(top_);
				statusBar_ = create_text_toolbar(status_);
				rebuild_images();
				slider_ = CreateWindowExW(
					0, TRACKBAR_CLASSW, nullptr,
					WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS | TBS_TOOLTIPS,
					0, 0, 0, 0, status_, nullptr, GetModuleHandleW(nullptr), nullptr);
				if (slider_)
				{
					SendMessageW(slider_, TBM_SETRANGE, TRUE, MAKELONG(64, 256));
					SendMessageW(slider_, TBM_SETPAGESIZE, 0, 16);
					SendMessageW(slider_, TBM_SETLINESIZE, 0, 8);
				}
				SetWindowSubclass(owner_, owner_proc, 1, reinterpret_cast<DWORD_PTR>(this));
				if (top_) SetWindowSubclass(top_, host_proc, 1, reinterpret_cast<DWORD_PTR>(this));
				if (status_) SetWindowSubclass(status_, host_proc, 1, reinterpret_cast<DWORD_PTR>(this));
			}

			~WinCommandSurface() override
			{
				if (owner_ && IsWindow(owner_))
				{
					SetMenu(owner_, nullptr);
					RemoveWindowSubclass(owner_, owner_proc, 1);
				}
				if (menu_) DestroyMenu(menu_);
				if (top_ && IsWindow(top_)) DestroyWindow(top_);
				if (status_ && IsWindow(status_)) DestroyWindow(status_);
				if (images_) ImageList_Destroy(images_);
			}

			void set_menu(std::vector<Menu> menus) override
			{
				menus_ = std::move(menus);
				rebuild_surfaces();
			}

			void set_toolbar(std::vector<ToolbarItem> items) override
			{
				toolbar_ = std::move(items);
				rebuild_surfaces();
			}

			void set_task_toolbar(std::vector<ToolbarItem> items) override
			{
				taskToolbar_ = std::move(items);
				taskActive_ = true;
				rebuild_surfaces();
			}

			void clear_task_toolbar() override
			{
				if (!taskActive_) return;
				taskToolbar_.clear();
				taskActive_ = false;
				rebuild_surfaces();
			}

			void set_status_toolbar(std::vector<ToolbarItem> items) override
			{
				statusToolbar_ = std::move(items);
				rebuild_surfaces();
			}

			void set_breadcrumbs(std::vector<Breadcrumb> breadcrumbs,
			                     std::function<void(const std::filesystem::path&)> selected) override
			{
				breadcrumbs_ = std::move(breadcrumbs);
				breadcrumbSelected_ = std::move(selected);
				breadcrumbIcon_ = breadcrumbs_.empty() ? -1 : win32::shell_icon_index(breadcrumbs_.back().path);
				rebuild_toolbar(topBar_, active_toolbar(), !taskActive_);
			}

			void set_status(StatusInfo status) override
			{
				statusInfo_ = std::move(status);
				if (status_) InvalidateRect(status_, nullptr, FALSE);
			}

			void set_thumbnail_size(const int value, std::function<void(int)> changed) override
			{
				thumbnailChanged_ = std::move(changed);
				if (slider_ && SendMessageW(slider_, TBM_GETPOS, 0, 0) != value)
					SendMessageW(slider_, TBM_SETPOS, TRUE, value);
			}

			recti layout(const recti client, const bool fullscreen) override
			{
				if (!top_ || !status_) return client;
				recti currentClient = client;
				if (fullscreen != fullscreen_)
				{
					fullscreen_ = fullscreen;
					SetMenu(owner_, fullscreen ? nullptr : menu_);
					DrawMenuBar(owner_);
					RECT bounds{};
					if (GetClientRect(owner_, &bounds))
						currentClient = {bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top};
				}
				ShowWindow(top_, fullscreen ? SW_HIDE : SW_SHOW);
				ShowWindow(status_, fullscreen ? SW_HIDE : SW_SHOW);
				if (fullscreen) return currentClient;
				const int topHeight = (std::min)(scale_for_dpi(34, dpi_), currentClient.height);
				const int statusHeight = (std::min)(scale_for_dpi(26, dpi_), currentClient.height - topHeight);
				MoveWindow(top_, currentClient.x, currentClient.y, currentClient.width, topHeight, TRUE);
				MoveWindow(status_, currentClient.x, currentClient.bottom() - statusHeight, currentClient.width, statusHeight, TRUE);
				layout_top(currentClient.width, topHeight);
				layout_status(currentClient.width, statusHeight);
				return {
					currentClient.x, currentClient.y + topHeight, currentClient.width,
					currentClient.height - topHeight - statusHeight
				};
			}

			void set_dpi(const unsigned int dpi) override
			{
				const unsigned int effective = dpi ? dpi : USER_DEFAULT_SCREEN_DPI;
				if (dpi_ == effective) return;
				dpi_ = effective;
				rebuild_images();
				rebuild_toolbar(topBar_, active_toolbar(), !taskActive_);
				rebuild_toolbar(statusBar_, statusToolbar_, false);
			}

			void refresh() override
			{
				refresh_menu(menu_);
				refresh_toolbar(topBar_);
				refresh_toolbar(statusBar_);
				if (top_) InvalidateRect(top_, nullptr, FALSE);
				if (status_) InvalidateRect(status_, nullptr, FALSE);
			}

		private:
			const std::vector<ToolbarItem>& active_toolbar() const { return taskActive_ ? taskToolbar_ : toolbar_; }

			void rebuild_images()
			{
				breadcrumbImageIndex_ = -1;
				if (images_)
				{
					SendMessageW(topBar_, TB_SETIMAGELIST, 0, 0);
					SendMessageW(statusBar_, TB_SETIMAGELIST, 0, 0);
					ImageList_Destroy(images_);
					images_ = nullptr;
				}
				const auto toolbarStrip = load_bitmap_resource(BitmapAsset::toolbarSymbols);
				const auto statusStrip = load_bitmap_resource(BitmapAsset::statusSymbols);
				constexpr int sourceSize = 24;
				if (toolbarStrip.size.height != sourceSize || toolbarStrip.size.width < sourceSize ||
				    toolbarStrip.pixels.empty() || statusStrip.size.height != sourceSize ||
				    statusStrip.size.width < sourceSize || statusStrip.pixels.empty()) return;
				const int iconSize = scale_for_dpi(20, dpi_);
				const int toolbarCount = toolbarStrip.size.width / sourceSize;
				const int count = toolbarCount + statusStrip.size.width / sourceSize;
				images_ = ImageList_Create(iconSize, iconSize, ILC_COLOR32 | ILC_MASK, count + 1, 0);
				if (!images_) return;
				const COLORREF text = GetSysColor(COLOR_BTNTEXT);
				for (int icon = 0; icon < count; ++icon)
				{
					const auto& strip = icon < toolbarCount ? toolbarStrip : statusStrip;
					const int stripIcon = icon < toolbarCount ? icon : icon - toolbarCount;
					BITMAPINFO info{};
					info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
					info.bmiHeader.biWidth = iconSize;
					info.bmiHeader.biHeight = -iconSize;
					info.bmiHeader.biPlanes = 1;
					info.bmiHeader.biBitCount = 32;
					info.bmiHeader.biCompression = BI_RGB;
					std::uint32_t* pixels = nullptr;
					const HBITMAP bitmap = CreateDIBSection(
						nullptr, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
					if (!bitmap || !pixels)
					{
						if (bitmap) DeleteObject(bitmap);
						continue;
					}
					for (int y = 0; y < iconSize; ++y)
						for (int x = 0; x < iconSize; ++x)
						{
							const int sourceX = stripIcon * sourceSize + x * sourceSize / iconSize;
							const int sourceY = y * sourceSize / iconSize;
							const std::uint32_t alpha = strip.pixels[
								static_cast<size_t>(sourceY) * strip.size.width + sourceX] >> 24;
							const auto channel = [alpha](const std::uint32_t value)
							{
								return value * alpha / 255;
							};
							pixels[static_cast<size_t>(y) * iconSize + x] = alpha << 24 |
								channel(GetRValue(text)) << 16 | channel(GetGValue(text)) << 8 |
								channel(GetBValue(text));
						}
					ImageList_Add(images_, bitmap, nullptr);
					DeleteObject(bitmap);
				}

				BITMAPINFO blankInfo{};
				blankInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				blankInfo.bmiHeader.biWidth = iconSize;
				blankInfo.bmiHeader.biHeight = -iconSize;
				blankInfo.bmiHeader.biPlanes = 1;
				blankInfo.bmiHeader.biBitCount = 32;
				blankInfo.bmiHeader.biCompression = BI_RGB;
				std::uint32_t* blankPixels = nullptr;
				const HBITMAP blankBitmap = CreateDIBSection(
					nullptr, &blankInfo, DIB_RGB_COLORS, reinterpret_cast<void**>(&blankPixels), nullptr, 0);
				const HBITMAP blankMask = CreateBitmap(iconSize, iconSize, 1, 1, nullptr);
				if (blankBitmap && blankPixels && blankMask)
				{
					std::fill_n(blankPixels, static_cast<size_t>(iconSize) * iconSize, 0u);
					const HDC maskDc = CreateCompatibleDC(nullptr);
					if (maskDc)
					{
						const HGDIOBJ previousMask = SelectObject(maskDc, blankMask);
						PatBlt(maskDc, 0, 0, iconSize, iconSize, WHITENESS);
						SelectObject(maskDc, previousMask);
						DeleteDC(maskDc);
						breadcrumbImageIndex_ = ImageList_Add(images_, blankBitmap, blankMask);
					}
				}
				if (blankBitmap) DeleteObject(blankBitmap);
				if (blankMask) DeleteObject(blankMask);
				SendMessageW(topBar_, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(images_));
				SendMessageW(statusBar_, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(images_));
			}

			static LRESULT CALLBACK owner_proc(const HWND window, const UINT message, const WPARAM wparam,
			                                   const LPARAM lparam, const UINT_PTR subclassId,
			                                   const DWORD_PTR data) noexcept
			{
				try
				{
					auto* self = reinterpret_cast<WinCommandSurface*>(data);
					if (message == WM_NCDESTROY)
					{
						self->owner_ = nullptr;
						RemoveWindowSubclass(window, owner_proc, subclassId);
						return DefSubclassProc(window, message, wparam, lparam);
					}
					const auto lifetime = self->shared_from_this();
					// lparam is 0 for a menu and for an accelerator, and the control handle otherwise.
					// HIWORD(wparam) is 1 for an accelerator, so testing it for 0 drops every shortcut.
					if (message == WM_COMMAND && lparam == 0 && self->invoke(LOWORD(wparam))) return 0;
					if (message == WM_INITMENUPOPUP) self->refresh_menu(reinterpret_cast<HMENU>(wparam));
				}
				catch (...) { return 0; }
				return DefSubclassProc(window, message, wparam, lparam);
			}

			static LRESULT CALLBACK host_proc(const HWND window, const UINT message, const WPARAM wparam,
			                                  const LPARAM lparam, const UINT_PTR subclassId,
			                                  const DWORD_PTR data) noexcept
			{
				try
				{
					auto* self = reinterpret_cast<WinCommandSurface*>(data);
					// The hosts are destroyed from the surface destructor, so a reference is unavailable here.
					if (message == WM_NCDESTROY)
					{
						RemoveWindowSubclass(window, host_proc, subclassId);
						return DefSubclassProc(window, message, wparam, lparam);
					}
					const auto lifetime = self->shared_from_this();
					if (message == WM_COMMAND)
					{
						const NativeCommandId command = LOWORD(wparam);
						if (command >= breadcrumbCommandFirst)
						{
							const NativeCommandId offset = command - breadcrumbCommandFirst;
							if (offset % 2 == 0)
							{
								const size_t index = offset / 2;
								if (index < self->breadcrumbs_.size() && self->breadcrumbSelected_)
									self->breadcrumbSelected_(self->breadcrumbs_[index].path);
							}
							else self->show_toolbar_menu(self->topBar_, command);
							return 0;
						}
						if (self->invoke(command)) return 0;
					}
					if (message == WM_HSCROLL && reinterpret_cast<HWND>(lparam) == self->slider_)
					{
						if (self->thumbnailChanged_)
							self->thumbnailChanged_(static_cast<int>(SendMessageW(
								self->slider_, TBM_GETPOS, 0, 0)));
						return 0;
					}
					if (message == WM_NOTIFY)
					{
						const auto* header = reinterpret_cast<const NMHDR*>(lparam);
						if (header->hwndFrom == self->topBar_ && header->code == NM_CUSTOMDRAW)
							return self->custom_draw_breadcrumb(
								*reinterpret_cast<NMTBCUSTOMDRAW*>(lparam));
						if (header->code == TBN_DROPDOWN)
						{
							const auto* notification = reinterpret_cast<const NMTOOLBARW*>(lparam);
							self->show_toolbar_menu(notification->hdr.hwndFrom, notification->iItem);
							return TBDDRET_DEFAULT;
						}
						if (header->code == TTN_GETDISPINFOW)
						{
							auto* info = reinterpret_cast<NMTTDISPINFOW*>(lparam);
							const NativeCommandId id = static_cast<NativeCommandId>(info->hdr.idFrom);
							if (id >= breadcrumbCommandFirst && (id - breadcrumbCommandFirst) % 2 != 0)
							{
								info->lpszText = const_cast<LPWSTR>(L"Show peer folders");
								return 0;
							}
							const auto found = self->commands_.find(
								id);
							if (found != self->commands_.end())
								info->lpszText = const_cast<LPWSTR>(found->second->tooltip.c_str());
							return 0;
						}
					}
					if (message == WM_PAINT) { self->paint_host(window); return 0; }
					if (message == WM_ERASEBKGND)
					{
						RECT client{};
						GetClientRect(window, &client);
						FillRect(reinterpret_cast<HDC>(wparam), &client, GetSysColorBrush(COLOR_3DFACE));
						return 1;
					}
				}
				catch (...) { return 0; }
				return DefSubclassProc(window, message, wparam, lparam);
			}

			bool invoke(const NativeCommandId id)
			{
				const auto found = commands_.find(id);
				if (found == commands_.end()) return false;
				const auto& command = found->second;
				if (command->enabled && !command->enabled()) return true;
				if (command->invoke) command->invoke();
				if (owner_) refresh();
				return true;
			}

			LRESULT custom_draw_breadcrumb(NMTBCUSTOMDRAW& draw) const
			{
				if (draw.nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
				if (draw.nmcd.dwItemSpec != breadcrumb_folder_command(0)) return CDRF_DODEFAULT;
				if (draw.nmcd.dwDrawStage == CDDS_ITEMPREPAINT) return CDRF_NOTIFYPOSTPAINT;
				if (draw.nmcd.dwDrawStage == CDDS_ITEMPOSTPAINT)
					draw_breadcrumb_icon(draw);
				return CDRF_DODEFAULT;
			}

			void draw_breadcrumb_icon(const NMTBCUSTOMDRAW& draw) const
			{
				if (breadcrumbImageIndex_ < 0 || breadcrumbIcon_ < 0) return;
				const auto shellImages = reinterpret_cast<HIMAGELIST>(win32::shell_small_image_list());
				int shellWidth{};
				int shellHeight{};
				int cellWidth{};
				int cellHeight{};
				if (!shellImages || !images_ ||
					!ImageList_GetIconSize(shellImages, &shellWidth, &shellHeight) ||
					!ImageList_GetIconSize(images_, &cellWidth, &cellHeight)) return;
				const int cellLeft = draw.rcText.left - draw.iListGap - cellWidth;
				const int x = cellLeft + (cellWidth - shellWidth) / 2;
				const int y = draw.nmcd.rc.top + (draw.nmcd.rc.bottom - draw.nmcd.rc.top - shellHeight) / 2;
				const UINT style = draw.nmcd.uItemState & CDIS_DISABLED ? ILD_BLEND50 : ILD_TRANSPARENT;
				ImageList_Draw(shellImages, breadcrumbIcon_, draw.nmcd.hdc, x, y, style);
			}

			NativeCommandId command_id(const CommandPtr& command)
			{
				if (!command) return 0;
				if (const auto found = commandIds_.find(command.get()); found != commandIds_.end())
					return found->second;
				if (nextCommandId_ >= breadcrumbCommandFirst) return 0;
				const NativeCommandId id = nextCommandId_++;
				commandIds_.emplace(command.get(), id);
				commands_.emplace(id, command);
				return id;
			}

			void rebuild_surfaces()
			{
				commands_.clear();
				commandIds_.clear();
				nextCommandId_ = 1;
				rebuild_menu();
				rebuild_toolbar(topBar_, active_toolbar(), !taskActive_);
				rebuild_toolbar(statusBar_, statusToolbar_, false);
				std::vector<CommandAccelerator> accelerators;
				for (const auto& [id, command] : commands_)
					if (command->shortcut.key != ShortcutKey::none)
						accelerators.push_back({command->shortcut, static_cast<std::uint16_t>(id)});
				ownerFrame_->set_accelerators(accelerators);
			}

			HMENU create_popup(const std::vector<MenuItem>& items)
			{
				const HMENU popup = CreatePopupMenu();
				for (const auto& item : items)
				{
					if (item.kind == MenuItemKind::separator)
					{
						AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
						continue;
					}
					if (item.kind == MenuItemKind::submenu)
					{
						const HMENU child = create_popup(item.children);
						AppendMenuW(popup, MF_POPUP, reinterpret_cast<UINT_PTR>(child),
						            item.name.c_str());
						continue;
					}
					if (!item.command) continue;
					std::wstring label = item.command->name;
					const auto shortcut = shortcut_text(item.command->shortcut);
					if (!shortcut.empty()) label += L'\t' + shortcut;
					const NativeCommandId id = command_id(item.command);
					if (id) AppendMenuW(popup, MF_STRING, id, label.c_str());
				}
				return popup;
			}

			void rebuild_menu()
			{
				if (!owner_) return;
				SetMenu(owner_, nullptr);
				if (menu_) DestroyMenu(menu_);
				menu_ = CreateMenu();
				for (const auto& menu : menus_)
				{
					const HMENU popup = create_popup(menu.items);
					AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(popup), menu.name.c_str());
				}
				SetMenu(owner_, menu_);
				DrawMenuBar(owner_);
				refresh_menu(menu_);
			}

			void refresh_menu(const HMENU menu)
			{
				if (!menu) return;
				for (const auto& [id, command] : commands_)
				{
					const bool enabled = !command->enabled || command->enabled();
					EnableMenuItem(menu, id, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
					if (command->checked)
						CheckMenuItem(menu, id, MF_BYCOMMAND |
						              (command->checked() ? MF_CHECKED : MF_UNCHECKED));
				}
			}

			void rebuild_toolbar(const HWND toolbar, const std::vector<ToolbarItem>& items,
			                     bool includeBreadcrumbs)
			{
				if (!toolbar) return;
				// A task view replaces Items, so the folder trail it names is no longer the subject.
				if (taskActive_) includeBreadcrumbs = false;
				clear_toolbar(toolbar);
				std::vector<TBBUTTON> buttons;
				buttons.reserve(items.size() + (includeBreadcrumbs ? breadcrumbs_.size() * 2 : 0));
				std::vector<std::wstring> labels;
				labels.reserve(items.size());
				for (const auto& item : items)
				{
					if (item.kind == ToolbarItemKind::separator)
					{
						TBBUTTON separator{};
						separator.fsStyle = BTNS_SEP;
						buttons.push_back(separator);
						continue;
					}
					if (!item.command) continue;
					const auto& command = item.command;
					const NativeCommandId id = command_id(command);
					if (!id) continue;
					TBBUTTON button{};
					// A text-only button collapses to one letter and an ellipsis once it is disabled.
					// Giving it the blank image keeps comctl32's own sizing honest.
					button.iBitmap = command->bitmap == CommandBitmap::none
						                 ? (command->toolbarText ? breadcrumbImageIndex_ : I_IMAGENONE)
						                 : static_cast<int>(command->bitmap);
					button.idCommand = static_cast<int>(id);
					button.fsState = TBSTATE_ENABLED;
					button.fsStyle = static_cast<BYTE>(
						item.kind == ToolbarItemKind::menu
							? BTNS_WHOLEDROPDOWN
							: command->checked ? BTNS_CHECK : BTNS_BUTTON);
					if (command->toolbarText)
					{
						button.fsStyle |= BTNS_SHOWTEXT;
						labels.push_back(command->toolbarText());
						button.iString = reinterpret_cast<INT_PTR>(labels.back().c_str());
					}
					else button.iString = -1;
					buttons.push_back(button);
				}
				if (includeBreadcrumbs)
				{
					TBBUTTON separator{};
					separator.fsStyle = BTNS_SEP;
					buttons.push_back(separator);
					for (size_t index = 0; index < breadcrumbs_.size() && index <= maxBreadcrumbIndex; ++index)
					{
						TBBUTTON button{};
						button.iBitmap = index == 0 ? breadcrumbImageIndex_ : I_IMAGENONE;
						button.idCommand = static_cast<int>(breadcrumb_folder_command(index));
						button.fsState = TBSTATE_ENABLED;
						button.fsStyle = BTNS_BUTTON | BTNS_SHOWTEXT | BTNS_NOPREFIX;
						button.iString = reinterpret_cast<INT_PTR>(breadcrumbs_[index].label.c_str());
						buttons.push_back(button);
						if (index + 1 < breadcrumbs_.size() && index + 1 <= maxBreadcrumbIndex)
						{
							TBBUTTON menuButton{};
							menuButton.iBitmap = static_cast<int>(CommandBitmap::breadcrumbChevron);
							menuButton.idCommand = static_cast<int>(breadcrumb_menu_command(index + 1));
							menuButton.fsState = TBSTATE_ENABLED;
							menuButton.fsStyle = BTNS_BUTTON;
							menuButton.iString = -1;
							buttons.push_back(menuButton);
						}
					}
				}
				if (!buttons.empty())
					SendMessageW(toolbar, TB_ADDBUTTONS, buttons.size(),
					             reinterpret_cast<LPARAM>(buttons.data()));
				if (includeBreadcrumbs)
				{
					for (size_t index = 1; index < breadcrumbs_.size() && index <= maxBreadcrumbIndex; ++index)
					{
						TBBUTTONINFOW info{sizeof(info)};
						info.dwMask = TBIF_SIZE;
						info.cx = static_cast<WORD>(scale_for_dpi(20, dpi_));
						SendMessageW(toolbar, TB_SETBUTTONINFOW, breadcrumb_menu_command(index),
						             reinterpret_cast<LPARAM>(&info));
					}
				}
				SendMessageW(toolbar, TB_AUTOSIZE, 0, 0);
				refresh_toolbar(toolbar);
			}

			void refresh_toolbar(const HWND toolbar)
			{
				if (!toolbar) return;
				for (const auto& [id, command] : commands_)
				{
					const bool enabled = !command->enabled || command->enabled();
					SendMessageW(toolbar, TB_ENABLEBUTTON, id, MAKELONG(enabled, 0));
					if (command->checked)
						SendMessageW(toolbar, TB_CHECKBUTTON, id,
						             MAKELONG(command->checked() ? TRUE : FALSE, 0));
					if (command->toolbarText)
					{
						auto label = command->toolbarText();
						TBBUTTONINFOW info{sizeof(info)};
						info.dwMask = TBIF_TEXT;
						info.pszText = label.data();
						SendMessageW(toolbar, TB_SETBUTTONINFOW, id, reinterpret_cast<LPARAM>(&info));
					}
				}
				SendMessageW(toolbar, TB_AUTOSIZE, 0, 0);
			}

			const ToolbarItem* find_toolbar_item(const NativeCommandId id) const
			{
				const auto find = [this, id](const std::vector<ToolbarItem>& items) -> const ToolbarItem*
				{
					for (const auto& item : items)
					{
						if (!item.command) continue;
						const auto found = commandIds_.find(item.command.get());
						if (found != commandIds_.end() && found->second == id) return &item;
					}
					return nullptr;
				};
				if (const auto* item = find(active_toolbar())) return item;
				return find(statusToolbar_);
			}

			void show_toolbar_menu(const HWND toolbar, const NativeCommandId id)
			{
				RECT bounds{};
				SendMessageW(toolbar, TB_GETRECT, id, reinterpret_cast<LPARAM>(&bounds));
				MapWindowPoints(toolbar, nullptr, reinterpret_cast<POINT*>(&bounds), 2);
				if (id >= breadcrumbCommandFirst)
				{
					const NativeCommandId offset = id - breadcrumbCommandFirst;
					if (offset % 2 != 0)
						show_breadcrumb_menu((offset + 1) / 2, {bounds.left, bounds.bottom});
					return;
				}
				const auto* item = find_toolbar_item(id);
				if (!item || item->kind != ToolbarItemKind::menu) return;
				show_popup(item->menuItems, {bounds.left, bounds.bottom});
			}

			void show_popup(const std::vector<MenuItem>& items, const POINT point)
			{
				const HMENU popup = create_popup(items);
				refresh_menu(popup);
				const NativeCommandId selected = TrackPopupMenu(
					popup, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
					point.x, point.y, 0, owner_, nullptr);
				DestroyMenu(popup);
				if (selected) invoke(selected);
			}

			void show_breadcrumb_menu(const size_t index, const POINT point)
			{
				if (index >= breadcrumbs_.size()) return;
				const auto& selected = breadcrumbs_[index].path;
				const auto parent = selected == selected.root_path() ? selected : selected.parent_path();
				std::vector<std::filesystem::path> folders;
				std::error_code error;
				if (selected == selected.root_path())
				{
					const DWORD drives = GetLogicalDrives();
					for (wchar_t letter = L'A'; letter <= L'Z'; ++letter)
						if (drives & (1u << (letter - L'A')))
							folders.emplace_back(std::wstring{letter, L':', L'\\'});
				}
				else
				{
					for (const auto& entry : std::filesystem::directory_iterator(
						     parent, std::filesystem::directory_options::skip_permission_denied,
						     error))
					{
						if (entry.is_directory(error)) folders.push_back(entry.path());
						if (error) error.clear();
					}
					std::ranges::sort(folders, {}, [](const auto& path)
					{
						return path.filename().wstring();
					});
				}
				const HMENU popup = CreatePopupMenu();
				for (size_t item = 0; item < folders.size(); ++item)
				{
					std::wstring label = folders[item].filename().wstring();
					if (label.empty()) label = folders[item].wstring();
					AppendMenuW(popup, MF_STRING | (paths::equal(folders[item], selected) ? MF_CHECKED : 0),
					            item + 1, label.c_str());
				}
				const size_t chosen = TrackPopupMenu(
					popup, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
					point.x, point.y, 0, owner_, nullptr);
				DestroyMenu(popup);
				if (chosen > 0 && chosen <= folders.size() && breadcrumbSelected_)
					breadcrumbSelected_(folders[chosen - 1]);
			}

			void layout_top(const int width, const int height) const
			{
				if (!topBar_) return;
				SIZE size{};
				SendMessageW(topBar_, TB_GETMAXSIZE, 0, reinterpret_cast<LPARAM>(&size));
				const int barHeight = (std::min)(height, (std::max)(static_cast<int>(size.cy),
				                                                    scale_for_dpi(24, dpi_)));
				MoveWindow(topBar_, 0, (height - barHeight) / 2, width, barHeight, TRUE);
			}

			void layout_status(const int width, const int height) const
			{
				if (!statusBar_) return;
				SIZE size{};
				SendMessageW(statusBar_, TB_GETMAXSIZE, 0, reinterpret_cast<LPARAM>(&size));
				const int grip = IsZoomed(owner_) ? 0 : height;
				const int barHeight = (std::min)(height, (std::max)(static_cast<int>(size.cy),
				                                                    scale_for_dpi(20, dpi_)));
				const int barLeft = (std::max)(0, width - grip - static_cast<int>(size.cx));
				MoveWindow(statusBar_, barLeft, (height - barHeight) / 2,
				           static_cast<int>(size.cx), barHeight, TRUE);
				const int sliderWidth = scale_for_dpi(150, dpi_);
				const int sliderLeft = barLeft - sliderWidth - scale_for_dpi(8, dpi_);
				const bool showSlider = !taskActive_ && slider_ && sliderLeft >= scale_for_dpi(220, dpi_);
				if (slider_)
				{
					ShowWindow(slider_, showSlider ? SW_SHOW : SW_HIDE);
					if (showSlider)
						MoveWindow(slider_, sliderLeft, 1, sliderWidth,
						           (std::max)(1, height - 2), TRUE);
				}
			}

			void paint_host(const HWND window) const
			{
				PAINTSTRUCT paint{};
				const HDC dc = BeginPaint(window, &paint);
				RECT client{};
				GetClientRect(window, &client);
				FillRect(dc, &client, GetSysColorBrush(COLOR_3DFACE));
				if (window == top_)
				{
					const RECT line{0, client.bottom - 1, client.right, client.bottom};
					FillRect(dc, &line, GetSysColorBrush(COLOR_3DSHADOW));
				}
				else
				{
					const RECT line{0, 0, client.right, 1};
					FillRect(dc, &line, GetSysColorBrush(COLOR_3DSHADOW));
					int textRight = client.right - (IsZoomed(owner_) ? 0 : client.bottom);
					if (statusBar_)
					{
						RECT bar{};
						GetWindowRect(statusBar_, &bar);
						MapWindowPoints(nullptr, status_, reinterpret_cast<POINT*>(&bar), 2);
						textRight = (std::min)(textRight, static_cast<int>(bar.left));
					}
					if (slider_ && IsWindowVisible(slider_))
					{
						RECT slider{};
						GetWindowRect(slider_, &slider);
						MapWindowPoints(nullptr, status_, reinterpret_cast<POINT*>(&slider), 2);
						textRight = (std::min)(textRight, static_cast<int>(slider.left));
					}
					const bool progress = !statusInfo_.scanning && statusInfo_.progressPercent < 100 &&
						textRight >= scale_for_dpi(220, dpi_);
					if (progress)
					{
						const int progressWidth = (std::min)(scale_for_dpi(140, dpi_), textRight / 3);
						RECT bar{
							textRight - progressWidth - scale_for_dpi(8, dpi_),
							scale_for_dpi(6, dpi_), textRight - scale_for_dpi(8, dpi_),
							client.bottom - scale_for_dpi(6, dpi_)
						};
						FrameRect(dc, &bar, GetSysColorBrush(COLOR_3DSHADOW));
						InflateRect(&bar, -1, -1);
						bar.right = bar.left + (bar.right - bar.left) *
							std::clamp(statusInfo_.progressPercent, 0, 100) / 100;
						FillRect(dc, &bar, GetSysColorBrush(COLOR_HIGHLIGHT));
						textRight -= progressWidth + scale_for_dpi(16, dpi_);
					}
					RECT text{
						scale_for_dpi(7, dpi_), 1,
						(std::max)(scale_for_dpi(7, dpi_), textRight - scale_for_dpi(7, dpi_)),
						client.bottom - 1
					};
					SetBkMode(dc, TRANSPARENT);
					SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
					DrawTextW(dc, statusInfo_.text.c_str(), -1, &text,
					          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
				}
				EndPaint(window, &paint);
			}

			WindowFramePtr ownerFrame_;
			HWND owner_{};
			HWND top_{};
			HWND topBar_{};
			HWND status_{};
			HWND statusBar_{};
			HWND slider_{};
			HIMAGELIST images_{};
			int breadcrumbImageIndex_{-1};
			HMENU menu_{};
			unsigned int dpi_{USER_DEFAULT_SCREEN_DPI};
			std::map<NativeCommandId, CommandPtr> commands_;
			std::map<const Command*, NativeCommandId> commandIds_;
			NativeCommandId nextCommandId_{1};
			std::vector<Menu> menus_;
			std::vector<ToolbarItem> toolbar_;
			std::vector<ToolbarItem> taskToolbar_;
			std::vector<ToolbarItem> statusToolbar_;
			std::vector<Breadcrumb> breadcrumbs_;
			std::function<void(const std::filesystem::path&)> breadcrumbSelected_;
			int breadcrumbIcon_{-1};
			std::function<void(int)> thumbnailChanged_;
			StatusInfo statusInfo_;
			bool fullscreen_{};
			bool taskActive_{};
		};
	}

	CommandSurfacePtr create_command_surface(const WindowFramePtr& owner)
	{
		if (!owner) return {};
		return std::make_shared<WinCommandSurface>(owner);
	}
}

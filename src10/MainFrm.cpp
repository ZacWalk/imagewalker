// The frame.

#include "stdafx.h"
#include "ImageWalker.h"
#include "MainFrm.h"
#include "iw/help.h"
#include "iw/logfile.h"

static constexpr char szSection[] = "Default";
static constexpr char szSplitKey[] = "Preview Window Width";
static constexpr char szAddressKey[] = "Show Address";
static constexpr char szToolBarKey[] = "Show Toolbar";
static constexpr char szStatusKey[] = "Show Status Bar";

namespace
{
	enum { kGlyphBack = 0, kGlyphForward = 1, kGlyphParent = 16 };
	enum { kStatusImage = 1, kStatusItems = 2 };

	INT_PTR CALLBACK AboutDialog(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_INITDIALOG:
#if defined(_WIN64)
			::SetDlgItemTextA(window, IDC_ABOUT_DETAILS, "64-bit build of " __DATE__ ".");
#else
			::SetDlgItemTextA(window, IDC_ABOUT_DETAILS, "32-bit build of " __DATE__ ".");
#endif
			return TRUE;
		case WM_COMMAND:
			if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
			{
				::EndDialog(window, LOWORD(wParam));
				return TRUE;
			}
			break;
		case WM_NOTIFY:
			if (const auto* header = reinterpret_cast<const NMHDR*>(lParam);
				header && header->idFrom == IDC_THEME_SONG && (header->code == NM_CLICK || header->code == NM_RETURN))
			{
				const auto* link = reinterpret_cast<const NMLINK*>(header);
				if (reinterpret_cast<INT_PTR>(::ShellExecuteW(window, L"open", link->item.szUrl,
				                                             nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
					::MessageBoxA(window, "Windows could not open the theme song link.", "ImageWalker 1.0", MB_ICONWARNING);
				return TRUE;
			}
			break;
		}
		return FALSE;
	}

	void Place(CWindow child, CRect bounds)
	{
		CRect current;
		child.GetWindowRect(&current);
		CWindow(child.GetParent()).ScreenToClient(&current);
		if (current != bounds || !(child.GetStyle() & WS_VISIBLE))
			child.SetWindowPos(nullptr, bounds.left, bounds.top, bounds.Width(), bounds.Height(),
			                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
	}
}

HWND CMainFrame::Create()
{
	const auto icon = ::LoadIconA(::GetModuleHandleA(nullptr), MAKEINTRESOURCEA(IDR_MAINFRAME));
	return CreateNative("IWImageWalker10Frame", nullptr, {CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT},
	                    "ImageWalker 1.0", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, icon);
}

LRESULT CMainFrame::WindowMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_CREATE: return OnCreate();
	case WM_DESTROY: OnDestroy(); return 0;
	case WM_SIZE:
		if (wParam != SIZE_MINIMIZED) UpdateLayout();
		return 0;
	case WM_TIMER:
		if (wParam != kStatusTimer) break;
		UpdateStatus();
		return 0;
	case WM_SETFOCUS:
		if (m_fullscreen) m_wndImage.SetFocus();
		else if (m_wndItems.IsWindow()) m_wndItems.SetFocus();
		return 0;
	case WM_DISPLAYCHANGE:
		if (m_fullscreen && !ResizeToMonitor(::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST)))
			SetFullscreen(false);
		break;
	case WM_CONTEXTMENU:
		if (m_fullscreen)
		{
			auto point = MessagePoint(lParam);
			if (point == CPoint(-1, -1)) ::GetCursorPos(&point);
			TrackResourceMenu(IDR_MAINFRAME, kMenuImage, TPM_RIGHTBUTTON, point, m_hWnd);
			return 0;
		}
		break;
	case WM_ERASEBKGND:
	{
		CRect rect;
		GetClientRect(&rect);
		::FillRect(reinterpret_cast<HDC>(wParam), &rect, ::GetSysColorBrush(COLOR_BTNFACE));
		return TRUE;
	}
	case WM_SETCURSOR:
		if (LOWORD(lParam) == HTCLIENT && reinterpret_cast<HWND>(wParam) == m_hWnd)
		{
			POINT point{};
			if (::GetCursorPos(&point) && ScreenToClient(&point) && m_splitterBounds.PtInRect(point))
			{
				::SetCursor(::LoadCursorA(nullptr, IDC_SIZEWE));
				return TRUE;
			}
		}
		break;
	case WM_LBUTTONDOWN:
		if (const auto point = MessagePoint(lParam); m_splitterBounds.PtInRect(point))
		{
			m_splitOffset = point.x - m_splitterBounds.left;
			SetCapture();
			m_splitDragging = ::GetCapture() == m_hWnd;
			return 0;
		}
		break;
	case WM_MOUSEMOVE:
		if (m_splitDragging)
		{
			CRect rect;
			GetClientRect(&rect);
			m_splitPosition = std::clamp(static_cast<int>(MessagePoint(lParam).x) - m_splitOffset, 0, rect.Width());
			UpdateLayout();
			return 0;
		}
		break;
	case WM_LBUTTONUP:
	case WM_CANCELMODE:
		if (m_splitDragging && ::GetCapture() == m_hWnd) ::ReleaseCapture();
		break;
	case WM_CAPTURECHANGED: m_splitDragging = false; break;
	case WM_INITMENUPOPUP:
		if (!HIWORD(lParam)) OnInitMenuPopup(reinterpret_cast<HMENU>(wParam));
		break;
	case WM_IW_SELECTIONCHANGED: OnSelectionChanged(); return 0;
	case WM_HSCROLL:
		if (m_thumbnailSlider.m_hWnd && reinterpret_cast<HWND>(lParam) == m_thumbnailSlider.m_hWnd)
		{
			if (!m_wndItems.IsDetailView())
				m_wndItems.SetThumbnailSize(m_thumbnailSlider.GetPos(), LOWORD(wParam) == TB_THUMBTRACK);
			return 0;
		}
		break;
	case WM_COMMAND:
		if (LOWORD(wParam) == IDC_ADDRESS_COMBO)
		{
			if (OnAddressCommand(HIWORD(wParam))) return 0;
		}
		else if (Command(LOWORD(wParam))) return 0;
		break;
	case WM_NOTIFY:
		if (auto* header = reinterpret_cast<NMHDR*>(lParam); header && header->code == TTN_GETDISPINFOA)
		{
			auto* tip = reinterpret_cast<NMTTDISPINFOA*>(header);
			CString text;
			if (text.LoadString(static_cast<UINT>(header->idFrom)))
			{
				const int newline = text.Find('\n');
				if (newline >= 0) text = text.Mid(newline + 1);
				strncpy_s(tip->szText, text, _TRUNCATE);
				tip->lpszText = tip->szText;
				return 0;
			}
		}
		break;
	}
	return SdkWindow::WindowMessage(message, wParam, lParam);
}

LRESULT CMainFrame::OnCreate()
{
	m_bShowAddress = Settings::GetInt(szSection, szAddressKey, TRUE) != 0;
	m_bShowToolBar = Settings::GetInt(szSection, szToolBarKey, TRUE) != 0;
	m_bShowStatusBar = Settings::GetInt(szSection, szStatusKey, TRUE) != 0;
	m_splitPosition = Settings::GetInt(szSection, szSplitKey, -1);

	const auto module = ::GetModuleHandleA(nullptr);
	const auto menu = ::LoadMenuA(module, MAKEINTRESOURCEA(IDR_MAINFRAME));
	if (!menu) return -1;
	if (!::SetMenu(m_hWnd, menu))
	{
		::DestroyMenu(menu);
		return -1;
	}
	m_accelerators = ::LoadAcceleratorsA(module, MAKEINTRESOURCEA(IDR_MAINFRAME));
	if (!m_accelerators) return -1;

	if (!m_toolBar.Create(m_hWnd, {}, nullptr, WS_CHILD | CCS_NODIVIDER | CCS_NORESIZE |
	                      CCS_NOPARENTALIGN | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS, 0, ID_VIEW_TOOLBAR))
		return -1;
	m_toolBar.SetButtonStructSize(sizeof(TBBUTTON));
	m_toolBar.SetImageList(LoadToolbarImages(IDB_BAR_COLD, 22));
	m_toolBar.SetHotImageList(LoadToolbarImages(IDB_BAR_HOT, 22));
	const TBBUTTON buttons[] =
	{
		{kGlyphBack, ID_BROWSE_BACK, TBSTATE_ENABLED, BTNS_BUTTON, {0, 0}, 0, 0},
		{kGlyphForward, ID_BROWSE_FORWARD, TBSTATE_ENABLED, BTNS_BUTTON, {0, 0}, 0, 0},
		{kGlyphParent, ID_BROWSE_PARENT, TBSTATE_ENABLED, BTNS_BUTTON, {0, 0}, 0, 0},
	};
	m_toolBar.SetBitmapSize({22, 22});
	m_toolBar.AddButtons(_countof(buttons), buttons);
	m_toolBar.SetButtonSize({30, 26});

	if (!m_wndAddress.Create(m_hWnd, IDC_ADDRESS_COMBO)) return -1;
	SHFILEINFOA info{};
	const auto images = ::SHGetFileInfoA("Folder", FILE_ATTRIBUTE_DIRECTORY, &info, sizeof(info),
	                                     SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
	m_wndAddress.SendMessage(CBEM_SETIMAGELIST, 0, static_cast<LPARAM>(images));

	if (!m_statusBar.Create(m_hWnd, {}, nullptr,
	                       WS_CHILD | WS_CLIPCHILDREN | SBARS_SIZEGRIP |
	                       (m_bShowStatusBar ? WS_VISIBLE : 0), 0, IDC_STATUS_BAR))
		return -1;
	m_statusBar.SendMessage(SB_SETMINHEIGHT, ::GetSystemMetrics(SM_CYHSCROLL) + 2);
	m_statusBar.SetParts(static_cast<int>(m_statusParts.size()), m_statusParts.data());
	if (!m_thumbnailSlider.Create(m_statusBar, {}, "Thumbnail size",
	                              WS_CHILD | WS_CLIPSIBLINGS | TBS_HORZ | TBS_NOTICKS | TBS_TOOLTIPS,
	                              0, kThumbnailTrackId))
		return -1;
	m_thumbnailSlider.SetRange(CThumbnailGrid::minEdge, CThumbnailGrid::maxEdge, FALSE);
	m_thumbnailSlider.SetLineSize(8);
	m_thumbnailSlider.SetPageSize(16);
	m_thumbnailSlider.SetPos(m_wndItems.ThumbnailSize());

	constexpr DWORD paneStyle = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
	if (!m_wndImage.Create(m_hWnd, {}, nullptr, paneStyle) ||
		!m_wndItems.Create(m_hWnd, {}, nullptr, paneStyle) ||
		!m_wndImage.CreateFooterControls(m_statusBar))
		return -1;
	if (!SetTimer(kStatusTimer, 150)) return -1;
	m_ready = true;
	UpdateLayout();
	OnSelectionChanged();
	return 0;
}

void CMainFrame::OnDestroy()
{
	if (!m_ready) return;
	m_ready = false;
	KillTimer(kStatusTimer);
	Settings::SetInt(szSection, szSplitKey, m_splitPosition);
	Settings::SetInt(szSection, szAddressKey, m_bShowAddress);
	Settings::SetInt(szSection, szToolBarKey, m_bShowToolBar);
	Settings::SetInt(szSection, szStatusKey, m_bShowStatusBar);
	if (m_fullscreen) Settings::SaveWindowPlacement(m_windowPlacement);
	else Settings::SaveWindowPlacement(m_hWnd);
	if (m_windowMenu) ::DestroyMenu(std::exchange(m_windowMenu, nullptr));
	::PostQuitMessage(0);
}

bool CMainFrame::ResizeToMonitor(HMONITOR target)
{
	MONITORINFO monitor{sizeof(monitor)};
	if (!::GetMonitorInfo(target, &monitor))
		return false;
	const CRect bounds(monitor.rcMonitor);
	return SetWindowPos(HWND_TOP, bounds.left, bounds.top, bounds.Width(), bounds.Height(),
	                    SWP_FRAMECHANGED | SWP_NOACTIVATE) != FALSE;
}

void CMainFrame::SetFullscreen(bool fullscreen)
{
	if (fullscreen == m_fullscreen) return;
	if (fullscreen)
	{
		if (!m_wndItems.FocusPhoto()) return;
		const HMONITOR target = ::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
		if (!::GetWindowPlacement(m_hWnd, &m_windowPlacement))
		{
			IW::Logging::LastError("Save window placement before fullscreen");
			return;
		}
		const auto menu = ::GetMenu(m_hWnd);
		if (!::SetMenu(m_hWnd, nullptr))
		{
			IW::Logging::LastError("Hide the menu for fullscreen");
			return;
		}
		m_windowMenu = menu;
		if (::IsZoomed(m_hWnd)) ShowWindow(SW_RESTORE);
		m_windowStyle = GetStyle();
		m_fullscreen = true;
		m_wndImage.SetFullscreen(true);
		::SetWindowLongPtrA(m_hWnd, GWL_STYLE, m_windowStyle & ~(WS_CAPTION | WS_THICKFRAME));
		if (!ResizeToMonitor(target))
		{
			IW::Logging::LastError("Open fullscreen on the current monitor");
			SetFullscreen(false);
			return;
		}
		m_wndImage.SetFocus();
	}
	else
	{
		m_fullscreen = false;
		::SetWindowLongPtrA(m_hWnd, GWL_STYLE, m_windowStyle);
		if (m_windowMenu)
		{
			if (::SetMenu(m_hWnd, m_windowMenu)) m_windowMenu = nullptr;
			else IW::Logging::LastError("Restore the menu after fullscreen");
		}
		if (!::SetWindowPlacement(m_hWnd, &m_windowPlacement))
			IW::Logging::LastError("Restore window placement after fullscreen");
		SetWindowPos(nullptr, 0, 0, 0, 0,
		             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
		m_wndImage.SetFullscreen(false);
		m_wndItems.SetFocus();
	}
	UpdateLayout();
}

bool CMainFrame::BackOut()
{
	if (m_wndImage.EndPan()) return true;
	if (m_splitDragging && ::GetCapture() == m_hWnd && ::ReleaseCapture()) return true;
	if ((m_fullscreen || ::GetFocus() == m_wndImage.m_hWnd) && !m_wndImage.IsFit())
	{
		m_wndImage.Command(ID_MODE_FITTOWINDOW);
		return true;
	}
	if (!m_fullscreen) return false;
	SetFullscreen(false);
	return true;
}

void CMainFrame::UpdateLayout()
{
	if (!m_ready) return;
	CRect client;
	GetClientRect(&client);
	if (m_fullscreen)
	{
		m_splitterBounds.SetRectEmpty();
		m_toolBar.ShowWindow(SW_HIDE);
		m_wndAddress.ShowWindow(SW_HIDE);
		m_statusBar.ShowWindow(SW_HIDE);
		m_wndItems.ShowWindow(SW_HIDE);
		Place(m_wndImage, client);
		return;
	}
	if (m_bShowStatusBar)
	{
		if (!(m_statusBar.GetStyle() & WS_VISIBLE)) m_statusBar.ShowWindow(SW_SHOWNA);
		m_statusBar.SendMessage(WM_SIZE);
		CRect bar;
		m_statusBar.GetClientRect(&bar);
		client.bottom = max(0L, client.bottom - bar.Height());
		LayoutStatusBar(bar);
	}

	CRect tools(0, 0, 0, 26);
	if (m_toolBar.GetButtonCount() > 0) m_toolBar.GetItemRect(m_toolBar.GetButtonCount() - 1, &tools);
	const int toolWidth = tools.right + 4;
	const bool showTools = m_bShowToolBar && (!m_bShowAddress || client.Width() >= toolWidth + 85);
	int rowHeight = 0;
	if (showTools)
	{
		rowHeight = max(tools.bottom + 2, 28L);
		Place(m_toolBar, {0, 0, min(toolWidth, client.Width()), rowHeight});
	}
	else
		m_toolBar.ShowWindow(SW_HIDE);

	if (m_bShowAddress)
	{
		const int left = showTools ? toolWidth : 3;
		const int editHeight = static_cast<int>(m_wndAddress.SendMessage(CB_GETITEMHEIGHT, static_cast<WPARAM>(-1)));
		rowHeight = max(rowHeight, editHeight + 10);
		// The height includes the dropdown, not just the visible field.
		CRect current;
		m_wndAddress.GetWindowRect(&current);
		ScreenToClient(&current);
		const int right = max(left, client.Width() - 3);
		if (current.left != left || current.top != 2 || current.right != right ||
			!(m_wndAddress.GetStyle() & WS_VISIBLE))
			m_wndAddress.SetWindowPos(nullptr, left, 2, right - left, 300,
			                          SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
	}
	else
		m_wndAddress.ShowWindow(SW_HIDE);
	client.top = min(static_cast<int>(client.bottom), rowHeight);

	const int splitter = min(max(4, ::GetSystemMetrics(SM_CXSIZEFRAME)), client.Width());
	const int space = client.Width() - splitter;
	const int minimum = min(72, space / 2);
	const int left = std::clamp(m_splitPosition < 0 ? space / 2 : m_splitPosition, minimum, space - minimum);
	const CRect old = m_splitterBounds;
	m_splitterBounds = CRect(left, client.top, left + splitter, client.bottom);
	Place(m_wndImage, {0, client.top, left, client.bottom});
	Place(m_wndItems, {m_splitterBounds.right, client.top, client.right, client.bottom});
	if (old != m_splitterBounds)
	{
		InvalidateRect(old, TRUE);
		InvalidateRect(m_splitterBounds, TRUE);
	}
}

void CMainFrame::LayoutStatusBar(const CRect& client)
{
	// Reserve the rightmost space for the slider and sizing grip, never status text.
	const int right = max(0, client.Width() - ::GetSystemMetrics(SM_CXVSCROLL) - 6);
	const int width = min(112, max(0, (right - 320) / 2));
	CRect slider;
	if (width >= 64 && client.Height() > 0)
	{
		const int height = min(client.Height(), ::GetSystemMetrics(SM_CYHSCROLL));
		const int top = (client.Height() - height) / 2;
		slider.SetRect(right - width, top, right, top + height);
	}
	if (slider != m_thumbnailSliderBounds)
	{
		m_thumbnailSliderBounds = slider;
		if (slider.IsRectEmpty()) m_thumbnailSlider.ShowWindow(SW_HIDE);
		else Place(m_thumbnailSlider, slider);
	}

	const int itemsEnd = slider.IsRectEmpty() ? right : slider.left - 8;
	const int controlsEnd = m_wndImage.LayoutFooterControls({0, 0, itemsEnd, client.Height()});
	const int imageEnd = itemsEnd - min(190, (itemsEnd - controlsEnd) * 2 / 3);
	const std::array parts{controlsEnd, imageEnd, itemsEnd, -1};
	if (parts != m_statusParts)
	{
		m_statusParts = parts;
		m_statusBar.SetParts(static_cast<int>(m_statusParts.size()), m_statusParts.data());
	}
}

bool CMainFrame::PreTranslateMessage(MSG* message)
{
	if (message->message >= WM_KEYFIRST && message->message <= WM_KEYLAST)
	{
		if (m_wndAddress.HasKey(*message))
		{
			if (message->message == WM_KEYDOWN && (message->wParam == VK_RETURN || message->wParam == VK_ESCAPE))
				return m_wndAddress.HandleKey(*message, m_wndItems.FolderItem(),
				                             [this](const CShellItem& item) { return AddressNavigate(item); });
			return false;
		}
		if (m_wndImage.ScaleEditHasFocus())
		{
			if (message->message != WM_KEYDOWN || message->wParam != VK_RETURN) return false;
			m_wndImage.ApplyScaleText();
			return true;
		}
		if (message->message == WM_KEYDOWN)
		{
			const bool control = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
			const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
			if (!shift && ((message->wParam == VK_ESCAPE && !control) || (message->wParam == VK_F4 && control)))
			{
				if (BackOut()) return true;
				if (control)
				{
					PostMessage(WM_CLOSE);
					return true;
				}
			}
			if (m_fullscreen && !control && !shift && m_wndImage.PanWithKey(message->wParam)) return true;
			if (m_fullscreen && !control && !shift &&
				(message->wParam == VK_LEFT || message->wParam == VK_RIGHT))
			{
				m_wndItems.StepPhoto(message->wParam == VK_RIGHT ? 1 : -1);
				return true;
			}
			const auto focus = ::GetFocus();
			if (!control && !shift && (message->wParam == VK_RETURN || message->wParam == VK_SPACE) &&
				::IsChild(m_statusBar, focus))
				return false;
		}
	}
	return ::TranslateAcceleratorA(m_hWnd, m_accelerators, message) != 0;
}

bool CMainFrame::Command(int id)
{
	switch (id)
	{
	case ID_APP_EXIT: PostMessage(WM_CLOSE); return true;
	case ID_MODE_FULLSCREEN: SetFullscreen(!m_fullscreen); return true;
	case ID_VIEW_ACTIVATE:
		if (m_fullscreen) m_wndItems.StepPhoto(1);
		else m_wndItems.ActivateFocus();
		return true;
	case ID_EDIT_TOGGLE_FOCUS:
		if (!m_fullscreen) m_wndItems.Command(id);
		return true;
	case ID_HELP_FINDER: IW::InvokeHelp(m_hWnd, HELP_IMAGEWALKER10); return true;
	case ID_APP_ABOUT:
		if (::DialogBoxParamA(::GetModuleHandleA(nullptr), MAKEINTRESOURCEA(IDD_ABOUTBOX),
		                      m_hWnd, AboutDialog, 0) == -1)
			IW::Logging::LastError("Open About dialog");
		return true;
	case ID_VIEW_ADDRESSBAR:
		m_bShowAddress = !m_bShowAddress;
		break;
	case ID_VIEW_TOOLBAR:
		m_bShowToolBar = !m_bShowToolBar;
		break;
	case ID_VIEW_STATUS_BAR:
		m_bShowStatusBar = !m_bShowStatusBar;
		m_statusBar.ShowWindow(m_bShowStatusBar ? SW_SHOWNA : SW_HIDE);
		break;
	default:
		return m_wndItems.Command(id) || m_wndImage.Command(id);
	}
	UpdateLayout();
	return true;
}

void CMainFrame::OnInitMenuPopup(HMENU menu)
{
	const auto enable = [menu](UINT id, bool enabled)
	{
		::EnableMenuItem(menu, id, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
	};
	const auto check = [menu](UINT id, bool checked)
	{
		::CheckMenuItem(menu, id, MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
	};
	enable(ID_BROWSE_PARENT, m_wndItems.CanGoParent());
	enable(ID_BROWSE_BACK, m_wndItems.CanGoBack());
	enable(ID_BROWSE_FORWARD, m_wndItems.CanGoForward());
	check(ID_VIEW_ADDRESSBAR, m_bShowAddress);
	check(ID_VIEW_TOOLBAR, m_bShowToolBar);
	check(ID_VIEW_STATUS_BAR, m_bShowStatusBar);
	// These IDs are not contiguous: Refresh and Address Bar also use this range.
	for (const UINT id : {ID_VIEW_SORT_NAME, ID_VIEW_SORT_TYPE, ID_VIEW_SORT_SIZE, ID_VIEW_SORT_DATE})
		check(id, id == static_cast<UINT>(m_wndItems.SortCommand()));
	check(ID_ITEMS_THUMBNAILS, !m_wndItems.IsDetailView());
	check(ID_ITEMS_DETAILS, m_wndItems.IsDetailView());

	const auto scale = m_wndImage.GetScaleType();
	check(ID_MODE_FITTOWINDOW, scale == CImageView::eFit);
	check(ID_MODE_SCALEDOWNTOFIT, scale == CImageView::eDown);
	check(ID_MODE_SCALEUPTOFIT, scale == CImageView::eUp);
	check(ID_MODE_ACTUALSIZE, m_wndImage.IsActualSize());
	check(ID_MODE_FULLSCREEN, m_fullscreen);
}

void CMainFrame::OnSelectionChanged()
{
	if (!m_ready) return;
	m_wndImage.Show(m_wndItems.SelectedItems());
	UpdateFolder();
	UpdateStatus();
}

void CMainFrame::UpdateFolder()
{
	const CShellItem item = m_wndItems.FolderItem();
	CString path;
	item.GetPath(path);
	const CString name = CShellDesktop().GetDisplayNameOf(item, SHGDN_NORMAL);
	const CString label = path.IsEmpty() ? name : path;
	if (label == m_strFolder) return;
	m_strFolder = label;

	CString title;
	title.LoadString(IDR_MAINFRAME);
	SetWindowText(name.IsEmpty() ? title : name + " - " + title);
	SHFILEINFOA info{};
	::SHGetFileInfoA(reinterpret_cast<LPCSTR>(item.GetItem()), 0, &info, sizeof(info),
	                 SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
	m_wndAddress.SetAddress(label, info.iIcon);
}

void CMainFrame::UpdateStatus()
{
	m_toolBar.EnableButton(ID_BROWSE_PARENT, m_wndItems.CanGoParent());
	m_toolBar.EnableButton(ID_BROWSE_BACK, m_wndItems.CanGoBack());
	m_toolBar.EnableButton(ID_BROWSE_FORWARD, m_wndItems.CanGoForward());
	const bool thumbnails = !m_wndItems.IsDetailView();
	if ((m_thumbnailSlider.IsWindowEnabled() != FALSE) != thumbnails)
		m_thumbnailSlider.EnableWindow(thumbnails);

	const CString items = m_wndItems.StatusText();
	const CString image = m_wndImage.StatusText();
	if (items != m_strStatusItems)
	{
		m_strStatusItems = items;
		m_statusBar.SetText(kStatusItems, CString("\t\t") + items, SBT_NOBORDERS);
	}
	if (image != m_strStatusImage)
	{
		m_strStatusImage = image;
		m_statusBar.SetText(kStatusImage, image, SBT_NOBORDERS);
	}
}

bool CMainFrame::OnAddressCommand(int code)
{
	switch (code)
	{
	case CBN_DROPDOWN:
		m_wndAddress.DropDown(m_wndItems.FolderItem());
		return true;
	case CBN_SELENDOK:
		if (auto item = m_wndAddress.SelectedFolder(); !item.IsNull()) AddressNavigate(item);
		return true;
	case CBN_CLOSEUP:
		m_wndAddress.RestoreAddress();
		return true;
	}
	return false;
}

bool CMainFrame::AddressNavigate(const CShellItem& item)
{
	try
	{
		m_wndItems.Navigate(item);
		m_wndItems.SetFocus();
		return true;
	}
	catch (const std::exception& error)
	{
		IW::Logging::Warn("Open address: %hs", error.what());
		::MessageBeep(MB_OK);
		return false;
	}
}

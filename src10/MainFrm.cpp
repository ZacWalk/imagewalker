// The frame.

#include "stdafx.h"
#include "ArtMate.h"
#include "MainFrm.h"

#include "iw/help.h"

static constexpr TCHAR szSection[] = _T("Default");
static constexpr TCHAR szSplitKey[] = _T("Preview Window Width");
static constexpr TCHAR szAddressKey[] = _T("Show Address");
static constexpr TCHAR szToolBarKey[] = _T("Show Toolbar");
static constexpr TCHAR szStatusKey[] = _T("Show Status Bar");

// Image list ids on the merged toolbar.
namespace
{
	enum { kGlyphBack = 0, kGlyphForward = 1, kGlyphParent = 16 };

	class CAboutDlg : public CDialogImpl<CAboutDlg>
	{
	public:
		enum { IDD = IDD_ABOUTBOX };

		BEGIN_MSG_MAP(CAboutDlg)
			MSG_WM_INITDIALOG(OnInitDialog)
			COMMAND_ID_HANDLER_EX(IDOK, OnClose)
			COMMAND_ID_HANDLER_EX(IDCANCEL, OnClose)
		END_MSG_MAP()

	private:
		BOOL OnInitDialog(CWindow, LPARAM)
		{
			CenterWindow(GetParent());
			SetDlgItemText(IDC_ABOUT_DETAILS, _T("64-bit build of ") _T(__DATE__) _T("."));

			m_song.SetHyperLink(_T("https://www.youtube.com/watch?v=TLGWQfK-6DY"));
			m_song.SetLabel(_T("\"It's Like That\" by Run-DMC vs. Jason Nevins"));
			m_song.SubclassWindow(GetDlgItem(IDC_THEME_SONG));

			return TRUE;
		}

		void OnClose(UINT, int nID, CWindow) { EndDialog(nID); }

		CHyperLink m_song;
	};
}

LRESULT CMainFrame::OnCreate(LPCREATESTRUCT)
{
	m_bShowAddress = Settings::GetInt(szSection, szAddressKey, TRUE);
	m_bShowToolBar = Settings::GetInt(szSection, szToolBarKey, TRUE);
	m_bShowStatusBar = Settings::GetInt(szSection, szStatusKey, TRUE);

	// The menu moves onto a command bar so that row one is ordinary client area
	// and the logo can share it. A real menu bar is non-client and cannot.
	if (m_CmdBar.Create(m_hWnd, rcDefault, nullptr, ATL_SIMPLE_CMDBAR_PANE_STYLE) == nullptr)
		return -1;

	m_CmdBar.AttachMenu(GetMenu());
	SetMenu(nullptr);

	m_wndLogo.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE);
	m_wndLogo.SetOverlayIcon(AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR, 16, 16), 16, 16);

	m_toolBar.Create(m_hWnd, rcDefault, nullptr,
	                 WS_CHILD | (m_bShowToolBar ? WS_VISIBLE : 0) | CCS_NODIVIDER | CCS_NORESIZE |
	                 CCS_NOPARENTALIGN | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS,
	                 0, ID_VIEW_TOOLBAR);

	m_toolBar.SetButtonStructSize(sizeof(TBBUTTON));

	{
		CImageList imgCold, imgHot;
		imgCold.Create(IDB_BAR_COLD, 22, 0, RGB(255, 0, 255));
		imgHot.Create(IDB_BAR_HOT, 22, 0, RGB(255, 0, 255));

		m_toolBar.SetImageList(imgCold.Detach());
		m_toolBar.SetHotImageList(imgHot.Detach());
	}

	TBBUTTON buttons[] =
	{
		{kGlyphBack, ID_BROWSE_BACK, TBSTATE_ENABLED, BTNS_BUTTON, {0, 0}, 0, 0},
		{kGlyphForward, ID_BROWSE_FORWARD, TBSTATE_ENABLED, BTNS_BUTTON, {0, 0}, 0, 0},
		{kGlyphParent, ID_BROWSE_PARENT, TBSTATE_ENABLED, BTNS_BUTTON, {0, 0}, 0, 0},
	};

	m_toolBar.AddButtons(_countof(buttons), buttons);
	m_toolBar.SetButtonSize(CSize(30, 26));

	if (!CreateAddressBar(m_hWnd))
		return -1;

	m_statusBar.Create(m_hWnd, nullptr,
	                   WS_CHILD | (m_bShowStatusBar ? WS_VISIBLE : 0) | SBARS_SIZEGRIP,
	                   ATL_IDW_STATUS_BAR);

	int panes[] = {ID_DEFAULT_PANE, IDS_STATUS_IMAGE};
	m_statusBar.SetPanes(panes, _countof(panes), false);

	m_splitter.Create(m_hWnd, rcDefault, nullptr, 0, WS_EX_CLIENTEDGE);
	m_hWndClient = m_splitter;

	// WS_CLIPCHILDREN: both panes fill their gutters in OnEraseBkgnd, and the
	// scroll bars and the strip sit in those gutters. Without it every repaint
	// paints the gutter first and the controls over it, which flickers badly
	// while the zoom slider is being dragged.
	if (m_wndImage.Create(m_splitter, rcDefault, nullptr,
	                      WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN) == nullptr ||
		m_wndItems.Create(m_splitter, rcDefault, nullptr,
		                  WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN) == nullptr)
		return -1;

	m_splitter.SetSplitterPanes(m_wndImage, m_wndItems);

	m_wndAddress.ShowWindow(m_bShowAddress ? SW_SHOW : SW_HIDE);

	UpdateLayout();

	// After the first layout, not before: SetSplitterPos clamps against the
	// splitter's current width, and until the frame has laid out that is still
	// the creation rect. Setting it first stored a clamped position, which the
	// save on exit then wrote back, so the image pane shrank on every run.
	m_splitter.SetSplitterPos(Settings::GetInt(szSection, szSplitKey, -1));

	SetTimer(kLogoTimer, 100);
	UpdateStatus();

	return 0;
}

void CMainFrame::OnDestroy()
{
	KillTimer(kLogoTimer);

	Settings::SetInt(szSection, szSplitKey, m_splitter.GetSplitterPos());
	Settings::SetInt(szSection, szAddressKey, m_bShowAddress);
	Settings::SetInt(szSection, szToolBarKey, m_bShowToolBar);
	Settings::SetInt(szSection, szStatusKey, m_bShowStatusBar);
	Settings::SaveWindowPlacement(m_hWnd);

	SetMsgHandled(FALSE);
}

void CMainFrame::OnSize(UINT nType, CSize size)
{
	if (nType != SIZE_MINIMIZED)
		UpdateLayout();

	SetMsgHandled(FALSE);
}

void CMainFrame::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != kLogoTimer)
		return;

	m_wndLogo.OnTimer();

	// Thumbnails arrive one at a time on the work queue; the counts follow them
	// here rather than through a notification per thumbnail.
	UpdateStatus();
}

void CMainFrame::OnSetFocus(CWindow)
{
	if (m_wndItems.IsWindow())
		m_wndItems.SetFocus();
}

// comctl32 implies TBSTYLE_TRANSPARENT for a flat toolbar, and WTL forces
// TBSTYLE_FLAT on command bars. A transparent toolbar does not erase itself; it
// asks its parent to paint into its DC, and nothing else answers here.
BOOL CMainFrame::OnEraseBkgnd(CDCHandle dc)
{
	CRect rect;
	GetClientRect(&rect);
	dc.FillRect(&rect, reinterpret_cast<HBRUSH>(LongToHandle(COLOR_BTNFACE + 1)));
	return TRUE;
}

// Row one is the menu with the logo at its right end; row two is the toolbar
// followed by the address combo.
//
// Priorities encode the give-up order: the logo goes first, then the toolbar.
// The address combo is Stretch, so it is never dropped - every toolbar command
// is also on the menu, but the address bar is the only navigation that is not.
void CMainFrame::UpdateLayout(BOOL /*bResizeBars*/)
{
	// A WM_SIZE can arrive before OnCreate has built the bars, and every window
	// this touches asserts on a null handle in a debug build.
	if (m_hWndClient == nullptr)
		return;

	CRect rectClient;
	GetClientRect(&rectClient);

	int yBottom = rectClient.bottom;

	if (m_statusBar.IsWindow() && m_statusBar.IsWindowVisible())
	{
		m_statusBar.SendMessage(WM_SIZE, 0, 0);

		CRect rectBar;
		m_statusBar.GetWindowRect(&rectBar);
		yBottom -= rectBar.Height();
	}

	m_chrome.Clear();
	m_chrome.Add(0, m_CmdBar, IW::CChromeLayout::Toolbar, IW::CChromeLayout::Stretch, 20);
	m_chrome.Add(0, m_wndLogo, IW::CChromeLayout::Custom, IW::CChromeLayout::Right, 0, true, 26);
	m_chrome.Add(1, m_toolBar, IW::CChromeLayout::Toolbar, IW::CChromeLayout::Left, 10,
	             m_bShowToolBar != FALSE);
	m_chrome.Add(1, m_wndAddress, IW::CChromeLayout::Combo, IW::CChromeLayout::Stretch, 30,
	             m_bShowAddress != FALSE);

	CRect rectChrome(rectClient.left, rectClient.top, rectClient.right, yBottom);
	m_chrome.Layout(rectChrome);

	if (m_splitter.IsWindow())
		m_splitter.SetWindowPos(nullptr, rectChrome.left, rectChrome.top,
		                        rectChrome.Width(), rectChrome.Height(),
		                        SWP_NOZORDER | SWP_NOACTIVATE);
}

// Hands a command to a pane's message map the way ATL's own window procedure
// would. The _EX handlers call SetMsgHandled, which writes through
// CWindowImplRoot::m_pCurrentMsg, and that only points at a live _ATL_MSG while
// ATL's WindowProc is on the stack. Calling ProcessWindowMessage straight out of
// the frame's map wrote into a returned stack frame; the access violation
// happened inside a kernel callback, where it is swallowed, so every menu
// command owned by a pane silently did nothing.
template <class TView>
static BOOL ForwardCommand(TView& view, WPARAM wParam, LPARAM lParam, LRESULT& lResult)
{
	if (!view.IsWindow())
		return FALSE;

	_ATL_MSG msg(view.m_hWnd, WM_COMMAND, wParam, lParam);
	const _ATL_MSG* pPrevious = view.m_pCurrentMsg;
	view.m_pCurrentMsg = &msg;

	const BOOL bHandled = view.ProcessWindowMessage(view.m_hWnd, WM_COMMAND, wParam, lParam, lResult, 0);

	view.m_pCurrentMsg = pPrevious;
	return bHandled;
}

BOOL CMainFrame::RouteCommandToPanes(WPARAM wParam, LPARAM lParam, LRESULT& lResult)
{
	return ForwardCommand(m_wndItems, wParam, lParam, lResult) ||
		ForwardCommand(m_wndImage, wParam, lParam, lResult);
}

BOOL CMainFrame::PreTranslateMessage(MSG* pMsg)
{
	// Enter and Escape in the address combo belong to the address bar; without
	// this a typed path was handed straight back to the combo and went nowhere.
	if (IW::CAddressBar<CMainFrame>::PreTranslateMessage(pMsg))
		return TRUE;

	// The address combo and the scale box want their own keys, so accelerators
	// stay out of the way while either has focus.
	if (pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST)
	{
		const HWND hWndFocus = ::GetFocus();
		const int nId = hWndFocus != nullptr ? ::GetDlgCtrlID(hWndFocus) : 0;

		if (m_wndImage.ScaleEditHasFocus())
		{
			if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN)
			{
				m_wndImage.ApplyScaleText();
				return TRUE;
			}

			::TranslateMessage(pMsg);
			::DispatchMessage(pMsg);
			return TRUE;
		}

		if (nId == kAddressBarId || ::IsChild(m_wndAddress, hWndFocus))
		{
			::TranslateMessage(pMsg);
			::DispatchMessage(pMsg);
			return TRUE;
		}
	}

	return CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg);
}

void CMainFrame::OnInitMenuPopup(CMenuHandle menu, UINT /*nIndex*/, BOOL bSysMenu)
{
	if (!bSysMenu)
	{
		menu.EnableMenuItem(ID_BROWSE_PARENT,
		                    MF_BYCOMMAND | (m_wndItems.CanGoParent() ? MF_ENABLED : MF_GRAYED));
		menu.EnableMenuItem(ID_BROWSE_BACK,
		                    MF_BYCOMMAND | (m_wndItems.CanGoBack() ? MF_ENABLED : MF_GRAYED));
		menu.EnableMenuItem(ID_BROWSE_FORWARD,
		                    MF_BYCOMMAND | (m_wndItems.CanGoForward() ? MF_ENABLED : MF_GRAYED));

		menu.CheckMenuItem(ID_VIEW_ADDRESSBAR, MF_BYCOMMAND | (m_bShowAddress ? MF_CHECKED : MF_UNCHECKED));
		menu.CheckMenuItem(ID_VIEW_TOOLBAR, MF_BYCOMMAND | (m_bShowToolBar ? MF_CHECKED : MF_UNCHECKED));
		menu.CheckMenuItem(ID_VIEW_STATUS_BAR, MF_BYCOMMAND | (m_bShowStatusBar ? MF_CHECKED : MF_UNCHECKED));

		menu.CheckMenuRadioItem(ID_VIEW_SORT_NAME, ID_VIEW_SORT_DATE,
		                        static_cast<UINT>(m_wndItems.SortCommand()), MF_BYCOMMAND);

		menu.CheckMenuItem(ID_ITEMS_THUMBNAILS,
		                   MF_BYCOMMAND | (m_wndItems.IsDetailView() ? MF_UNCHECKED : MF_CHECKED));
		menu.CheckMenuItem(ID_ITEMS_DETAILS,
		                   MF_BYCOMMAND | (m_wndItems.IsDetailView() ? MF_CHECKED : MF_UNCHECKED));

		const CImageView::ScaleType eScale = m_wndImage.GetScaleType();

		menu.CheckMenuItem(ID_MODE_FITTOWINDOW,
		                   MF_BYCOMMAND | (eScale == CImageView::eFit ? MF_CHECKED : MF_UNCHECKED));
		menu.CheckMenuItem(ID_MODE_SCALEDOWNTOFIT,
		                   MF_BYCOMMAND | (eScale == CImageView::eDown ? MF_CHECKED : MF_UNCHECKED));
		menu.CheckMenuItem(ID_MODE_SCALEUPTOFIT,
		                   MF_BYCOMMAND | (eScale == CImageView::eUp ? MF_CHECKED : MF_UNCHECKED));
		menu.CheckMenuItem(ID_MODE_ACTUALSIZE,
		                   MF_BYCOMMAND | (m_wndImage.IsActualSize() ? MF_CHECKED : MF_UNCHECKED));
	}

	SetMsgHandled(FALSE);
}

LRESULT CMainFrame::OnSelectionChanged(UINT, WPARAM, LPARAM, BOOL&)
{
	// The one place the two panes meet: what the items pane has selected is what
	// the image pane shows.
	m_wndImage.Show(m_wndItems.SelectedItems());

	UpdateFolder();
	UpdateStatus();
	return 0;
}

// The title and the address name the folder, and both cost a shell call. This
// arrives on every arrow key, so do that work only when the folder moved.
void CMainFrame::UpdateFolder()
{
	const CShellItem item = m_wndItems.FolderItem();

	CString strFull;
	item.GetPath(strFull);

	CShellDesktop desktop;
	const CString strPath = desktop.GetDisplayNameOf(item, SHGDN_NORMAL);
	const CString strKey = strFull.IsEmpty() ? strPath : strFull;

	if (strKey == m_strFolder)
		return;

	m_strFolder = strKey;

	CString strTitle;
	strTitle.LoadString(IDR_MAINFRAME);

	if (!strPath.IsEmpty())
		strTitle = strPath + _T(" - ") + strTitle;

	SetWindowText(strTitle);

	SHFILEINFO sfi = {0};
	SHGetFileInfo((LPCTSTR)item.GetItem(), 0, &sfi, sizeof(sfi),
	              SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);

	SetAddress(strKey, sfi.iIcon);
}

void CMainFrame::UpdateStatus()
{
	if (m_toolBar.IsWindow())
	{
		// Nothing else updates the toolbar, so Back and Forward used to look
		// available in a folder with no history behind it.
		m_toolBar.EnableButton(ID_BROWSE_PARENT, m_wndItems.CanGoParent());
		m_toolBar.EnableButton(ID_BROWSE_BACK, m_wndItems.CanGoBack());
		m_toolBar.EnableButton(ID_BROWSE_FORWARD, m_wndItems.CanGoForward());
	}

	if (!m_statusBar.IsWindow())
		return;

	const CString strItems = m_wndItems.StatusText();
	const CString strImage = m_wndImage.StatusText();

	if (strItems != m_strStatusItems)
	{
		m_strStatusItems = strItems;
		m_statusBar.SetPaneText(ID_DEFAULT_PANE, strItems);
	}

	if (strImage != m_strStatusImage)
	{
		m_strStatusImage = strImage;
		m_statusBar.SetPaneText(IDS_STATUS_IMAGE, strImage);
	}
}

void CMainFrame::OnAppExit(UINT, int, CWindow)
{
	PostMessage(WM_CLOSE);
}

void CMainFrame::OnHelpFinder(UINT, int, CWindow)
{
	IW::InvokeHelp(m_hWnd, 0);
}

void CMainFrame::OnAppAbout(UINT, int, CWindow)
{
	CAboutDlg dlg;
	dlg.DoModal(m_hWnd);
}

void CMainFrame::OnViewAddressBar(UINT, int, CWindow)
{
	m_bShowAddress = !m_bShowAddress;
	m_wndAddress.ShowWindow(m_bShowAddress ? SW_SHOW : SW_HIDE);
	UpdateLayout();
}

void CMainFrame::OnViewToolBar(UINT, int, CWindow)
{
	m_bShowToolBar = !m_bShowToolBar;
	m_toolBar.ShowWindow(m_bShowToolBar ? SW_SHOW : SW_HIDE);
	UpdateLayout();
}

void CMainFrame::OnViewStatusBar(UINT, int, CWindow)
{
	m_bShowStatusBar = !m_bShowStatusBar;
	m_statusBar.ShowWindow(m_bShowStatusBar ? SW_SHOW : SW_HIDE);
	UpdateLayout();
}

void CMainFrame::AddressNavigate(const CShellItem& item)
{
	try
	{
		m_wndItems.Navigate(item);
	}
	catch (const std::exception&)
	{
	}
}

CShellItem CMainFrame::AddressFolder()
{
	return m_wndItems.FolderItem();
}

HIMAGELIST CMainFrame::AddressImageList()
{
	SHFILEINFO sfi = {0};

	return (HIMAGELIST)SHGetFileInfo(_T("C:\\"), 0, &sfi, sizeof(sfi),
	                                 SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
}

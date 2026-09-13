// ImageWalker by Zac Walker
//
// Purpose: CMainFrame implementation - mode switching, layout, toolbars, the
//          Coupling the workers call back through, and the frame's commands.
//          Also the only place that needs the complete frame type, so it owns
//          the process globals and the message loop.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"

#include "iw/logfile.h"

#pragma comment(lib, "Wininet")
#pragma comment(lib, "HtmlHelp")

class CMainFrame;
class State;

State* g_pState = nullptr;
CMainFrame* g_pMainWin = nullptr;

#include "ViewModelState.h"
#include "ViewSkin.h"
#include "ViewToolTip.h"
#include "AppCommands.h"
#include "ViewShellMenu.h"
#include "ViewDialogs.h"
#include "ViewScale.h"
#include "iw/chromelayout.h"
#include "iw/logowindow.h"
#include "AppThreadImage.h"
#include "ViewBackBuffer.h"
#include "AppThreadImage.h"
#include "iw/palettewindow.h"
#include "ViewDropTarget.h"
#include "ViewImageWindow.h"
#include "ViewModelImage.h"
#include "ViewFolderCtrl.h"
#include "ViewImageCtrl.h"
#include "iw/addressbar.h"
#include "ViewOptionsDlg.h"
#include "ImagingTransform.h"
#include "ViewDescriptionWindow.h"
#include "ViewSearchDlg.h"
#include "ViewStatusBar.h"
#include "ViewAboutDlg.h"
#include "ViewTagDlg.h"
#include "ViewSortDlg.h"

#include "ToolContactSheet.h"
#include "ToolConvert.h"
#include "ToolJpeg.h"
#include "ToolResize.h"

#include "iw/shelltree.h"
#include "ViewNormal.h"
#include "ViewEdit.h"
#include "ViewPrint.h"

#include "AppMainFrame.h"
#include "iw/mainframedispatchchecks.h"
#include "iw/fullscreenchecks.h"

CWindow IW::GetMainWindow()
{
	CWindow wnd;
	if (g_pMainWin)
	{
		wnd = *g_pMainWin;
	}
	return wnd;
}

Search::Spec Search::Any;

// The window's whole life, so main.cpp never needs the complete frame type and
// therefore never needs the fifty headers above.
int RunMainWindow(LPTSTR lpCmdLine)
{
	g_pMainWin = new CMainFrame(lpCmdLine);

	CMessageLoop theLoop;
	_Module.AddMessageLoop(&theLoop);

	try
	{
		CMainFrame::CreateMainWindow();
	}
	catch (...)
	{
		_Module.RemoveMessageLoop();
		delete g_pMainWin;
		g_pMainWin = nullptr;
		throw;
	}

	const int nRet = theLoop.Run();

	_Module.RemoveMessageLoop();

	// A delayed-render clipboard object renders out of g_pState, which the frame
	// owns -- so the flush has to happen while the frame is still here.
	::OleFlushClipboard();

	delete g_pMainWin;
	g_pMainWin = nullptr;

	return nRet;
}

bool CMainFrame::CreateMainWindow(int nCmdShow)
{
	WINDOWPLACEMENT wp;
	IW::MemZero(&wp, sizeof(WINDOWPLACEMENT));
	wp.length = sizeof(WINDOWPLACEMENT);
	wp.showCmd = nCmdShow;

	{
		if (App.Settings.Placement.length == sizeof(WINDOWPLACEMENT))
		{
			IW::MemCopy(&wp, &App.Settings.Placement, sizeof(wp));

			// Never start minimized!
			if (wp.showCmd == SW_SHOWMINIMIZED)
				wp.showCmd = nCmdShow;

			HDC hdcScreen = CreateDC(_T("DISPLAY"), nullptr, nullptr, nullptr);

			if (hdcScreen != nullptr)
			{
				// Never start bigger than the screen?
				CRect r;
				if (r.IntersectRect(&wp.rcNormalPosition,
				                    CRect(0, 0,
				                          GetDeviceCaps(hdcScreen, HORZRES),
				                          GetDeviceCaps(hdcScreen, VERTRES))))
				{
					wp.rcNormalPosition = r;
				}

				DeleteDC(hdcScreen);
			}
		}
	}

	if (!IsRectEmpty(&wp.rcNormalPosition))
	{
		if (g_pMainWin->CreateEx(nullptr, wp.rcNormalPosition) == nullptr || g_pMainWin->_bCreateFailed)
		{
			// OnCreate leaves bHandled FALSE so WTL's own WM_CREATE still runs, so
			// its -1 is discarded and the window exists even when setup failed.
			if (g_pMainWin->IsWindow())
				g_pMainWin->DestroyWindow();

			throw IW::startup_exception();
		}

		g_pMainWin->SetWindowPlacement(&wp);
	}
	else
	{
		if (g_pMainWin->CreateEx() == nullptr || g_pMainWin->_bCreateFailed)
		{
			if (g_pMainWin->IsWindow())
				g_pMainWin->DestroyWindow();

			throw IW::startup_exception();
		}

		g_pMainWin->ShowWindow(nCmdShow);
	}

	CMessageLoop* pLoop = _Module.GetMessageLoop();
	pLoop->AddMessageFilter(g_pMainWin);
	pLoop->AddIdleHandler(g_pMainWin);


	_Module.Lock();

	CString str;
	str.LoadString(IDS_MAIN_WINDOW_CREATED);
	App.Log(str);

	g_pMainWin->OpenDefaultFolder();

	return true;
}


CMainFrame::CMainFrame(LPTSTR lpCmdLine, bool openInitialFolder) :
	_state(this, openInitialFolder),
	_nThumbnailSize(App.Settings.ThumbnailSize),
	_viewNormal(this, _state),
	_viewEdit(this, _state),
	_viewPrint(this, _state),
	_statusBar(_state),
	_decodeThumbs1(this, _state),
	_pView(nullptr),
	_imageLoaderThread(_state.Loaders, this, _state),
	_lpCmdLine(lpCmdLine),
	_nTimerID(0),
	_waitForFolderToChange(this, _state),
	_nDefaultSplitterPos(App.Settings.SplitterPos)
{
	_imagePreview = IW::Image::LoadPreviewImage(_state.Loaders);
	g_pState = &_state;
}

bool CMainFrame::InvokeCommand(DWORD id)
{
	switch (id)
	{
	case ID_APP_ABOUT:
		{
			CAboutDlg dlg;
			dlg.DoModal();
		}
		return true;

	case ID_TOOLS_OPTIONS:
		{
			CViewOptions dlg(_state);

			if (dlg.DoModal() == IDOK)
			{
				OnOptionsChanged();
			}
		}
		return true;

	case ID_HELP_FINDER:
		App.InvokeHelp(IW::GetMainWindow(), 0);
		return true;

	case ID_HELP_IMAGEWALKERHOMEPAGE:
		IW::NavigateToWebPage(_T("http://www.ImageWalker.com"));
		return true;

	case ID_VIEW_SEARCHADVANCED:
		SetMode(_mode == Mode::Search ? Mode::Normal : Mode::Search);
		return true;

	case ID_VIEW_FOLDERS:
		SetMode(_mode == Mode::Folders ? Mode::Normal : Mode::Folders);
		return true;

	case ID_VIEW_DESCRIPTION:
		SetMode(_mode == Mode::Metadata ? Mode::Normal : Mode::Metadata);
		return true;

	case ID_VIEW_NORMAL:
		SetMode(Mode::Normal);
		return true;

	// Every mode button toggles: pressing the one that is already down is the
	// way back to the plain items view, which is what a pressed button means.
	case ID_VIEW_EDIT:
		SetMode(_mode == Mode::Edit ? Mode::Normal : Mode::Edit);
		return true;

	case ID_VIEW_PRINT:
		SetMode(_mode == Mode::Print ? Mode::Normal : Mode::Print);
		return true;

	// Ctrl+P outside print mode. Nobody else claims it there, so it was silent.
	case ID_FILE_PRINT:
		if (_mode != Mode::Print)
		{
			SetMode(Mode::Print);
			return true;
		}
		return false;

	case ID_VIEW_IMAGEFULLSCREEN:
		ShowImageFullScreen();
		return true;

	// Stepping the picture is the same gesture whatever the mode: edit reloads
	// itself around the new one, and the items list is the same list throughout.
	case ID_IMAGE_NEXT:
		_viewNormal.NextImage();
		return true;

	case ID_IMAGE_PREVIOUS:
		_viewNormal.PreviousImage();
		return true;

	// Both of these only open their own drop-down.
	case ID_THUMBNAILS:
	case ID_VIEW_ARRANGEICONS:
		return true;
	}

	return CommandFrameBase<CMainFrame>::InvokeCommand(id);
}

bool CMainFrame::GetCommandState(DWORD id, bool &bEnabled, bool &bChecked)
{
	switch (id)
	{
	case ID_VIEW_IMAGEFULLSCREEN:
		bChecked = _mode == Mode::FullScreen;
		return true;

	// Print mode steps pages instead, on ID_PP_BACK/ID_PP_FORWARD.
	case ID_IMAGE_NEXT:
	case ID_IMAGE_PREVIOUS:
		bEnabled = _mode != Mode::Print && _state.Folder.HasImages();
		return true;

	case ID_APP_ABOUT:
	case ID_TOOLS_OPTIONS:
	case ID_HELP_FINDER:
	case ID_HELP_IMAGEWALKERHOMEPAGE:
	case ID_THUMBNAILS:
	case ID_VIEW_ARRANGEICONS:
		return true;

	case ID_VIEW_NORMAL: bChecked = _mode == Mode::Normal; return true;
	case ID_VIEW_EDIT: bChecked = _mode == Mode::Edit; bEnabled = CanEnterEditMode(); return true;
	case ID_VIEW_PRINT: bChecked = _mode == Mode::Print; return true;

	// Claimed here only outside print mode, where PrintView answers for it.
	case ID_FILE_PRINT:
		if (_mode != Mode::Print)
			return true;
		return false;

	// All five modes are one list on one variable, so every mode button is live
	// from every mode. The chrome that belongs to the items modes is not.
	case ID_VIEW_FOLDERS:
		bChecked = _mode == Mode::Folders;
		return true;

	case ID_VIEW_SEARCHADVANCED:
		bChecked = _mode == Mode::Search;
		return true;

	// Always enterable: the panel says what it knows about whatever is focused,
	// including that it knows nothing.
	case ID_VIEW_DESCRIPTION:
		bChecked = _mode == Mode::Metadata;
		return true;
	}

	return CommandFrameBase<CMainFrame>::GetCommandState(id, bEnabled, bChecked);
}

// Full screen is the items view with the chrome gone: same view object, same
// picture, laid out as image-over-filmstrip.
void CMainFrame::ShowImageFullScreen()
{
	if (_mode == Mode::FullScreen)
	{
		SetMode(Mode::Normal);
		return;
	}

	SetMode(Mode::Normal);

	// The switch can be refused -- unsaved edits, or no image to edit.
	if (!IsItemsMode())
		return;

	if (!_state.Image.IsImageShown())
		_state.Image.NextImage();

	SetMode(Mode::FullScreen);
}

CMainFrame::~CMainFrame()
{
	ATLTRACE(_T("Delete CMainFrame\n"));

	// A delayed-render clipboard object reads this after the frame is deleted.
	g_pState = nullptr;
}


LRESULT CMainFrame::UpdateTitle(const CString& strFolderName)
{
	CString str, strTitleBase;
	strTitleBase.LoadString(IDR_MAINFRAME);

	str.Format(_T("%s - %s"), static_cast<LPCTSTR>(strFolderName), static_cast<LPCTSTR>(strTitleBase));

#ifdef _DEBUG
	str += _T(" Debug");
#endif //_DEBUG

	SetWindowText(str);
	return 0;
}

LRESULT CMainFrame::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	bHandled = FALSE;

	//_state.Loaders.Load();

	ReadFromRegistry();

	CreateSimpleStatusBar(IDS_READY, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | SBARS_TOOLTIPS);

	_statusBar.SubclassWindow(m_hWndStatusBar);
	_statusBar.SetOwner(m_hWnd);
	_statusBar.CreateThumbnailSlider();
	UpdateThumbnailSlider();

	if (!CreateToolBars())
	{
		// bHandled is FALSE so that WTL's own WM_CREATE still runs, and a -1
		// returned under it is discarded -- CreateMainWindow checks this instead.
		_bCreateFailed = true;
		return -1;
	}

	m_hWndClient = _folderSplitter.Create(m_hWnd);
	_folderSplitter.SetProportionalPos(_nDefaultSplitterPos);
	_folders.SetTreeHost(this);
	_folders.Create(_folderSplitter);

	_searchPane.GetDialog()._pFolder = &_state.Folder;
	_searchPane.Create(_folderSplitter, rcDefault, nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);

	// The scroller scrolls the template's own height; the panel lays itself out
	// to a different one, and every row of it is one line tall whatever the
	// width, so this is settled once.
	_searchPane._rectClient.bottom =
		_searchPane._rectClient.top + _searchPane.GetDialog().Layout(200, false);

	UpdateSidePane();

	// The tree used to bind these itself; the frame owns the State, so it binds.
	_state.Folder.ChangedDelegates.Bind(this, &CMainFrame::OnTreeFolderChanged);
	_state.Folder.RefreshDelegates.Bind(this, &CMainFrame::OnTreeFolderRefresh);

	SetView(&_viewNormal);

	// Start timer for animation
	_nTimerID = SetTimer(0, 1000 / 20);

	_imageLoaderThread.StartThread();
	_decodeThumbs1.StartThread();
	_waitForFolderToChange.StartThread();

	_state.Folder.ChangedDelegates.Bind(this, &ThisClass::OnFolderChanged);
	_state.Folder.SelectionDelegates.Bind(this, &ThisClass::OnSelectionChanged);

	return 0;
}

bool CMainFrame::CreateToolBars()
{
	InitMenu();

	_hWndToolBarMain = CreateToolbar(m_hWnd, IDR_MAINFRAME, s_toolbarMain);
	_hWndToolBarEdit = CreateToolbar(m_hWnd, IDC_EDIT_TOOLBAR, s_toolbarEdit);
	_hWndToolBarPrint = CreateToolbar(m_hWnd, IDC_PRINT, s_toolbarPrint);

	// Create toolbars
	if (!_hWndToolBarMain || !_hWndToolBarEdit || !_hWndToolBarPrint)
	{
		CString str;
		str.LoadString(IDS_FAILEDTOCREATETOOLBAR);
		App.Log(str);
		return false;
	}

	CRect rectLogo(0, 0, 50, 20);
	_logo.Create(m_hWnd, rectLogo, nullptr, WS_CHILD | WS_VISIBLE);

	// cx is 70 on an 80px bitmap, so the last 10 pixels are deliberately unused.
	_logo.SetOverlayImage(ImageList_LoadImage(App.GetResourceInstance(),
	                                          MAKEINTRESOURCE(IDB_APP_LOGO), 70, 1,
	                                          RGB(255, 0, 255), IMAGE_BITMAP, LR_CREATEDIBSECTION));

	_modeTabs.Create(m_hWnd, rcDefault, nullptr,
	                 WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);

	CreateAddressBar(m_hWnd);

	return true;
}

LRESULT CMainFrame::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	ATLTRACE(_T("Destroy NormalView\n"));

	if (_nTimerID) KillTimer(_nTimerID);

	// Ask everybody to wind down first, then join
	_imageLoaderThread.SignalExit();
	_decodeThumbs1.SignalExit();
	_waitForFolderToChange.SignalExit();

	_imageLoaderThread.StopThread();
	_decodeThumbs1.StopThread();
	_waitForFolderToChange.StopThread();

	// Save to registry
	SaveToRegistry();

	_Module.Unlock();
	bHandled = FALSE;

	return 0;
}

LRESULT CMainFrame::OnTimer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	if (IsWindowVisible())
	{
		// The frame ticks at 20Hz for the fades; the plasma is a 10Hz animation
		// and stepping it twice as fast just burns the palette walk.
		_nLogoTick = (_nLogoTick + 1) & 1;

		if (_nLogoTick == 0)
			_logo.OnTimer();

		_pView->OnTimer();
	}

	return 0;
}


void CMainFrame::ReadFromRegistry()
{
	_nDefaultSplitterPos = App.Settings.SplitterPos;

	// Only the three items modes are remembered: Edit and Print both need an
	// image the next run has not opened yet.
	const int nMode = App.Settings.ViewMode;

	if (nMode == static_cast<int>(Mode::Folders)) _mode = Mode::Folders;
	else if (nMode == static_cast<int>(Mode::Search)) _mode = Mode::Search;
	else if (nMode == static_cast<int>(Mode::Metadata)) _mode = Mode::Metadata;
	else _mode = Mode::Normal;

	_state.Load();

	_viewNormal.LoadDefaultSettings();
	_viewEdit.LoadDefaultSettings();
	_viewPrint.LoadDefaultSettings();

	IW::Style::SetPalette();
}

void CMainFrame::SaveToRegistry()
{
	App.Settings.SplitterPos = _folderSplitter.GetProportionalPos();
	App.Settings.ViewMode = static_cast<int>(IsItemsMode() ? _mode : Mode::Normal);
	App.Settings.ThumbnailSize = _nThumbnailSize;

	_state.Save();

	_viewNormal.SaveDefaultSettings();
	_viewEdit.SaveDefaultSettings();
	_viewPrint.SaveDefaultSettings();

	WINDOWPLACEMENT wp;
	IW::MemZero(&wp, sizeof(WINDOWPLACEMENT));
	wp.length = sizeof(WINDOWPLACEMENT);

	if (GetWindowPlacement(&wp))
	{
		App.Settings.Placement = wp;
	}
}


LRESULT CMainFrame::OnStatusViewChange(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/)
{
	CStatusBarCtrl sb(m_hWndStatusBar);

	if (sb.IsSimple())
	{
		// Hide status
	}
	else
	{
		// Show status
	}

	return 0;
}

LRESULT CMainFrame::OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	CPaintDC dc(m_hWnd);

	CRect rectClient;
	GetClientRect(rectClient);

	return 0;
}

// Row 1 shares with the toolbar, row 2 is a line of its own. The threshold is
// how much of the width the toolbar on show has taken.
int CMainFrame::AddressBarRow(int cxClient) const
{
	if (cxClient <= 0 || _pView == nullptr)
		return 2;

	const HWND hWndBar =
		_pView->CanShowToolbar(IDC_VIEW_TOOLBAR) ? _hWndToolBarMain :
		_pView->CanShowToolbar(IDC_EDIT_TOOLBAR) ? _hWndToolBarEdit :
		_pView->CanShowToolbar(IDC_PRINT) ? _hWndToolBarPrint : nullptr;

	if (hWndBar == nullptr)
		return 1;

	SIZE size = {0, 0};

	if (!::SendMessage(hWndBar, TB_GETMAXSIZE, 0, (LPARAM)&size) || size.cx <= 0)
		return 2;

	constexpr int cxAddressMin = 240;

	return (size.cx * 10 <= cxClient * 7 && cxClient - size.cx >= cxAddressMin) ? 1 : 2;
}

void CMainFrame::UpdateLayout(BOOL bResizeBars)
{
	CRect rectClient;
	GetClientRect(rectClient);

	// The status bar still positions itself; the top chrome does not.
	if ((m_hWndStatusBar != nullptr) && IW::HasVisibleStyle(m_hWndStatusBar))
	{
		if (bResizeBars)
			::SendMessage(m_hWndStatusBar, WM_SIZE, 0, 0);

		CRect rectSB;
		::GetWindowRect(m_hWndStatusBar, &rectSB);
		rectClient.bottom -= rectSB.Height();
	}

	// The view decides which bars belong to it; the layout decides whether they
	// also fit. Rebuilt every pass so it cannot cache handles that do not exist
	// yet -- a frame gets its first WM_SIZE part way through OnCreate.
	_chrome.Clear();
	const bool bShowChrome = !IsFullScreen();

	if (_pView != nullptr)
	{
		_chrome.Add(0, m_CmdBar, IW::CChromeLayout::Toolbar, IW::CChromeLayout::Stretch, 30,
		            bShowChrome && _pView->CanShowToolbar(IDC_COMMAND_BAR));
		_chrome.Add(0, _logo, IW::CChromeLayout::Custom, IW::CChromeLayout::Right, 0,
		            bShowChrome && _pView->CanShowToolbar(IDC_LOGO));

		_chrome.Add(1, _hWndToolBarMain, IW::CChromeLayout::Toolbar, IW::CChromeLayout::Left, 10,
		            bShowChrome && _pView->CanShowToolbar(IDC_VIEW_TOOLBAR));
		_chrome.Add(1, _hWndToolBarEdit, IW::CChromeLayout::Toolbar, IW::CChromeLayout::Left, 10,
		            bShowChrome && _pView->CanShowToolbar(IDC_EDIT_TOOLBAR));
		_chrome.Add(1, _hWndToolBarPrint, IW::CChromeLayout::Toolbar, IW::CChromeLayout::Left, 10,
		            bShowChrome && _pView->CanShowToolbar(IDC_PRINT));

		_chrome.Add(AddressBarRow(rectClient.Width()), m_wndAddress, IW::CChromeLayout::Combo,
		            IW::CChromeLayout::Stretch, 40, bShowChrome && _pView->CanShowToolbar(IDC_VIEW_ADDRESS));
	}

	// Hide the controls even in full screen, but do not reserve the empty
	// chrome's outer padding above the image.
	CRect rectChrome(rectClient);
	_chrome.Layout(rectChrome);
	if (bShowChrome)
		rectClient = rectChrome;
	_cyChrome = rectClient.top;

	// Setup the status bar
	if (IW::HasVisibleStyle(m_hWndStatusBar))
	{
		CRect rectStatus;
		const bool bThumbnailSlider = _statusBar._hWndThumbnailSlider != nullptr &&
			IW::HasVisibleStyle(_statusBar._hWndThumbnailSlider);

		const int cxThumbnailSlider = bThumbnailSlider ? 140 : 0;
		const int cxThumbnailGap = bThumbnailSlider ? 8 : 0;
		constexpr int cxFolderStatus = 340;

		// Two parts: what just happened, and what the folder is doing. The
		// view-mode strip is in the items pane's own gutter in this generation,
		// so the third part 2.3 needed for it is gone.
		const int cxFolderPart = IW::Max(0, rectClient.Width() - cxThumbnailSlider - cxThumbnailGap);

		int arrWidths[] = {
			IW::Max(0, cxFolderPart - cxFolderStatus),
			-1
		};

		CStatusBarCtrl sb(m_hWndStatusBar);
		sb.SetParts(2, arrWidths);
		sb.GetRect(1, rectStatus);

		if (bThumbnailSlider)
		{
			CRect rectSlider(rectStatus.right - cxThumbnailSlider, rectStatus.top,
			                 rectStatus.right, rectStatus.bottom);
			::MoveWindow(_statusBar._hWndThumbnailSlider, rectSlider.left, rectSlider.top,
			             rectSlider.Width(), rectSlider.Height(), TRUE);
		}
	}

	// resize client window
	if (nullptr != m_hWndClient)
	{
		if (_modeTabs.m_hWnd != nullptr && IW::HasVisibleStyle(_modeTabs))
		{
			const int cyTabs = IW::Min(static_cast<int>(CModeTabs::kHeight), rectClient.Height());
			_modeTabs.SetWindowPos(nullptr, rectClient.left, rectClient.top, rectClient.Width(), cyTabs,
			                       SWP_NOZORDER | SWP_NOACTIVATE);
			rectClient.top += cyTabs;
		}

		::SetWindowPos(m_hWndClient, nullptr, rectClient.left, rectClient.top,
		               rectClient.right - rectClient.left, rectClient.bottom - rectClient.top,
		               SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

BOOL CMainFrame::OnIdle()
{
	if (IsWindowVisible())
	{
		EnableMenuItems();
	}

	return false;
}

LRESULT CMainFrame::OnClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	if (_pView != nullptr && !_pView->QueryLeave())
		return 0;

	if (_state.Image.QuerySave())
	{
		bHandled = false;
	}

	return 0;
}

void CMainFrame::SetView(ViewBase* pNewView)
{
	CWaitCursor wait;
	IW::CLockWindowUpdate lock(m_hWnd);

	if (pNewView != _pView)
	{
		if (_pView) _pView->Deactivate();
		_pView = pNewView;
		HWND hWnd = _pView->Activate(_folderSplitter);
		_folderSplitter.SetSplitterPane(SPLIT_PANE_RIGHT, hWnd, false);
	}

	UpdateToolbarsShown();
	EnableMenuItems();
}

void CMainFrame::SetMode(Mode mode)
{
	if (mode == _mode)
		return;

	// The mode that is leaving gets to veto; the mode that is arriving gets to
	// refuse. Neither happens once _mode has moved, so a refusal leaves nothing
	// half-switched.
	if (_pView != nullptr && !_pView->QueryLeave())
		return;

	if (mode == Mode::Edit && !CanEnterEditMode())
		return;

	const Mode oldMode = _mode;
	const bool bWasFullScreen = oldMode == Mode::FullScreen;

	_mode = mode;

	// The window placement moves before anything measures a client rect.
	if (bWasFullScreen != (mode == Mode::FullScreen) &&
		!ApplyFullScreen(mode == Mode::FullScreen))
	{
		_mode = oldMode;
		return;
	}

	// Reflect only after _mode has actually moved: QueryLeave above can refuse,
	// and a bar that moved its own highlight first would be lying.
	switch (mode)
	{
	case Mode::Metadata: _modeTabs.SetMode(ID_VIEW_DESCRIPTION); break;
	case Mode::Folders: _modeTabs.SetMode(ID_VIEW_FOLDERS); break;
	case Mode::Search: _modeTabs.SetMode(ID_VIEW_SEARCHADVANCED); break;
	case Mode::Edit: _modeTabs.SetMode(ID_VIEW_EDIT); break;
	case Mode::Print: _modeTabs.SetMode(ID_VIEW_PRINT); break;
	default: _modeTabs.SetMode(ID_VIEW_NORMAL); break;
	}

	// Before SetView: it ends in UpdateToolbarsShown, which sizes the splitter
	// around whichever panel this leaves in the left pane.
	UpdateSidePane();

	switch (mode)
	{
	case Mode::Edit: SetView(&_viewEdit); break;
	case Mode::Print: SetView(&_viewPrint); break;
	default: SetView(&_viewNormal); break;
	}
}

// Edit mode's stack is built on one file, so showing a different one loses it
// just as leaving the mode would -- same question, same prompt.
bool CMainFrame::CanChangeImage()
{
	return _pView == nullptr || _pView->QueryLeave();
}

// Edit mode works on one image, so there has to be one: either the items mode
// is already showing it, or the focused item is an image edit mode can load.
bool CMainFrame::CanEnterEditMode()
{
	if (_state.Image.IsImageReady())
		return true;

	IW::FolderPtr pFolder = _state.Folder.GetFolder();
	const int nFocus = pFolder->GetFocusItem();

	return nFocus != -1 && pFolder->IsItemImage(nFocus);
}

// Escape climbs down one rung at a time -- the most temporary thing first, and
// the plain items view last. Returns false when there is nothing left to leave.
bool CMainFrame::OnEscape()
{
	if (_mode == Mode::FullScreen)
	{
		SetMode(Mode::Normal);
		return true;
	}

	if (_pView != nullptr && _pView->OnEscape())
		return true;

	if (_mode != Mode::Normal)
	{
		SetMode(Mode::Normal);
		return true;
	}

	return false;
}

// Which panel occupies the one slot to the left of the view is a function of
// the mode and nothing else.
void CMainFrame::UpdateSidePane()
{
	IW::CLockWindowUpdate lock(m_hWnd);

	// Only the chosen child is a splitter pane; the other is hidden, not merely covered.
	_folders.ShowWindow(_mode == Mode::Folders ? SW_SHOW : SW_HIDE);
	_searchPane.ShowWindow(_mode == Mode::Search ? SW_SHOW : SW_HIDE);

	HWND hWndSide = nullptr;
	if (_mode == Mode::Folders) hWndSide = _folders;
	else if (_mode == Mode::Search) hWndSide = _searchPane;

	_folderSplitter.SetSplitterPane(SPLIT_PANE_LEFT, hWndSide, false);
}

LRESULT CMainFrame::OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

	if (m_CmdBar.m_stackMenuWnd.GetSize() != 0)
	{
		bHandled = false;
		return 0;
	}

	IW::ScopeLockedBool dontHideCursor(App.Settings._bDontHideCursor);

	HWND hwndPoint = WindowFromPoint(point);

	if (IW::IsActive(_viewNormal._image, hwndPoint))
	{
		CMenu menuContext;
		menuContext.LoadMenu(IDR_POPUP_IMAGE);
		CMenuHandle menuPopup(menuContext.GetSubMenu(0));
		TrackPopupMenu(menuPopup, TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y);
	}
	else if (IW::IsActive(_viewNormal._folder, hwndPoint))
	{
		CMenu menuContext;
		menuContext.LoadMenu(IDR_POPUP_FOLDER);
		CMenuHandle menuPopup(menuContext.GetSubMenu(0));
		TrackPopupMenu(menuPopup, TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y);
	}
	else
	{
		bHandled = false;
		return 0;
	}

	return 0;
}

static bool IsPreTranslateMessageMessage(const int message)
{
	return message != WM_TIMER &&
		message != WM_PAINT &&
		message != WM_ERASEBKGND;
}

static bool IsEditControlMessage(const MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN)
	{
		HWND hWndFocus = GetFocus();
		if (IW::IsEdit(hWndFocus))
		{
			int nChar = static_cast<int>(pMsg->wParam);

			if (VK_ESCAPE != nChar)
			{
				return true;
			}
		}
	}

	return false;
}

BOOL CMainFrame::PreTranslateMessage(MSG* pMsg)
{
	if (IsPreTranslateMessageMessage(pMsg->message))
	{
		if (IsEditControlMessage(pMsg))
			return FALSE;

		if (!m_CmdBar.m_bMenuActive && !m_CmdBar.m_bShowKeyboardCues)
		{
			if (_searchPane.GetDialog().PreTranslateMessage(pMsg))
				return TRUE;

			if (_pView->PreTranslateMessage(pMsg))
				return TRUE;
		}

		if (IW::CAddressBar<CMainFrame>::PreTranslateMessage(pMsg))
			return TRUE;

		if (pMsg->message == WM_KEYDOWN)
		{
			int nChar = static_cast<int>(pMsg->wParam);

			switch (nChar)
			{
			case VK_CONTROL:
				if (!App.ControlKeyDown)
				{
					App.ControlKeyDown = true;
					_state.ResetFrames.Invoke();
				}
				break;

			case VK_ESCAPE:
				if (OnEscape())
					return TRUE;
				break;
			}
		}

		if (pMsg->message == WM_KEYUP)
		{
			int nChar = static_cast<int>(pMsg->wParam);

			switch (nChar)
			{
			case VK_CONTROL:
				if (App.ControlKeyDown)
				{
					App.ControlKeyDown = false;
					_state.ResetFrames.Invoke();
				}
				break;
			}
		}

		if (m_hAccel != nullptr && ::TranslateAccelerator(m_hWnd, m_hAccel, pMsg))
			return TRUE;
	}

	return FALSE;
}

void CMainFrame::UpdateToolbarsShown()
{
	IW::CLockWindowUpdate lock(m_hWnd);

	const bool bShowStatus = _mode != Mode::FullScreen;
	::ShowWindow(m_hWndStatusBar, bShowStatus ? SW_SHOW : SW_HIDE);

	_modeTabs.ShowWindow(bShowStatus ? SW_SHOW : SW_HIDE);

	if (_statusBar._hWndThumbnailSlider != nullptr)
		::ShowWindow(_statusBar._hWndThumbnailSlider,
		             bShowStatus && IsItemsMode() && _viewNormal.IsThumbnailMode() ? SW_SHOW : SW_HIDE);

	const bool bShowSidePane = _mode == Mode::Folders || _mode == Mode::Search;
	_folderSplitter.SetSinglePaneMode(bShowSidePane ? SPLIT_PANE_NONE : SPLIT_PANE_RIGHT);

	_viewNormal.ShowDescriptionPanel(_mode == Mode::Metadata);

	UpdateLayout(true);
}

bool CMainFrame::SaveNewImage(const IW::Image& dib)
{
	return _viewNormal.SaveNewImage(dib);
}


LRESULT CMainFrame::OnGetMinMaxInfo(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
	auto lpMMI = (LPMINMAXINFO)lParam;

	if (_mode == Mode::FullScreen && !_FullScreenWindowRect.IsRectEmpty())
	{
		int nWidth = _FullScreenWindowRect.right - _FullScreenWindowRect.left;
		int nHeight = _FullScreenWindowRect.bottom - _FullScreenWindowRect.top;

		lpMMI->ptMaxPosition = {0, 0};
		lpMMI->ptMaxSize.y = nHeight;
		lpMMI->ptMaxTrackSize.y = lpMMI->ptMaxSize.y;
		lpMMI->ptMaxSize.x = nWidth;
		lpMMI->ptMaxTrackSize.x = lpMMI->ptMaxSize.x;
	}

	return 0;
}

// Kept for the Coupling: the panes ask for full screen without knowing it is
// a mode. SetMode is the only thing that changes _mode.
bool CMainFrame::ViewFullScreen(bool bFullScreen)
{
	const bool bOldValue = _mode == Mode::FullScreen;

	if (bOldValue != bFullScreen)
		SetMode(bFullScreen ? Mode::FullScreen : Mode::Normal);

	return bOldValue;
}

// The scroll bars are the one thing full screen drops: the picture is fitted to
// the screen or panned with the navigator, which is the only way left to pan.
bool CMainFrame::CanShowGutterItem(DWORD id) const
{
	switch (id)
	{
	case ID_IMAGE_SCROLL:
	case ID_IMAGE_SCROLLV:
		return _mode != Mode::FullScreen;

	case ID_VIEW_FILMSTRIP:
		return _mode == Mode::FullScreen;
	}

	return true;
}

// The window-placement half of entering and leaving full screen. Called by
// SetMode once _mode has moved, and by nothing else.
bool CMainFrame::ApplyFullScreen(bool bFullScreen)
{
	constexpr DWORD frameStyle = WS_OVERLAPPEDWINDOW;
	constexpr DWORD frameExStyle =
		WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE;

	if (bFullScreen)
	{
		MONITORINFO monitorInfo = {sizeof(monitorInfo)};
		_wpBeforeFullScreen.length = sizeof(_wpBeforeFullScreen);
		if (!GetWindowPlacement(&_wpBeforeFullScreen) ||
			!::GetMonitorInfo(::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &monitorInfo))
		{
			App.Log(_T("Cannot enter full screen: window placement or monitor information is unavailable."));
			return false;
		}

		_FullScreenWindowRect = monitorInfo.rcMonitor;
		_styleBeforeFullScreen = GetStyle();
		_exStyleBeforeFullScreen = GetExStyle();
	}

	{
		IW::CLockWindowUpdate lock(m_hWnd);

		if (bFullScreen)
		{
			if (IsZoomed() || IsIconic())
				ShowWindow(SW_RESTORE);

			ModifyStyle(frameStyle, 0);
			ModifyStyleEx(frameExStyle, 0);

			// Monitor rectangles are screen coordinates, not the workspace
			// coordinates used by WINDOWPLACEMENT (notably with a top/left taskbar).
			SetWindowPos(nullptr, _FullScreenWindowRect.left, _FullScreenWindowRect.top,
				_FullScreenWindowRect.Width(), _FullScreenWindowRect.Height(),
				SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
		}
		else
		{
			ModifyStyle(frameStyle, _styleBeforeFullScreen & frameStyle);
			ModifyStyleEx(frameExStyle, _exStyleBeforeFullScreen & frameExStyle);
			SetWindowPlacement(&_wpBeforeFullScreen);
			SetWindowPos(nullptr, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
		}
	}

	// Outside the update lock, so the panes measure the client rect the
	// placement just produced rather than the one before it.
	_viewNormal.OnFullScreenChanged(bFullScreen);
	return true;
}

LRESULT CMainFrame::OnEnableMenuItems(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	EnableMenuItems();
	return 0;
}


LRESULT CMainFrame::OnToolbarDropDown(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/)
{
	NMTOOLBAR* ptb = (NMTOOLBAR*)pnmh;
	CRect rc;


	CToolBarCtrl tbar(pnmh->hwndFrom);
	BOOL b = tbar.GetItemRect(tbar.CommandToIndex(ptb->iItem), &rc);
	b;

	ATLASSERT(b);
	tbar.MapWindowPoints(HWND_DESKTOP, rc);

	if (ptb->iItem == ID_BROWSE_BACK)
	{
		IW::CShellMenu cmdbar;
		_state.History.GetBrowseBackMenu(cmdbar);
		cmdbar.TrackPopupMenu(m_hWnd, TPM_LEFTALIGN | TPM_RIGHTBUTTON, rc.left, rc.bottom);
	}
	else if (ptb->iItem == ID_BROWSE_FORWARD)
	{
		IW::CShellMenu cmdbar;
		_state.History.GetBrowseForwardMenu(cmdbar);
		cmdbar.TrackPopupMenu(m_hWnd, TPM_LEFTALIGN | TPM_RIGHTBUTTON, rc.left, rc.bottom);
	}
	else if (ptb->iItem == ID_SEARCH)
	{
		CMenu menuContext;
		menuContext.LoadMenu(IDR_POPUPS);
		m_CmdBar.TrackPopupMenu(menuContext.GetSubMenu(3), TPM_LEFTALIGN | TPM_RIGHTBUTTON, rc.left, rc.bottom);
	}
	else if (ptb->iItem == ID_TAG)
	{
		CMenu menuContext;
		menuContext.LoadMenu(IDR_POPUPS);
		m_CmdBar.TrackPopupMenu(menuContext.GetSubMenu(4), TPM_LEFTALIGN | TPM_RIGHTBUTTON, rc.left, rc.bottom);
	}
	else if (ptb->iItem == ID_THUMBNAILS) // Navigation
	{
		CMenu menuContext;
		menuContext.LoadMenu(IDR_POPUPS);
		m_CmdBar.TrackPopupMenu(menuContext.GetSubMenu(0), TPM_LEFTALIGN | TPM_RIGHTBUTTON, rc.left, rc.bottom);
	}
	else if (ptb->iItem == ID_VIEW_ARRANGEICONS)
	{
		CMenu menuContext;
		menuContext.LoadMenu(IDR_POPUPS);
		m_CmdBar.TrackPopupMenu(menuContext.GetSubMenu(1), TPM_LEFTALIGN | TPM_RIGHTBUTTON, rc.left, rc.bottom);
	}
	else if (ptb->iItem == ID_FILE_COPYTO)
	{
		IW::CShellMenu menu;
		_state.Favourite.GetCopyToMenu(menu);
		//cmdbar.TrackPopupMenu(m_hWnd, TPM_LEFTALIGN | TPM_RIGHTBUTTON,  rc.left, rc.bottom);
		m_CmdBar.TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, rc.left, rc.bottom);
	}
	else if (ptb->iItem == ID_FILE_GOTO)
	{
		IW::CShellMenu menu;
		_state.Favourite.GetGotoMenu(menu);
		//cmdbar.TrackPopupMenu(m_hWnd, TPM_LEFTALIGN | TPM_RIGHTBUTTON,  rc.left, rc.bottom);		
		m_CmdBar.TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, rc.left, rc.bottom);
	}
	else if (ptb->iItem == ID_FILE_MOVETO)
	{
		IW::CShellMenu menu;
		_state.Favourite.GetMoveToMenu(menu);
		//cmdbar.TrackPopupMenu(m_hWnd, TPM_LEFTALIGN | TPM_RIGHTBUTTON,  rc.left, rc.bottom);
		m_CmdBar.TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, rc.left, rc.bottom);
	}
	else
	{
		// Unknown popup
		ATLASSERT(0);

		return 0;
	}

	return 0;
}

HWND CMainFrame::CreateEx(HWND hWndParent, _U_RECT rect, DWORD dwStyle, DWORD dwExStyle, LPVOID lpCreateParam)
{
	TCHAR szWindowName[256];
	szWindowName[0] = 0;
	::LoadString(App.GetResourceInstance(), GetWndClassInfo().m_uCommonResourceID, szWindowName, 256);

	CMenuHandle menu;
	menu.LoadMenu(GetWndClassInfo().m_uCommonResourceID);
	return Create(hWndParent, rect, szWindowName, dwStyle, dwExStyle, menu, lpCreateParam);
}

LRESULT CMainFrame::OnCopyTo(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	_state.Favourite.OnCopyTo(wID - ID_COPYTO_FIRST, _pView->GetSelectedFileList());
	return 0;
};

LRESULT CMainFrame::OnMoveTo(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	_state.Favourite.OnMoveTo(wID - ID_MOVETO_FIRST, _pView->GetSelectedFileList());
	return 0;
};

LRESULT CMainFrame::OnGoTo(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	_viewNormal.OnGoTo(wID - ID_GOTO_FIRST);
	return 0;
};

LRESULT CMainFrame::OnHistoryForward(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	_viewNormal.OnHistory(wID - ID_HISTORY_FORWARD_FIRST);
	return 0;
};


LRESULT CMainFrame::OnHistoryBack(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	_viewNormal.OnHistory(ID_HISTORY_BACK_FIRST - wID);
	return 0;
};

void State::ContextSwitch(Delegate::List0& list)
{
	g_pMainWin->PostMessage(WM_CONTEXTSWITCH, (WPARAM)&list);
}

LRESULT CMainFrame::OnContextSwitch(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& lResult)
{
	auto pList = reinterpret_cast<Delegate::List0*>(wParam);
	pList->Invoke();
	return 0;
}

LRESULT CMainFrame::OnLoadComplete(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& lResult)
{
	IW::RefPtr<CImageLoad> pInfo;
	pInfo.Attach(reinterpret_cast<CImageLoad*>(lParam));

	if (pInfo)
	{
		ATLTRACE(_T("Received load %s complete with StopLoading=%d\n"), static_cast<LPCTSTR>(pInfo->_path), pInfo->_bWasStopped);
		_state.Image.OnLoadComplete(pInfo);
	}

	return 0;
}

LRESULT CMainFrame::OnThumbnailing(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	_logo.SetWorking(true);
	_viewNormal.OnThumbsLoaded();
	UpdateSelectStatus();
	return 0;
}

LRESULT CMainFrame::OnThumbnailingComplete(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	_logo.SetWorking(false);
	_viewNormal.OnThumbsLoaded();
	UpdateSelectStatus();
	return 0;
}


LRESULT CMainFrame::OnSearching(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	_logo.SetWorking(true);
	_viewNormal.OnThumbsLoaded();
	UpdateSelectStatus();
	return 0;
}

LRESULT CMainFrame::OnSearchingComplete(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	_viewNormal.OnThumbsLoaded();
	_logo.SetWorking(false);
	UpdateSelectStatus();
	SetStatusText(App.LoadString(IDS_READY));

	return 0;
}

LRESULT CMainFrame::OnSearchingFolder(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
	_viewNormal.OnThumbsLoaded();

	auto szFolder = (LPTSTR)wParam;

	if (!IW::IsNullOrEmpty(szFolder))
	{
		CString str;
		str.Format(IDS_STATUS_SEARCHINGFOLDER, szFolder);
		SetStatusText(str);
	}
	else
	{
		SetStatusText(App.LoadString(IDS_READY));
	}

	IW::Free(szFolder);

	return 0;
}

void CMainFrame::OnFolderChanged()
{
	IW::CShellItem item = _state.Folder.GetFolderItem();
	CString strCurrentFolder;

	// Update the title
	SHFILEINFO sfi;
	IW::MemZero(&sfi, sizeof(sfi));

	SHGetFileInfo((LPCTSTR)static_cast<LPCITEMIDLIST>(item),
	              0,
	              &sfi,
	              sizeof(SHFILEINFO),
	              SHGFI_PIDL |
	              SHGFI_DISPLAYNAME |
	              SHGFI_SYSICONINDEX |
	              SHGFI_SMALLICON);

	if (!item.GetPath(strCurrentFolder))
	{
		strCurrentFolder = sfi.szDisplayName;
	}

	// Notify the status
	CString str;
	str.Format(IDS_OPENEDFOLDER, static_cast<LPCTSTR>(strCurrentFolder));
	SetStatusText(str);

	UpdateTitle(sfi.szDisplayName);

	SetAddress(strCurrentFolder, sfi.iIcon);

	_state.History.HistoryAdd(item);
}

static CString CommandLineToPath(const CString& strCmdLine)
{
	CString str = strCmdLine;

	if (str[0] == _T('\"'))
	{
		str.Replace(_T("\""), g_szEmptyString);
	}

	return str;
}


LRESULT CMainFrame::OnCustomDraw(int idCtrl, LPNMHDR pnmh, BOOL& bHandled)
{
	auto lpCustomDraw = (LPNMCUSTOMDRAW)pnmh;

	if (m_hWndToolBar == lpCustomDraw->hdr.hwndFrom)
	{
		if (lpCustomDraw->dwDrawStage == CDDS_PREERASE)
		{
			CDCHandle dc(lpCustomDraw->hdc);
			CRect r;
			::GetClientRect(m_hWndToolBar, r);
			dc.FillSolidRect(r, IW::Style::Color::Face);
			bHandled = TRUE;
			return CDRF_SKIPDEFAULT;
		}
		if (lpCustomDraw->dwDrawStage == CDDS_PREPAINT)
		{
			bHandled = TRUE;
			return CDRF_NOTIFYITEMDRAW;
		}
		if (lpCustomDraw->dwDrawStage == CDDS_ITEMPREPAINT)
		{
			COLORREF clrText = ::GetSysColor(COLOR_BTNTEXT);

			// Custom paint the gripper bars in the rebar bands

			CDCHandle dc(lpCustomDraw->hdc);
			CRect rcItem = lpCustomDraw->rc;

			auto szText = (LPCTSTR)lpCustomDraw->lItemlParam;

			HFONT hOldFont = dc.SelectFont(static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
			dc.SetBkMode(TRANSPARENT);
			dc.SetTextColor(clrText);
			dc.DrawText(szText, -1, &rcItem, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
			dc.SelectFont(hOldFont);

			bHandled = TRUE;
			return CDRF_SKIPDEFAULT;
		}
	}
	else if (_hWndToolBarMain == lpCustomDraw->hdr.hwndFrom ||
		_hWndToolBarEdit == lpCustomDraw->hdr.hwndFrom ||
		_hWndToolBarPrint == lpCustomDraw->hdr.hwndFrom)
	{
		// 2.2 leaves the toolbars to comctl32: TBSTYLE_FLAT over the system face
		// is the flat look of its era. IW::Skin::DrawButton is 2.3's, and it only
		// reads right against the dark chrome it was written for.
		bHandled = FALSE;
		return CDRF_DODEFAULT;
	}

	bHandled = false;
	return CDRF_DODEFAULT;
}

void CMainFrame::OpenDefaultFolder()
{
	bool success = false;

	if (_lpCmdLine && _tcsclen(_lpCmdLine))
	{
		IW::CFilePath path = CommandLineToPath(_lpCmdLine);

		if (IW::Path::IsDirectory(path))
		{
			success = _state.Folder.OpenFolder(path);
		}
		else
		{
			CString strFileName = IW::Path::FindFileName(path);
			path.StripToPath();

			if (_state.Folder.OpenFolder(path))
			{
				IW::FolderPtr pFolder = _state.Folder.GetFolder();
				int nItem = pFolder->Find(strFileName);

				if (nItem != -1)
				{
					_state.Folder.Select(nItem, 0);
				}

				success = true;
			}
		}
	}

	if (!success)
	{
		_state.Folder.OpenDefaultFolder();
	}
}


LRESULT CMainFrame::OnSettingChange(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	OnOptionsChanged();
	return 0;
}

void CMainFrame::OnOptionsChanged()
{
	IW::Style::SetPalette();
	UpdateThumbnailSlider();

	_viewNormal.OnOptionsChanged();
	if (_viewPrint.m_hWnd) _viewPrint.OnOptionsChanged();
	if (_viewEdit.m_hWnd) _viewEdit.OnOptionsChanged();
}

CSize CMainFrame::GetThumbnailSize() const
{
	const CSize sizeMaximum = App.Settings._sizeThumbImage;
	const int nMaximum = IW::Max(1, IW::Max(sizeMaximum.cx, sizeMaximum.cy));
	const int nMinimum = IW::Min(64, nMaximum);
	const int nSize = IW::Clamp(_nThumbnailSize, nMinimum, nMaximum);

	return CSize(IW::Max(1, MulDiv(sizeMaximum.cx, nSize, nMaximum)),
	             IW::Max(1, MulDiv(sizeMaximum.cy, nSize, nMaximum)));
}

void CMainFrame::UpdateThumbnailSlider()
{
	const CSize sizeMaximum = App.Settings._sizeThumbImage;
	const int nMaximum = IW::Max(1, IW::Max(sizeMaximum.cx, sizeMaximum.cy));
	const int nMinimum = IW::Min(64, nMaximum);

	if (_nThumbnailSize <= 0)
		_nThumbnailSize = nMaximum;
	else
		_nThumbnailSize = IW::Clamp(_nThumbnailSize, nMinimum, nMaximum);

	_statusBar.SetThumbnailRange(nMaximum, _nThumbnailSize);
}

LRESULT CMainFrame::OnHScroll(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
	if (reinterpret_cast<HWND>(lParam) != _statusBar._hWndThumbnailSlider)
	{
		bHandled = FALSE;
		return 0;
	}

	const int nSize = static_cast<int>(::SendMessage(_statusBar._hWndThumbnailSlider, TBM_GETPOS, 0, 0));

	if (nSize != _nThumbnailSize)
	{
		_nThumbnailSize = nSize;
		_viewNormal.OnThumbnailSizeChanged();
	}

	return 0;
}

bool CMainFrame::OpenFolder(const CString& strPath)
{
	DWORD attribs = GetFileAttributes(strPath);

	if (attribs != INVALID_FILE_ATTRIBUTES)
	{
		if (attribs & FILE_ATTRIBUTE_DIRECTORY)
		{
			return _state.Folder.OpenFolder(strPath);
		}
		return OpenImage(strPath);
	}

	return false;
}

bool CMainFrame::OpenImage(const CString& strCommandLine)
{
	IW::CFilePath pathFolder(strCommandLine);
	pathFolder.RemoveFileName();

	IW::CFilePath pathImage(strCommandLine);
	pathImage.StripToFilenameAndExtension();

	return _state.Folder.OpenFolder(pathFolder) &&
		_state.Folder.Select(pathImage);
}

void CMainFrame::TrackPopupMenu(HMENU hMenu, UINT uFlags, int x, int y)
{
	EnableMenuItems();
	m_CmdBar.TrackPopupMenu(hMenu, uFlags, x, y);
}

void CMainFrame::OnSortOrderChanged(int order)
{
	_statusBar.OnSortOrderChanged(order);
	UpdateSelectStatus();
}

void CMainFrame::UpdateSelectStatus()
{
	_statusBar.UpdateMessage();

	CStatusBarCtrl sb(m_hWndStatusBar);
	sb.SetText(1, g_szEmptyString, SBT_OWNERDRAW);

	// SB_SETTEXT with the value it already holds does not repaint, and the
	// thumbnail progress bar behind that text moves on every progress message.
	CRect rectPart;

	if (sb.GetRect(1, rectPart))
		_statusBar.InvalidateRect(rectPart, FALSE);
}

void CMainFrame::SetStatusText(const CString& str)
{
	CStatusBarCtrl sb(m_hWndStatusBar);
	sb.SetText(0, str);
}

CString CMainFrame::GetFolderPath() const
{
	return _state.Folder.GetFolderPath();
}

void CMainFrame::OnSelectionChanged()
{
	UpdateSelectStatus();
}

void CMainFrame::AfterCopy(bool bMove)
{
	_pView->OnAfterCopy(bMove);
}

void CMainFrame::BeginImageFade(int nSteps)
{
	_viewNormal.BeginImageFade(nSteps);
}

bool CMainFrame::IsItemFolder(long nItem) const { return _viewNormal._folder.IsItemFolder(nItem); };
bool CMainFrame::SelectFolderItem(const CString& strFileName) { return _state.Folder.Select(strFileName); };
CString CMainFrame::GetItemPath(int nItem) const { return _state.Folder.GetFolder()->GetItemPath(nItem); };
IW::ITEMLIST CMainFrame::GetItemList() const { return _state.Folder.GetFolder()->GetItemList(); };
int CMainFrame::GetItemCount() const { return _viewNormal._folder.GetItemCount(); };
int CMainFrame::GetFocusItem() const { return _viewNormal._folder.GetFocusItem(); };
void CMainFrame::SetFocusItem(int nFocusItem, bool bSignalEvent) { _state.Folder.Select(nFocusItem, 0, bSignalEvent); };

// Not CWindow::PostMessage: with a NULL m_hWnd that degrades to
// PostThreadMessage on the worker's own queue and reports success, so the
// notification is never seen.
void CMainFrame::PostFromWorker(UINT msg)
{
	if (m_hWnd != nullptr && ::IsWindow(m_hWnd))
		::PostMessage(m_hWnd, msg, 0, 0);
}

void CMainFrame::SignalSearching()
{
	PostFromWorker(WM_SEARCHING);
};

void CMainFrame::SignalSearchingComplete()
{
	PostFromWorker(WM_SEARCHING_COMPLETE);
};

void CMainFrame::SignalThumbnailing()
{
	PostFromWorker(WM_THUMBNAILING);
};

void CMainFrame::SignalThumbnailingComplete()
{
	PostFromWorker(WM_THUMBNAILING_COMPLETE);
};

void CMainFrame::SignalSearchingFolder(const CString& strPath)
{
	LPTSTR szPath = IW::StrDup(strPath);

	if (m_hWnd == nullptr || !::IsWindow(m_hWnd) ||
		!::PostMessage(m_hWnd, WM_SEARCHING_FOLDER, (WPARAM)szPath, 0))
	{
		IW::Free(szPath);
	}
};

void CMainFrame::SignalImageLoadComplete(CImageLoad* pInfo)
{
	if (m_hWnd == nullptr || !::IsWindow(m_hWnd) ||
		!::PostMessage(m_hWnd, WM_LOADCOMPLETE, 0, (LPARAM)pInfo))
	{
		// Take back the reference the worker detached for us
		IW::RefPtr<CImageLoad> pOrphan;
		pOrphan.Attach(pInfo);
	}
};

// Image
void CMainFrame::Command(WORD id) { OnCommand(id); };

HWND CMainFrame::GetImageWindow()
{
	return _pView->GetImageWindow();
}

void CMainFrame::NewImage(bool bScrollToCenter)
{
	_viewNormal.OnNewImage(bScrollToCenter);
	if (_viewEdit.m_hWnd) _viewEdit.OnNewImage(bScrollToCenter);
};

void CMainFrame::SortOrderChanged(int order) { OnSortOrderChanged(order); };
void CMainFrame::UpdateStatusText() { _state.Image.UpdateStatusText(); };

void CMainFrame::StartSearchThread(Search::Type type, const Search::Spec& ss) { _decodeThumbs1.StartSearch(type, ss); };
void CMainFrame::StartSearching() { _viewNormal.OnStartSearching(); };
void CMainFrame::ResetThumbThread() { _decodeThumbs1.ResetThread(); };
void CMainFrame::StopLoadingThumbs() { _decodeThumbs1.Abort(); };
void CMainFrame::ResetWaitForFolderToChangeThread() { _waitForFolderToChange.ResetThread(); };
void CMainFrame::ShowImage(CImageLoad* pInfo)
{
	// Asked before the load starts: the placeholder below already replaces what
	// is on screen, and edit mode reloads from it.
	if (pInfo != nullptr &&
	    pInfo->_path.CompareNoCase(_state.Image.GetImageFileName()) != 0 &&
	    !CanChangeImage())
		return;

	ShowThumbnailPlaceholder(pInfo);
	_imageLoaderThread.ShowImage(pInfo);
};

// The items pane has already decoded a thumbnail of this file. Standing it in at
// the size the real picture will take fills the pane at once and makes the swap
// a repaint rather than a re-layout.
//
// This is the funnel every request goes through -- LoadImage, StepImage and the
// "seek past what is not an image" chain all end here -- so there is nowhere
// else that has to remember to do it.
void CMainFrame::ShowThumbnailPlaceholder(CImageLoad* pInfo)
{
	if (pInfo == nullptr || _state.Image.IsDirty())
		return;

	if (pInfo->_path.CompareNoCase(_state.Image.GetImageFileName()) == 0)
		return;

	IW::FolderPtr pFolder = _state.Folder.GetFolder();
	const int nItem = pInfo->_nLoadItem;

	// By index, checked against the path: GetItemPath is a shell round trip, and
	// the folder may have been re-sorted since the request was made.
	if (nItem < 0 || nItem >= pFolder->GetItemCount() ||
		pFolder->GetItemPath(nItem).CompareNoCase(pInfo->_path) != 0)
		return;

	IW::Image thumbnail;
	pFolder->GetItemImage(nItem, thumbnail);

	if (thumbnail.IsEmpty())
		return;

	_state.Image.ShowPlaceholder(thumbnail, pInfo->_path,
	                             thumbnail.GetCameraSettings().OriginalImageSize);
}
void CMainFrame::SetStopLoading() { _imageLoaderThread.SetStopLoading(); };

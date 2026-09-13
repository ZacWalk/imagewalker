#pragma once

// The frame: a command bar with the logo, one toolbar row with the address
// combo, a status bar, and a splitter holding the image pane on the left and
// the items pane on the right.

#include "ImageView.h"
#include "ItemsView.h"

#include "iw/addressbar.h"
#include "iw/chromelayout.h"
#include "iw/logowindow.h"

class CMainFrame : public CFrameWindowImpl<CMainFrame>, public IW::CAddressBar<CMainFrame>
{
public:
	DECLARE_FRAME_WND_CLASS(_T("IWArtMateFrame"), IDR_MAINFRAME)

	enum { kLogoTimer = 1 };

	BEGIN_MSG_MAP(CMainFrame)
		MSG_WM_CREATE(OnCreate)
		MSG_WM_DESTROY(OnDestroy)
		MSG_WM_SIZE(OnSize)
		MSG_WM_TIMER(OnTimer)
		MSG_WM_SETFOCUS(OnSetFocus)
		MSG_WM_ERASEBKGND(OnEraseBkgnd)
		MSG_WM_INITMENUPOPUP(OnInitMenuPopup)
		MESSAGE_HANDLER(WM_IW_SELECTIONCHANGED, OnSelectionChanged)

		CHAIN_MSG_MAP(IW::CAddressBar<CMainFrame>)

		COMMAND_ID_HANDLER_EX(ID_APP_EXIT, OnAppExit)
		COMMAND_ID_HANDLER_EX(ID_APP_ABOUT, OnAppAbout)
		COMMAND_ID_HANDLER_EX(ID_HELP_FINDER, OnHelpFinder)
		COMMAND_ID_HANDLER_EX(ID_HELP, OnHelpFinder)
		COMMAND_ID_HANDLER_EX(ID_VIEW_ADDRESSBAR, OnViewAddressBar)
		COMMAND_ID_HANDLER_EX(ID_VIEW_TOOLBAR, OnViewToolBar)
		COMMAND_ID_HANDLER_EX(ID_VIEW_STATUS_BAR, OnViewStatusBar)

		CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)

		// Commands the frame does not own belong to the panes.
		if (uMsg == WM_COMMAND && RouteCommandToPanes(wParam, lParam, lResult))
			return TRUE;
	END_MSG_MAP()

	BOOL RouteCommandToPanes(WPARAM wParam, LPARAM lParam, LRESULT& lResult);
	BOOL PreTranslateMessage(MSG* pMsg);
	void UpdateLayout(BOOL bResizeBars = TRUE);

	// IW::CAddressBar host contract.
	enum { kAddressBarId = IDC_ADDRESS_COMBO };
	void AddressNavigate(const CShellItem& item);
	CShellItem AddressFolder();
	HIMAGELIST AddressImageList();

private:
	LRESULT OnCreate(LPCREATESTRUCT lpcs);
	void OnDestroy();
	void OnSize(UINT nType, CSize size);
	void OnTimer(UINT_PTR nIDEvent);
	void OnSetFocus(CWindow wndOld);
	BOOL OnEraseBkgnd(CDCHandle dc);
	void OnInitMenuPopup(CMenuHandle menu, UINT nIndex, BOOL bSysMenu);
	LRESULT OnSelectionChanged(UINT, WPARAM, LPARAM, BOOL&);

	void OnAppExit(UINT, int, CWindow);
	void OnAppAbout(UINT, int, CWindow);
	void OnHelpFinder(UINT, int, CWindow);
	void OnViewAddressBar(UINT, int, CWindow);
	void OnViewToolBar(UINT, int, CWindow);
	void OnViewStatusBar(UINT, int, CWindow);

	void UpdateStatus();
	void UpdateFolder();

	WTL::CCommandBarCtrl m_CmdBar;
	IW::CLogoWindow m_wndLogo;
	IW::CChromeLayout m_chrome;
	CToolBarCtrl m_toolBar;
	CMultiPaneStatusBarCtrl m_statusBar;
	CSplitterWindow m_splitter;

	CImageView m_wndImage;
	CItemsView m_wndItems;

	BOOL m_bShowAddress = TRUE;
	BOOL m_bShowToolBar = TRUE;
	BOOL m_bShowStatusBar = TRUE;

	CString m_strFolder;
	CString m_strStatusItems;
	CString m_strStatusImage;
};

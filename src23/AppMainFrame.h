// ImageWalker by Zac Walker
//
// Purpose: CMainFrame: the frame window, its five modes, and its
//          implementation of Coupling.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once



class CMainFrame :
	public IW::Skin::WindowImpl<CMainFrame>,
	//public AnimateWindowImpl<CMainFrame>,
	public IW::CAddressBar<CMainFrame>,
	public CommandFrameBase<CMainFrame>,
	public CFrameWindowImpl<CMainFrame>,
	public CMessageFilter,
	public CIdleHandler,
	public Coupling
{
public:
	typedef CMainFrame ThisClass;
	typedef IW::Skin::WindowImpl<ThisClass> SkinBase;

	DECLARE_FRAME_WND_CLASS(NULL, IDR_MAINFRAME);

	// IW::CAddressBar host contract.
	enum { kAddressBarId = ID_ADDRESS };
	void AddressNavigate(const IW::CShellItem &item) { _state.Folder.OpenFolder(item); }
	IW::CShellItem AddressFolder() { return _state.Folder.GetFolderItem(); }
	HIMAGELIST AddressImageList() const { return App.GetShellImageList(true); }

	// IW::CShellTreeView host contract.
	void TreeNavigate(const IW::CShellItem &item) { _state.Folder.OpenFolder(item); }
	bool TreeShowHidden() { return App.Settings.m_bShowHidden; }
	HIMAGELIST TreeImageList() { return App.GetShellImageList(true); }

	void OnTreeFolderChanged() { _folders.SetCurFolder(_state.Folder.GetFolderItem()); }
	void OnTreeFolderRefresh() { _folders.Refresh(); }

	// What the frame is for at any moment. Full screen is not one of these --
	// it is a property of the frame that the items modes read.
	//
	// Normal, Folders and Search are the same view with a different occupant in
	// the one slot to its left, so "which panel is open" is not separate state
	// that can disagree with the mode.
	enum class Mode { Normal, Folders, Search, Edit, Print };

	State _state;
	int _nThumbnailSize;
	NormalView _viewNormal;
	EditView _viewEdit;
	PrintView _viewPrint;

	IW::CLogoWindow _logo;
	IW::CChromeLayout _chrome;
	int _cyChrome = 0;
	IW::CShellTreeView<CMainFrame> _folders;
	IW::CDialogScroll<CSearchPanelDlg> _searchPane;
	Mode _mode = Mode::Normal;
	CSkinedStatusBarCtrl _statusBar;
	CRect _FullScreenWindowRect = {0, 0, 0, 0};
	WINDOWPLACEMENT _wpBeforeFullScreen = {};
	DWORD _styleBeforeFullScreen = 0;
	DWORD _exStyleBeforeFullScreen = 0;
	CSplitter2Window _folderSplitter;
	CToolBarCtrl _toolbarFolderOptions;
	DecodeThumbsThread _decodeThumbs1;
	HWND _hWndToolBarMain = nullptr;
	HWND _hWndToolBarEdit = nullptr;
	HWND _hWndToolBarPrint = nullptr;
	IW::Image _imagePreview;
	ViewBase *_pView;
	ImageLoaderThread _imageLoaderThread;
	LPTSTR _lpCmdLine;
	UINT_PTR _nTimerID;
	WaitForFolderToChangeThread _waitForFolderToChange;
	bool _bFullScreen;
	bool _bCreateFailed = false;
	int _nDefaultSplitterPos;

	// Construction
	CMainFrame(LPTSTR lpCmdLine, bool openInitialFolder = true);
	~CMainFrame();	

	// Message map
	BEGIN_MSG_MAP(CMainFrame)

		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseChrome)
		MESSAGE_HANDLER(WM_CLOSE, OnClose)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_GETMINMAXINFO, OnGetMinMaxInfo)
		MESSAGE_HANDLER(WM_HSCROLL, OnHScroll)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)

		MESSAGE_HANDLER(WM_CONTEXTSWITCH, OnContextSwitch)
		MESSAGE_HANDLER(WM_LOADCOMPLETE, OnLoadComplete)
		MESSAGE_HANDLER(WM_SEARCHING, OnSearching)
		MESSAGE_HANDLER(WM_SEARCHING_COMPLETE, OnSearchingComplete)
		MESSAGE_HANDLER(WM_SEARCHING_FOLDER, OnSearchingFolder)
		MESSAGE_HANDLER(WM_THUMBNAILING, OnThumbnailing)
		MESSAGE_HANDLER(WM_THUMBNAILING_COMPLETE, OnThumbnailingComplete)

		COMMAND_RANGE_HANDLER(ID_COPYTO_FIRST, ID_COPYTO_LAST, OnCopyTo)
		COMMAND_RANGE_HANDLER(ID_GOTO_FIRST, ID_GOTO_LAST, OnGoTo)
		COMMAND_RANGE_HANDLER(ID_HISTORY_BACK_FIRST, ID_HISTORY_BACK_LAST, OnHistoryBack)
		COMMAND_RANGE_HANDLER(ID_HISTORY_FORWARD_FIRST, ID_HISTORY_FORWARD_LAST, OnHistoryForward)
		COMMAND_RANGE_HANDLER(ID_MOVETO_FIRST, ID_MOVETO_LAST, OnMoveTo)

		NOTIFY_CODE_HANDLER(TBN_DROPDOWN, OnToolbarDropDown)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)

		NOTIFY_HANDLER(ATL_IDW_STATUS_BAR, SBN_SIMPLEMODECHANGE, OnStatusViewChange)

		if (!_bFullScreen)
		{
			CHAIN_MSG_MAP(SkinBase)
		}
		CHAIN_MSG_MAP(IW::CAddressBar<CMainFrame>)
		CHAIN_MSG_MAP(CommandFrameBase<CMainFrame>)
		CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
		//CHAIN_MSG_MAP(AnimateWindowImpl<CMainFrame>)

	END_MSG_MAP()

	LRESULT OnAddressDeleteItem(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/);
	LRESULT OnAddressDropDown(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnAddressSelectionChange(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnContextSwitch(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& lResult);
	LRESULT OnCopyTo(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnEnableMenuItems(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGetMinMaxInfo(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnGoTo(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHistoryBack(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHistoryForward(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHScroll(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnLang(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnLoadComplete(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& lResult);
	LRESULT OnMoveTo(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSearching(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnSearchingComplete(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnSearchingFolder(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnSettingChange(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnStatusViewChange(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/);
	LRESULT OnThumbnailing(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnThumbnailingComplete(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnTimer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnToolbarDropDown(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/);


	BOOL OnIdle();

	// A flat toolbar is implicitly transparent, so it does not erase itself; it
	// asks its parent to paint into its DC. The rebar used to answer that, with
	// this gradient -- 2.31 is skinned, so the system button face is wrong here.
	LRESULT OnEraseChrome(UINT, WPARAM wParam, LPARAM, BOOL&)
	{
		CDCHandle dc(reinterpret_cast<HDC>(wParam));

		CRect rc;
		GetClientRect(rc);

		CRect rcChrome(rc.left, rc.top, rc.right, rc.top + _cyChrome);

		if (!rcChrome.IsRectEmpty())
			IW::Skin::DrawGradient(dc, rcChrome, IW::Emphasize(IW::Style::Color::Window, 64),
			                       IW::Style::Color::Window);

		if (rcChrome.bottom < rc.bottom)
		{
			CRect rcRest(rc.left, rcChrome.bottom, rc.right, rc.bottom);
			dc.FillSolidRect(rcRest, IW::Style::Color::Window);
		}

		return 1;
	}
	BOOL PreTranslateMessage(MSG* pMsg);
	CString GetFolderPath() const;
	CString GetItemPath(int nItem) const;
	HWND CreateEx(HWND hWndParent = NULL, _U_RECT rect = NULL, DWORD dwStyle = 0, DWORD dwExStyle = 0, LPVOID lpCreateParam = NULL);
	HWND GetImageWindow();
	ITEMLIST GetItemList() const;
	CSize GetThumbnailSize() const;
	ViewBase *GetView() { return  _pView; }	
	LRESULT UpdateTitle(const CString &strFolderName);
	bool CreateToolBars();
	bool IsItemFolder(long nItem) const;	
	bool IsFullScreen() const { return _bFullScreen; }
	bool OpenFolder(const CString &strPath);
	bool OpenImage(const CString &strCommandLine);
	bool SaveNewImage(const IW::Image &dib);
	bool SelectFolderItem(const CString &strFileName);		
	bool ViewFullScreen(bool bFullScreen);
	int GetFocusItem() const;
	int GetItemCount() const;
	static bool CreateMainWindow(int nCmdShow = SW_SHOWDEFAULT);
	void AfterCopy(bool bMove);
	void BeginImageFade(int nSteps);
	void Command(WORD id);		
	void CreateFolderBar();
	bool GetCommandState(DWORD id, bool &bEnabled, bool &bChecked);
	bool InvokeCommand(DWORD id);
	void NewImage(bool bScrollToCenter);
	void OnFavouritesChanged();
	void OnFolderChanged();
	void OnOptionsChanged();
	void OnSelectionChanged();
	void OnSortOrderChanged(int order);
	void OpenDefaultFolder();
	void ReadFromRegistry();
	void ResetThumbThread();
	void ResetWaitForFolderToChangeThread();
	void SaveToRegistry();
	void SetFocusItem(int nFocusItem, bool bSignalEvent);
	void UpdateSidePane();
	bool HasSearchSpec() { return _searchPane.GetDialog().HasSpec(); }
	void SetStatusText(const CString &str);
	void SetStopLoading();
	void SetView(ViewBase *pNewView);
	void SetMode(Mode mode);

	// The three modes that share the items view, and therefore the main toolbar,
	// the address bar and the view-mode strip.
	bool IsItemsMode() const { return _mode == Mode::Normal || _mode == Mode::Folders || _mode == Mode::Search; }
	bool CanEnterEditMode();
	bool CanChangeImage();
	bool OnEscape();
	void ShowImage(CImageLoad *pInfo);
	void ShowThumbnailPlaceholder(CImageLoad *pInfo);
	void ShowImageFullScreen();
	void ShowSearchPane() { SetMode(Mode::Search); }
	void StartSearch(Search::Type type) { _searchPane.GetDialog().Search(type); }
	void SignalImageLoadComplete(CImageLoad *pInfo);
	void SignalSearching();
	void SignalSearchingComplete();
	void SignalSearchingFolder(const CString &strPath);
	void SignalThumbnailing();
	void SignalThumbnailingComplete();
	void PostFromWorker(UINT msg);
	void SortOrderChanged(int order);
	void StartSearchThread(Search::Type type, const Search::Spec &ss); 
	void StartSearching();
	void StopLoadingThumbs();
	void TrackPopupMenu(HMENU hMenu, UINT uFlags);
	void TrackPopupMenu(HMENU hMenu, UINT uFlags, int x, int y);
	void UpdateLayout(BOOL bResizeBars = TRUE);
	int AddressBarRow(int cxClient) const;
	void UpdateSelectStatus();
	void UpdateStatusText();
	void UpdateToolbarsShown();
	void UpdateThumbnailSlider();
};

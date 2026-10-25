// ImageWalker by Zac Walker
//
// Purpose: The normal mode: the items pane and the image pane side by side,
//          or the film strip when full screen.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

class NormalView : 
	public CWindowImpl<NormalView>,
	public CSplitter2Impl<NormalView>,
	public ImageCommandImpl<NormalView>,
	public ViewBase
{
public:

	typedef NormalView ThisClass;

	CImageCtrl _image;
	CFolderCtrl _folder;
	Coupling *_pCoupling;
	State &_state;

	int m_nAttribs;
	bool m_bInDesktopFolder;
	int _nDefaultSplitterPos;
	CString _strDefaultScale;

	// Remembered across a trip to full screen, which rearranges the panes.
	int _nWindowedSplitterPos = 0;

	Coupling *GetCoupling()
	{
		return _pCoupling;
	}

	CString GetFolderPath() const
	{
		return _state.Folder.GetFolderPath();
	}

	void UpdateToolbarsShown()
	{
		return _pCoupling->UpdateToolbarsShown();
	}

	bool HasSearchSpec()
	{
		return _pCoupling->HasSearchSpec();
	}

	NormalView(Coupling *pCoupling, State &state) :
		_state(state),
		_folder(pCoupling, state),
		_image(pCoupling, state),
		_pCoupling(pCoupling),
		m_nAttribs(0),
		m_bInDesktopFolder(false),
		_nDefaultSplitterPos(App.Settings.NormalSplitterPos),
		_strDefaultScale(App.Settings.NormalScale)
	{	
	}

	

	~NormalView()
	{
		ATLTRACE(_T("Delete NormalView\n"));
	}

	

	bool InvokeCommand(DWORD id)
	{
		switch (id)
		{
		// Arrange
		case ID_ARRANGEICONS_MODIFIED: _folder.SetSortOrder(IW::ePropertyModifiedDate); return true;
		case ID_ARRANGEICONS_DATETAKEN: _folder.SetSortOrder(IW::ePropertyDateTaken); return true;
		case ID_ARRANGEICONS_NAME: _folder.SetSortOrder(IW::ePropertyName); return true;
		case ID_ARRANGEICONS_SIZE: _folder.SetSortOrder(IW::ePropertySize); return true;
		case ID_ARRANGEICONS_TYPE: _folder.SetSortOrder(IW::ePropertyType); return true;

		case ID_ARRANGEICONS_MORE:
			{
				CSortDlg dlg;
				dlg._nSortOrder = _state.Folder._nSortOrder;
				dlg._bAscending = _state.Folder._bAscending;

				if (IDOK == dlg.DoModal())
				{
					_folder.SetSortOrder(dlg._nSortOrder, dlg._bAscending);
				}
			}
			return true;

		// Browse
		case ID_BROWSE_BACK: OnHistory(-1); return true;
		case ID_BROWSE_FORWARD: OnHistory(1); return true;
		case ID_BROWSE_NEWFOLDER: _folder.CreateFolder(); return true;
		case ID_BROWSE_PARENT: _state.Folder.OpenParent(); return true;
		case ID_BROWSE_RENAME: RenameSelection(); return true;

		// Edit
		case ID_EDIT_COPY: _folder.Copy(); return true;
		case ID_EDIT_COPY_PATHS: CopyFilePaths(); return true;
		case ID_EDIT_CUT: _folder.Cut(); return true;
		case ID_EDIT_PASTE: _folder.Paste(); return true;
		case ID_EDIT_SELECT_ALL: _state.Folder.SelectAll(); return true;
		case ID_EDIT_INVERTSELECTION: _state.Folder.SelectInverse(); return true;
		case ID_EDIT_SELECTALLIMAGES: _state.Folder.SelectImages(); return true;
		case ID_EDIT_ROTATELEFT: RotateSelection(IW::Rotation::Left); return true;
		case ID_EDIT_ROTATERIGHT: RotateSelection(IW::Rotation::Right); return true;

		// File
		case ID_FILE_OPEN: _state.Folder.OnFileDefault(); return true;
		case ID_FILE_OPEN_WITH: OpenWith(); return true;
		case ID_FILE_OPENCONTAINING: OpenContainingFolder(); return true;
		case ID_FILE_EXPLORE: OpenInExplorer(); return true;
		case ID_FILE_PROPERTIES: _folder.ShowProperties(); return true;

		// Favourites. The Go To item only opens its own drop-down.
		case ID_FILE_GOTO:
			return true;

		case ID_GOTO_ADDCURRENT:
			_state.Favourite.Add(_folder._state.Folder.GetFolderItem());
			return true;

		case ID_GOTO_NEWLOCATION: GoToNewLocation(); return true;

		// Image stepping is the frame's: it is the same gesture in edit mode.

		// Layout
		case ID_THUMBNAILS_DETAIL: SetViewMode(eViewDetail); return true;
		case ID_THUMBNAILS_MATRIX: SetViewMode(eViewMatrix); return true;
		case ID_THUMBNAILS_THUMBNAIL: SetViewMode(eViewNormal); return true;

		// Tools
		case ID_TOOLS_CONVERTIMAGES: { CToolConvert tool(_state); tool.DoModal(); } return true;
		case ID_TOOLS_RESIZE: { CToolResize tool(_state); tool.DoModal(); } return true;
		case ID_TOOLS_LOSSLESS: { CToolJpeg tool(_state); tool.DoModal(); } return true;

		// View
		case ID_VIEW_REFRESHX: _state.Folder.RefreshFolder(); return true;
		case ID_SEARCH_STOP: _state.Folder.RefreshFolder(); return true;

		case ID_VIEW_DESCRIPTION:
			App.Settings.ShowDescription = !App.Settings.ShowDescription;
			_state.ResetFrames.Invoke();
			return true;

		case ID_VIEW_ADVANCEDIMAGE:
			SetImageDetailLevel(App.Settings.ImageDetailLevel + 1);
			return true;

		case ID_VIEW_LESSIMAGE:
			SetImageDetailLevel(App.Settings.ImageDetailLevel - 1);
			return true;

		// Search
		case ID_SEARCH_MYPICTURES: SearchToolbar(Search::MyPictures); return true;
		case ID_SEARCH_CURRENT: SearchToolbar(Search::Current); return true;

		case ID_SEARCH_IMAGESINSUBFOLDERS:
			{
				Search::Spec spec;
				spec._bOnlyShowImages = true;
				_state.Folder.Search(Search::Current, spec);
			}
			return true;

		// Tags
		case ID_TAG_SELECT: SelectTagFromDialog(); return true;

		// Options
		case ID_OPTIONS_SHOWDESCRIPTIONSINDETAIL: ToggleOption(App.Settings.ShowDescriptions); return true;
		case ID_OPTIONS_SHOWHIDDENFILES: ToggleOption(App.Settings.m_bShowHidden); return true;
		case ID_OPTIONS_SHOWMETADATAMARKERS: ToggleOption(App.Settings.m_bShowMarkers); return true;
		case ID_OPTIONS_SHOWTHUMBNAILTOOLTIPS: ToggleOption(App.Settings.m_bShowToolTips); return true;
		case ID_OPTIONS_USESHORTDATES: ToggleOption(App.Settings.m_bShortDates); return true;
		case ID_OPTIONS_ZOOMTHUMBNAILSINMATRIXVIEW: ToggleOption(App.Settings.ZoomThumbnails); return true;
		}

		return InvokeImageCommand(id);
	}

	bool GetCommandState(DWORD id, bool &bEnabled, bool &bChecked)
	{
		switch (id)
		{
		case ID_ARRANGEICONS_MODIFIED:
		case ID_ARRANGEICONS_DATETAKEN:
		case ID_ARRANGEICONS_NAME:
		case ID_ARRANGEICONS_SIZE:
		case ID_ARRANGEICONS_TYPE:
		case ID_ARRANGEICONS_MORE:
		case ID_BROWSE_NEWFOLDER:
		case ID_FILE_GOTO:
		case ID_GOTO_ADDCURRENT:
		case ID_GOTO_NEWLOCATION:
		case ID_TOOLS_CONVERTIMAGES:
		case ID_TOOLS_RESIZE:
		case ID_TOOLS_LOSSLESS:
		case ID_VIEW_REFRESHX:
		case ID_SEARCH_STOP:
		case ID_SEARCH_IMAGESINSUBFOLDERS:
			return true;

		case ID_BROWSE_BACK: bEnabled = _state.History.CanBrowseBack(); return true;
		case ID_BROWSE_FORWARD: bEnabled = _state.History.CanBrowseForward(); return true;
		case ID_BROWSE_PARENT: bEnabled = !InDesktopFolder(); return true;
		case ID_BROWSE_RENAME: bEnabled = CanRename(); return true;

		case ID_EDIT_COPY: bEnabled = CanCopy(); return true;
		case ID_EDIT_COPY_PATHS: bEnabled = HasSelection(); return true;
		case ID_EDIT_CUT: bEnabled = CanMove(); return true;
		case ID_EDIT_SELECT_ALL:
		case ID_EDIT_INVERTSELECTION:
			return true;

		case ID_EDIT_PASTE:
			bEnabled = ::IsClipboardFormatAvailable(CF_HDROP) ||
				::IsClipboardFormatAvailable(CF_DIB);
			return true;

		case ID_EDIT_SELECTALLIMAGES: bEnabled = _state.Folder.HasImages(); return true;

		case ID_FILE_OPEN:
		case ID_FILE_OPEN_WITH:
		case ID_FILE_OPENCONTAINING:
			bEnabled = HasAFocusItem();
			return true;

		case ID_FILE_PROPERTIES: bEnabled = HasProperties(); return true;

		case ID_FILE_EXPLORE:
			bEnabled = !GetFolder()->GetFolderPath().IsEmpty();
			return true;

		case ID_EDIT_ROTATELEFT:
		case ID_EDIT_ROTATERIGHT:
			bEnabled = HasSelection();
			return true;

		case ID_THUMBNAILS_DETAIL: bChecked = _folder._displayMode == eViewDetail; return true;
		case ID_THUMBNAILS_MATRIX: bChecked = _folder._displayMode == eViewMatrix; return true;
		case ID_THUMBNAILS_THUMBNAIL: bChecked = _folder._displayMode == eViewNormal; return true;

		case ID_VIEW_DESCRIPTION: bChecked = App.Settings.ShowDescription; return true;

		case ID_VIEW_ADVANCEDIMAGE:
			bChecked = App.Settings.ImageDetailLevel > 0;
			bEnabled = App.Settings.ImageDetailLevel < AppSettings::ImageDetailMax;
			return true;

		case ID_VIEW_LESSIMAGE:
			bEnabled = App.Settings.ImageDetailLevel > 0;
			return true;

		case ID_SEARCH_MYPICTURES:
		case ID_SEARCH_CURRENT:
			// Always live: greying these out on a fresh install left the only two
			// search entries in the menus permanently dead, with nothing to say
			// that the way in is the Search mode button.
			bEnabled = true;
			return true;

		case ID_TAG_SELECT: return true;

		case ID_OPTIONS_SHOWDESCRIPTIONSINDETAIL: bChecked = App.Settings.ShowDescriptions; return true;
		case ID_OPTIONS_SHOWHIDDENFILES: bChecked = App.Settings.m_bShowHidden; return true;
		case ID_OPTIONS_SHOWMETADATAMARKERS: bChecked = App.Settings.m_bShowMarkers; return true;
		case ID_OPTIONS_SHOWTHUMBNAILTOOLTIPS: bChecked = App.Settings.m_bShowToolTips; return true;
		case ID_OPTIONS_USESHORTDATES: bChecked = App.Settings.m_bShortDates; return true;
		case ID_OPTIONS_ZOOMTHUMBNAILSINMATRIXVIEW: bChecked = App.Settings.ZoomThumbnails; return true;
		}

		return GetImageCommandState(id, bEnabled, bChecked);
	}

	bool HasAFocusItem()
	{
		IW::FolderPtr pFolder = GetFolder();
		return pFolder->GetFocusItem() != -1;
	}

	void ToggleOption(bool &b)
	{
		b = !b;
		OnOptionsChanged();
	}

	void SetViewMode(FolderDisplayMode eMode)
	{
		_folder.SetViewMode(eMode);
		_folder.SizeClients();
		UpdateToolbarsShown();
	}

	bool IsThumbnailMode() const { return _folder._displayMode == eViewNormal; }

	// The Search menu items act on whatever the search panel currently holds.
	void SearchToolbar(Search::Type type)
	{
		_pCoupling->ShowSearchPane();

		// An empty spec matches everything, which means a full recursive decode
		// that finds nothing. Open the panel and let the user fill it in.
		if (HasSearchSpec())
			_pCoupling->StartSearch(type);
	}

	void RenameSelection()
	{
		if (!CanRename())
			return;

		IW::FolderPtr pFolder = GetFolder();
		int nFocusItem = pFolder->GetFocusItem();

		if (pFolder->GetSelectCount() > 1)
		{
			CRenameSelected renamer;
			renamer.Rename(pFolder);
		}
		else if (nFocusItem != -1)
		{
			CRenameDlg dlg(pFolder->GetItemName(nFocusItem));

			if (dlg.DoModal() == IDOK)
			{
				IW::FolderItemPtr pItem = pFolder->GetItem(nFocusItem);
				if (!pItem) return;

				pItem->SetItemName(dlg.GetFileName());
				pFolder->UpdateSelectedItems();
			}
		}
	}

	void OpenWith()
	{
		IW::FolderPtr pFolder = GetFolder();
		int nSelected = pFolder->GetFocusItem();

		if (nSelected != -1)
		{
			IW::FolderItemPtr pItem = pFolder->GetItem(nSelected);
			if (!pItem) return;

			CString strName = pItem->GetFilePath();

			SHELLEXECUTEINFO sei = { sizeof(sei) };
			sei.fMask = SEE_MASK_FLAG_DDEWAIT;
			sei.nShow = SW_SHOWNORMAL;
			sei.lpVerb = _T("OpenAs");
			sei.lpFile = strName;
			ShellExecuteEx(&sei);
		}
	}

	void OpenContainingFolder()
	{
		IW::FolderPtr pFolder = GetFolder();
		int nSelected = pFolder->GetFocusItem();

		if (nSelected != -1)
		{
			IW::FolderItemPtr pItem = pFolder->GetItem(nSelected);
			if (!pItem) return;

			_state.Folder.OpenFolder(pItem->GetShellFolder().GetShellItem());
		}
	}

	void GoToNewLocation()
	{
		IW::CShellItem item;

		if (item.Open(m_hWnd, CSIDL_MYPICTURES))
		{
			CString strPath;
			FavouriteState &favourite = _state.Favourite;

			if (!favourite.IsEmpty())
				item = favourite.GetTop();

			if (IW::CShellDesktop::GetDirectory(m_hWnd, item)
				&& item.GetPath(strPath))
			{
				favourite.Add(item);
				_state.Folder.OpenFolder(item);
			}
		}
	}

	// One path per line, so the result pastes straight into a shell or an editor.
	void CopyFilePaths()
	{
		IW::FolderPtr pFolder = GetFolder();
		CString str;

		for (const auto& pItem : pFolder->GetItemList())
		{
			if (pItem->IsSelected())
			{
				if (!str.IsEmpty()) str += g_szCRLF;
				str += pItem->GetFilePath();
			}
		}

		if (str.IsEmpty())
			return;

		if (!::OpenClipboard(m_hWnd))
			return;

		::EmptyClipboard();

		const SIZE_T cb = (static_cast<SIZE_T>(str.GetLength()) + 1) * sizeof(TCHAR);
		HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, cb);

		if (hMem != nullptr)
		{
			auto sz = static_cast<LPTSTR>(::GlobalLock(hMem));

			if (sz != nullptr)
			{
				::memcpy(sz, static_cast<LPCTSTR>(str), cb);
				::GlobalUnlock(hMem);

				if (::SetClipboardData(CF_UNICODETEXT, hMem) == nullptr)
					::GlobalFree(hMem);
			}
			else
			{
				::GlobalFree(hMem);
			}
		}

		::CloseClipboard();
	}

	// Explorer at this folder, with the focused file selected in it.
	void OpenInExplorer()
	{
		IW::FolderPtr pFolder = GetFolder();
		const int nFocusItem = pFolder->GetFocusItem();
		CString strFilePath;

		if (nFocusItem != -1)
		{
			IW::FolderItemPtr pItem = pFolder->GetItem(nFocusItem);
			if (pItem) strFilePath = pItem->GetFilePath();
		}

		if (!strFilePath.IsEmpty())
		{
			LPITEMIDLIST pidl = ::ILCreateFromPath(strFilePath);

			if (pidl != nullptr)
			{
				::SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
				::ILFree(pidl);
				return;
			}
		}

		const CString strFolderPath = pFolder->GetFolderPath();

		if (!strFolderPath.IsEmpty())
		{
			SHELLEXECUTEINFO sei = { sizeof(sei) };
			sei.fMask = SEE_MASK_FLAG_DDEWAIT;
			sei.nShow = SW_SHOWNORMAL;
			sei.lpVerb = _T("explore");
			sei.lpFile = strFolderPath;
			ShellExecuteEx(&sei);
		}
	}

	void SetImageDetailLevel(long nLevel)
	{
		const long nClamped = IW::Clamp(nLevel, 0L, static_cast<long>(AppSettings::ImageDetailMax));

		if (nClamped != App.Settings.ImageDetailLevel)
		{
			App.Settings.ImageDetailLevel = nClamped;
			_state.ResetFrames.Invoke();
		}
	}

	template<class THandler>
	void ApplyToSelection(THandler *pVisitor, UINT nStatusId)
	{
		CWaitCursor wait;
		CProgressDlg pd(IDD_PROGRESS_ADVANCED);
		pd.Create(IW::GetMainWindow(), nStatusId);

		IW::FolderPtr pFolder = _folder.GetFolder();

		const bool bRan = pFolder->IterateSelectedItems(pVisitor, &pd);

		if (pd.QueryCancel())
			return;

		// IterateSelectedItems answers false only for a cancel or a folder
		// failure -- it discards each item's result -- so the error the items
		// reported is the only sign that nothing was changed.
		const CString strError = pd.GetError();

		if (!bRan || !strError.IsEmpty())
		{
			IW::CMessageBoxIndirect mb;
			mb.Show(strError.IsEmpty() ? App.LoadString(IDS_FAILEDTO_UPDATE) : strError);
		}
	}

	void SelectTagFromDialog()
	{
		CTagDlg dlg;

		if (dlg.DoModal() != IDOK)
			return;

		CString strTag = dlg._tags;
		strTag.Trim();

		// Selecting by tag deselects everything else, so a tag that matches
		// nothing silently throws away a hand-picked selection.
		if (_state.Folder.SelectTag(strTag) == 0)
		{
			IW::CMessageBoxIndirect mb;
			mb.Show(IW::Format(IDS_NO_MATCHING_TAG_FMT, static_cast<LPCTSTR>(strTag)));
		}
	}

	void RotateSelection(IW::Rotation::Direction rotation)
	{
		ItemRotater rotater(_state.Loaders, rotation);
		ApplyToSelection(&rotater, IDS_ROTATING);
	}


	void TrackPopupMenu(HMENU hMenu, UINT uFlags, int x, int y)
	{
		_pCoupling->TrackPopupMenu(hMenu, uFlags, x, y);
	}

	void Redraw()
	{
		_image.Redraw();
	}	

	void BeginImageFade(int nSteps)
	{
		_image.BeginFade(nSteps);
	}


	inline CRect GetImageRectSelected() const
	{
		return _image.GetImageRectSelected();
	}	

	inline bool HasSelection() const throw()
	{
		IW::FolderPtr pFolder = GetFolder();
		return pFolder->HasSelection();
	}

	inline bool CanDelete() const throw()
	{
		return HasSelection() && (m_nAttribs & SFGAO_CANDELETE);
	}

	inline bool CanCopy() const throw()
	{
		return HasSelection() && (m_nAttribs & SFGAO_CANCOPY);
	}

	inline bool CanMove() const throw()
	{
		return HasSelection() && (m_nAttribs & SFGAO_CANMOVE);
	}

	inline bool CanRename() const throw()
	{
		return HasSelection() && (m_nAttribs & SFGAO_CANRENAME);
	}

	inline bool HasProperties() const throw()
	{
		return HasSelection() && (m_nAttribs & SFGAO_HASPROPSHEET);
	}

	inline bool InDesktopFolder() const throw()
	{
		return m_bInDesktopFolder;
	}

	inline IW::FolderPtr GetFolder() throw()
	{
		return _state.Folder.GetFolder();
	}

	inline const IW::FolderPtr GetFolder() const throw()
	{
		return _state.Folder.GetFolder();
	}

	void SetScale(LPCTSTR sz)
	{
		_image.SetScale(sz);
	}

	CString GetScaleText()
	{
		return _image.GetScaleText();
	}

	

	void LoadDefaultSettings()
	{
		_strDefaultScale = App.Settings.NormalScale;
		_nDefaultSplitterPos = App.Settings.NormalSplitterPos;
		_folder._displayMode = CFolderCtrl::ValidateViewMode(App.Settings.NormalFolderView);
	}

	void SaveDefaultSettings()
	{
		App.Settings.NormalSplitterPos = GetProportionalPos();
		App.Settings.NormalScale = _image.GetScaleText();
		App.Settings.NormalFolderView = static_cast<int>(_folder._displayMode);
	}

	void OnStartSearching()
	{
		_folder.OnStartSearching();
	}

	void OnThumbsLoaded()
	{
		_folder.OnThumbsLoaded();
		_image.OnThumbsLoaded();
	}

	void OnBeforeFileOperation()
	{
		_state.Image.StopLoading();
	}

	void OnAfterDelete()
	{
		_folder.OnAfterDelete();
	}

	void OnAfterCopy(bool bMove)
	{
		_state.Folder.RefreshList(true);
	}

	HWND Activate(HWND hWndParent)
	{
		if (m_hWnd == 0)
		{
			Create(hWndParent, rcDefault, NULL, IW_WS_CHILD, 0);
		}

		_image.OnActivate();
		_folder.OnActivate();

		ShowWindow(SW_SHOW);
		_folder.SetFocus();

		return m_hWnd;
	}

	void Deactivate()
	{
		ShowWindow(SW_HIDE);
	}

	void RefreshDescription()
	{
		_image.RefreshDescription();
	}	

	bool CanEditImages() const 
	{
		return true;
	}

	bool CanShowToolbar(DWORD id)
	{
		if (_pCoupling->IsFullScreen())
			return false;

		return id == IDC_VIEW_TOOLBAR ||
			id == IDC_VIEW_ADDRESS ||
			id == IDC_LOGO ||
			id == IDC_COMMAND_BAR;			
	}

	// Full screen is src30's shape: the image fills the window with the items
	// as a single-row filmstrip beneath it, images only.
	void OnFullScreenChanged(bool bFullScreen)
	{
		if (m_hWnd == 0)
			return;

		if (bFullScreen)
		{
			_nWindowedSplitterPos = GetProportionalPos();

			SetAspectAspect(false);
			_folder.SetFilmStrip(true);
			SizeStrip();
			_image.SetFocus();
		}
		else
		{
			SetAspectAspect(true);
			_folder.SetFilmStrip(false);
			SetProportionalPos(_nWindowedSplitterPos);
			_folder.SetFocus();
		}

		_folder.SizeClients();
		SetSplitterRect();
	}

	// One thumbnail row plus its horizontal scroll bar, however tall the frame is.
	void SizeStrip()
	{
		if (m_hWnd == 0 || !_folder.IsFilmStrip())
			return;

		CRect rcClient;
		GetClientRect(rcClient);

		const int cyStrip = _folder.GetLayout()->GetThumbSize().cy +
			::GetSystemMetrics(SM_CYHSCROLL) + 4;

		SetSplitterPos(IW::Max(0, rcClient.Height() - cyStrip));
	}

	// The one rule of the items mode: what has the focus is what is shown.
	// A file that is not a picture is still something the pane has to show --
	// leaving the previous photo up makes the selection look like it did nothing.
	void OnFocusChanged() 
	{
		IW::FolderPtr pFolder = _state.Folder.GetFolder();
		const long nFocus = pFolder->GetFocusItem();

		if (nFocus == -1)
		{
			_image.SetNoPreview(g_szEmptyString);
			return;
		}

		if (!pFolder->IsItemImage(nFocus))
		{
			CString strTitle, strDetail;
			DescribeItem(pFolder, nFocus, strTitle, strDetail);
			_image.SetNoPreview(strTitle, strDetail);

			// Unload resets the history, so it must not run over unsaved edits.
			if (_state.Image.IsImageShown() && !_state.Image.IsDirty())
				_state.Image.Unload();

			return;
		}

		_image.SetNoPreview(g_szEmptyString);

		if (pFolder->GetItemPath(nFocus).CompareNoCase(_state.Image.GetImageFileName()) != 0)
		{				
			_state.Image.SetItems(_pCoupling);
			_state.Image.LoadImage(nFocus, false);
		}
	}

	// Name, kind and size: enough to know what was picked and why there is no
	// picture of it.
	static void DescribeItem(IW::FolderPtr pFolder, int nItem, CString &strTitle, CString &strDetail)
	{
		IW::FolderItemPtr pItem = pFolder->GetItem(nItem);
		if (!pItem) return;

		strTitle = pItem->GetDisplayName();
		strDetail = pItem->GetType();

		if (!pItem->IsFolder())
			IW::AddToList(strDetail, pItem->GetFileSize().ToString());

		if (!strDetail.IsEmpty())
			strDetail += g_szCRLF;

		strDetail += pItem->IsFolder() ? _T("Open this folder to see what is in it")
		                               : _T("No preview available");
	}

	// The image pane shows exactly what is selected: the focused picture, or a
	// collage of the selection when more than one item is picked.
	void OnSelectionChanged()
	{
		IW::FolderPtr pFolder = _state.Folder.GetFolder();
		m_nAttribs = pFolder->GetSelectionAttributes();
		_image.UpdateSelection();
	}

	// One rung: two images laid over each other come apart before the frame gets
	// to do anything else with Escape.
	bool OnEscape() override
	{
		return _image.EndCompareSplit();
	}

	void OnFolderChanged()
	{
		m_nAttribs = 0;
		m_bInDesktopFolder = _state.Folder.GetFolderItem().IsDesktop();
		_image.SetNoPreview(g_szEmptyString);
	}

	BOOL PreTranslateMessage(MSG* pMsg)
	{
		return _image.PreTranslateMessage(pMsg);
	}

	void OnTimer()
	{
		_image.OnTimer();
		_folder.OnTimer();
	}

	void OnOptionsChanged()
	{
		_image.OnOptionsChanged();
		_folder.OnOptionsChanged();
	}

	void OnThumbnailSizeChanged()
	{
		_folder.OnThumbnailSizeChanged();
	}

	
	BEGIN_MSG_MAP(CMainFrame)

		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		MESSAGE_HANDLER(WM_SIZE, OnSize)

		CHAIN_MSG_MAP(CSplitter2Impl<NormalView>)

	END_MSG_MAP()

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		_folder.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0);
		_image.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0);

		_state.Folder.ChangedDelegates.Bind(this, &ThisClass::OnFolderChanged);
		_state.Folder.SelectionDelegates.Bind(this, &ThisClass::OnSelectionChanged);		
		_state.Folder.FocusDelegates.Bind(this, &ThisClass::OnFocusChanged);

		bHandled = FALSE;

		SetSplitterPanes(_image, _folder);
		SetProportionalPos(_nDefaultSplitterPos);
		_image.SetScale(_strDefaultScale);	

		return 0;
	}

	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		ATLTRACE(_T("Destroy CMainFrame\n"));
		//m_wndAddress.SetImageList(NULL);

		bHandled = false;

		return 0;
	}
	
	LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		return 1;
	}

	LRESULT OnSize(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		if(wParam != SIZE_MINIMIZED)
		{
			SizeStrip();
			SetSplitterRect();
		}

		bHandled = FALSE;
		return 1;
	}
	
	void OnHistory(int n)
	{
		IW::CShellItem item;

		if (_state.History.SetHistoryPos(n, item))
		{
			_state.Folder.OpenFolder(item);
		}
	}

	
	void OnGoTo(long nItem)
	{
		IW::CShellItem item;

		if (_state.Favourite.UseItem(nItem, item))
		{
			_state.Folder.OpenFolder(item);
		}
	}	

	CString GetSelectedFileList() const
	{
		return GetFolder()->GetSelectedFileList();
	}

	void NextImage()
	{
		StepImage(1);
	}	

	void PreviousImage()
	{
		StepImage(-1);
	}

	void StepImage(int nStep)
	{
		IW::FolderPtr pFolder = _state.Folder.GetFolder();
		long nCount = pFolder->GetItemCount();
		int nSeek = IW::Clamp(pFolder->GetFocusItem(), 0, nCount - 1);

		for(int i = 0; i < nCount; i++)
		{	
			nSeek += nStep;

			if (nSeek < 0) nSeek = nCount - 1;
			if (nSeek >= nCount) nSeek = 0;

			if (pFolder->IsItemImage(nSeek))
			{
				_state.Folder.Select(nSeek, 0, true);
				break;
			}
		}
	}

	CString GetImageFileName() const 
	{ 
		return _state.Image.GetImageFileName(); 
	};

	void UpdateStatusText()
	{
		_state.Image.UpdateStatusText();
	}

	HWND GetImageWindow()
	{
		return _image;
	}

	void OnNewImage(bool bScrollToCenter)
	{	
		_image.Refresh(bScrollToCenter);
		_image.OnNewImage(bScrollToCenter);
		UpdateStatusText();
	}	

	bool SaveNewImage(const IW::Image &dib)
	{
		return _folder.SaveNewImage(dib);
	}

	void ToggleScale()
	{
		_image.ToggleScale();
	}
};

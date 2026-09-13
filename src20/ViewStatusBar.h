// ImageWalker by Zac Walker
//
// Purpose: The status bar. Standard control, with one owner-drawn part for the
//          thumbnail progress it paints behind the folder status.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

// 2.2 keeps the system's status bar: only part 1 is owner-drawn, and the frame
// routes WM_DRAWITEM here for it. 2.3 paints the whole control instead, which
// is what its dark chrome needs. See docs/architecture-2x.md.
class CStatusBarWithProgress :
	public CWindowImpl<CStatusBarWithProgress, CStatusBarCtrl>
{
public:
	HWND _hWndOwner;
	State &_state;
	CString _strFolderStatus;
	int _nSortOrder;
	HWND _hWndThumbnailSlider;

	CStatusBarWithProgress(State &state) : 
		_hWndOwner(0),		 
		_nSortOrder(0),
		_hWndThumbnailSlider(nullptr),
		_state(state)
	{
	}

	void SetOwner(HWND hWndOwner)
	{
		_hWndOwner = hWndOwner;
	}

	BEGIN_MSG_MAP(CStatusBarWithProgress)
		MESSAGE_HANDLER(WM_COMMAND, OnForward)
		MESSAGE_HANDLER(WM_HSCROLL, OnForward)
		MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
	END_MSG_MAP()

	// Part 1 is SBT_OWNERDRAW: the thumbnail progress runs behind the folder
	// status, so the bar and the text have to be drawn together.
	void DrawPart(LPDRAWITEMSTRUCT lpdis)
	{
		CDCHandle dc(lpdis->hDC);
		CRect r(lpdis->rcItem);

		if (_state.Folder.IsThumbnailing && !_state.Folder.IsSearching)
		{
			IW::FolderPtr pFolder = _state.Folder.GetFolder();
			const int nSplit = MulDiv(r.Width(), pFolder->GetPercentComplete(), 100);

			CRect rBar(r);
			rBar.right = rBar.left + nSplit;
			dc.FillSolidRect(rBar, IW::Style::Color::Highlight);
		}

		HFONT hOldFont = dc.SelectFont(static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(::GetSysColor(COLOR_BTNTEXT));
		dc.DrawText(_strFolderStatus, -1, r, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
		dc.SelectFont(hOldFont);
	}

	bool CreateThumbnailSlider()
	{
		_hWndThumbnailSlider = ::CreateWindowEx(
			0, TRACKBAR_CLASS, nullptr,
			WS_CHILD | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS | TBS_TOOLTIPS,
			0, 0, 0, 0, m_hWnd, nullptr, _Module.GetModuleInstance(), nullptr);

		if (_hWndThumbnailSlider == nullptr)
			return false;

		::SendMessage(_hWndThumbnailSlider, TBM_SETPAGESIZE, 0, 16);
		::SendMessage(_hWndThumbnailSlider, TBM_SETLINESIZE, 0, 8);
		return true;
	}

	void SetThumbnailRange(int nMaximum, int nValue)
	{
		if (_hWndThumbnailSlider == nullptr)
			return;

		const int nMinimum = IW::Min(64, nMaximum);
		::SendMessage(_hWndThumbnailSlider, TBM_SETRANGE, TRUE, MAKELONG(nMinimum, nMaximum));
		::SendMessage(_hWndThumbnailSlider, TBM_SETPOS, TRUE, nValue);
	}

	// The thumbnail slider is a stock trackbar here. 2.3 custom-draws it to
	// match its dark chrome; on a system-coloured bar the default is right.
	LRESULT OnNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		return OnForward(uMsg, wParam, lParam, bHandled);
	}

	LRESULT OnForward(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		// Without this the message also runs on through the status bar's own
		// handlers, so anything the owner does not claim is handled twice.
		bHandled = TRUE;
		return ::SendMessage(_hWndOwner, uMsg, wParam, lParam);
	}


	void OnSortOrderChanged(int order)
	{
		_nSortOrder = order;
	}	

	void UpdateMessage()
	{
		IW::FolderPtr pFolder = _state.Folder.GetFolder();

		if (_state.Folder.IsSearching)
		{			
			_strFolderStatus.Format(IDS_STATUS_FOUND, pFolder->GetImageCount(), pFolder->GetSize());
		}
		else if (pFolder->GetSelectedItemCount() > 1)
		{
			int nCount = 0, nImages = 0;
			IW::FileSize size;
			pFolder->GetSelectStatus(nCount, nImages, size);

			_strFolderStatus.Format(IDS_IMAGES_FILES_FOLDERS, nImages, nCount, static_cast<LPCTSTR>(size.ToString()));
		}
		else
		{
			long nFilesOrFolders = pFolder->GetSize();
			long nImageCount = pFolder->GetImageCount();
			long nSecondsRemaining = pFolder->GetTimeRemaining();
			long nSecondsTaken = pFolder->GetTimeTaken();		

			const CString strOrder = App.GetMetaDataTitle(_nSortOrder);
			int nSecondsMin = 50;

#ifdef _DEBUG

			nSecondsMin = 0;

#endif

			if (_state.Folder.IsThumbnailing && nSecondsRemaining > 0)
			{
				_strFolderStatus.Format(IDS_STATUS_REMAINING, nImageCount, nFilesOrFolders, nSecondsRemaining / 10);
			}
			else if (nSecondsTaken >= nSecondsMin)
			{
				_strFolderStatus.Format(IDS_STATUS_SECONDS, nImageCount, nFilesOrFolders, static_cast<LPCTSTR>(strOrder), nSecondsTaken / 10);
			}
			else
			{
				_strFolderStatus.Format(IDS_STATUS_NORMAL, nImageCount, nFilesOrFolders, static_cast<LPCTSTR>(strOrder));
			}
		}
	}
};

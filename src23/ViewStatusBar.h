// ImageWalker by Zac Walker
//
// Purpose: The owner-drawn status bar, including the thumbnail progress it
//          paints behind the text.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

class CSkinedStatusBarCtrl :
	public CWindowImpl<CSkinedStatusBarCtrl, CStatusBarCtrl>
{
public:
	HWND _hWndOwner;
	State &_state;
	CString _strFolderStatus;
	int _nSortOrder;
	HWND _hWndThumbnailSlider;

	CSkinedStatusBarCtrl(State &state) : 
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

	BEGIN_MSG_MAP(CSkinedStatusBarCtrl)
		MESSAGE_HANDLER(WM_COMMAND, OnForward)
		MESSAGE_HANDLER(WM_HSCROLL, OnForward)
		MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
	END_MSG_MAP()

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

	LRESULT OnNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		LPNMHDR pHeader = reinterpret_cast<LPNMHDR>(lParam);

		if (pHeader->hwndFrom != _hWndThumbnailSlider || pHeader->code != NM_CUSTOMDRAW)
			return OnForward(uMsg, wParam, lParam, bHandled);

		bHandled = TRUE;
		LPNMCUSTOMDRAW pDraw = reinterpret_cast<LPNMCUSTOMDRAW>(lParam);

		if (pDraw->dwDrawStage != CDDS_PREPAINT)
			return CDRF_DODEFAULT;

		// The whole control is drawn here. comctl32 sends the trackbar's
		// per-part item stages on its first paint and never again, so a skin
		// split across CDDS_ITEMPREPAINT draws once and leaves an empty
		// rectangle for the rest of the session.
		CDCHandle dc(pDraw->hdc);

		CRect rect;
		::GetClientRect(_hWndThumbnailSlider, rect);
		IW::Skin::DrawGradient(dc, rect, IW::Style::Color::Window,
		                       IW::Emphasize(IW::Style::Color::Window, 64));

		CRect rectChannel, rectThumb;
		::SendMessage(_hWndThumbnailSlider, TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&rectChannel));
		::SendMessage(_hWndThumbnailSlider, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&rectThumb));

		rectChannel.top = rectChannel.CenterPoint().y - 2;
		rectChannel.bottom = rectChannel.top + 4;
		dc.FillSolidRect(rectChannel, IW::Emphasize(IW::Style::Color::Window, 96));
		dc.FrameRect(rectChannel, IW::Style::Brush::EmphasizedHighlight);

		const bool bActive = ::GetCapture() == _hWndThumbnailSlider;
		dc.FillSolidRect(rectThumb, bActive ? IW::Style::Color::Highlight
		                                    : IW::Emphasize(IW::Style::Color::Window, 128));
		dc.FrameRect(rectThumb, IW::Style::Brush::EmphasizedHighlight);

		return CDRF_SKIPDEFAULT;
	}

	LRESULT OnForward(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		// Without this the message also runs on through the status bar's own
		// handlers, so anything the owner does not claim is handled twice.
		bHandled = TRUE;
		return ::SendMessage(_hWndOwner, uMsg, wParam, lParam);
	}


	LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		CDCHandle dc((HDC)wParam);
		CRect r; GetClientRect(r);
		//dc.FillSolidRect(r, IW::Style::Color::Window);
		DWORD c1 = IW::Style::Color::Window;			
		DWORD c2 = IW::Emphasize(IW::Style::Color::Window, 64);	
		IW::Skin::DrawGradient(dc, r, c1, c2);
		return 1;
	}

	LRESULT OnPaint(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		CPaintDC dc(m_hWnd);
		CRect r; GetClientRect(r);
		CString str;

		if (IsSimple())
		{
			r.left += 4;
			GetWindowText(str);
			HFONT hOldFont = dc.SelectFont((HFONT)GetStockObject (DEFAULT_GUI_FONT));
			dc.SetBkMode(TRANSPARENT);
			dc.SetTextColor(IW::Style::Color::WindowText);
			dc.DrawText(str, -1, r, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
			dc.SelectFont(hOldFont);	
		}
		else
		{
			int nCount = (int) GetParts(0, NULL);
			for( int i = 0; i < nCount; i++ ) 
			{
				GetRect(i, r);
				GetText(i, str);

				if (i == 0)
				{
					r.left += 4;
					HFONT hOldFont = dc.SelectFont((HFONT)GetStockObject (DEFAULT_GUI_FONT));
					dc.SetBkMode(TRANSPARENT);
					dc.SetTextColor(IW::Style::Color::WindowText);
					dc.DrawText(str, -1, r, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
					dc.SelectFont(hOldFont);			
				}
				else if (i == 1)
				{
					if (_state.Folder.IsThumbnailing && !_state.Folder.IsSearching)
					{
						IW::FolderPtr pFolder = _state.Folder.GetFolder();
						int nPercentComplete = pFolder->GetPercentComplete();
						int nWidth = r.Width();
						int nSplit = MulDiv(nWidth, nPercentComplete, 100);

						CRect rBar(r);
						rBar.right = rBar.left + nSplit;
						dc.FillSolidRect(rBar, IW::Style::Color::Highlight);
						dc.FrameRect(rBar, IW::Style::Brush::EmphasizedHighlight);
					}

					HFONT hOldFont = dc.SelectFont((HFONT)GetStockObject (DEFAULT_GUI_FONT));
					dc.SetBkMode(TRANSPARENT);
					dc.SetTextColor(IW::Style::Color::WindowText);
					dc.DrawText(_strFolderStatus, -1, r, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
					dc.SelectFont(hOldFont);
				}
			}	
		}

		return 0;
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

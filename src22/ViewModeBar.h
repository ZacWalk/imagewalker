// ImageWalker by Zac Walker
//
// Purpose: The mode strip down the left edge, and the favourites bar under it.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

// 2.2's way into a mode: a vertical toolbar at the left edge with a 32px image
// over a caption, and a second bar beneath it for the user's folders. 2.3 put
// the same five commands at the head of every toolbar instead; 2.0 uses a tab
// strip. Only the control differs - it posts the command and CMainFrame::SetMode
// decides, so a refusal cannot leave the bar showing a mode the frame is not in.
class CModeBar : public CWindowImpl<CModeBar>
{
public:
	DECLARE_WND_CLASS_EX(_T("IWModeBar"), CS_HREDRAW | CS_VREDRAW, COLOR_BTNFACE)

	enum
	{
		kButtonSize = 64,

		// The bar is as wide as one button plus the frame's own edge.
		kWidth = kButtonSize + 4,

		// Child window ids. Nothing routes on the modes bar; the favourites bar
		// shares ID_GOTO_FIRST with the commands its buttons post.
		kModesBarId = ID_GOTO_LAST + 1
	};

	State &_state;
	HWND _hWndModes = nullptr;
	HWND _hWndPlaces = nullptr;
	CImageList _images;

	CModeBar(State &state) : _state(state)
	{
	}

	BEGIN_MSG_MAP(CModeBar)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)

		// A toolbar posts to its own parent, and that is this container, not the
		// frame - without these the buttons look alive and do nothing at all.
		MESSAGE_HANDLER(WM_COMMAND, OnForwardToFrame)
		MESSAGE_HANDLER(WM_NOTIFY, OnForwardToFrame)
	END_MSG_MAP()

	// Reflect, never command: called after CMainFrame::_mode has actually moved.
	void SetMode(int idActive)
	{
		if (_hWndModes == nullptr)
			return;

		for (int i = 0; s_modes[i]._id != 0; i++)
		{
			::SendMessage(_hWndModes, TB_CHECKBUTTON, s_modes[i]._id,
			              MAKELONG(s_modes[i]._id == idActive, 0));
		}
	}

	// A mode you cannot enter must not look available. The frame asks its view
	// and itself for each command's state on idle and hands the answer here.
	void EnableModeFromState(int id, bool bEnable)
	{
		if (_hWndModes == nullptr)
			return;

		for (int i = 0; s_modes[i]._id != 0; i++)
		{
			if (s_modes[i]._id == id)
			{
				::SendMessage(_hWndModes, TB_ENABLEBUTTON, id, MAKELONG(bEnable, 0));
				return;
			}
		}
	}

	void EnableMode(int id, bool bEnable)
	{
		if (_hWndModes != nullptr)
			::SendMessage(_hWndModes, TB_ENABLEBUTTON, id, MAKELONG(bEnable, 0));
	}

	// The favourites are the user's, so the bar is rebuilt whenever they change.
	void SetLocations()
	{
		if (_hWndPlaces == nullptr)
			return;

		while (::SendMessage(_hWndPlaces, TB_BUTTONCOUNT, 0, 0) > 0)
			::SendMessage(_hWndPlaces, TB_DELETEBUTTON, 0, 0);

		const IW::ShellItemList &items = _state.Favourite._items;

		for (int i = 0; i < items.GetSize(); i++)
		{
			SHFILEINFO sfi;
			IW::MemZero(&sfi, sizeof(sfi));

			if (SHGetFileInfo(reinterpret_cast<LPCTSTR>(items[i].GetItem()), 0, &sfi, sizeof(sfi),
			                  SHGFI_PIDL | SHGFI_DISPLAYNAME | SHGFI_SYSICONINDEX | SHGFI_LARGEICON) == 0)
				continue;

			CString str = sfi.szDisplayName;
			str += _T('\n');

			const int iString = static_cast<int>(::SendMessage(_hWndPlaces, TB_ADDSTRING, 0,
			                                                   reinterpret_cast<LPARAM>(Terminate(str))));

			TBBUTTON tbb;
			IW::MemZero(&tbb, sizeof(tbb));
			tbb.iBitmap = sfi.iIcon;
			tbb.idCommand = ID_GOTO_FIRST + i;
			tbb.fsState = TBSTATE_ENABLED;
			tbb.fsStyle = BTNS_BUTTON;
			tbb.iString = iString;

			::SendMessage(_hWndPlaces, TB_ADDBUTTONS, 1, reinterpret_cast<LPARAM>(&tbb));
		}

		Layout();
	}

private:

	// ::SendMessage, never the frame's ProcessWindowMessage: an _EX handler over
	// there writes through m_pCurrentMsg, which only points at a live message
	// while ATL's own WindowProc is on the frame's stack.
	LRESULT OnForwardToFrame(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
	{
		const HWND hWndFrame = ::GetParent(m_hWnd);

		if (hWndFrame == nullptr)
		{
			bHandled = FALSE;
			return 0;
		}

		bHandled = TRUE;
		return ::SendMessage(hWndFrame, uMsg, wParam, lParam);
	}

	struct ModeButton
	{
		int _id;
		int _nImage;
	};

	// Image indices into IDB_MODE_BAR_BUTTONS (ten 32px frames).
	static const ModeButton s_modes[];

	// TB_ADDSTRING wants a double-terminated buffer; CString gives us one
	// terminator and the caller appends the other.
	static LPCTSTR Terminate(CString &str)
	{
		const int nLength = str.GetLength();
		LPTSTR sz = str.GetBufferSetLength(nLength + 1);
		sz[nLength] = 0;
		return sz;
	}

	LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
	{
		bHandled = FALSE;

		_images.Attach(::ImageList_LoadImage(App.GetResourceInstance(),
		                                     MAKEINTRESOURCE(IDB_MODE_BAR_BUTTONS), 32, 1,
		                                     RGB(255, 0, 255), IMAGE_BITMAP, LR_CREATEDIBSECTION));

		_hWndModes = CreateBar(kModesBarId);
		_hWndPlaces = CreateBar(ID_GOTO_FIRST);

		if (_hWndModes == nullptr || _hWndPlaces == nullptr)
			return -1;

		::SendMessage(_hWndModes, TB_SETIMAGELIST, 0,
		              reinterpret_cast<LPARAM>(static_cast<HIMAGELIST>(_images)));
		::SendMessage(_hWndPlaces, TB_SETIMAGELIST, 0,
		              reinterpret_cast<LPARAM>(App.GetShellImageList(false)));

		for (int i = 0; s_modes[i]._id != 0; i++)
		{
			CString str;
			str.LoadString(s_modes[i]._id);

			const int nFound = str.Find(_T('\n'));
			if (nFound != -1) str.Truncate(nFound);
			str += _T('\n');

			const int iString = static_cast<int>(::SendMessage(_hWndModes, TB_ADDSTRING, 0,
			                                                    reinterpret_cast<LPARAM>(Terminate(str))));

			TBBUTTON tbb;
			IW::MemZero(&tbb, sizeof(tbb));
			tbb.iBitmap = s_modes[i]._nImage;
			tbb.idCommand = s_modes[i]._id;
			tbb.fsState = TBSTATE_ENABLED;
			tbb.fsStyle = BTNS_CHECK;
			tbb.iString = iString;

			::SendMessage(_hWndModes, TB_ADDBUTTONS, 1, reinterpret_cast<LPARAM>(&tbb));
		}

		SetLocations();
		return 0;
	}

	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL &bHandled)
	{
		bHandled = FALSE;

		// The shell's image list is not ours to destroy with the toolbar.
		if (_hWndPlaces != nullptr)
			::SendMessage(_hWndPlaces, TB_SETIMAGELIST, 0, 0);

		return 0;
	}

	LRESULT OnSetCursor(UINT, WPARAM, LPARAM, BOOL &)
	{
		::SetCursor(::LoadCursor(nullptr, IDC_ARROW));
		return TRUE;
	}

	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL &bHandled)
	{
		bHandled = FALSE;
		Layout();
		return 0;
	}

	HWND CreateBar(int nId)
	{
		const HWND hWnd = ::CreateWindowEx(0, TOOLBARCLASSNAME, nullptr,
		                                   WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
		                                   CCS_LEFT | CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN |
		                                   TBSTYLE_TRANSPARENT | TBSTYLE_TOOLTIPS | TBSTYLE_WRAPABLE |
		                                   TBSTYLE_FLAT,
		                                   0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(nId)),
		                                   App.GetResourceInstance(), nullptr);

		if (hWnd == nullptr)
			return nullptr;

		::SendMessage(hWnd, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
		::SendMessage(hWnd, TB_SETBUTTONSIZE, 0, MAKELONG(kButtonSize, kButtonSize));
		::SendMessage(hWnd, TB_SETBUTTONWIDTH, 0, MAKELONG(kButtonSize, kButtonSize));

		return hWnd;
	}

	// Modes on top at the height their buttons need, favourites underneath
	// taking the rest. The favourites list is as long as the user's, so it is
	// clipped rather than scrolled when the window is too short.
	void Layout()
	{
		if (m_hWnd == nullptr)
			return;

		CRect rectClient;
		GetClientRect(rectClient);

		int cyModes = 0;

		if (_hWndModes != nullptr)
		{
			const int nButtons = static_cast<int>(::SendMessage(_hWndModes, TB_BUTTONCOUNT, 0, 0));
			const DWORD dwSize = static_cast<DWORD>(::SendMessage(_hWndModes, TB_GETBUTTONSIZE, 0, 0));

			cyModes = IW::Min(HIWORD(dwSize) * nButtons, rectClient.Height());

			::SetWindowPos(_hWndModes, nullptr, 0, 0, rectClient.Width(), cyModes,
			               SWP_NOZORDER | SWP_NOACTIVATE);
		}

		if (_hWndPlaces != nullptr)
		{
			::SetWindowPos(_hWndPlaces, nullptr, 0, cyModes, rectClient.Width(),
			               IW::Max(0, rectClient.Height() - cyModes),
			               SWP_NOZORDER | SWP_NOACTIVATE);
		}
	}
};

__declspec(selectany) const CModeBar::ModeButton CModeBar::s_modes[] =
{
	{ID_VIEW_NORMAL, 8},
	{ID_VIEW_FOLDERS, 0},
	{ID_VIEW_SEARCHADVANCED, 4},
	{ID_VIEW_DESCRIPTION, 3},
	{ID_VIEW_EDIT, 5},
	{ID_VIEW_PRINT, 7},
	{ID_VIEW_IMAGEFULLSCREEN, 1},
	{0, 0}
};

// ImageWalker by Zac Walker
//
// Purpose: Asks which tag to select by. It does not write anything: the
//          caption and the header comment used to say "Tag Selected Images",
//          which read as though typing here put the tags into the files.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once 

class CTagDlg : 
	public CDialogImpl<CTagDlg>
{
public:

	CString _tags;

	CTagDlg()
	{
	}

	enum { IDD = IDD_TAG};

	BEGIN_MSG_MAP(CTagDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
	END_MSG_MAP()


	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		CenterWindow();

		_tags = App.Settings.Tags;
		SetDlgItemText(IDC_TAGS, _tags);
		return 1;
	}


	LRESULT OnCloseCmd(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		// Wired to IDCANCEL too, and Escape reaches it -- remembering text the
		// user explicitly backed out of is not what Cancel means.
		if (wID == IDOK)
		{
			GetDlgItemText(IDC_TAGS, _tags);
			App.Settings.Tags = _tags;
		}

		EndDialog(wID);
		return 0;
	}
};

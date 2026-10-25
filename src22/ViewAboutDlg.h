// ImageWalker by Zac Walker
//
// Purpose: The About box.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

class CAboutDlg : public CDialogImpl<CAboutDlg>
{
public:
	enum { IDD = IDD_ABOUT };
	CHyperLink	_link;
	CHyperLink	_source;
	CHyperLink	_song;

	BEGIN_MSG_MAP(CAboutDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		CenterWindow(GetParent());
		
		CString str;
		str.Format(_T("%s - released 2003\nCopyright © Zac Walker"), _T(APP_VERSION_NAME));
		SetDlgItemText(IDC_TITLE, str);

		// Describes the 2003 release, not this binary -- see docs/versions.md.
		SetDlgItemText(IDC_ABOUT_DETAILS,
		               _T("The widest feature set of any version: Normal, Compare and Print ")
		               _T("modes, the Filters and Actions views and six batch tools.\r\n\r\n")
		               _T("Free software. No registration, no reminders, no limits. ")
		               _T("64-bit build of ") _T(__DATE__) _T("."));

		_link.SetHyperLink(_T("www.ImageWalker.com"));
		_link.SetLabel(_T("www.ImageWalker.com"));
		_link.SubclassWindow(GetDlgItem(IDC_WEB_PAGE));	

		_source.SetHyperLink(_T("https://github.com/ZacWalk/imagewalker"));
		_source.SetLabel(_T("github.com/ZacWalk/imagewalker"));
		_source.SubclassWindow(GetDlgItem(IDC_SOURCE_PAGE));

		_song.SetHyperLink(_T("https://www.youtube.com/watch?v=WpYeekQkAdc"));
		_song.SetLabel(_T("\"Where Is the Love?\" by The Black Eyed Peas"));
		_song.SubclassWindow(GetDlgItem(IDC_THEME_SONG));

		return (LRESULT)TRUE;
	}

	LRESULT OnCloseCmd(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		EndDialog(wID);
		return 0;
	}
};

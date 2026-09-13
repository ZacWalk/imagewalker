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
		str.Format(_T("%s - released 2001\nCopyright © Zac Walker"), _T(APP_VERSION_NAME));
		SetDlgItemText(IDC_TITLE, str);

		// Describes the 2001 release, not this binary -- see docs/versions.md.
		SetDlgItemText(IDC_ABOUT_DETAILS,
		               _T("The first release under the ImageWalker name: thumbnail browsing, ")
		               _T("descriptions, print preview and a slide show.\r\n\r\n")
		               _T("Free software. No registration, no reminders, no limits. ")
		               _T("64-bit build of ") _T(__DATE__) _T("."));

		_link.SetHyperLink(_T("www.ImageWalker.com"));
		_link.SetLabel(_T("www.ImageWalker.com"));
		_link.SubclassWindow(GetDlgItem(IDC_WEB_PAGE));	

		_source.SetHyperLink(_T("https://github.com/ZacWalk/imagewalker"));
		_source.SetLabel(_T("github.com/ZacWalk/imagewalker"));
		_source.SubclassWindow(GetDlgItem(IDC_SOURCE_PAGE));

		_song.SetHyperLink(_T("https://www.youtube.com/watch?v=75fyFAh29XY"));
		_song.SetLabel(_T("\"It Wasn't Me\" by Shaggy"));
		_song.SubclassWindow(GetDlgItem(IDC_THEME_SONG));

		return (LRESULT)TRUE;
	}

	LRESULT OnCloseCmd(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		EndDialog(wID);
		return 0;
	}
};

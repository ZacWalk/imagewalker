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
		str.Format(_T("%s - released 2006\nCopyright © Zac Walker"), _T(APP_VERSION_NAME));
		SetDlgItemText(IDC_TITLE, str);

		SetDlgItemText(IDC_ABOUT_DETAILS,
		               _T("Tag images, view their metadata, turn photos upright using EXIF ")
		               _T("and resize images in batches.\r\n\r\n")
		               _T("Free software. No registration, no reminders, no limits. ")
#if defined(_WIN64)
		               _T("64-bit build of ")
#else
		               _T("32-bit build of ")
#endif
		               _T(__DATE__) _T("."));

		_link.SetHyperLink(_T("www.ImageWalker.com"));
		_link.SetLabel(_T("www.ImageWalker.com"));
		_link.SubclassWindow(GetDlgItem(IDC_WEB_PAGE));	

		_source.SetHyperLink(_T("https://github.com/ZacWalk/imagewalker"));
		_source.SetLabel(_T("github.com/ZacWalk/imagewalker"));
		_source.SubclassWindow(GetDlgItem(IDC_SOURCE_PAGE));

		_song.SetHyperLink(_T("https://www.youtube.com/watch?v=-N4jf6rtyuw"));
		_song.SetLabel(_T("\"Crazy\" by Gnarls Barkley"));
		_song.SubclassWindow(GetDlgItem(IDC_THEME_SONG));

		return (LRESULT)TRUE;
	}

	LRESULT OnCloseCmd(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		EndDialog(wID);
		return 0;
	}
};

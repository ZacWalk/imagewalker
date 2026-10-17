// ImageWalker by Zac Walker
//
// Purpose: The open and save dialogs, with the format filter and the format
//          settings button.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

//
// ViewFileDialog.h : Declaration of the CImageFileDialog

#pragma once

#include "FileFormatAny.h"

/////////////////////////////////////////////////////////////////////////////
// CImageFileDialog
class CImageFileDialog : 
	public CFileDialogImpl<CImageFileDialog>
{
protected:
	CLoadAny _loader;
	CString _strType;

	// The app default is only moved once the save is confirmed.
	IW::CodecSettings _codec;
	CWindow _wndSettings;

public:
	CImageFileDialog(ImageLoaders &loaders, BOOL bOpenFileDialog, // TRUE for FileOpen, FALSE for FileSaveAs
		LPCTSTR lpszDefExt = NULL,
		LPCTSTR lpszFileName = NULL,
		DWORD dwFlags = OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
		LPCTSTR lpszFilter = NULL,
		HWND hWndParent = NULL)
		: CFileDialogImpl<CImageFileDialog>(bOpenFileDialog, lpszDefExt, lpszFileName, dwFlags, lpszFilter, hWndParent),
		_loader(loaders),
		_codec(App.Settings.Codec)
	{
		m_ofn.lpstrFilter = _loader.GetSaveFilter();
		m_ofn.Flags |= OFN_ENABLETEMPLATE | OFN_EXPLORER | OFN_SHOWHELP;
		m_ofn.lpTemplateName = MAKEINTRESOURCE(IDD);

		const int nJpg = _loader.MapSaveFilter(g_szJPG);
		m_ofn.nFilterIndex = (nJpg == -1) ? 1 : nJpg;
	}

	~CImageFileDialog()
	{
	}


	void SetDefaults(const IW::Image &imagePreview)
	{
		m_ofn.nFilterIndex = _loader.MapSaveFilter(imagePreview.GetLoaderName());

		if (m_ofn.nFilterIndex == -1)
		{
			m_ofn.nFilterIndex = _loader.MapSaveFilter(g_szJPG);
		}

		_strType = GetLoaderType();
	}

	CString GetLoaderType()
	{
		return _loader.MapSaveFilter(m_ofn.nFilterIndex);
	}

	enum { IDD = IDD_IMAGEFILE_SAVE };


	BEGIN_MSG_MAP(CImageFileDialog)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		CHAIN_MSG_MAP(CFileDialogImpl<CImageFileDialog>)
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		CenterWindow();

		IW::CFilePath path(m_ofn.lpstrFile);
		path.SetExtension(_loader.MapSaveFilter(m_ofn.nFilterIndex));
		path.CopyTo(m_ofn.lpstrFile, MAX_PATH);

		OnTypeChange(m_ofn.nFilterIndex);		

		return 1;  // Let the system set the focus
	}

	void OnTypeChange(LPOFNOTIFY lpon)
	{
		int nFilterIndex = lpon->lpOFN->nFilterIndex;
		OnTypeChange(nFilterIndex);
	}

	void OnTypeChange(int nFilterIndex)
	{
		TCHAR sz[MAX_PATH];
		GetFilePath(sz, MAX_PATH);

		OnTypeChange(nFilterIndex, sz);
	}

	void OnTypeChange(int nFilterIndex, LPCTSTR szSourcePath)
	{
		// Alter extension
		TCHAR szFileName[_MAX_FNAME];
		TCHAR szExt[_MAX_EXT] = _T("");

		CString strExtNew = _loader.MapSaveFilter(nFilterIndex);
		_tsplitpath_s( szSourcePath, NULL, 0, NULL, 0, szFileName, _MAX_FNAME, szExt, _MAX_EXT);

		if (_tcsclen(szExt))
		{
			TCHAR sz[MAX_PATH];
			_tmakepath_s( sz, MAX_PATH, 0, 0, szFileName, strExtNew);
			SetControlText(edt1, sz);
		}

		// Also set the default extension 
		SetDefExt(strExtNew);
		_strType = strExtNew;

		// Setup dialog
		SetDlgItemText(IDC_DESCRIPTION, _loader.GetDescription(strExtNew));
		UpdateSettingsPanel();
	}

	// The panel belongs to the format, so it is rebuilt whenever the format
	// changes rather than enabled and disabled in place.
	void UpdateSettingsPanel()
	{
		if (_wndSettings.IsWindow())
			_wndSettings.DestroyWindow();

		CWindow wndHost = GetDlgItem(IDC_SETTINGS_HOST);

		if (wndHost.IsWindow())
		{
			CRect rcHost;
			wndHost.GetWindowRect(rcHost);
			ScreenToClient(rcHost);
			wndHost.ShowWindow(SW_HIDE);

			_wndSettings = _loader.CreateSettingsWindow(_strType, m_hWnd, _codec);

			if (_wndSettings.IsWindow())
			{
				// Without it the dialog manager will not tab into the panel.
				_wndSettings.ModifyStyleEx(0, WS_EX_CONTROLPARENT);
				_wndSettings.SetWindowPos(HWND_TOP, rcHost.left, rcHost.top, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
			}
		}

		GetDlgItem(IDC_NO_OPTIONS).ShowWindow(_wndSettings.IsWindow() ? SW_HIDE : SW_SHOW);
	}

	void SetDefExt(LPCTSTR lpstrExt)
	{
		USES_CONVERSION;

		ATLASSERT(::IsWindow(m_hWnd));
		ATLASSERT((m_ofn.Flags & OFN_EXPLORER) != 0);

		if (IW::IsWindowsAscii())
		{
			GetFileDialogWindow().SendMessage(CDM_SETDEFEXT, 0, (LPARAM)(LPCSTR)CT2CA(lpstrExt));
		}
		else
		{
			GetFileDialogWindow().SendMessage(CDM_SETDEFEXT, 0, (LPARAM)(LPCWSTR)CT2CW(lpstrExt));
		}
	}

	// The panel edits a copy, so a cancelled save leaves the app default alone.
	BOOL OnFileOK(LPOFNOTIFY /*lpon*/)
	{
		App.Settings.Codec = _codec;
		return TRUE;
	}

	void OnHelp(LPOFNOTIFY pnmh)
	{
		App.InvokeHelp(IW::GetMainWindow(), HELP_IMAGE_LOADER);
	}
};

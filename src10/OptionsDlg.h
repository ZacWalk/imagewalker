#pragma once

// One flat options dialog. There is not enough here to justify tabs.

#include "ArtMate.h"

class COptionsDlg : public CDialogImpl<COptionsDlg>
{
public:
	enum { IDD = IDD_OPTIONS };

	BEGIN_MSG_MAP(COptionsDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_ID_HANDLER_EX(IDOK, OnOK)
		COMMAND_ID_HANDLER_EX(IDCANCEL, OnCancel)
		COMMAND_ID_HANDLER_EX(IDC_SELECT_FOLDER, OnSelectFolder)
		COMMAND_ID_HANDLER_EX(IDC_USE_CACHE, OnUseCache)
	END_MSG_MAP()

private:
	BOOL OnInitDialog(CWindow, LPARAM)
	{
		CenterWindow(GetParent());

		CheckDlgButton(IDC_USE_CACHE,
		               Settings::GetInt(_T("Options"), _T("Cache"), TRUE) ? BST_CHECKED : BST_UNCHECKED);
		SetDlgItemText(IDC_CACHE_FOLDER, Settings::CacheFolder());

		EnableCacheControls();
		return TRUE;
	}

	void OnOK(UINT, int, CWindow)
	{
		const BOOL bCache = IsDlgButtonChecked(IDC_USE_CACHE) == BST_CHECKED;

		CString strFolder;
		GetDlgItemText(IDC_CACHE_FOLDER, strFolder);

		Settings::SetInt(_T("Options"), _T("Cache"), bCache);
		Settings::SetString(_T("Options"), _T("Cache Folder"), strFolder);

		EndDialog(IDOK);
	}

	void OnCancel(UINT, int, CWindow)
	{
		EndDialog(IDCANCEL);
	}

	void OnUseCache(UINT, int, CWindow)
	{
		EnableCacheControls();
	}

	void EnableCacheControls()
	{
		const BOOL bCache = IsDlgButtonChecked(IDC_USE_CACHE) == BST_CHECKED;

		GetDlgItem(IDC_CACHE_FOLDER).EnableWindow(bCache);
		GetDlgItem(IDC_SELECT_FOLDER).EnableWindow(bCache);
	}

	void OnSelectFolder(UINT, int, CWindow)
	{
		TCHAR szPath[MAX_PATH] = {0};

		BROWSEINFO bi = {0};
		bi.hwndOwner = m_hWnd;
		bi.pszDisplayName = szPath;
		bi.lpszTitle = _T("Select the thumbnail cache folder");
		bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

		LPITEMIDLIST pidl = ::SHBrowseForFolder(&bi);

		if (pidl == nullptr)
			return;

		if (::SHGetPathFromIDList(pidl, szPath))
			SetDlgItemText(IDC_CACHE_FOLDER, szPath);

		::CoTaskMemFree(pidl);
	}
};

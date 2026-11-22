// ImageWalker by Zac Walker
//
// Purpose: The single-item rename and batch renamed-copy dialogs.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "iw/legacy/RenameCopies.h"

class CRenameDlg : public CDialogImpl<CRenameDlg>
{
protected:
public:
	CRenameDlg(const CString &strName, const CString &strExtension) : _strName(strName), _strExtension(strExtension)
	{
	}

	enum { IDD = IDD_RENAME_SINGLE };

	CString _strName;
	CString _strExtension;

	BEGIN_MSG_MAP(CRenameDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
	END_MSG_MAP()

	CRenameDlg(const CString &strFileName)
	{
		TCHAR szFileName[_MAX_FNAME];
		TCHAR szExt[_MAX_EXT] = _T("");

		_tsplitpath_s( strFileName, NULL, 0, NULL, 0, szFileName, _MAX_FNAME, szExt, _MAX_EXT);

		_strName = szFileName;
		_strExtension = szExt;
	}

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		CenterWindow(GetParent());
		SetDlgItemText(IDC_NAME, _strName);
		SetDlgItemText(IDC_EXTENSION, _strExtension);
		return (LRESULT)TRUE;
	}

	CString GetFileName() const
	{
		TCHAR sz[MAX_PATH];
		_tmakepath_s( sz, MAX_PATH, 0, 0, _strName, _strExtension);
		return sz;
	}

	LRESULT OnCloseCmd(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		GetDlgItemText(IDC_NAME, _strName);
		GetDlgItemText(IDC_EXTENSION, _strExtension);

		EndDialog(wID);
		return 0;
	}
};

class CRenameSelectedDlg : public CDialogImpl<CRenameSelectedDlg>
{
public:
	typedef CRenameSelectedDlg ThisClass;

	enum { IDD = IDD_RENAME };

	RENAMEPLAN &_plan;
	CString _strTemplate;
	int _nStart;
	bool _bReady;
	CListViewCtrl _list;

	CRenameSelectedDlg(RENAMEPLAN &plan, const CString &strTemplate, int nStart) :
		_plan(plan), _strTemplate(strTemplate), _nStart(nStart), _bReady(false)
	{
	}

	BEGIN_MSG_MAP(ThisClass)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
		COMMAND_ID_HANDLER(IDHELP, OnHelp)
		COMMAND_HANDLER(IDC_TEMPLATE, EN_CHANGE, OnFieldChange)
		COMMAND_HANDLER(IDC_START_AT, EN_CHANGE, OnFieldChange)
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		CenterWindow(GetParent());

		_list.Attach(GetDlgItem(IDC_RENAME_LIST));
		_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP);
		_list.InsertColumn(0, _T("Original"), LVCFMT_LEFT, 130);
		_list.InsertColumn(1, _T("Copy name"), LVCFMT_LEFT, 130);
		_list.InsertColumn(2, _T("Problem"), LVCFMT_LEFT, 150);

		for (const auto &item : _plan)
		{
			_list.InsertItem(_list.GetItemCount(), item.strName);
		}

		CUpDownCtrl spin = GetDlgItem(IDC_START_AT_PICK);
		spin.SetRange32(0, 999999);

		// Filling either field notifies, and a preview reads BOTH of them -- so
		// without the guard the template's own EN_CHANGE reads the start field while
		// it is still empty and puts the numbering back to zero.
		SetDlgItemText(IDC_TEMPLATE, _strTemplate);
		SetDlgItemInt(IDC_START_AT, _nStart, FALSE);
		_bReady = true;

		UpdatePreview();
		return (LRESULT)TRUE;
	}

	LRESULT OnFieldChange(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		UpdatePreview();
		return 0;
	}

	void UpdatePreview()
	{
		if (!_bReady)
			return;

		GetDlgItemText(IDC_TEMPLATE, _strTemplate);
		BOOL bTranslated = FALSE;
		const UINT nStart = GetDlgItemInt(IDC_START_AT, &bTranslated, FALSE);
		const bool bStartValid = bTranslated && nStart <= 999999;
		if (bStartValid)
			_nStart = static_cast<int>(nStart);

		BuildRenamePlan(_plan, _strTemplate, _nStart);

		int nProblems = 0;
		int nChanged = 0;

		_list.SetRedraw(FALSE);

		for (size_t i = 0; i < _plan.size(); i++)
		{
			const RenameItem &item = _plan[i];
			const int n = static_cast<int>(i);

			_list.SetItemText(n, 1, item.strProblem.IsEmpty() && !RenameCopyUnchanged(item) ?
				item.strNewName : g_szEmptyString);
			_list.SetItemText(n, 2, item.strProblem);

			if (!item.strProblem.IsEmpty())
				nProblems += 1;
			else if (!RenameCopyUnchanged(item))
				nChanged += 1;
		}

		_list.SetRedraw(TRUE);

		const int nTotal = static_cast<int>(_plan.size());
		CString str;

		if (_strTemplate.IsEmpty())
			str = _T("Type a template above.");
		else if (!bStartValid)
			str = _T("Enter a starting number from 0 to 999999.");
		else if (nProblems > 0)
			str.Format(_T("%d of %d names cannot be used. No copies will be made."),
			           nProblems, nTotal);
		else if (nChanged == 0)
			str = _T("Every file already has the name this template gives it.");
		else
			str.Format(_T("%d of %d files will be copied. Originals stay in place."), nChanged, nTotal);

		SetDlgItemText(IDC_RENAME_STATUS, str);
		GetDlgItem(IDOK).EnableWindow(!_strTemplate.IsEmpty() && bStartValid && nProblems == 0 && nChanged > 0);
	}

	LRESULT OnHelp(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		App.InvokeHelp(m_hWnd, HELP_TOOL_RENAME);
		return 0;
	}

	LRESULT OnCloseCmd(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		if (wID == IDOK)
		{
			UpdatePreview();
			if (!GetDlgItem(IDOK).IsWindowEnabled())
				return 0;
		}
		EndDialog(wID);
		return 0;
	}
};

class CRenameSelected
{
public:

	CString _strTemplate;
	int _nStart;

	CRenameSelected() :
		_strTemplate(App.Settings.Rename.Template),
		_nStart(IW::Max(1, App.Settings.Rename.Position))
	{
	}

	~CRenameSelected()
	{
		App.Settings.Rename.Template = _strTemplate;
		App.Settings.Rename.Position = _nStart;
	}

	void Rename(IW::FolderPtr pFolder)
	{
		RENAMEPLAN plan;

		for (const auto &pItem : pFolder->GetItemList())
		{
			if (!pItem->IsSelected() || pItem->IsFolder())
				continue;

			RenameItem item;
			item.strPath = pItem->GetFilePath();
			const std::filesystem::path path(item.strPath.GetString());
			item.strFolder = path.parent_path().c_str();
			item.strBase = path.stem().c_str();
			item.strExt = path.extension().c_str();
			item.strName = path.filename().c_str();

			plan.push_back(item);
		}

		if (plan.empty())
			return;

		CRenameSelectedDlg dlg(plan, _strTemplate, _nStart);

		if (IDOK != dlg.DoModal())
			return;

		_strTemplate = dlg._strTemplate;
		_nStart = dlg._nStart;

		const RenameCopyResult result = CopyRenamePlan(plan);
		if (!result.strFailed.IsEmpty())
		{
			CString str;
			str.Format(_T("%u copies were created. Originals stay in place.\n\nCould not create: %s"),
			            result.nCopied, result.strFailed.GetString());
			IW::CMessageBoxIndirect mb;
			mb.Show(str);
		}
	}
};

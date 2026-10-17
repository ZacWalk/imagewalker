// ImageWalker by Zac Walker
//
// Purpose: The rename dialogs. The batch one shows every new name before it
//          touches anything, and does the run through temporary names so a
//          template that shifts the numbering cannot collide with its own
//          inputs.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

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

struct RenameItem
{
	CString strFolder;
	CString strPath;
	CString strName;
	CString strBase;
	CString strExt;
	CString strNewName;
	CString strProblem;
	bool bLocked = false;
};

typedef std::vector<RenameItem> RENAMEPLAN;

// A run of #'s is the number, zero padded to the length of the run; ? is the
// whole original name. The dialog has always said so. What it did was copy one
// character of the original per ?, from the same index in the template, so the
// template "?" renamed every file to a single character.
inline CString FormatRenameName(const CString &strTemplate, const CString &strOriginal, int nPosition)
{
	CString strOut;
	const int nLength = strTemplate.GetLength();

	for (int i = 0; i < nLength;)
	{
		const TCHAR ch = strTemplate[i];

		if (ch == _T('#'))
		{
			int nRun = 0;

			while (i + nRun < nLength && strTemplate[i + nRun] == _T('#'))
				nRun += 1;

			CString strNumber;
			strNumber.Format(_T("%0*d"), nRun, nPosition);
			strOut += strNumber;
			i += nRun;
		}
		else if (ch == _T('?'))
		{
			strOut += strOriginal;
			i += 1;
		}
		else
		{
			strOut += ch;
			i += 1;
		}
	}

	return strOut;
}

// Fills in the new name and, where there is one, the reason it cannot be used.
// Every reason is a reason to stop: skipping one half of a pair of files that
// want the same name is not a decision to make on the user's behalf.
inline void BuildRenamePlan(RENAMEPLAN &plan, const CString &strTemplate, int nStart)
{
	int nPosition = nStart;

	for (auto &item : plan)
	{
		item.strNewName = FormatRenameName(strTemplate, item.strBase, nPosition) + item.strExt;
		item.strProblem.Empty();
		nPosition += 1;
	}

	for (size_t i = 0; i < plan.size(); i++)
	{
		RenameItem &item = plan[i];

		if (item.bLocked)
		{
			item.strProblem = _T("Cannot be renamed");
			continue;
		}

		if (item.strNewName.IsEmpty() || item.strNewName == item.strExt)
		{
			item.strProblem = _T("No name");
			continue;
		}

		// CFilePath::CheckFileName splits a path first, so it reads "a:b" as a
		// drive and a name and lets it through. These are bare names.
		static const TCHAR szIllegal[] = _T("\\/:*?\"<>|");

		if (item.strNewName.FindOneOf(szIllegal) >= 0 || item.strNewName.GetLength() > 215)
		{
			item.strProblem = _T("Not a usable file name");
			continue;
		}

		for (size_t j = 0; j < plan.size(); j++)
		{
			// Both files, not just the later one: the row that reads as fine is the
			// one the user would go and look at.
			if (j != i && !plan[j].bLocked && 0 == item.strNewName.CompareNoCase(plan[j].strNewName))
			{
				item.strProblem = _T("Two files would get this name");
				break;
			}
		}

		if (!item.strProblem.IsEmpty() || 0 == item.strNewName.CompareNoCase(item.strName))
			continue;

		// A name held by a file that this run is going to move out of the way is
		// free by the time it is wanted. A file that keeps its name is not.
		bool bInPlan = false;

		for (const auto &other : plan)
		{
			if (!other.bLocked &&
				0 != other.strNewName.CompareNoCase(other.strName) &&
				0 == item.strNewName.CompareNoCase(other.strName))
			{
				bInPlan = true;
				break;
			}
		}

		if (!bInPlan && IW::Path::FileExists(IW::Path::Combine(item.strFolder, item.strNewName)))
			item.strProblem = _T("A file of that name is already there");
	}
}

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
		_list.InsertColumn(0, _T("Now"), LVCFMT_LEFT, 130);
		_list.InsertColumn(1, _T("After"), LVCFMT_LEFT, 130);
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

	// Every keystroke, because the whole point of the list is that nothing is
	// renamed until the user has read what the template does to their files.
	void UpdatePreview()
	{
		if (!_bReady)
			return;

		GetDlgItemText(IDC_TEMPLATE, _strTemplate);
		_nStart = static_cast<int>(GetDlgItemInt(IDC_START_AT, nullptr, FALSE));

		BuildRenamePlan(_plan, _strTemplate, _nStart);

		int nProblems = 0;
		int nChanged = 0;

		_list.SetRedraw(FALSE);

		for (size_t i = 0; i < _plan.size(); i++)
		{
			const RenameItem &item = _plan[i];
			const int n = static_cast<int>(i);

			_list.SetItemText(n, 1, item.strProblem.IsEmpty() ? item.strNewName : g_szEmptyString);
			_list.SetItemText(n, 2, item.strProblem);

			if (!item.strProblem.IsEmpty())
				nProblems += 1;
			else if (0 != item.strNewName.Compare(item.strName))
				nChanged += 1;
		}

		_list.SetRedraw(TRUE);

		const int nTotal = static_cast<int>(_plan.size());
		CString str;

		if (_strTemplate.IsEmpty())
			str = _T("Type a template above.");
		else if (nProblems > 0)
			str.Format(_T("%d of %d names cannot be used. Nothing will be renamed until they can."),
			           nProblems, nTotal);
		else if (nChanged == 0)
			str = _T("Every file already has the name this template gives it.");
		else
			str.Format(_T("%d of %d files will be renamed."), nChanged, nTotal);

		SetDlgItemText(IDC_RENAME_STATUS, str);
		GetDlgItem(IDOK).EnableWindow(nProblems == 0 && nChanged > 0);
	}

	LRESULT OnHelp(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		App.InvokeHelp(m_hWnd, HELP_TOOL_RENAME);   
		return 0;
	}

	LRESULT OnCloseCmd(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
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

			TCHAR szDrive[_MAX_DRIVE];
			TCHAR szDir[_MAX_DIR];
			TCHAR szName[_MAX_FNAME];
			TCHAR szExt[_MAX_EXT];

			RenameItem item;
			item.strPath = pItem->GetFilePath();

			_tsplitpath_s(item.strPath, szDrive, countof(szDrive), szDir, countof(szDir),
			              szName, countof(szName), szExt, countof(szExt));

			item.strFolder = CString(szDrive) + szDir;
			item.strBase = szName;
			item.strExt = szExt;
			item.strName = item.strBase + item.strExt;
			item.bLocked = !pItem->CanRename() || pItem->IsReadOnly();

			plan.push_back(item);
		}

		if (plan.empty())
			return;

		CRenameSelectedDlg dlg(plan, _strTemplate, _nStart);

		if (IDOK != dlg.DoModal())
			return;

		_strTemplate = dlg._strTemplate;
		_nStart = dlg._nStart;

		Apply(plan);
	}

private:

	// Two passes through temporary names. Renaming 1, 2, 3 to 2, 3, 4 collides
	// with its own inputs one file at a time; moving everything out of the way
	// first cannot. It also means a failure part way leaves the folder as it was
	// rather than half renamed with no record of which half.
	static void Apply(const RENAMEPLAN &plan)
	{
		std::vector<CString> temps(plan.size());
		size_t nMoved = 0;

		for (; nMoved < plan.size(); nMoved++)
		{
			const RenameItem &item = plan[nMoved];

			if (!item.strProblem.IsEmpty() || 0 == item.strNewName.Compare(item.strName))
				continue;

			TCHAR szTemp[MAX_PATH] = { 0 };

			// A zero unique number makes the call create the file, so the name
			// cannot be handed out twice while this is running.
			if (::GetTempFileName(item.strFolder, _T("IW"), 0, szTemp) == 0)
				break;

			if (!::MoveFileEx(item.strPath, szTemp, MOVEFILE_REPLACE_EXISTING))
			{
				// GetTempFileName created the file, so it is litter in the user's
				// folder unless this takes it away again.
				::DeleteFile(szTemp);
				break;
			}

			temps[nMoved] = szTemp;
		}

		CString strStranded;

		if (nMoved < plan.size())
		{
			// Nothing has taken any of these names yet, so a plain move is enough
			// and cannot land on top of anything.
			for (size_t i = 0; i < nMoved; i++)
			{
				if (temps[i].IsEmpty())
					continue;

				if (!::MoveFileEx(temps[i], plan[i].strPath, 0))
					IW::AddToList(strStranded, temps[i]);
			}

			Report(_T("Nothing was renamed: one of the files could not be moved."), strStranded);
			return;
		}

		CString strFailed;

		for (size_t i = 0; i < plan.size(); i++)
		{
			if (temps[i].IsEmpty())
				continue;

			const RenameItem &item = plan[i];
			const CString strTo = IW::Path::Combine(item.strFolder, item.strNewName);

			// No MOVEFILE_REPLACE_EXISTING: the preview said this name was free.
			if (::MoveFileEx(temps[i], strTo, 0))
				continue;

			// Nor on the way back. An earlier file in this run may already have
			// taken the name this one arrived with, and replacing it would destroy
			// a rename that had succeeded.
			if (::MoveFileEx(temps[i], item.strPath, 0))
				IW::AddToList(strFailed, item.strName);
			else
				IW::AddToList(strStranded, temps[i]);
		}

		CString str;

		if (!strFailed.IsEmpty())
			str.Format(_T("These kept the names they had: %s"), static_cast<LPCTSTR>(strFailed));

		Report(str, strStranded);
	}

	// A file left under its temporary name has not been lost, but nothing else
	// will ever tell the user where it went.
	static void Report(const CString &strMessage, const CString &strStranded)
	{
		CString str = strMessage;

		if (!strStranded.IsEmpty())
		{
			if (!str.IsEmpty())
				str += _T("\n\n");

			str += _T("These could not be put back and are still under a temporary name: ");
			str += strStranded;
		}

		if (str.IsEmpty())
			return;

		IW::CMessageBoxIndirect mb;
		mb.Show(str);
	}
};

// ImageWalker by Zac Walker
//
// Purpose: CToolDlg: the two-page shape every batch tool shares - choose the
//          files, then run with a progress log.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "ViewDialogs.h"
#include "ViewModelItems.h"
#include "FileFormatAny.h"
#include "ViewModelState.h"

class FolderStack : public CSimpleValArray<CString>
{
};

class ScopeLockFolderStack
{
private:
	FolderStack &_stack;

public:

	ScopeLockFolderStack(FolderStack &stack, IW::Folder *pFolder) : _stack(stack)
	{
		_stack.Add(pFolder->GetFolderName());
	}

	ScopeLockFolderStack(FolderStack &stack, const CString &str) : _stack(stack)
	{
		_stack.Add(str);
	}

	~ScopeLockFolderStack()
	{
		_stack.RemoveAt(_stack.GetSize() - 1);
	}
};

// One dialog per tool, in two modes.
//
// Setup mode is where the files come from, the tool's own options as a child
// dialog in the middle, and where the results go. Start disables all of that
// where it stands and grows the window downwards to show the progress readout
// and a report that fills in as it goes -- one window, and the settings the run
// is using stay on screen beside it.
//
// It replaces a seven page PSH_WIZARD97 sheet per tool -- introduction, input,
// options, output, "about to process", progress, report -- of which the first
// and fifth were prose.
//
// What T provides:
//
//   CString GetKey() const;                    ini section for its settings
//   CString GetTitle() const;                  window caption
//   CString GetCompletedText() const;          what the status line says at the end
//   void    OnHelp() const;
//   HWND    OnCreateOptions(HWND hWndParent);  its own options page
//   bool    OnApplyOptions();                  false vetoes Start
//   void    OnProcess(IW::IStatus *pStatus);
//   void    OnComplete();
//   TSettings &Settings();                     its slot in App.Settings.Tools
//   bool    AllowOverwrite() const;            optional, default true
//   bool    AllowSource() const;               optional, default true
//
template<class T, class TSettings = ToolTargetSettings>
class CToolDlg :
	public IW::IStatus,
	public CDialogImpl<T>,
	public TSettings
{
public:

	typedef CToolDlg<T, TSettings> ThisClass;
	typedef CDialogImpl<T> BaseClass;

	enum { IDD = IDD_TOOL };

	State &_state;
	FolderStack m_arrayFolderNames;

	// Where they come from.
	int m_nSelectedItemCount;
	int m_nItemCount;
	bool _bSelected;

	CToolDlg(State &state) :
		_state(state),
		m_nSelectedItemCount(0),
		m_nItemCount(0),
		_bSelected(false),
		_bCancel(false),
		_bRunning(false),
		_bFinished(false),
		_hWndOptions(nullptr),
		_nReportItem(-1),
		_cyProgress(0),
		_cyWindow(0)
	{
		IW::FolderPtr pFolder = state.Folder.GetFolder();

		m_nSelectedItemCount = pFolder->GetSelectedItemCount();
		m_nItemCount = pFolder->GetItemCount();

		// Opening a tool with three photos selected and defaulting to "all files
		// in this folder" contradicts the selection the user just made.
		_bSelected = m_nSelectedItemCount > 0;
	}

	BEGIN_MSG_MAP(CToolDlg<T>)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_START, OnStart)
		COMMAND_ID_HANDLER(IDOK, OnOk)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
		COMMAND_ID_HANDLER(IDHELP, OnHelpButton)
		COMMAND_ID_HANDLER(IDC_BROWSE, OnBrowse)
		COMMAND_ID_HANDLER(ID_SELECT_BROWSE, OnBrowseFolder)
		COMMAND_ID_HANDLER(ID_SELECT_CURRENTFOLDER, OnSelectCurrent)
		COMMAND_ID_HANDLER(IDC_OVERWRITE, OnDestinationChanged)
		COMMAND_ID_HANDLER(IDC_WRITE_FOLDER, OnDestinationChanged)
	END_MSG_MAP()

	INT_PTR DoModal(HWND hWndParent = IW::GetMainWindow())
	{
		T *pT = static_cast<T*>(this);

		pT->LoadSettings();
		const INT_PTR nRet = BaseClass::DoModal(hWndParent);
		pT->SaveSettings();

		return nRet;
	}

	// The output path is remembered between runs, and a tool that has never been
	// run has none. The default is a sub folder of the one being browsed rather
	// than that folder itself: most of these tools keep the file name, so writing
	// beside the originals would only land on their own input.
	void LoadSettings()
	{
		T *pT = static_cast<T*>(this);
		TSettings::operator=(pT->Settings());

		if (TSettings::_pathFolderOut.ToString().IsEmpty())
		{
			IW::CFilePath path = GetFolderPath();
			const CString strSub = pT->DefaultOutputSubFolder();

			if (!strSub.IsEmpty())
				path += strSub;

			TSettings::_pathFolderOut = path;
		}
	}

	CString DefaultOutputSubFolder() const
	{
		return g_szEmptyString;
	}

	void SaveSettings()
	{
		T *pT = static_cast<T*>(this);
		pT->Settings() = *this;
	}

	// A tool whose output is not one file per input says so, and the overwrite
	// choice goes away rather than offering something it cannot do.
	bool AllowOverwrite() const
	{
		return true;
	}

	// A tool told which files to work on by whoever opened it says so, and the
	// Files group goes away rather than offering a second answer to a question
	// that already has one.
	bool AllowSource() const
	{
		return true;
	}

	CString GetFolderPath() const
	{
		return _state.Folder.GetFolderPath();
	}

	// _pathFolderOut plus whatever sub folders the recursion is currently inside.
	IW::CFilePath OutputFolder() const
	{
		IW::CFilePath path = TSettings::_pathFolderOut;

		for (int i = 0; i < m_arrayFolderNames.GetSize(); i++)
			path += m_arrayFolderNames[i];

		return path;
	}

	// A tool writes new files. Landing on the file it is reading is what the
	// overwrite mode used to do, and one of the ways it did it deleted the result.
	bool RefuseToOverwriteSource(const IW::CFilePath &path, IW::FolderItem *pItem, IW::IStatus *pStatus) const
	{
		// The destination is whatever the user typed and the source is whatever the
		// shell said, so comparing them as written would miss a trailing separator
		// or a relative segment and let the write through.
		if (0 != _tcsicmp(FullPath(path), FullPath(pItem->GetFilePath())))
			return false;

		CString str;
		str.Format(_T("Skipped '%s': the destination is that file itself."),
		           static_cast<LPCTSTR>(pItem->GetFileName()));
		pStatus->SetError(str);
		return true;
	}

	// A tool only ever writes a file it named itself. Convert changes the
	// extension, so its destination routinely names some *other* picture in the
	// same folder, and a second run of any of them collides with the first.
	static bool MakeUniqueOutputPath(IW::CFilePath &path)
	{
		if (!IW::Path::FileExists(path))
			return true;

		const CString strPath = path;
		const CString strName = path.GetFileName();
		const CString strExtension = IW::Path::FindExtension(strPath);

		for (int n = 2; n < 10000; n++)
		{
			CString str;
			str.Format(_T("%s (%d)"), static_cast<LPCTSTR>(strName), n);
			path.SetFileNameAndExtension(str, strExtension);

			if (!IW::Path::FileExists(path))
				return true;
		}

		// Every candidate is taken. Returning here would hand back a name the
		// loop just proved exists, which is the overwrite this exists to prevent.
		return false;
	}

	static CString FullPath(const CString &str)
	{
		TCHAR sz[MAX_PATH] = { 0 };

		if (::GetFullPathName(str, MAX_PATH, sz, nullptr) == 0)
			return str;

		return sz;
	}

	IW::IStatus *GetStatus()
	{
		return this;
	}

	template<class THandeler>
	void IterateItems(THandeler *pItemHandeler)
	{
		if (_bSelected)
		{
			_state.Folder.GetFolder()->IterateSelectedItems(pItemHandeler, GetStatus());
		}
		else
		{
			_state.Folder.GetFolder()->IterateItems(pItemHandeler, GetStatus());
		}
	}

	///////////////////////////////////////////////////////////////////////
	// IW::IStatus. The run owns the UI thread, so every one of these pumps.

	void Progress(int nCurrentStep, int nTotalSteps)
	{
		if (nTotalSteps > 0) _barFile.SetPos(MulDiv(nCurrentStep, 100, nTotalSteps));
		PumpMessages();
	}

	void SetHighLevelProgress(int nCurrentStep, int nTotalSteps)
	{
		if (nTotalSteps > 0) _barAll.SetPos(MulDiv(nCurrentStep, 100, nTotalSteps));
		PumpMessages();
	}

	void SetStatusMessage(const CString &strMessage)
	{
		SetDlgItemText(IDC_STATUS2, strMessage);
	}

	void SetHighLevelStatusMessage(const CString &strMessage)
	{
		SetDlgItemText(IDC_STATUS, strMessage);
	}

	bool QueryCancel()
	{
		return _bCancel;
	}

	// One row per file, added when the file is reached rather than listed at the
	// end: a report that only appears once the work is over cannot be watched.
	void SetContext(const CString &strContext)
	{
		_nReportItem = _report.InsertItem(_report.GetItemCount(), strContext, ImageIndex::OK);
		_report.EnsureVisible(_nReportItem, FALSE);
		_barFile.SetPos(0);
	}

	void SetMessage(const CString &strMessage)
	{
		SetReportResult(strMessage, false);
	}

	void SetWarning(const CString &strWarning)
	{
		SetReportResult(strWarning, false);
	}

	void SetError(const CString &strError)
	{
		SetReportResult(strError, true);
	}

private:

	CListViewCtrl _report;
	CProgressBarCtrl _barFile;
	CProgressBarCtrl _barAll;
	CRect _rcButton[3];
	HWND _hWndOptions;
	int _nReportItem;
	int _cyProgress;
	int _cyWindow;
	bool _bCancel;
	bool _bRunning;
	bool _bFinished;

	void SetReportResult(const CString &str, bool bError)
	{
		if (_nReportItem < 0)
			return;

		_report.SetItemText(_nReportItem, 1, str);
		if (bError) _report.SetItem(_nReportItem, 0, LVIF_IMAGE, nullptr, ImageIndex::Error, 0, 0, 0);
	}

	static void EnableGroup(HWND hWnd, const int *pIds, bool bEnable)
	{
		for (; *pIds != 0; pIds++)
			::EnableWindow(::GetDlgItem(hWnd, *pIds), bEnable);
	}

public:

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		T *pT = static_cast<T*>(this);

		CenterWindow(GetParent());
		SetWindowText(pT->GetTitle());

		_report = GetDlgItem(IDC_REPORT);
		_barFile = GetDlgItem(IDC_PROGRESS);
		_barAll = GetDlgItem(IDC_PROGRESS2);

		_barFile.SetRange(0, 100);
		_barAll.SetRange(0, 100);

		_report.SetImageList(App.GetGlobalBitmap(), LVSIL_SMALL);

		// Sized to the control, or the two columns add up to more than it is
		// wide and the report gets a horizontal scroll bar it never needs.
		CRect rcReport;
		_report.GetClientRect(rcReport);
		const int cxName = rcReport.Width() / 3;

		_report.InsertColumn(0, App.LoadString(IDS_FILE), LVCFMT_LEFT, cxName);
		_report.InsertColumn(1, App.LoadString(IDS_STATUS), LVCFMT_LEFT,
		                     rcReport.Width() - cxName - ::GetSystemMetrics(SM_CXVSCROLL));

		// Source
		const bool bHasSelection = m_nSelectedItemCount > 0;
		GetDlgItem(IDC_FILES_SELECTED).EnableWindow(bHasSelection);

		if (!bHasSelection) _bSelected = false;

		CheckRadioButton(IDC_FILES_ALL, IDC_FILES_SELECTED, _bSelected ? IDC_FILES_SELECTED : IDC_FILES_ALL);
		CheckDlgButton(IDC_RECURSE, _bRecurse ? BST_CHECKED : BST_UNCHECKED);

		if (!pT->AllowSource())
			CollapseSource();

		// The options page is the tool's own; it goes in the hole the template
		// leaves for it and nothing here knows what is on it.
		CWindow wndHost = GetDlgItem(IDC_TOOL_OPTIONS_HOST);
		_hWndOptions = pT->OnCreateOptions(m_hWnd);

		if (_hWndOptions != nullptr)
		{
			// Without CONTROLPARENT the dialog manager will not recurse into it,
			// so Tab skips every option. Slotting it into the placeholder's place
			// in the Z-order is what puts it between Files and Save to, rather
			// than wherever creating it last happened to leave it.
			::SetWindowLong(_hWndOptions, GWL_EXSTYLE,
			                ::GetWindowLong(_hWndOptions, GWL_EXSTYLE) | WS_EX_CONTROLPARENT);

			if (wndHost.IsWindow())
				::SetWindowPos(_hWndOptions, wndHost, 0, 0, 0, 0,
				               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}

		if (wndHost.IsWindow())
		{
			CRect rc;
			wndHost.GetWindowRect(rc);
			ScreenToClient(rc);
			FitOptions(rc);
			wndHost.ShowWindow(SW_HIDE);
		}

		// Destination
		if (pT->AllowOverwrite())
		{
			CheckRadioButton(IDC_OVERWRITE, IDC_WRITE_FOLDER, m_bOverwrite ? IDC_OVERWRITE : IDC_WRITE_FOLDER);
		}
		else
		{
			m_bOverwrite = false;
			m_bFolder = true;

			GetDlgItem(IDC_OVERWRITE).ShowWindow(SW_HIDE);
			GetDlgItem(IDC_WRITE_FOLDER).ShowWindow(SW_HIDE);

			// With the radios gone the edit would sit in from the group edge with
			// nothing to the left of it, on the second of two rows that now has
			// only one. Take both the space and the row back.
			CRect rcRadio, rcOverwrite, rcEdit;
			GetDlgItem(IDC_WRITE_FOLDER).GetWindowRect(rcRadio);
			GetDlgItem(IDC_OVERWRITE).GetWindowRect(rcOverwrite);
			GetDlgItem(IDC_FOLDER).GetWindowRect(rcEdit);
			ScreenToClient(rcRadio);
			ScreenToClient(rcOverwrite);
			ScreenToClient(rcEdit);

			const int dyRow = rcRadio.top - rcOverwrite.top;

			GetDlgItem(IDC_FOLDER).SetWindowPos(nullptr, rcRadio.left, rcEdit.top - dyRow,
			                                    rcEdit.right - rcRadio.left, rcEdit.Height(),
			                                    SWP_NOZORDER | SWP_NOACTIVATE);
			OffsetChild(IDC_BROWSE, -dyRow);
			ShrinkFromDestination(dyRow);
		}

		SetDlgItemText(IDC_FOLDER, _pathFolderOut);
		UpdateDestinationEnabled();

		MeasureAndCollapse();

		return TRUE;
	}

	// Takes dy off the bottom of the Save to group and brings everything under
	// it, and the window, up to match.
	void ShrinkFromDestination(int dy)
	{
		if (dy <= 0)
			return;

		static const int below[] = {
			IDC_STATUS, IDC_PROGRESS, IDC_STATUS2, IDC_PROGRESS2, IDC_REPORT,
			IDOK, IDCANCEL, IDHELP, 0
		};

		for (const int *pId = below; *pId != 0; pId++)
			OffsetChild(*pId, -dy);

		CWindow wndGroup = GetDlgItem(IDC_TOOL_DEST_GROUP);
		CRect rcGroup;
		wndGroup.GetWindowRect(rcGroup);
		ScreenToClient(rcGroup);
		rcGroup.bottom -= dy;
		wndGroup.MoveWindow(rcGroup);

		CRect rcWnd;
		GetWindowRect(rcWnd);
		SetWindowPos(nullptr, 0, 0, rcWnd.Width(), rcWnd.Height() - dy,
		             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	void CollapseSource()
	{
		static const int source[] = { IDC_TOOL_FILES_GROUP, IDC_FILES_ALL, IDC_FILES_SELECTED, IDC_RECURSE, 0 };

		static const int below[] = {
			IDC_TOOL_OPTIONS_GROUP, IDC_TOOL_OPTIONS_HOST,
			IDC_TOOL_DEST_GROUP, IDC_OVERWRITE, IDC_WRITE_FOLDER, IDC_FOLDER, IDC_BROWSE,
			IDC_STATUS, IDC_PROGRESS, IDC_STATUS2, IDC_PROGRESS2, IDC_REPORT,
			IDOK, IDCANCEL, IDHELP, 0
		};

		CRect rcGroup, rcOptions;
		GetDlgItem(IDC_TOOL_FILES_GROUP).GetWindowRect(rcGroup);
		GetDlgItem(IDC_TOOL_OPTIONS_GROUP).GetWindowRect(rcOptions);

		const int dy = rcOptions.top - rcGroup.top;

		for (const int *pId = source; *pId != 0; pId++)
			GetDlgItem(*pId).ShowWindow(SW_HIDE);

		for (const int *pId = below; *pId != 0; pId++)
			OffsetChild(*pId, -dy);

		CRect rcWnd;
		GetWindowRect(rcWnd);
		SetWindowPos(nullptr, 0, 0, rcWnd.Width(), rcWnd.Height() - dy,
		             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	// The template leaves the tallest hole any tool needs. A page shorter than
	// that takes the height of its own template and everything below it comes
	// up, so each tool gets a dialog the size of its own options rather than the
	// size of the worst one.
	void FitOptions(const CRect &rcHost)
	{
		if (_hWndOptions == nullptr)
			return;

		CRect rcChild;
		::GetWindowRect(_hWndOptions, rcChild);

		const int cyChild = IW::Clamp(static_cast<int>(rcChild.Height()), 16, rcHost.Height());
		const int dy = rcHost.Height() - cyChild;

		::SetWindowPos(_hWndOptions, HWND_TOP, rcHost.left, rcHost.top, rcHost.Width(), cyChild, SWP_SHOWWINDOW);

		if (dy <= 0)
			return;

		static const int below[] = {
			IDC_TOOL_DEST_GROUP, IDC_OVERWRITE, IDC_WRITE_FOLDER, IDC_FOLDER, IDC_BROWSE,
			IDC_STATUS, IDC_PROGRESS, IDC_STATUS2, IDC_PROGRESS2, IDC_REPORT,
			IDOK, IDCANCEL, IDHELP, 0
		};

		for (const int *pId = below; *pId != 0; pId++)
			OffsetChild(*pId, -dy);

		CWindow wndGroup = GetDlgItem(IDC_TOOL_OPTIONS_GROUP);
		CRect rcGroup;
		wndGroup.GetWindowRect(rcGroup);
		ScreenToClient(rcGroup);
		rcGroup.bottom -= dy;
		wndGroup.MoveWindow(rcGroup);

		CRect rcWnd;
		GetWindowRect(rcWnd);
		SetWindowPos(nullptr, 0, 0, rcWnd.Width(), rcWnd.Height() - dy,
		             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	void OffsetChild(int nId, int dy)
	{
		CWindow wnd = GetDlgItem(nId);

		if (!wnd.IsWindow())
			return;

		CRect rc;
		wnd.GetWindowRect(rc);
		ScreenToClient(rc);
		wnd.SetWindowPos(nullptr, rc.left, rc.top + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	// The template is drawn at its full height with the progress readout at the
	// bottom; setup mode is that minus the readout, with the buttons moved up.
	void MeasureAndCollapse()
	{
		CRect rcStatus, rcReport, rcWnd;

		GetDlgItem(IDC_STATUS).GetWindowRect(rcStatus);
		GetDlgItem(IDC_REPORT).GetWindowRect(rcReport);
		GetWindowRect(rcWnd);

		_cyProgress = rcReport.bottom - rcStatus.top;
		_cyWindow = rcWnd.Height();

		static const int buttons[] = { IDOK, IDCANCEL, IDHELP };

		for (int i = 0; i < 3; i++)
		{
			GetDlgItem(buttons[i]).GetWindowRect(_rcButton[i]);
			ScreenToClient(_rcButton[i]);
		}

		ShowProgress(false);
	}

	void ShowProgress(bool bShow)
	{
		static const int progress[] = { IDC_STATUS, IDC_PROGRESS, IDC_STATUS2, IDC_PROGRESS2, IDC_REPORT, 0 };
		static const int buttons[] = { IDOK, IDCANCEL, IDHELP };

		for (const int *pId = progress; *pId != 0; pId++)
			GetDlgItem(*pId).ShowWindow(bShow ? SW_SHOW : SW_HIDE);

		const int dy = bShow ? 0 : -_cyProgress;

		for (int i = 0; i < 3; i++)
		{
			GetDlgItem(buttons[i]).SetWindowPos(nullptr, _rcButton[i].left, _rcButton[i].top + dy, 0, 0,
			                                    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		}

		CRect rcWnd;
		GetWindowRect(rcWnd);
		SetWindowPos(nullptr, 0, 0, rcWnd.Width(), _cyWindow + dy,
		             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	void UpdateDestinationEnabled()
	{
		const bool bEnable = m_bFolder && !_bRunning;
		GetDlgItem(IDC_FOLDER).EnableWindow(bEnable);
		GetDlgItem(IDC_BROWSE).EnableWindow(bEnable);
	}

	LRESULT OnDestinationChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		m_bOverwrite = BST_CHECKED == IsDlgButtonChecked(IDC_OVERWRITE);
		m_bFolder = !m_bOverwrite;
		UpdateDestinationEnabled();
		return 0;
	}

	// Everything the run needs, read out of the dialog once.
	bool ApplySettings()
	{
		T *pT = static_cast<T*>(this);

		_bSelected = BST_CHECKED == IsDlgButtonChecked(IDC_FILES_SELECTED);
		_bRecurse = BST_CHECKED == IsDlgButtonChecked(IDC_RECURSE);

		if (pT->AllowOverwrite())
		{
			m_bOverwrite = BST_CHECKED == IsDlgButtonChecked(IDC_OVERWRITE);
			m_bFolder = !m_bOverwrite;
		}

		if (!pT->OnApplyOptions())
			return false;

		if (m_bFolder)
		{
			GetDlgItemText(IDC_FOLDER, _pathFolderOut);

			if (!_pathFolderOut.CreateAllDirectories())
			{
				IW::CMessageBoxIndirect mb;
				mb.ShowOsErrorWithFile(_pathFolderOut, IDS_FAILEDTO_CREATE_FOLDER);
				return false;
			}
		}

		return true;
	}

	LRESULT OnOk(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		if (_bFinished)
		{
			EndDialog(IDOK);
			return 0;
		}

		if (_bRunning || !ApplySettings())
			return 0;

		EnterProgressMode();

		// Posted, so the button click finishes and the window is in its new mode
		// before anything long starts.
		PostMessage(WM_START);
		return 0;
	}

	void EnterProgressMode()
	{
		static const int setup[] = {
			IDC_FILES_ALL, IDC_FILES_SELECTED, IDC_RECURSE,
			IDC_OVERWRITE, IDC_WRITE_FOLDER, IDC_FOLDER, IDC_BROWSE, 0
		};

		_bRunning = true;

		EnableGroup(m_hWnd, setup, false);
		if (_hWndOptions != nullptr) ::EnableWindow(_hWndOptions, FALSE);

		GetDlgItem(IDOK).EnableWindow(FALSE);
		ShowProgress(true);
	}

	LRESULT OnStart(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		T *pT = static_cast<T*>(this);

		SetHighLevelStatusMessage(pT->GetTitle());

		try
		{
			pT->OnProcess(GetStatus());
		}
		catch (const std::exception &e)
		{
			USES_CONVERSION;
			SetError(CString(CA2T(e.what())));
		}

		pT->OnComplete();

		_bRunning = false;
		_bFinished = true;

		_barFile.SetPos(100);
		_barAll.SetPos(100);
		SetStatusMessage(g_szEmptyString);
		SetHighLevelStatusMessage(_bCancel ? App.LoadString(IDS_CANCELED) : pT->GetCompletedText());

		SetDlgItemText(IDOK, App.LoadString(IDS_CLOSE));
		GetDlgItem(IDOK).EnableWindow(TRUE);
		GetDlgItem(IDCANCEL).EnableWindow(FALSE);
		GetDlgItem(IDOK).SetFocus();

		return 0;
	}

	LRESULT OnCancel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		// Mid-run this is a request the worker loop reads, not an exit: tearing
		// the window down under OnProcess would leave it writing to dead controls.
		if (_bRunning)
		{
			_bCancel = true;
			GetDlgItem(IDCANCEL).EnableWindow(FALSE);
			return 0;
		}

		EndDialog(IDCANCEL);
		return 0;
	}

	LRESULT OnHelpButton(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		static_cast<T*>(this)->OnHelp();
		return 0;
	}

	LRESULT OnBrowse(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		CWindow wndButton = GetDlgItem(IDC_BROWSE);

		if (wndButton.IsWindow())
		{
			CRect r;
			wndButton.GetWindowRect(r);

			CMenu menu;
			menu.LoadMenu(IDR_POPUPS);
			CMenuHandle menuPopup = menu.GetSubMenu(2);

			TPMPARAMS tpm;
			tpm.cbSize = sizeof(TPMPARAMS);
			tpm.rcExclude = r;

			menuPopup.TrackPopupMenuEx(0, r.left, r.bottom, m_hWnd, &tpm);
		}

		return 0;
	}

	LRESULT OnBrowseFolder(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		IW::CFilePath path;
		GetDlgItemText(IDC_FOLDER, path);
		path.Normalize(true);

		if (IW::CShellDesktop::GetDirectory(m_hWnd, path))
			SetDlgItemText(IDC_FOLDER, path);

		return 0;
	}

	LRESULT OnSelectCurrent(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		SetDlgItemText(IDC_FOLDER, GetFolderPath());
		return 0;
	}

	// The run holds the UI thread, so Cancel and repainting only happen here.
	void PumpMessages()
	{
		MSG msg;

		while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (!IsDialogMessage(&msg))
			{
				::TranslateMessage(&msg);
				::DispatchMessage(&msg);
			}
		}
	}
};

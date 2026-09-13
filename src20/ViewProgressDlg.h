// ImageWalker by Zac Walker
//
// Purpose: The progress dialog, and the IStatus a long operation reports
//          through.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

class CProgressDlg : 
	public CDialogImpl<CProgressDlg>,
	public IW::IStatus
{
private:

	CString _strError;
	HWND _hParentWnd;
	bool _bParentDisabled;
	bool _bCancel;
	int _nPercent1;
	int _nPercent2;

	CProgressBarCtrl _progressBar1;
	CProgressBarCtrl _progressBar2;

public:

	int IDD;

	// IDD_PROGRESS 
	// IDD_PROGRESS_ADVANCED

	typedef CProgressDlg ThisClass;
	typedef CDialogImpl<ThisClass> BaseClass;	


	CProgressDlg(int id = IDD_PROGRESS) : IDD(id)
	{
		_bCancel = false;
		_bParentDisabled = false;
		_nPercent1 = -1;
		_nPercent2 = -1;
	}

	~CProgressDlg()
	{
		ReEnableParent();

		if(m_hWnd!=NULL)
			DestroyWindow();
	}

	BOOL Create(HWND hParent, UINT nId)
	{
		// Set the title
		TCHAR szTitle[100];
		LoadString(App.GetResourceInstance(), nId, szTitle, 100);

		return Create(hParent, szTitle);
	}

	BOOL Create(HWND hParent, const CString &strTitle)
	{
		// Get the true parent of the dialog
		_hParentWnd = hParent;

		if(!BaseClass::Create(hParent))
		{
			ReEnableParent();
			return FALSE;
		}	

		SetWindowText(strTitle);
		_bCancel = false;

		// Center
		CenterWindow(hParent);

		// Show!!
		ShowWindow(SW_SHOW);

		// m_bParentDisabled is used to re-enable the parent window
		// when the dialog is destroyed. So we don't want to set
		// it to TRUE unless the parent was already enabled.
		if(_hParentWnd && ::IsWindowEnabled(_hParentWnd))
		{
			::EnableWindow(_hParentWnd, FALSE);
			_bParentDisabled = true;
		}

		return TRUE;
	}

	void ReEnableParent()
	{

		if(_bParentDisabled && _hParentWnd)
		{
			::EnableWindow(_hParentWnd, TRUE);
			//::SetForegroundWindow(_hParentWnd);
		}

		_bParentDisabled=false;
	}

	void End()
	{
		ReEnableParent();

		if(m_hWnd!=NULL)
			DestroyWindow();
	}

	BEGIN_MSG_MAP(ThisClass)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		_progressBar1.Attach(GetDlgItem(IDC_PROGRESS));
		_progressBar2.Attach(GetDlgItem(IDC_PROGRESS2));

		_progressBar1.SetRange(0, 100);
		if (_progressBar2.m_hWnd) _progressBar2.SetRange(0, 100);
		return 0;
	}

	LRESULT OnCancel(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		_bCancel=true;
		return 0;
	}

	void Progress(int nCurrentStep, int nTotalSteps)
	{
		int nPercent = MulDiv(IW::Min(nCurrentStep, nTotalSteps), 100, IW::Max(nTotalSteps, 1));

		if (_nPercent1 != nPercent)
		{
			_nPercent1 = nPercent;
			_progressBar1.SetPos(nPercent);

			CString str; 
			str.Format(_T("%d%%"), nPercent);
			SetDlgItemText(IDC_PERCENT, str);

			PumpMessages();
		}
	}	

	void SetHighLevelProgress(int nCurrentStep, int nTotalSteps)
	{
		int nPercent = MulDiv(IW::Min(nCurrentStep, nTotalSteps), 100, IW::Max(nTotalSteps, 1));

		if (_nPercent2 != nPercent)
		{
			_nPercent2 = nPercent;
			_progressBar2.SetPos(nPercent);
			PumpMessages();
		}
	}	

	bool QueryCancel()
	{
		return _bCancel;
	}

	void SetStatusMessage(int id)
	{
		CString str;
		str.LoadString(id);
		SetStatusMessage(str);
	}

	void SetStatusMessage(const CString &strMessage)
	{
		ATLASSERT(m_hWnd);
		SetDlgItemText(IDC_STATUS, strMessage);
		PumpMessages();
	}

	void SetHighLevelStatusMessage(const CString &strMessage)
	{
		ATLASSERT(m_hWnd);
		SetDlgItemText(IDC_STATUS2, strMessage);
		PumpMessages();
	}

	void SetMessage(const CString &strMessage)
	{
	}

	void SetWarning(const CString &strWarning)
	{
	}

	void SetError(const CString &strError)
	{
		_strError = strError;
	}

	void SetContext(const CString &strContext)
	{
	}	

	void PumpMessages()
	{
		// Must call Create() before using the dialog
		ATLASSERT(m_hWnd!=NULL);

		MSG msg;
		// Handle dialog messages
		while(PeekMessage(&msg, NULL, 0 , 0, PM_REMOVE))
		{
			if(!IsDialogMessage(&msg))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);  
			}
		}
	}

	CString GetError() const
	{
		return _strError;
	}
};

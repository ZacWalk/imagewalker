// ImageWalker by Zac Walker
//
// Purpose: The search panel shown in the side pane.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "ViewDialogs.h"

inline void PopulateSearchByDateCombos(CComboBox &comboMonth, CComboBox &comboYear)
{
		comboMonth.AddString(App.LoadString(IDS_ANYMONTH));
		comboMonth.AddString(App.LoadString(IDS_JANUARY));
		comboMonth.AddString(App.LoadString(IDS_FEBRUARY));
		comboMonth.AddString(App.LoadString(IDS_MARCH));
		comboMonth.AddString(App.LoadString(IDS_APRIL));
		comboMonth.AddString(App.LoadString(IDS_MAY));
		comboMonth.AddString(App.LoadString(IDS_JUNE));
		comboMonth.AddString(App.LoadString(IDS_JULY));
		comboMonth.AddString(App.LoadString(IDS_AUGUST));
		comboMonth.AddString(App.LoadString(IDS_SEPTEMBER));
		comboMonth.AddString(App.LoadString(IDS_OCTOBER));
		comboMonth.AddString(App.LoadString(IDS_NOVEMBER));
		comboMonth.AddString(App.LoadString(IDS_DECEMBER));

		int nThisYear = IW::FileTime::Now().GetYear();

		for(int i = 0; i < 10; i++)
		{
			int year = nThisYear - i;
			int index = comboYear.AddString(IW::IToStr(year));
			comboYear.SetItemData(index, year);
		}
}

class CSearchAdvancedDlg : public CDialogImpl<CSearchAdvancedDlg>
{
protected:
	CString m_strQuery;
	IW::CSearchNodeList m_children;

public:
	CSearchAdvancedDlg(const CString &strQuery) : m_strQuery(strQuery)
	{
	}

	enum { IDD = IDD_SEARCH_ADVANCED };

	BEGIN_MSG_MAP(CSearchAdvancedDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
		COMMAND_ID_HANDLER(IDHELP, OnHelp)
	END_MSG_MAP()

	bool ParseFromString(const CString &str)
	{
		return m_children.ParseFromString(str);
	}	

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		CenterWindow(GetParent());

		ParseFromString(m_strQuery);

		CString strNOT, strAND, strOR;
		m_children.Format(strNOT, strAND, strOR);

		SetDlgItemText(IDC_NOT, strNOT);
		SetDlgItemText(IDC_AND, strAND);
		SetDlgItemText(IDC_OR, strOR);

		return TRUE;
	}

	LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		if (IDOK == wID)
		{
			CString strNot, strAnd, strOr;

			GetDlgItemText(IDC_NOT, strNot);
			GetDlgItemText(IDC_AND, strAnd);
			GetDlgItemText(IDC_OR, strOr);

			m_children.ParseFromString(strNot, strAnd, strOr);
		}

		EndDialog(wID);
		return 0;
	}

	CString GetSearchQuery()
	{
		m_children.Format(m_strQuery);
		return m_strQuery;
	}

	LRESULT OnHelp(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		App.InvokeHelp(IW::GetMainWindow(), HELP_SEARCH);
		return 0;
	}
};


class CSearchPanelDlg :
	public CDialogImpl<CSearchPanelDlg>
{
public:

	typedef CSearchPanelDlg ThisClass;

	enum { IDD = IDD_SEARCH_PANEL };

	// Set by the pane before the dialog is created; the panel drives the search itself.
	FolderState *_pFolder;

	CSearchPanelDlg() : _pFolder(nullptr)
	{
	}

	BEGIN_MSG_MAP(ThisClass)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_SIZE, OnSize)

		COMMAND_ID_HANDLER(ID_SEARCHMYPICS, OnSearch)
		COMMAND_ID_HANDLER(ID_SEARCHCURRENT, OnSearch)
		COMMAND_ID_HANDLER(ID_SEARCH_STOP, OnStop)
		COMMAND_ID_HANDLER(ID_VIEW_REFRESHX, OnStop)
		COMMAND_ID_HANDLER(IDC_SEARCH_ADV, OnAdvanced)

	ALT_MSG_MAP(1)

		COMMAND_ID_HANDLER(ID_SEARCHMYPICS, OnSearch)
		COMMAND_ID_HANDLER(ID_SEARCHCURRENT, OnSearch)
		COMMAND_ID_HANDLER(ID_SEARCH_STOP, OnStop)
		COMMAND_ID_HANDLER(ID_VIEW_REFRESHX, OnStop)
	END_MSG_MAP()

	bool PreTranslateMessage(MSG* pMsg)
	{
		if (m_hWnd == nullptr || !IsWindowVisible())
			return false;

		// Only claim keys while focus is inside the panel, or it eats the frame's accelerators.
		HWND hWndFocus = ::GetFocus();

		if (hWndFocus != m_hWnd && !IsChild(hWndFocus))
			return false;

		if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN)
		{
			Search(Search::Current);
			return true;
		}

		return IsDialogMessage(pMsg) != FALSE;
	}

	LRESULT OnSearch(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		Search(ID_SEARCHMYPICS == wID ? Search::MyPictures : Search::Current);
		return 0;
	}

	LRESULT OnStop(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		if (_pFolder == nullptr)
			return 0;

		// Stop means stop. It used to share Refresh's handler, which re-reads
		// the folder and throws away everything the search had found.
		if (wID == ID_SEARCH_STOP)
			_pFolder->StopLoading();
		else
			_pFolder->RefreshFolder();

		return 0;
	}

	void Search(Search::Type type)
	{
		if (_pFolder) _pFolder->Search(type, PopulateSearchSpec());
	}

	LRESULT OnAdvanced(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		CString str;
		GetDlgItemText(IDC_SEARCH_TEXT, str);

		CSearchAdvancedDlg dlg(str);

		if (IDOK == dlg.DoModal())
		{
			SetDlgItemText(IDC_SEARCH_TEXT, dlg.GetSearchQuery());
		}

		return 1;
	}

	// True when the panel holds enough to search on -- what the Search menu items enable against.
	bool HasSpec() const
	{
		if (m_hWnd == nullptr)
			return false;

		CString str;
		GetDlgItemText(IDC_SEARCH_TEXT, str);

		return !str.IsEmpty() ||
			BST_CHECKED == IsDlgButtonChecked(IDC_SEARCH_ONLYSHOWIMAGES) ||
			BST_CHECKED == IsDlgButtonChecked(IDC_SEARCH_SIZE) ||
			BST_CHECKED == IsDlgButtonChecked(IDC_SEARCH_DATE_MODIFIED) ||
			BST_CHECKED == IsDlgButtonChecked(IDC_SEARCH_DATE_TAKEN);
	}

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		const SearchSettings &s = App.Settings.Search;

		CComboBox comboSize = GetDlgItem(IDC_SIZE_TYPE);
		AddListText(comboSize, IDS_AT_LEAST);
		AddListText(comboSize, IDS_AT_MOST);
		comboSize.SetCurSel(s.SizeOption);

		CComboBox comboMonth = GetDlgItem(IDC_MONTH);
		CComboBox comboYear = GetDlgItem(IDC_YEAR);

		PopulateSearchByDateCombos(comboMonth, comboYear);
		
		comboMonth.SetCurSel(s.Month);
		comboYear.SetCurSel(s.Year);

		CheckDlgButton(IDC_SEARCH_ONLYSHOWIMAGES, s.OnlyShowImages ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(IDC_SEARCH_SIZE, s.Size ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(IDC_SEARCH_DATE_MODIFIED, s.DateModified ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(IDC_SEARCH_DATE_TAKEN, s.DateTaken ? BST_CHECKED : BST_UNCHECKED);
		SetDlgItemText(IDC_SEARCH_TEXT, s.Text);
		SetDlgItemText(IDC_SIZE_KB, IW::IToStr(s.SizeKB));
		SetDlgItemText(IDC_SEARCH_DATE_DAY_COUNT, IW::IToStr(s.NumberOfDays));		

		return 0;
	}

	Search::Spec PopulateSearchSpec()
	{
		CString strText;
		GetDlgItemText(IDC_SEARCH_TEXT, strText);

		bool bOnlyShowImages = BST_CHECKED == IsDlgButtonChecked( IDC_SEARCH_ONLYSHOWIMAGES); 
		bool bSize = BST_CHECKED == IsDlgButtonChecked( IDC_SEARCH_SIZE);
		bool bDateModified = BST_CHECKED == IsDlgButtonChecked( IDC_SEARCH_DATE_MODIFIED );
		bool bDateTaken = BST_CHECKED == IsDlgButtonChecked( IDC_SEARCH_DATE_TAKEN );

		int nSizeOption = (int)SendDlgItemMessage(IDC_SIZE_TYPE, CB_GETCURSEL, 0, 0L);
		int nSizeKB = GetDlgItemInt(IDC_SIZE_KB);
		int nNumberOfDays = GetDlgItemInt(IDC_SEARCH_DATE_DAY_COUNT);
		int nMonth = (int)SendDlgItemMessage(IDC_MONTH, CB_GETCURSEL, 0, 0L);
		int nYear = GetDlgItemInt(IDC_YEAR);

		Search::Spec spec(
			strText, 
			bOnlyShowImages,
			bSize,
			nSizeOption, 
			nSizeKB, 
			bDateModified, 
			nNumberOfDays, 
			bDateTaken, 
			nMonth, 
			nYear);

		SearchSettings &s = App.Settings.Search;
		s.Text = strText;
		s.OnlyShowImages = bOnlyShowImages;
		s.Size = bSize;
		s.DateModified = bDateModified;
		s.DateTaken = bDateTaken;
		s.SizeOption = nSizeOption;
		s.SizeKB = nSizeKB;
		s.NumberOfDays = nNumberOfDays;
		s.Month = nMonth;

		// The year combo's selection index, not the year the spec searches on.
		s.Year = (int)SendDlgItemMessage(IDC_YEAR, CB_GETCURSEL, 0, 0L);

		return spec;
	}

	

	void AddListText(CComboBox &combo, int nIDString)
	{
		CString str;
		str.LoadString(nIDString);
		
		combo.AddString(str);
	}


	void EnableDlgItem(UINT nId, bool bEnable)
	{
		HWND hwnd = GetDlgItem(nId);
		::EnableWindow(hwnd, bEnable);
	}

	LRESULT OnSize(UINT, WPARAM, LPARAM lParam, BOOL& bHandled)
	{
		Layout(LOWORD(lParam), true);
		bHandled = FALSE;
		return 0;
	}

	// Positions every control for the width the pane actually has, and returns
	// the height it needs. The template only says which controls exist:
	// CDialogScroll stretches the dialog window and leaves each control at its
	// design width with a band of dead space beside it.
	//
	// Every row here is one line tall whatever the width, so the height does not
	// depend on the width and nobody has to be told when it changes.
	int Layout(int cx, bool bApply)
	{
		CClientDC dc(m_hWnd);
		const HFONT hFontOld = dc.SelectFont(GetFont());

		TEXTMETRIC tm;
		dc.GetTextMetrics(&tm);

		const auto measure = [&](int id)
		{
			CString str;
			GetDlgItemText(id, str);

			CSize size;
			dc.GetTextExtent(str, str.GetLength(), &size);
			return static_cast<int>(size.cx);
		};

		const int pad = 6;
		const int cyText = tm.tmHeight;
		const int cyRow = IW::Max(cyText + 8, 22);
		const int cyButton = IW::Max(cyText + 10, 24);
		const int cyCheck = IW::Max(cyText + 2, 14);

		const int left = pad;
		const int width = IW::Max(80, cx - pad * 2);
		const int indent = 14;

		HDWP hdwp = bApply ? ::BeginDeferWindowPos(20) : nullptr;

		int y = pad;

		const auto place = [&](int id, int x, int yItem, int cxItem, int cyItem)
		{
			if (!bApply || hdwp == nullptr)
				return;

			const HWND hWnd = GetDlgItem(id);

			if (hWnd != nullptr)
				hdwp = ::DeferWindowPos(hdwp, hWnd, nullptr, x, yItem, cxItem, cyItem,
				                        SWP_NOZORDER | SWP_NOACTIVATE);
		};

		const auto fullRow = [&](int id, int cyItem)
		{
			place(id, left, y, width, cyItem);
			y += cyItem + pad;
		};

		const auto checkRow = [&](int id)
		{
			place(id, left, y, width, cyCheck);
			y += cyCheck + 4;
		};

		// The text box and its "advanced" button.
		fullRow(IDC_SEARCH_TITLE_TEXT, cyText);
		{
			const int cxAdv = IW::Max(24, measure(IDC_SEARCH_ADV) + 16);
			place(IDC_SEARCH_TEXT, left, y, width - cxAdv - pad, cyRow);
			place(IDC_SEARCH_ADV, left + width - cxAdv, y, cxAdv, cyRow);
			y += cyRow + pad;
		}

		checkRow(IDC_SEARCH_ONLYSHOWIMAGES);

		// A drop-down's window height is what the list drops into, so it has to
		// ask for far more than the closed control shows.
		checkRow(IDC_SEARCH_SIZE);
		{
			const int cxKb = IW::Max(16, measure(IDC_SEARCH_KB) + 4);
			const int cxRow = width - indent;
			const int cxType = (cxRow - cxKb - pad * 2) / 2;

			place(IDC_SIZE_TYPE, left + indent, y, cxType, cyRow + 120);
			place(IDC_SIZE_KB, left + indent + cxType + pad, y,
			      cxRow - cxType - cxKb - pad * 2, cyRow);
			place(IDC_SEARCH_KB, left + width - cxKb, y + (cyRow - cyText) / 2, cxKb, cyText);
			y += cyRow + pad;
		}

		checkRow(IDC_SEARCH_DATE_MODIFIED);
		{
			const int cxDays = IW::Max(24, measure(IDC_SEARCH_DAYS_LBL) + 4);
			const int cxCount = IW::Min(80, width - indent - cxDays - pad);

			place(IDC_SEARCH_DATE_DAY_COUNT, left + indent, y, cxCount, cyRow);
			place(IDC_SEARCH_DAYS_LBL, left + indent + cxCount + pad, y + (cyRow - cyText) / 2,
			      cxDays, cyText);
			y += cyRow + pad;
		}

		checkRow(IDC_SEARCH_DATE_TAKEN);
		{
			const int cxRow = width - indent;
			const int cxMonth = (cxRow - pad) / 2;

			place(IDC_MONTH, left + indent, y, cxMonth, cyRow + 160);
			place(IDC_YEAR, left + indent + cxMonth + pad, y, cxRow - cxMonth - pad, cyRow + 160);
			y += cyRow + pad;
		}

		y += pad;
		fullRow(ID_SEARCHCURRENT, cyButton);
		fullRow(ID_SEARCHMYPICS, cyButton);

		{
			const int cxHalf = (width - pad) / 2;
			place(ID_SEARCH_STOP, left, y, cxHalf, cyButton);
			place(ID_VIEW_REFRESHX, left + cxHalf + pad, y, width - cxHalf - pad, cyButton);
			y += cyButton + pad;
		}

		dc.SelectFont(hFontOld);

		if (hdwp != nullptr)
			::EndDeferWindowPos(hdwp);

		if (bApply)
			Invalidate();

		return y + pad;
	}
};

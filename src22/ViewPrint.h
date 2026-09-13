// ImageWalker by Zac Walker
//
// Purpose: Print mode: the page preview, the print panel and the printer
//          plumbing.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "ViewPrintFolder.h"
#include "ViewSplitter.h"
#include "iw/printrange.h"

class PrintView;

///////////////////////////////////////////////////////////////////////
// The page preview.

class PrintCanvas : public CWindowImpl<PrintCanvas>
{
public:

	DECLARE_WND_CLASS_EX(_T("IWPrintCanvas"), CS_HREDRAW | CS_VREDRAW, NULL)

	enum { m_cxOffset = 10, m_cyOffset = 10 };

	CPrintFolder &_folder;

	explicit PrintCanvas(CPrintFolder &folder) : _folder(folder)
	{
	}

	BEGIN_MSG_MAP(PrintCanvas)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
	END_MSG_MAP()

	LRESULT OnEraseBackground(UINT, WPARAM, LPARAM, BOOL&) { return 1; }

	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&)
	{
		CPaintDC dcPaint(m_hWnd);

		CRect rcClient;
		GetClientRect(rcClient);	

		if (rcClient.IsRectEmpty())
			return 0;

		CMemoryDC dc(dcPaint, rcClient);

		CRect rcArea = rcClient;
		rcArea.InflateRect(-m_cxOffset, -m_cyOffset);		

		if (rcArea.left > rcArea.right) rcArea.right = rcArea.left;
		if (rcArea.top > rcArea.bottom) rcArea.bottom = rcArea.top;			

		CRect rc;
		_folder.GetPageRect(rcArea, &rc);

		CRgn rgn1, rgn2;
		rgn1.CreateRectRgnIndirect(&rc);
		rgn2.CreateRectRgnIndirect(&rcClient);
		rgn2.CombineRgn(rgn1, RGN_DIFF);

		dc.SelectClipRgn(rgn2);
		dc.FillSolidRect(&rcClient, IW::Style::Color::Window);

		dc.SelectClipRgn(NULL);
		dc.FillRect(&rc, (HBRUSH)::GetStockObject(WHITE_BRUSH));

		const int nSavedDC = dc.SaveDC();					
		_folder.DoPaint((HDC)dc, rc, _folder.m_nCurPage);
		dc.RestoreDC(nSavedDC);

		return 0;
	}
};

///////////////////////////////////////////////////////////////////////
// Every print and contact sheet setting, in one panel.

class CPrintPanelDlg :
	public CDialogImpl<CPrintPanelDlg>,
	public IW::CPropertyDlgImpl<CPrintPanelDlg>
{
public:

	typedef CPrintPanelDlg ThisClass;
	typedef IW::CPropertyDlgImpl<CPrintPanelDlg> PropertyBase;

	enum { IDD = IDD_PRINT_PANEL };

	PrintView *_pView;

	// Set while the panel writes to its own controls, so the change
	// notifications that causes do not read half-updated state back out.
	bool _bSetting;

	CPrintPanelDlg() : _pView(nullptr), _bSetting(false)
	{
	}

	BEGIN_MSG_MAP(ThisClass)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_SIZE, OnSize)

		COMMAND_HANDLER(IDC_WIDTH, CBN_SELCHANGE, OnChange)
		COMMAND_HANDLER(IDC_HEIGHT, CBN_SELCHANGE, OnChange)
		COMMAND_HANDLER(IDC_ROTATE_BEST_FIT, CBN_SELCHANGE, OnChange)
		COMMAND_HANDLER(IDC_HEADER, EN_CHANGE, OnChange)
		COMMAND_HANDLER(IDC_FOOTER, EN_CHANGE, OnChange)

		COMMAND_ID_HANDLER(IDC_ONE_PER_PAGE, OnChange)
		COMMAND_ID_HANDLER(IDC_SELECTED, OnChange)
		COMMAND_ID_HANDLER(IDC_PRINT_CENTER, OnChange)
		COMMAND_ID_HANDLER(IDC_PRINT_FRAME, OnChange)
		COMMAND_ID_HANDLER(IDC_PRINT_SHADOW, OnChange)
		COMMAND_ID_HANDLER(IDC_SHOW_HEADERS, OnChange)
		COMMAND_ID_HANDLER(IDC_SHOW_FOOTERS, OnChange)
		COMMAND_ID_HANDLER(IDC_SHOW_PAGENUMBERS, OnChange)

		COMMAND_ID_HANDLER(ID_FILE_PAGE_SETUP, OnButton)
		COMMAND_ID_HANDLER(ID_FILE_PRINT, OnButton)
		COMMAND_ID_HANDLER(ID_FILE_PRINTING_SAVECONTACTSHEET, OnButton)
		COMMAND_ID_HANDLER(ID_PP_BACK, OnButton)
		COMMAND_ID_HANDLER(ID_PP_FORWARD, OnButton)

		// The annotation list edits itself in place; Add/Remove/Move live here.
		CHAIN_MSG_MAP(PropertyBase)

	ALT_MSG_MAP(1)

		COMMAND_ID_HANDLER(ID_FILE_PAGE_SETUP, OnButton)
		COMMAND_ID_HANDLER(ID_FILE_PRINT, OnButton)
		COMMAND_ID_HANDLER(ID_FILE_PRINTING_SAVECONTACTSHEET, OnButton)
		COMMAND_ID_HANDLER(ID_PP_BACK, OnButton)
		COMMAND_ID_HANDLER(ID_PP_FORWARD, OnButton)
	END_MSG_MAP()

	bool PreTranslateMessage(MSG *pMsg)
	{
		if (m_hWnd == nullptr || !IsWindowVisible())
			return false;

		const HWND hWndFocus = ::GetFocus();

		if (hWndFocus != m_hWnd && !IsChild(hWndFocus))
			return false;

		return IsDialogMessage(pMsg) != FALSE;
	}

	LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL& bHandled);
	LRESULT OnChange(WORD, WORD, HWND, BOOL&);
	LRESULT OnButton(WORD, WORD wID, HWND, BOOL&);

	// What CPropertyDlgImpl calls after it has edited the list in place.
	void OnChange();

	// Positions every control for the width the pane actually has and returns
	// the height it needs. The template only says which controls exist.
	int Layout(int cx, bool bApply);

	void ReadFrom(CPrintFolder &settings);
	void WriteTo(CPrintFolder &settings);
	void SetInfo(const CString &str);
};

///////////////////////////////////////////////////////////////////////
// The mode: preview on the left, every setting on the right.

class PrintView : 
	public CWindowImpl<PrintView>,
	public CSplitter2Impl<PrintView>,
	public ViewBase
{
public:

	typedef PrintView ThisClass;

	DECLARE_WND_CLASS_EX(_T("IWPrintView"), CS_HREDRAW | CS_VREDRAW, NULL)

	Coupling *_pCoupling;
	State &_state;
	CPrintFolder _folder;

	PrintCanvas _canvas;
	IW::CDialogScroll<CPrintPanelDlg> _panel;

	int _nDefaultSplitterPos;

	PrintView(Coupling *pCoupling, State &state) : 
		_pCoupling(pCoupling), 
		_state(state),
		_folder(state),
		_canvas(_folder),
		_nDefaultSplitterPos(App.Settings.PrintSplitterPos)
	{
	}

	~PrintView()
	{
		ATLTRACE(_T("Delete PrintView\n"));
	}

	BEGIN_MSG_MAP(PrintView)

		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		MESSAGE_HANDLER(WM_SIZE, OnSize)

		CHAIN_MSG_MAP(CSplitter2Impl<PrintView>)
		CHAIN_MSG_MAP_ALT_MEMBER(_panel, 1)

	END_MSG_MAP()

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL& bHandled)
	{
		_panel.GetDialog()._pView = this;

		// The sunken border round each pane is 2.2's, the same as the browse
		// panes either side of the splitter.
		_canvas.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
		               WS_EX_CLIENTEDGE);
		_panel.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
		              WS_EX_CLIENTEDGE);

		// The scroller scrolls the template's own height, which is not the height
		// the panel lays itself out to.
		_panel._rectClient.bottom = _panel._rectClient.top + _panel.GetDialog().Layout(200, false);

		SetSplitterPanes(_canvas, _panel);
		SetProportionalPos(_nDefaultSplitterPos);

		UpdatePrintPreview();
		_state.Folder.ChangedDelegates.Bind(this, &ThisClass::OnFolderChanged);

		bHandled = FALSE;
		return 0;
	}

	// The panel stacks its rows differently at different widths, so how far it
	// scrolls is not known until it has laid itself out.
	void PanelHeightChanged(int cy)
	{
		if (_panel.m_hWnd == nullptr || _panel._rectClient.Height() == cy)
			return;

		_panel._rectClient.bottom = _panel._rectClient.top + cy;
		_panel.DoSize();
	}

	LRESULT OnEraseBackground(UINT, WPARAM, LPARAM, BOOL&) { return 1; }

	LRESULT OnSize(UINT, WPARAM wParam, LPARAM, BOOL& bHandled)
	{
		if (wParam != SIZE_MINIMIZED)
			SetSplitterRect();

		bHandled = FALSE;
		return 1;
	}

	bool InvokeCommand(DWORD id) override
	{
		switch (id)
		{
		case ID_FILE_PRINT: Print(); return true;
		case ID_FILE_PAGE_SETUP: PageSetup(); return true;

		case ID_FILE_PRINTING_SAVECONTACTSHEET:
			{
				CToolContactSheet tool(_folder, _state);
				tool.DoModal();
			}
			return true;

		case ID_PP_BACK:
			if (_folder.m_nCurPage > _folder.m_nMinPage && _folder.m_nCurPage != 0)
			{
				_folder.m_nCurPage -= 1;
				UpdatePrintPreview();
			}
			return true;

		case ID_PP_FORWARD:
			if (_folder.m_nCurPage != _folder.m_nMaxPage)
			{
				_folder.m_nCurPage++;
				UpdatePrintPreview();
			}
			return true;

		case ID_OK:
			UpdatePrintPreview();
			return true;
		}

		return false;
	}

	bool GetCommandState(DWORD id, bool &bEnabled, bool &bChecked) override
	{
		switch (id)
		{
		case ID_FILE_PRINT:
		case ID_FILE_PAGE_SETUP:
		case ID_FILE_PRINTING_SAVECONTACTSHEET:
		case ID_OK:
			return true;

		case ID_PP_BACK:
			bEnabled = _folder.m_nCurPage > _folder.m_nMinPage;
			return true;

		case ID_PP_FORWARD:
			bEnabled = _folder.m_nCurPage < _folder.m_nMaxPage;
			return true;
		}

		return false;
	}

	void Print()
	{
		if(!_folder.m_bHasPrinter)
		{
			IW::CMessageBoxIndirect mb;
			mb.Show(IDS_PRINTFAILED);
			return;
		}

		CPrintDialog dlg(FALSE);
		dlg.m_pd.hDevMode = _folder.m_devmode.CopyToHDEVMODE();
		dlg.m_pd.hDevNames = _folder.m_printer.CopyToHDEVNAMES();
		IW::SetPrintDialogPageRange(dlg.m_pd, _folder.m_nMaxPage);
		dlg.m_pd.Flags &= ~PD_NOPAGENUMS;

		if (dlg.DoModal() == IDOK)
		{
			_folder.m_devmode.CopyFromHDEVMODE(dlg.m_pd.hDevMode);
			_folder.m_printer.ClosePrinter();
			_folder.m_printer.OpenPrinter(dlg.m_pd.hDevNames, _folder.m_devmode.m_pDevMode);

			CPrintJob job;
			unsigned long nMin = 0;
			unsigned long nMax = 0;

			CProgressDlg pd(IDD_PROGRESS_ADVANCED);
			pd.Create(IW::GetMainWindow(), App.LoadString(IDS_PRINTING));

			// Reset printing
			_folder.CalcLayout();
			_folder.m_bPrinting = true;
			_folder._pStatus = &pd;

			if (IW::GetPrintJobPageRange(dlg.m_pd, _folder.m_nMaxPage, nMin, nMax))
				job.StartPrintJob(false,
				_folder.m_printer,
				_folder.m_devmode.m_pDevMode,
				&_folder,
				App.LoadString(IDS_PRINT_JOB_NAME),
				nMin, nMax);

			_folder._pStatus = IW::CNullStatus::Instance;
			_folder.m_bPrinting = false;
		}

		GlobalFree(dlg.m_pd.hDevMode);
		GlobalFree(dlg.m_pd.hDevNames);

		UpdatePrintPreview();
	}

	void PageSetup()
	{
		if(!_folder.m_bHasPrinter)
		{
			IW::CMessageBoxIndirect mb;
			mb.Show(IDS_PRINTFAILED);
			return;
		}

		CPageSetupDialog dlg(PSD_INTHOUSANDTHSOFINCHES | PSD_MARGINS | PSD_INWININIINTLMEASURE);

		_folder.m_devmode.m_pDevMode->dmOrientation = _folder.m_bPrintLandscape ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;

		dlg.m_psd.hDevMode = _folder.m_devmode.CopyToHDEVMODE();
		dlg.m_psd.hDevNames = _folder.m_printer.CopyToHDEVNAMES();
		dlg.m_psd.rtMargin = _folder.m_rcMargin;

		if (dlg.DoModal() == IDOK)
		{
			_folder.m_devmode.CopyFromHDEVMODE(dlg.m_psd.hDevMode);
			_folder.m_printer.ClosePrinter();
			_folder.m_printer.OpenPrinter(dlg.m_psd.hDevNames, _folder.m_devmode.m_pDevMode);

			_folder.m_rcMargin = dlg.m_psd.rtMargin;
			_folder.m_bPrintLandscape = _folder.m_devmode.m_pDevMode->dmOrientation == DMORIENT_LANDSCAPE;
		}

		GlobalFree(dlg.m_psd.hDevMode);
		GlobalFree(dlg.m_psd.hDevNames);

		UpdatePrintPreview();
	}

	// Runs on every panel change and on every folder change.
	void UpdatePrintPreview();

	void LoadDefaultSettings()
	{
		_folder.LoadSettings(App.Settings.Print);
		_nDefaultSplitterPos = App.Settings.PrintSplitterPos;
	}

	void SaveDefaultSettings()
	{
		App.Settings.Print = _folder.SaveSettings();
		App.Settings.PrintSplitterPos = m_hWnd ? GetProportionalPos() : _nDefaultSplitterPos;
	}

	HWND GetImageWindow() override
	{
		return _canvas;
	}

	HWND Activate(HWND hWndParent) override
	{
		if (m_hWnd == 0)
			Create(hWndParent, rcDefault, NULL, IW_WS_CHILD, 0);

		_panel.GetDialog().ReadFrom(_folder);
		UpdatePrintPreview();
		ShowWindow(SW_SHOW);
		return m_hWnd;
	}

	void Deactivate() override
	{
		ShowWindow(SW_HIDE);
	}

	bool CanEditImages() const override
	{
		return true;
	}

	bool CanShowToolbar(DWORD id) override
	{
		return id == IDC_PRINT ||
			id == IDC_LOGO ||
			id == IDC_COMMAND_BAR;
	}

	BOOL PreTranslateMessage(MSG* pMsg) override
	{
		return _panel.GetDialog().PreTranslateMessage(pMsg) ? TRUE : FALSE;
	}

	void OnTimer() override
	{		
	}

	void OnOptionsChanged() override
	{
		UpdatePrintPreview();
	}

	void OnFolderChanged()
	{
		UpdatePrintPreview();		
	}
};

///////////////////////////////////////////////////////////////////////
// CPrintPanelDlg

inline LRESULT CPrintPanelDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&)
{
	{
		IW::ScopeLockedBool lockSetting(_bSetting);

		CComboBox comboRotate = GetDlgItem(IDC_ROTATE_BEST_FIT);
		comboRotate.AddString(_T("Don't rotate"));
		comboRotate.AddString(_T("Rotate left"));
		comboRotate.AddString(_T("Rotate right"));

		CComboBox comboWidth = GetDlgItem(IDC_WIDTH);
		CComboBox comboHeight = GetDlgItem(IDC_HEIGHT);

		for (int i = 1; i <= 16; i++)
		{
			IW::SetItem(comboWidth, i);
			IW::SetItem(comboHeight, i);
		}

		if (_pView) OnInitProperties(&_pView->_folder.m_annotations);
	}

	if (_pView) ReadFrom(_pView->_folder);

	return 0;
}

// Every control is placed here rather than by the template: CDialogScroll
// stretches the dialog window to the pane and leaves each control at its
// design width, with a band of dead space beside it.
inline int CPrintPanelDlg::Layout(int cx, bool bApply)
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

	// pad is the horizontal gap; the vertical rhythm is padY between rows and
	// padSection above each section title.
	const int pad = 6;
	const int padY = 10;
	const int padSection = 18;
	const int cyText = tm.tmHeight;
	const int cyRow = IW::Max(cyText + 10, 24);
	const int cyButton = IW::Max(cyText + 14, 28);
	const int cyCheck = IW::Max(cyText + 6, 18);

	const int left = pad;
	const int width = IW::Max(60, cx - pad * 2);

	int cxLabel = 0;
	cxLabel = IW::Max(cxLabel, measure(IDC_PRINT_COLUMNS_LBL));
	cxLabel = IW::Max(cxLabel, measure(IDC_PRINT_ROWS_LBL));
	cxLabel = IW::Max(cxLabel, measure(IDC_PRINT_ROTATE_LBL));
	cxLabel = IW::Min(cxLabel + pad, width / 2);

	HDWP hdwp = bApply ? ::BeginDeferWindowPos(40) : nullptr;

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
		y += cyItem + padY;
	};

	// A title opens a section, so the air belongs above it, not below.
	const auto titleRow = [&](int id)
	{
		y += padSection - padY;
		place(id, left, y, width, cyText);
		y += cyText + padY;
	};

	// A drop-down's window height is what the list drops into, so it has to ask
	// for far more than the closed control shows.
	const auto comboRow = [&](int idLabel, int idCombo)
	{
		const int dy = (cyRow - cyText) / 2;
		place(idLabel, left, y + dy, cxLabel - pad, cyText);
		place(idCombo, left + cxLabel, y, width - cxLabel, cyRow + 160);
		y += cyRow + padY / 2;
	};

	const auto checkRow = [&](int id)
	{
		place(id, left, y, width, cyCheck);
		y += cyCheck + padY / 2;
	};

	// Buttons share a row while their captions still fit; below that they take
	// fewer per row rather than being clipped to "Move u".
	const auto buttonRow = [&](int id1, int id2, int id3, int id4)
	{
		const int ids[] = {id1, id2, id3, id4};
		const int n = id4 != 0 ? 4 : id3 != 0 ? 3 : 2;

		int cxCaption = 0;

		for (int i = 0; i < n; i++)
			cxCaption = IW::Max(cxCaption, measure(ids[i]));

		int nPerRow = n;

		while (nPerRow > 1 && (width - pad * (nPerRow - 1)) / nPerRow < cxCaption + 10)
			nPerRow--;

		for (int i = 0; i < n; i += nPerRow)
		{
			const int nThis = IW::Min(nPerRow, n - i);
			const int cxOne = (width - pad * (nThis - 1)) / nThis;

			for (int j = 0; j < nThis; j++)
			{
				const int x = left + (cxOne + pad) * j;
				place(ids[i + j], x, y, j == nThis - 1 ? width - (cxOne + pad) * j : cxOne, cyButton);
			}

			y += cyButton + padY;
		}
	};

	titleRow(IDC_PRINT_TITLE_LAYOUT);
	comboRow(IDC_PRINT_COLUMNS_LBL, IDC_WIDTH);
	comboRow(IDC_PRINT_ROWS_LBL, IDC_HEIGHT);
	comboRow(IDC_PRINT_ROTATE_LBL, IDC_ROTATE_BEST_FIT);

	y += padSection - padY;
	checkRow(IDC_ONE_PER_PAGE);
	checkRow(IDC_SELECTED);
	checkRow(IDC_PRINT_CENTER);
	checkRow(IDC_PRINT_FRAME);
	checkRow(IDC_PRINT_SHADOW);

	titleRow(IDC_PRINT_TITLE_HEADER);
	checkRow(IDC_SHOW_HEADERS);
	fullRow(IDC_HEADER, cyRow);
	checkRow(IDC_SHOW_FOOTERS);
	fullRow(IDC_FOOTER, cyRow);
	checkRow(IDC_SHOW_PAGENUMBERS);

	titleRow(IDC_PRINT_TITLE_ANNOTATIONS);
	fullRow(IDC_LIST, cyRow * 5);
	buttonRow(IDC_ADD, IDC_REMOVE, IDC_MOVE_UP, IDC_MOVE_DOWN);

	y += padSection - padY;
	fullRow(IDC_PRINT_INFO, cyText);
	buttonRow(ID_PP_BACK, ID_PP_FORWARD, 0, 0);
	fullRow(ID_FILE_PAGE_SETUP, cyButton);
	buttonRow(ID_FILE_PRINT, ID_FILE_PRINTING_SAVECONTACTSHEET, 0, 0);

	// Only now: buttonRow measures its captions as it goes, and it has to do
	// that in the font the panel is drawn in.
	dc.SelectFont(hFontOld);

	if (hdwp != nullptr)
		::EndDeferWindowPos(hdwp);

	if (bApply)
		Invalidate();

	return y + padY;
}

inline LRESULT CPrintPanelDlg::OnSize(UINT, WPARAM, LPARAM lParam, BOOL& bHandled)
{
	const int cy = Layout(LOWORD(lParam), true);

	if (_pView != nullptr)
		_pView->PanelHeightChanged(cy);

	bHandled = FALSE;
	return 0;
}

inline void CPrintPanelDlg::ReadFrom(CPrintFolder &settings)
{
	if (m_hWnd == nullptr)
		return;

	IW::ScopeLockedBool lockSetting(_bSetting);

	CComboBox(GetDlgItem(IDC_WIDTH)).SetCurSel(settings._sizeRowsColumns.cx - 1);
	CComboBox(GetDlgItem(IDC_HEIGHT)).SetCurSel(settings._sizeRowsColumns.cy - 1);
	CComboBox(GetDlgItem(IDC_ROTATE_BEST_FIT)).SetCurSel(settings.m_nPrintRotateBest);

	CheckDlgButton(IDC_ONE_PER_PAGE, settings.m_bPrintOnePerPage ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_SELECTED, settings.m_bPrintSelected ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_PRINT_CENTER, settings.m_bCenter ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_PRINT_FRAME, settings.m_bFrame ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_PRINT_SHADOW, settings.m_bShadow ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_SHOW_HEADERS, settings.m_bShowHeader ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_SHOW_FOOTERS, settings.m_bShowFooter ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_SHOW_PAGENUMBERS, settings.m_bShowPageNumbers ? BST_CHECKED : BST_UNCHECKED);

	SetDlgItemText(IDC_HEADER, settings._strHeader);
	SetDlgItemText(IDC_FOOTER, settings._strFooter);

	OnRevertProperties(&settings.m_annotations);
}

inline void CPrintPanelDlg::WriteTo(CPrintFolder &settings)
{
	if (m_hWnd == nullptr)
		return;

	const int nColumns = CComboBox(GetDlgItem(IDC_WIDTH)).GetCurSel();
	const int nRows = CComboBox(GetDlgItem(IDC_HEIGHT)).GetCurSel();

	if (nColumns >= 0) settings._sizeRowsColumns.cx = nColumns + 1;
	if (nRows >= 0) settings._sizeRowsColumns.cy = nRows + 1;

	const int nRotate = CComboBox(GetDlgItem(IDC_ROTATE_BEST_FIT)).GetCurSel();
	if (nRotate >= 0) settings.m_nPrintRotateBest = nRotate;

	settings.m_bPrintOnePerPage = BST_CHECKED == IsDlgButtonChecked(IDC_ONE_PER_PAGE);
	settings.m_bPrintSelected = BST_CHECKED == IsDlgButtonChecked(IDC_SELECTED);
	settings.m_bCenter = BST_CHECKED == IsDlgButtonChecked(IDC_PRINT_CENTER);
	settings.m_bFrame = BST_CHECKED == IsDlgButtonChecked(IDC_PRINT_FRAME);
	settings.m_bShadow = BST_CHECKED == IsDlgButtonChecked(IDC_PRINT_SHADOW);
	settings.m_bShowHeader = BST_CHECKED == IsDlgButtonChecked(IDC_SHOW_HEADERS);
	settings.m_bShowFooter = BST_CHECKED == IsDlgButtonChecked(IDC_SHOW_FOOTERS);
	settings.m_bShowPageNumbers = BST_CHECKED == IsDlgButtonChecked(IDC_SHOW_PAGENUMBERS);

	GetDlgItemText(IDC_HEADER, settings._strHeader);
	GetDlgItemText(IDC_FOOTER, settings._strFooter);
}

inline void CPrintPanelDlg::SetInfo(const CString &str)
{
	if (m_hWnd) SetDlgItemText(IDC_PRINT_INFO, str);
}

inline LRESULT CPrintPanelDlg::OnChange(WORD, WORD, HWND, BOOL&)
{
	if (_bSetting || _pView == nullptr)
		return 0;

	WriteTo(_pView->_folder);
	_pView->UpdatePrintPreview();

	return 0;
}

// The annotation list edits itself in place, so an Add/Remove/Move is a change
// to the settings like any other.
inline void CPrintPanelDlg::OnChange()
{
	if (_bSetting || _pView == nullptr)
		return;

	OnApplyProperties(&_pView->_folder.m_annotations);
	_pView->UpdatePrintPreview();
}

inline LRESULT CPrintPanelDlg::OnButton(WORD, WORD wID, HWND, BOOL&)
{
	if (_pView) _pView->InvokeCommand(wID);
	return 0;
}

inline void PrintView::UpdatePrintPreview()
{
	_folder.CalcLayout();

	if (_canvas.m_hWnd) _canvas.Invalidate();

	CString str;
	str.Format(_T("Page %d of %d"), _folder.m_nCurPage, _folder.m_nMaxPage);
	_panel.GetDialog().SetInfo(str);
}

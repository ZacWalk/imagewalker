// ImageWalker by Zac Walker
//
// Purpose: The resize filter settings dialog.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "ImagingFilter.h"

//class  CFilterResizeDlg : public IW::CSettingsDialogImpl<CFilterResizeDlg>
//class CToolResizePage : public CToolPropertyPage<CToolResizePage>

template<class T>
class  CResizeFields
{
public:
	bool m_bSetting;
	CFilterResize *_pFilter;	

	CResizeFields(CFilterResize *pFilter) : _pFilter(pFilter), m_bSetting(false)
	{
	}

	void EnableDlgItem(UINT nId, bool bEnable)
	{
		T *pT = static_cast<T*>(this);
		pT->GetDlgItem(nId).EnableWindow(bEnable);
	}

	void UpdateResolution()
	{
		T *pT = static_cast<T*>(this);
		EnableDlgItem(IDC_CX, _pFilter->m_nType >= 1);
		EnableDlgItem(IDC_CY, _pFilter->m_nType >= 1);

		CString str;
		str.LoadString((_pFilter->m_nType == 1) ? IDS_PIXELS_PER_INCH : IDS_PIXELS_PER_CM);
		pT->SetDlgItemText(IDC_RES_LBL, str);
	}

	LRESULT OnChangeRes(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& bHandled)
	{
		T *pT = static_cast<T*>(this);

		if (!m_bSetting)
		{
			CString str;
			pT->GetDlgItemText(IDC_CX, str);
			_pFilter->m_dwXPelsPerMeter = _ttol(str);

			pT->GetDlgItemText(IDC_CY, str);
			_pFilter->m_dwYPelsPerMeter = _ttol(str);

			if (_pFilter->m_nType == 1)
			{
				_pFilter->m_dwXPelsPerMeter = IW::InchToMeter(_pFilter->m_dwXPelsPerMeter);
				_pFilter->m_dwYPelsPerMeter = IW::InchToMeter(_pFilter->m_dwYPelsPerMeter);			
			}
			else
			{
				_pFilter->m_dwXPelsPerMeter = IW::CMToMeter(_pFilter->m_dwXPelsPerMeter);
				_pFilter->m_dwYPelsPerMeter = IW::CMToMeter(_pFilter->m_dwYPelsPerMeter);
			}
		}

		return 0;
	}

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		T *pT = static_cast<T*>(this);
		IW::ScopeLockedBool lockSetting(m_bSetting);
		
		CComboBox comboWidth = pT->GetDlgItem(IDC_WIDTH);
		CComboBox comboHeight = pT->GetDlgItem(IDC_HEIGHT);

		int widths[] = { 320, 640, 800, 1024, 1280, 1600, 2048, GetSystemMetrics(SM_CXFULLSCREEN), _pFilter->m_nOriginalWidth, -1 };
		int heights[] = { 320, 640, 800, 1024, 1280, 1600, 2048, GetSystemMetrics(SM_CYFULLSCREEN), _pFilter->m_nOriginalHeight, -1 };

		IW::SetItems(comboWidth, widths, _pFilter->m_nWidth);
		IW::SetItems(comboHeight, heights, _pFilter->m_nHeight);

		// Add Types
		CString str;
		CComboBox combo = pT->GetDlgItem(IDC_TYPE);		
		str.LoadString(IDS_PIXELS); combo.AddString(str);
		str.LoadString(IDS_INCHES); combo.AddString(str);
		str.LoadString(IDS_CENTIMETERS); combo.AddString(str);
		combo.SetCurSel(_pFilter->m_nType);

		CComboBox comboFilter = pT->GetDlgItem(IDC_FILTER);		
		comboFilter.AddString(_T("Optimised"));
		comboFilter.AddString(_T("Box Point"));
		comboFilter.AddString(_T("Box Large"));
		comboFilter.AddString(_T("Box Small"));
		comboFilter.AddString(_T("Triangle"));
		comboFilter.AddString(_T("Hermite"));
		comboFilter.AddString(_T("Hanning"));
		comboFilter.AddString(_T("Hamming"));
		comboFilter.AddString(_T("Blackman"));
		comboFilter.AddString(_T("Gaussian"));
		comboFilter.AddString(_T("Quadratic"));
		comboFilter.AddString(_T("Cubic"));
		comboFilter.AddString(_T("Catrom"));
		comboFilter.AddString(_T("Mitchell"));
		comboFilter.AddString(_T("Lanczos"));
		comboFilter.AddString(_T("Blackman Bessel"));
		comboFilter.AddString(_T("Blackman Sinc"));
		comboFilter.SetCurSel(_pFilter->m_nFilter);

		/*
		if (m_bHasImage)
		{
		SetDlgItemText(IDC_OWIDTH, szWidth);
		SetDlgItemText(IDC_OHEIGHT, szHeight);
		}
		*/

		pT->CheckDlgButton(IDC_KEEP_ASPECT, _pFilter->m_bKeepAspect ? BST_CHECKED : BST_UNCHECKED);
		pT->CheckDlgButton(IDC_SCALE_DOWN, _pFilter->m_bScaleDown ? BST_CHECKED : BST_UNCHECKED);

		DWORD dwPelsPerMeterCX = _pFilter->m_dwXPelsPerMeter;
		DWORD dwPelsPerMeterCY = _pFilter->m_dwYPelsPerMeter;

		if (_pFilter->m_nType == 1)
		{
			dwPelsPerMeterCX = IW::MeterToInch(dwPelsPerMeterCX);
			dwPelsPerMeterCY = IW::MeterToInch(dwPelsPerMeterCY);
		}
		else
		{
			dwPelsPerMeterCX = IW::MeterToCM(dwPelsPerMeterCX);
			dwPelsPerMeterCY = IW::MeterToCM(dwPelsPerMeterCY);
		}

		pT->SetDlgItemInt(IDC_CX, dwPelsPerMeterCX);
		pT->SetDlgItemInt(IDC_CY, dwPelsPerMeterCY);

		OnWidthChange(_pFilter->m_nWidth);
		UpdateResolution();

		bHandled = FALSE;

		return 1;  // Let the system set the focus
	}

	void OnChangeResolution(int nNew, int nOld)
	{
		IW::ScopeLockedBool lockSetting(m_bSetting);
		T *pT = static_cast<T*>(this);

		CString str;
		pT->GetDlgItemText(IDC_CX, str);
		DWORD dwPelsPerMeterCX = _ttol(str);

		pT->GetDlgItemText(IDC_CY, str);
		DWORD dwPelsPerMeterCY = _ttol(str);

		pT->GetDlgItemText(IDC_WIDTH, str);
		DWORD dwWidth = _ttol(str);

		pT->GetDlgItemText(IDC_HEIGHT, str);
		DWORD dwHeight = _ttol(str);		

		// The resolution fields are free text and can be empty. Substituting a
		// value would resize the picture to its own inch count.
		if (dwPelsPerMeterCX == 0 || dwPelsPerMeterCY == 0)
			return;

		if (nOld != 0)
		{
			dwWidth *= dwPelsPerMeterCX;
			dwHeight *= dwPelsPerMeterCY;
		}

		if (nOld == 1)
		{
			dwPelsPerMeterCX = IW::InchToMeter(dwPelsPerMeterCX);
			dwPelsPerMeterCY = IW::InchToMeter(dwPelsPerMeterCY);			
		}
		else
		{
			dwPelsPerMeterCX = IW::CMToMeter(dwPelsPerMeterCX);
			dwPelsPerMeterCY = IW::CMToMeter(dwPelsPerMeterCY);
		}


		_pFilter->m_dwXPelsPerMeter = dwPelsPerMeterCX;
		_pFilter->m_dwYPelsPerMeter = dwPelsPerMeterCY;

		if (nNew == 1)
		{
			dwPelsPerMeterCX = IW::MeterToInch(dwPelsPerMeterCX);
			dwPelsPerMeterCY = IW::MeterToInch(dwPelsPerMeterCY);
		}
		else
		{
			dwPelsPerMeterCX = IW::MeterToCM(dwPelsPerMeterCX);
			dwPelsPerMeterCY = IW::MeterToCM(dwPelsPerMeterCY);
		}

		if (nNew != 0)
		{
			dwWidth /= IW::Max(static_cast<int>(dwPelsPerMeterCX), 1);
			dwHeight /= IW::Max(static_cast<int>(dwPelsPerMeterCY), 1);
		}

		pT->SetDlgItemInt(IDC_CX, dwPelsPerMeterCX);
		pT->SetDlgItemInt(IDC_CY, dwPelsPerMeterCY);

		pT->SetDlgItemInt(IDC_WIDTH, dwWidth);
		pT->SetDlgItemInt(IDC_HEIGHT, dwHeight);

		_pFilter->m_nWidth = dwWidth;
		_pFilter->m_nHeight = dwHeight;
	}


	LRESULT OnChangeFilter(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		if (m_bSetting)
			return 0;

		T *pT = static_cast<T*>(this);
		CComboBox comboFilter = pT->GetDlgItem(IDC_FILTER);
		_pFilter->m_nFilter = comboFilter.GetCurSel();
		pT->OnChange();
		return 0;
	}

	LRESULT OnChangeType(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		if (m_bSetting)
			return 0;

		T *pT = static_cast<T*>(this);
		CComboBox combo = pT->GetDlgItem(IDC_TYPE);
		int nNewType = combo.GetCurSel();

		OnChangeResolution(nNewType, _pFilter->m_nType);

		_pFilter->m_nType = nNewType;
		pT->OnChange();
		UpdateResolution();

		return 0;
	}

	void OnWidthChange(int nWidth)
	{
		T *pT = static_cast<T*>(this);
		_pFilter->m_nWidth = nWidth;		

		if (_pFilter->m_bKeepAspect && _pFilter->m_bHasImage)
		{
			_pFilter->m_nHeight = MulDiv(_pFilter->m_nWidth, _pFilter->m_nOriginalHeight, _pFilter->m_nOriginalWidth);
			IW::ScopeLockedBool lockSetting(m_bSetting);
			pT->SetDlgItemInt(IDC_HEIGHT, _pFilter->m_nHeight);
		}

		pT->OnChange();
	}

	void OnHeightChange(int nHeight)
	{
		T *pT = static_cast<T*>(this);
		_pFilter->m_nHeight = nHeight;

		if (_pFilter->m_bKeepAspect && _pFilter->m_bHasImage)
		{
			_pFilter->m_nWidth = MulDiv(_pFilter->m_nHeight, _pFilter->m_nOriginalWidth, _pFilter->m_nOriginalHeight);
			IW::ScopeLockedBool lockSetting(m_bSetting);
			pT->SetDlgItemInt(IDC_WIDTH, _pFilter->m_nWidth);
		}

		pT->OnChange();
	}


	LRESULT OnWidthChangeSel(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		T *pT = static_cast<T*>(this);
		CComboBox combo = pT->GetDlgItem(IDC_WIDTH);
		int nIndex = combo.GetCurSel();
		if(nIndex != -1)
		{
			CString str;
			combo.GetLBText(nIndex, str);
			OnWidthChange(_ttoi(str));
		}

		return 0;
	}

	LRESULT OnWidthChange(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		if (m_bSetting)
			return 0;

		T *pT = static_cast<T*>(this);
		CString strWidth;
		pT->GetDlgItemText(IDC_WIDTH, strWidth);
		OnWidthChange(_ttoi(strWidth));

		return 0;
	}

	LRESULT OnHeightChangeSel(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		T *pT = static_cast<T*>(this);
		CComboBox combo = pT->GetDlgItem(IDC_HEIGHT);
		int nIndex = combo.GetCurSel();
		if(nIndex != -1)
		{
			CString str;
			combo.GetLBText(nIndex, str);
			OnHeightChange(_ttoi(str));
		}
		return 0;
	}

	LRESULT OnHeightChange(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		if (m_bSetting)
			return 0;

		T *pT = static_cast<T*>(this);
		CString strHeight;		
		pT->GetDlgItemText(IDC_HEIGHT, strHeight);		
		OnHeightChange(_ttoi(strHeight));
		return 0;
	}

	LRESULT OnButtonChange(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		if (m_bSetting)
			return 0;

		T *pT = static_cast<T*>(this);
		_pFilter->m_bKeepAspect = BST_CHECKED == pT->IsDlgButtonChecked(IDC_KEEP_ASPECT);
		_pFilter->m_bScaleDown = BST_CHECKED == pT->IsDlgButtonChecked( IDC_SCALE_DOWN);

		if (_pFilter->m_bKeepAspect && _pFilter->m_bHasImage)
		{
			_pFilter->m_nHeight = MulDiv(_pFilter->m_nWidth, _pFilter->m_nOriginalHeight, _pFilter->m_nOriginalWidth);
			SetDimensions();
		}

		return 0;
	}

	void SetDimensions()
	{
		IW::ScopeLockedBool lockSetting(m_bSetting);
		T *pT = static_cast<T*>(this);
		pT->SetDlgItemInt(IDC_WIDTH, _pFilter->m_nWidth);
		pT->SetDlgItemInt(IDC_HEIGHT, _pFilter->m_nHeight);
	}
};

class  CFilterResizeDlg : 
	public IW::CSettingsDialogImpl<CFilterResizeDlg>, 
	public CResizeFields<CFilterResizeDlg>
{
	typedef IW::CSettingsDialogImpl<CFilterResizeDlg> BaseClass;
	typedef CResizeFields<CFilterResizeDlg> BaseClass2;
public:

	CFilterResizeDlg(CFilterResize *pFilter, const IW::Image &imagePreview) : BaseClass(imagePreview, pFilter), BaseClass2(pFilter)
	{
	}

	enum { IDD = IDD_SCALE };	

	BEGIN_MSG_MAP(CFilterResizeDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_HANDLER(IDC_WIDTH, CBN_EDITCHANGE, OnWidthChange)
		COMMAND_HANDLER(IDC_HEIGHT, CBN_EDITCHANGE, OnHeightChange)
		COMMAND_HANDLER(IDC_WIDTH, CBN_SELCHANGE, OnWidthChangeSel)
		COMMAND_HANDLER(IDC_HEIGHT, CBN_SELCHANGE, OnHeightChangeSel)
		COMMAND_ID_HANDLER(IDC_KEEP_ASPECT, OnButtonChange)
		COMMAND_ID_HANDLER(IDC_SCALE_DOWN, OnButtonChange)
		COMMAND_HANDLER(IDC_TYPE, CBN_SELCHANGE, OnChangeType)
		COMMAND_HANDLER(IDC_FILTER, CBN_SELCHANGE, OnChangeFilter)
		COMMAND_HANDLER(IDC_CX, EN_CHANGE, OnChangeRes)
		COMMAND_HANDLER(IDC_CY, EN_CHANGE, OnChangeRes)
		CHAIN_MSG_MAP(BaseClass)
	END_MSG_MAP()		

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		ResizeAddItem(IDC_TYPE, eRight | eLeft);	
		ResizeAddItem(IDC_FILTER, eRight | eLeft);	
		ResizeAddItem(IDC_KEEP_ASPECT, eRight | eLeft);
		ResizeAddItem(IDC_SCALE_DOWN, eRight | eLeft);

		return BaseClass2::OnInitDialog(uMsg, wParam, lParam, bHandled);
	}
};


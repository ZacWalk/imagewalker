#include "stdafx.h"
#include "FileJpeg.h"
#include "FileFormatAny.h"

HWND CLoadAny::CreateSettingsWindow(const CString& strType, HWND hWndParent, IW::CodecSettings& settings)
{
	if (GetKey(strType) != CLoadJpg::_GetKey()) return nullptr;
	auto pPanel = new CJpegSettingsPanel(settings);
	const HWND hWnd = pPanel->Create(hWndParent);

	// OnFinalMessage is what deletes it, and it never runs if there is no window.
	if (hWnd == nullptr)
		delete pPanel;

	return hWnd;
}

LRESULT CJpegSettingsPanel::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	IW::ScopeLockedBool lockSetting(_bSetting);

	const HWND hwndTrack = GetDlgItem(IDC_QUALITY_SLIDER);
	::SendMessage(hwndTrack, TBM_SETRANGE, TRUE, MAKELONG(1, 100));
	::SendMessage(hwndTrack, TBM_SETPAGESIZE, 0, 10);
	::SendMessage(hwndTrack, TBM_SETLINESIZE, 0, 1);
	::SendMessage(hwndTrack, TBM_SETTICFREQ, 10, 0);
	::SendMessage(hwndTrack, TBM_SETPOS, TRUE, _settings.JpegQuality);

	SetDlgItemInt(IDC_QUALITY, _settings.JpegQuality, FALSE);
	CheckDlgButton(IDC_OPTIMIZE, _settings.JpegOptimize ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_PROGRESSIVE, _settings.JpegProgressive ? BST_CHECKED : BST_UNCHECKED);

	return TRUE;
}

LRESULT CJpegSettingsPanel::OnHScroll(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
	const auto hwndSlider = (HWND)lParam;

	if (GetDlgItem(IDC_QUALITY_SLIDER) != hwndSlider)
		return 0;

	IW::ScopeLockedBool lockSetting(_bSetting);

	_settings.JpegQuality = static_cast<long>(::SendMessage(hwndSlider, TBM_GETPOS, 0, 0));
	SetDlgItemInt(IDC_QUALITY, _settings.JpegQuality, FALSE);

	return 0;
}

LRESULT CJpegSettingsPanel::OnQualityChange(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	if (_bSetting)
		return 0;

	IW::ScopeLockedBool lockSetting(_bSetting);

	CString str;
	GetDlgItemText(IDC_QUALITY, str);

	// The edit is not rewritten here: half-typed numbers are the user's to finish.
	_settings.JpegQuality = IW::Clamp(_ttoi(str), 1, 100);
	::SendMessage(GetDlgItem(IDC_QUALITY_SLIDER), TBM_SETPOS, TRUE, _settings.JpegQuality);

	return 0;
}

LRESULT CJpegSettingsPanel::OnButtonChange(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	if (_bSetting)
		return 0;

	_settings.JpegOptimize = BST_CHECKED == IsDlgButtonChecked(IDC_OPTIMIZE);
	_settings.JpegProgressive = BST_CHECKED == IsDlgButtonChecked(IDC_PROGRESSIVE);

	return 0;
}

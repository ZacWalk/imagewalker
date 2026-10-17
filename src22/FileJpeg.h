#pragma once
#include "iw/imaging/FileJpeg.h"

// The JPEG encoder settings, as a child panel every dialog that saves hosts
// inline. It writes straight into the settings it was given: the dialog that
// owns them decides whether they are kept.
class CJpegSettingsPanel :
	public CDialogImpl<CJpegSettingsPanel>
{
public:
	typedef CJpegSettingsPanel ThisClass;

	IW::CodecSettings &_settings;
	bool _bSetting;

	CJpegSettingsPanel(IW::CodecSettings &settings) : _settings(settings), _bSetting(false)
	{
	}

	enum { IDD = IDD_JPEG_SETTINGS };

	BEGIN_MSG_MAP(ThisClass)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_HSCROLL, OnHScroll)
		COMMAND_HANDLER(IDC_QUALITY, EN_CHANGE, OnQualityChange)
		COMMAND_ID_HANDLER(IDC_OPTIMIZE, OnButtonChange)
		COMMAND_ID_HANDLER(IDC_PROGRESSIVE, OnButtonChange)
	END_MSG_MAP()

	// Nobody keeps a pointer to this: it is created, positioned and forgotten,
	// and goes when the dialog hosting it does.
	void OnFinalMessage(HWND /*hWnd*/) { delete this; }

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnHScroll(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnQualityChange(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
	LRESULT OnButtonChange(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
};

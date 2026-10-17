// ImageWalker by Zac Walker
//
// Purpose: Options dialog implementation.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"
#include "ViewModelState.h"
#include "ViewDialogs.h"
#include "ViewOptionsDlg.h"

///////////////////////////////////////////////////////////////////////

namespace
{
	// The size combos are drop-downs the user can type into.
	constexpr int kMinThumbnailSize = 32;
	constexpr int kMaxThumbnailSize = 512;

	// Each check box in the Behaviour group and the setting it edits.
	struct OptionCheck
	{
		int Id;
		bool *pValue;
	};

	std::vector<OptionCheck> Options()
	{
		return {
			{IDC_OPTION_FULLSCREEN, &App.Settings.m_bDoubleClickShowsFullScreen},
			{IDC_OPTION_WALKFOLDERS, &App.Settings.m_bWalkFolders},
			{IDC_OPTION_SHOWHIDDENFILES, &App.Settings.m_bShowHidden},
			{IDC_OPTION_SHOWMARKERS, &App.Settings.m_bShowMarkers},
			{IDC_OPTION_SHOWDESCRIPTIONS, &App.Settings.ShowDescriptions},
			{IDC_OPTION_SHOWTOOLTIPS, &App.Settings.m_bShowToolTips},
			{IDC_OPTION_ZOOMTHUMBNAILS, &App.Settings.ZoomThumbnails},
			{IDC_OPTION_SHORTDATES, &App.Settings.m_bShortDates},
			{IDC_OPTION_FADE, &App.Settings.m_bUseEffects},
			{IDC_OPTION_EXIFAUTOROTATE, &App.Settings._bExifAutoRotate}
		};
	}
}

///////////////////////////////////////////////////////////////////////

LRESULT CViewOptions::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	CenterWindow(GetParent());

	CComboBox comboWidth = GetDlgItem(IDC_WIDTH);
	CComboBox comboHeight = GetDlgItem(IDC_HEIGHT);

	int values[] = {80, 100, 128, 160, 200, 256, 320, -1};

	IW::SetItems(comboWidth, values, App.Settings._sizeThumbImage.cx);
	IW::SetItems(comboHeight, values, App.Settings._sizeThumbImage.cy);

	_annotations.Init(GetDlgItem(IDC_ANNOTATION_LIST), App.Settings.m_annotations);
	_columns.Init(GetDlgItem(IDC_COLUMN_LIST), App.Settings.m_columns);

	for (const auto &option : Options())
		CheckDlgButton(option.Id, *option.pValue ? BST_CHECKED : BST_UNCHECKED);

	bHandled = TRUE;
	return 1;
}

LRESULT CViewOptions::OnOK(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	BOOL b;

	// Both are drop-downs the user can type into, and a blank or non-numeric
	// entry reads as zero -- which persists a grid of one-pixel thumbnails.
	const int cx = GetDlgItemInt(IDC_WIDTH, &b, TRUE);
	const int cy = GetDlgItemInt(IDC_HEIGHT, &b, TRUE);

	App.Settings._sizeThumbImage = CSize(IW::Clamp(cx, kMinThumbnailSize, kMaxThumbnailSize),
	                                     IW::Clamp(cy, kMinThumbnailSize, kMaxThumbnailSize));

	for (const auto &option : Options())
		*option.pValue = BST_CHECKED == IsDlgButtonChecked(option.Id);

	_annotations.Apply(App.Settings.m_annotations);
	_columns.Apply(App.Settings.m_columns);

	OnApplyLoader();

	App.Settings.Codec = Codec;

	if (m_pLoaderFactory != nullptr)
		App.Settings.CaptureFormat = m_pLoaderFactory->GetKey();

	EndDialog(wID);
	return 0;
}

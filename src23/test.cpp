// ImageWalker by Zac Walker
//
// Purpose: The unit tests, run by passing /test. Covers the loaders, the
//          pixel pipeline, the edit stack and the metadata readers.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "StdAfx.h"

#include "ImagingStreams.h"
#include "iw/legacy/FilePng.h"
#include "iw/legacy/FileBmp.h"
#include "iw/legacy/FileGif.h"
#include "FileJpeg.h"
#include "iw/legacy/FileTiff.h"
#include "iw/legacy/FilePcx.h"
#include "iw/legacy/FilePsd.h"
#include "iw/legacy/FileWpg.h"
#include "iw/legacy/FileFormatList.h"
#include "FileFormatAny.h"
#include "ImagingFilter.h"
#include "iw/legacy/ImagingEdits.h"
#include "ImagingTransform.h"
#include "iw/legacy/MetadataExif.h"
#include "iw/legacy/ImagingBlitter.h"
#include "ViewRenderSurface.h"
#include "iw/legacy/ViewRenameDlg.h"
#include "ViewAboutDlg.h"

#include "iw/commontests.h"
#include "iw/rendertests.h"

#include "AppCommands.h"
#include "ViewModelState.h"
#include "ViewBase.h"
#include "ViewSearchDlg.h"
#include "ViewEdit.h"
#include "ToolContactSheet.h"
#include "iw/legacyworkflowtests.h"
#include "iw/legacyboundarytests.h"
#include "iw/legacynativeworkflowtests.h"

// ImageWalker 2.3 -- run with /test.

IW_TEST(SearchAndEditSidePanesUseTheSkinPaletteOnlyWithBlackSkin)
{
	IW_CHECK(AtlInitCommonControls(ICC_BAR_CLASSES | ICC_STANDARD_CLASSES));
	const HWND host = ::CreateWindowEx(0, WC_STATIC, _T(""), WS_POPUP,
		0, 0, 420, 800, nullptr, nullptr, _Module.GetModuleInstance(), nullptr);
	IW_CHECK(host != nullptr);
	if (host == nullptr) return;
	WorkflowDialogLifetime hostLifetime{host};

	CSearchPanelDlg search;
	CEditPanelDlg edit;
	CSearchAdvancedDlg advanced(_T(""));
	WorkflowDialogLifetime searchLifetime{search.Create(host)};
	WorkflowDialogLifetime editLifetime{edit.Create(host)};
	WorkflowDialogLifetime advancedLifetime{advanced.Create(host)};
	IW_CHECK(search.m_hWnd != nullptr && edit.m_hWnd != nullptr && advanced.m_hWnd != nullptr);
	if (!search.m_hWnd || !edit.m_hWnd || !advanced.m_hWnd) return;

	const auto restoreSkin = App.Settings.BlackSkin;
	IW::Style::SetMood();
	HIGHCONTRAST contrast = {};
	contrast.cbSize = sizeof(contrast);
	const bool useSkin = ::SystemParametersInfo(SPI_GETHIGHCONTRAST, contrast.cbSize, &contrast, 0) &&
		!(contrast.dwFlags & HCF_HIGHCONTRASTON);

	RenderSurface surface;
	const bool created = surface.Create(nullptr, CRect(0, 0, 420, 64));
	IW_CHECK(created);
	if (!created) return;
	const HDC dc = surface.GetDC();
	const auto dialogColor = [&](HWND dialog)
	{
		surface.Fill(RGB(133, 71, 57), nullptr);
		::SendMessage(dialog, WM_ERASEBKGND, reinterpret_cast<WPARAM>(dc), 0);
		return ::GetPixel(dc, 0, 0);
	};

	const COLORREF nativeBackground = dialogColor(advanced);
	const HWND size = search.GetDlgItem(IDC_SIZE_KB);
	const HWND stop = search.GetDlgItem(ID_SEARCH_STOP);
	const HWND month = search.GetDlgItem(IDC_MONTH);
	const HWND compare = edit.GetDlgItem(IDC_EDIT_COMPARE_SPLIT);
	CTrackBarCtrl slider = edit.GetDlgItem(IDC_EDIT_STRAIGHTEN);

	const auto paint = [&](HWND control)
	{
		// Paint native controls into the memory DC. The host is never shown.
		surface.Fill(RGB(133, 71, 57), nullptr);
		::SendMessage(control, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_ERASEBKGND);
	};

	for (const bool black : {false, true, false})
	{
		App.Settings.BlackSkin = black;
		const bool dark = black && useSkin;
		const COLORREF background = dark ? IW::Style::Color::TaskBackground : nativeBackground;
		IW_CHECK_EQ(dialogColor(search), background);
		IW_CHECK_EQ(dialogColor(edit), background);
		IW_CHECK_EQ(dialogColor(advanced), nativeBackground);

		if (!dark)
			continue;

		const COLORREF field = IW::Average(background, RGB(0, 0, 0));
		::SendMessage(search, WM_CTLCOLOREDIT, reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(size));
		IW_CHECK_EQ(::GetTextColor(dc), IW::Style::Color::TaskText);
		IW_CHECK_EQ(::GetBkColor(dc), field);

		search.ResizeClient(330, 720);
		paint(stop);
		IW_CHECK_EQ(::GetPixel(dc, 3, 3), IW::Average(background, field));

		CComboBox combo(month);
		combo.SetCurSel(2);
		paint(month);
		IW_CHECK_EQ(::GetPixel(dc, 3, 3), field);
		IW_CHECK_EQ(combo.GetCurSel(), 2);

		edit.ResizeClient(310, 900);
		edit.ShowCompare(0);
		paint(compare);
		const COLORREF unchecked = ::GetPixel(dc, 3, 3);
		edit.ShowCompare(1);
		paint(compare);
		IW_CHECK(::GetPixel(dc, 3, 3) != unchecked);
		IW_CHECK_EQ(::SendMessage(compare, BM_GETCHECK, 0, 0), BST_CHECKED);

		slider.SetPos(20);
		paint(slider);
		CRect thumb;
		slider.GetThumbRect(thumb);
		const CPoint center = thumb.CenterPoint();
		IW_CHECK_EQ(::GetPixel(dc, center.x, center.y), IW::Style::Color::TaskText);
		IW_CHECK_EQ(::GetPixel(dc, 1, 1), background);
	}

	App.Settings.BlackSkin = restoreSkin;
	IW_CHECK(!::IsWindowVisible(host));
}

#include "iw/imagingtests.h"

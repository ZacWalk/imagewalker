#pragma once

#include "ViewSkin.h"
#include "ViewScale.h"
#include "iw/palettewindow.h"
#include "ViewDropTarget.h"
#include "ViewImageWindow.h"
#include "ViewImageCtrl.h"

namespace
{
	struct ClassicViewTestCoupling : Coupling
	{
		bool fullScreen = false;
		CString GetItemPath(int) const override { return CString(); }
		HWND GetImageWindow() override { return nullptr; }
		ITEMLIST GetItemList() const override { return {}; }
		CSize GetThumbnailSize() const override { return CSize(160, 160); }
		bool HasSearchSpec() override { return false; }
		bool IsFullScreen() const override { return fullScreen; }
		bool IsItemFolder(long) const override { return false; }
		bool SelectFolderItem(const CString &) override { return false; }
		bool ViewFullScreen(bool value) override { fullScreen = value; return true; }
		int GetFocusItem() const override { return -1; }
		int GetItemCount() const override { return 0; }
		void AfterCopy(bool) override {}
		void BeginImageFade(int) override {}
		bool CanChangeImage() override { return true; }
		bool CanShowGutterItem(DWORD id) const override
		{
			return id == ID_VIEW_FILMSTRIP ? fullScreen : !fullScreen;
		}
		void Command(WORD) override {}
		void NewImage(bool) override {}
		void ResetThumbThread() override {}
		void ResetWaitForFolderToChangeThread() override {}
		void SetFocusItem(int, bool) override {}
		void SetStatusText(const CString &) override {}
		void SetStopLoading() override {}
		void ShowImage(CImageLoad *) override {}
		void ShowSearchPane() override {}
		void StartSearch(Search::Type) override {}
		void SignalImageLoadComplete(CImageLoad *) override {}
		void SignalSearching() override {}
		void SignalSearchingComplete() override {}
		void SignalSearchingFolder(const CString &) override {}
		void SignalThumbnailing() override {}
		void SignalThumbnailingComplete() override {}
		void SortOrderChanged(int) override {}
		void StartSearchThread(Search::Type, const Search::Spec &) override {}
		void StartSearching() override {}
		void StopLoadingThumbs() override {}
		void TrackPopupMenu(HMENU, UINT, int, int) override {}
		void UpdateStatusText() override {}
		void UpdateToolbarsShown() override {}
	};

	struct ClassicNavigationDriver
	{
		inline static ClassicNavigationDriver *active = nullptr;
		UINT_PTR timer;
		bool opened = false;

		ClassicNavigationDriver() : timer(SetTimer(nullptr, 0, 10, OnTimer))
		{
			active = this;
		}
		~ClassicNavigationDriver()
		{
			if (timer) KillTimer(nullptr, timer);
			active = nullptr;
		}
		static void CALLBACK OnTimer(HWND, UINT, UINT_PTR, DWORD)
		{
			const HWND popup = GetCapture();
			if (!active || !popup) return;
			TCHAR className[128]{};
			GetClassName(popup, className, _countof(className));
			if (_tcscmp(className, CImageNavigation<CImageCtrl>::GetWndClassInfo().m_wc.lpszClassName) != 0)
				return;
			active->opened = true;
			PostMessage(popup, WM_LBUTTONUP, 0, 0);
		}
	};
}

IW_TEST(ClassicImageScrollBarsFollowTheVisibleViewport)
{
	ClassicViewTestCoupling coupling;
	State state(&coupling, false);
	const HWND parent = CreateWindowEx(0, _T("STATIC"), _T("Image viewport test"), WS_POPUP,
		0, 0, 640, 480, nullptr, nullptr, _Module.GetModuleInstance(), nullptr);
	IW_CHECK(parent != nullptr);
	if (!parent) return;
	WorkflowDialogLifetime parentLifetime{parent};

	CImageCtrl pane(&coupling, state);
	IW_CHECK(pane.Create(parent, CRect(0, 0, 640, 480), nullptr,
		WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS) != nullptr);
	if (!pane.m_hWnd) return;
	WorkflowDialogLifetime paneLifetime{pane.m_hWnd};

	auto show = [&](int width, int height)
	{
		IW::Image image;
		image.CreatePage(width, height, IW::PixelFormat::PF24);
		state.Image.SetImage(image);
		pane.Refresh(false);
		pane.OnResetFrames();
	};
	auto checkBars = [&](bool horizontal, bool vertical)
	{
		IW_CHECK(IW::HasVisibleStyle(pane._scrollH) == horizontal);
		IW_CHECK(IW::HasVisibleStyle(pane._scrollV) == vertical);
		IW_CHECK(pane.GetClientSize() == pane.GetImageClientRect().Size());
		IW_CHECK(pane.GutterWidth() == (vertical ? GetSystemMetrics(SM_CXVSCROLL) : 0));
		IW_CHECK(!pane._frameNavigation.IsVisible());
		IW_CHECK(!pane.CanShowNavigation());
	};

	pane.SetScale(100);
	checkBars(false, false);
	const int height = 480 - pane.GutterHeight();
	show(640, height);
	checkBars(false, false);
	IW_CHECK(!pane.CanNavigate());
	IW_CHECK(!pane._gutterBar.IsButtonEnabled(ID_NAVIGATE));

	show(640, height + 1);
	checkBars(true, true);
	IW_CHECK(pane.CanNavigate());
	IW_CHECK(pane._gutterBar.IsButtonEnabled(ID_NAVIGATE));
	pane.ScrollTo(CPoint(10000, 10000));
	IW_CHECK(pane.GetScrollOffset() == CPoint(GetSystemMetrics(SM_CXVSCROLL), 1));
	IW_CHECK(pane.GetDrawnImageRect().bottom == pane.GetImageClientRect().bottom);

	show(100, height + 1);
	checkBars(false, true);
	show(641, 100);
	checkBars(true, false);
	show(1200, 900);
	checkBars(true, true);

	state.Image.CreateThumbnail();
	{
		ClassicNavigationDriver driver;
		IW_CHECK(driver.timer != 0);
		if (driver.timer)
		{
			NMTOOLBAR dropdown{};
			dropdown.hdr.hwndFrom = pane._gutterBar;
			dropdown.hdr.code = TBN_DROPDOWN;
			dropdown.iItem = ID_NAVIGATE;
			pane.SendMessage(WM_NOTIFY, CImageCtrl::kGutterId, reinterpret_cast<LPARAM>(&dropdown));
			IW_CHECK(driver.opened);
			IW_CHECK(GetCapture() == nullptr);
		}
	}

	for (const auto mode : {ScaleMode::Fit, ScaleMode::Down, ScaleMode::Up, ScaleMode::Fill})
	{
		pane.SetScale(mode);
		const bool overflow = mode == ScaleMode::Up || mode == ScaleMode::Fill;
		for (int pass = 0; pass != 4; ++pass)
		{
			pane.OnResetFrames();
			checkBars(overflow, overflow);
		}
	}

	pane.SetScale(100);
	pane.SetWindowPos(nullptr, 0, 0, 1400, 1100, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	checkBars(false, false);
	pane.SetWindowPos(nullptr, 0, 0, 640, 480, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	checkBars(true, true);

	pane._bCollage = true;
	pane.OnResetFrames();
	checkBars(false, false);
	IW_CHECK(!pane._gutterBar.IsButtonEnabled(ID_NAVIGATE));
	pane._bCollage = false;
	pane.OnResetFrames();
	checkBars(true, true);

	coupling.fullScreen = true;
	pane.OnResetFrames();
	checkBars(false, false);
	IW_CHECK(pane._gutterBar.IsButtonEnabled(ID_NAVIGATE));
	coupling.fullScreen = false;
	pane.OnResetFrames();
	checkBars(true, true);

	IW::Image empty;
	state.Image.SetImage(empty);
	pane.Refresh(false);
	checkBars(false, false);
	IW_CHECK(!pane._gutterBar.IsButtonEnabled(ID_NAVIGATE));
}

#pragma once

#include "testrunner.h"

// Included beside the complete frame type; called only by the /test suite.
void CheckFullScreenLayoutForTests()
{
	struct RestoreState
	{
		AppSettings settings = App.Settings;
		State *state = g_pState;
		CMainFrame *frame = g_pMainWin;
		~RestoreState()
		{
			App.Settings = settings;
			g_pState = state;
			g_pMainWin = frame;
		}
	} restore;
	App.Settings.ViewMode = static_cast<int>(CMainFrame::Mode::Normal);

	CMainFrame frame(nullptr, false);
	g_pMainWin = &frame;
	_Module.Lock();
	if (!frame.CreateEx(nullptr, CRect(100, 100, 900, 700)))
	{
		IW_CHECK(false);
		_Module.Unlock();
		return;
	}
	struct WindowLifetime
	{
		CMainFrame &frame;
		~WindowLifetime()
		{
			frame.DestroyWindow();
			MSG message;
			while (PeekMessage(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
		}
	} windowLifetime{frame};
	IW_CHECK(!frame._bCreateFailed);
	if (frame._bCreateFailed) return;

	std::vector<MONITORINFO> monitors;
	const auto collect = [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL
	{
		MONITORINFO info = {sizeof(info)};
		if (!GetMonitorInfo(monitor, &info)) return FALSE;
		reinterpret_cast<std::vector<MONITORINFO> *>(context)->push_back(info);
		return TRUE;
	};
	IW_CHECK(EnumDisplayMonitors(nullptr, nullptr, collect, reinterpret_cast<LPARAM>(&monitors)));
	IW_CHECK(!monitors.empty());

	for (const auto &monitor : monitors)
	{
		const CRect work(monitor.rcWork);
		const CRect screen(monitor.rcMonitor);
		IW::Test::Detail::Print("\r\n    fullscreen monitor (%ld,%ld)-(%ld,%ld)",
			screen.left, screen.top, screen.right, screen.bottom);
		for (const bool maximized : {false, true})
		{
			frame.ShowWindow(SW_RESTORE);
			frame.SetWindowPos(nullptr, work.left + 30, work.top + 30,
				IW::Min(800, work.Width() - 60), IW::Min(600, work.Height() - 60),
				SWP_NOZORDER | SWP_NOACTIVATE);
			if (maximized) frame.ShowWindow(SW_MAXIMIZE);

			WINDOWPLACEMENT placement = {sizeof(placement)};
			IW_CHECK(frame.GetWindowPlacement(&placement));
			const DWORD style = frame.GetStyle();
			const DWORD exStyle = frame.GetExStyle();
			CRect windowBefore;
			frame.GetWindowRect(windowBefore);

			for (int pass = 0; pass != 2; ++pass)
			{
				frame.ViewFullScreen(true);
				IW_CHECK(frame.IsFullScreen());
				CRect window, client, content;
				frame.GetWindowRect(window);
				frame.GetClientRect(client);
				frame.ClientToScreen(client);
				IW_CHECK(window == screen);
				IW_CHECK(client == screen);
				IW_CHECK(frame._cyChrome == 0);
				::GetWindowRect(frame.m_hWndClient, content);
				IW_CHECK(content == screen);
				IW_CHECK(!IW::HasVisibleStyle(frame.m_CmdBar));
				IW_CHECK(!IW::HasVisibleStyle(frame._hWndToolBarMain));
				IW_CHECK(!IW::HasVisibleStyle(frame.m_hWndStatusBar));

				frame.ViewFullScreen(false);
				IW_CHECK(!frame.IsFullScreen());
				IW_CHECK((frame.IsZoomed() != FALSE) == maximized);
				IW_CHECK(frame.GetStyle() == style);
				IW_CHECK(frame.GetExStyle() == exStyle);
				frame.GetWindowRect(window);
				IW_CHECK(window == windowBefore);
				WINDOWPLACEMENT restored = {sizeof(restored)};
				IW_CHECK(frame.GetWindowPlacement(&restored));
				IW_CHECK(restored.showCmd == placement.showCmd);
				IW_CHECK(CRect(restored.rcNormalPosition) == CRect(placement.rcNormalPosition));
				IW_CHECK(frame._cyChrome > 0);
			}
		}
	}
}

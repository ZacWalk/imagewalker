// ImageWalker 1.0 - entry point.

#include "stdafx.h"

#include "ImageWalker.h"
#include "MainFrm.h"
#include "SplashWnd.h"

#include "iw/crashreport.h"
#include "iw/logfile.h"
#include "iw/testrunner.h"
#include "iw/workqueue.h"
#include "window_screenshot.h"

extern const std::string_view s_app_name = "ImageWalker10";

// The folder named on the command line, if any.
CString g_strCmdLine;

// Background jobs come back as work items rather than as a pointer in a message
// parameter, so the loop has to look at the queue itself as well as at input.
static int RunMessageLoop(CMainFrame& frame)
{
	MSG msg = {};

	IW::CDispatchQueue& dispatch = IW::UiQueue();
	const HANDLE hDispatch = dispatch.Handle();

	for (;;)
	{
		dispatch.DrainAll();

		if (!::PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE))
		{
			// Nothing to do: block on input and on the queue together, so a
			// finished job wakes us without having to post a window message.
			IW::WaitForMessageOrHandles(&hDispatch, 1);
			continue;
		}

		const BOOL result = ::GetMessage(&msg, nullptr, 0, 0);
		if (result == -1)
		{
			IW::Logging::LastError(_T("GetMessage"));
			return 1;
		}
		if (result == 0)
			break;

		if (!frame.PreTranslateMessage(&msg))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}

	return static_cast<int>(msg.wParam);
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR lpstrCmdLine, int nCmdShow)
{
	// Before anything else: the reporter ends in a modal MessageBox, and a
	// faulting case must reach the harness's SEH filter instead.
	if (IW::Test::IsRequested(lpstrCmdLine))
		return IW::Test::RunAll("ImageWalker 1.0");

	const auto screenshot = iw::screenshot::command_line();
	if (screenshot.enabled && screenshot.output.empty())
	{
		iw::screenshot::report_usage();
		return 2;
	}
	iw::screenshot::Timer capture(screenshot);

	IW::Crash::Install(L"imagewalker10");
	IW::Logging::Open(_T("ImageWalker 1.0"));
	if (FAILED(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
	{
		IW::Logging::Error("Shell COM initialization failed");
		return 1;
	}

	// The toolbar, status bar and COMBOBOXEX classes.
	INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_WIN95_CLASSES | ICC_USEREX_CLASSES | ICC_LINK_CLASS};

	if (!InitCommonControlsEx(&icc))
		IW::Logging::LastError(_T("InitCommonControlsEx"));

	if (screenshot.enabled)
		g_strCmdLine = NarrowText(screenshot.input.c_str());
	else
		g_strCmdLine = lpstrCmdLine;
	g_strCmdLine.Remove(_T('\"'));
	g_strCmdLine.Trim();

	int nRet = 1;

	{
		CSplashWnd splash;
		CMainFrame frame;

		if (frame.Create() == nullptr)
		{
			IW::Logging::Error(_T("Failed to create the main frame"));
		}
		else
		{
			if (screenshot.enabled)
				frame.ShowWindow(SW_SHOWNOACTIVATE);
			else if (!Settings::RestoreWindowPlacement(frame))
				frame.ShowWindow(nCmdShow);

#ifndef _DEBUG
			if (!screenshot.enabled && !frame.IsIconic())
				splash.Show(frame);
#endif

			frame.UpdateWindow();

			IW::Logging::Write(_T("entering message loop"));
			if (capture.start(frame))
				nRet = capture.result(RunMessageLoop(frame));
			else
			{
				iw::screenshot::report(L"could not start the capture timer.");
				frame.DestroyWindow();
			}
			if (frame.IsWindow()) frame.DestroyWindow();
		}

		// Close before the panes go, not after: nothing drains this queue once
		// the loop has returned, so a job left on it holds its owner token past
		// the point the design says it should die.
		IW::UiQueue().Close();

		// The frame's destructor runs here, and with it the two panes', which is
		// what stops the workers and writes the current folder back to the ini.
	}

	::CoUninitialize();

	IW::Logging::Write(_T("exit code %d"), nRet);
	IW::ShutdownWorkQueues();
	IW::Logging::Close();

	return nRet;
}

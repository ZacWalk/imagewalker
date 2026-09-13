// ArtMate 1.00 - entry point.

#include "stdafx.h"

#include "ArtMate.h"
#include "MainFrm.h"
#include "SplashWnd.h"

#include "iw/crashreport.h"
#include "iw/logfile.h"
#include "iw/testrunner.h"
#include "iw/workqueue.h"

CAppModule _Module;

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

		if (!::GetMessage(&msg, nullptr, 0, 0))
			break;

		if (!frame.PreTranslateMessage(&msg))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}

	return static_cast<int>(msg.wParam);
}

int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE, LPTSTR lpstrCmdLine, int nCmdShow)
{
	// Before anything else: the reporter ends in a modal MessageBox, and a
	// faulting case must reach the harness's SEH filter instead.
	if (IW::Test::IsRequested(lpstrCmdLine))
		return IW::Test::RunAll("ArtMate 1.00");

	IW::Crash::Install(L"imagewalker10");
	IW::Logging::Open(_T("ArtMate 1.00"));

	// Drag and drop needs full OLE, not just COM.
	if (FAILED(::OleInitialize(nullptr)))
	{
		IW::Logging::Error(_T("OleInitialize failed"));
		return 1;
	}

	_Module.Init(nullptr, hInstance);

	// The toolbar, status bar and COMBOBOXEX classes.
	INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_WIN95_CLASSES | ICC_USEREX_CLASSES};

	if (!InitCommonControlsEx(&icc))
		IW::Logging::LastError(_T("InitCommonControlsEx"));

	g_strCmdLine = lpstrCmdLine;
	g_strCmdLine.Remove(_T('\"'));
	g_strCmdLine.Trim();

	int nRet = 0;

	{
		CSplashWnd splash;

#ifndef _DEBUG
		splash.Show();
#endif

		CMainFrame frame;

		if (frame.CreateEx() == nullptr)
		{
			IW::Logging::Error(_T("Failed to create the main frame"));
		}
		else
		{
			if (!Settings::RestoreWindowPlacement(frame))
				frame.ShowWindow(nCmdShow);

			frame.UpdateWindow();

			IW::Logging::Write(_T("entering message loop"));
			nRet = RunMessageLoop(frame);
		}

		// Close before the panes go, not after: nothing drains this queue once
		// the loop has returned, so a job left on it holds its owner token past
		// the point the design says it should die.
		IW::UiQueue().Close();

		// The frame's destructor runs here, and with it the two panes', which is
		// what stops the workers and writes the current folder back to the ini.
	}

	::OleUninitialize();
	_Module.Term();

	IW::Logging::Write(_T("exit code %d"), nRet);
	IW::ShutdownWorkQueues();
	IW::Logging::Close();

	return nRet;
}

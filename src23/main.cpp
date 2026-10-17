// ImageWalker by Zac Walker
//
// Purpose: Process entry point. Runs the test harness for /test, otherwise
//          brings up COM, ATL and the common controls and hands off to the
//          main window.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"

#include "iw/crashreport.h"
#include "iw/logfile.h"
#include "iw/testrunner.h"

#pragma comment(lib, "HtmlHelp")

CAppModule _Module;

// In AppMainFrame.cpp, which is the only translation unit that needs the
// complete frame type.
int RunMainWindow(LPTSTR lpCmdLine);

int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPTSTR lpCmdLine, int nCmdShow)
{
	// Enough of the app to let the tests drive the real loaders and filters:
	// resources, the halftone palette and XMP, but no window. Ahead of
	// Crash::Install, whose reporter ends in a modal MessageBox - a faulting
	// case must reach the harness's SEH filter instead.
	if (IW::Test::IsRequested(lpCmdLine))
	{
		CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		_Module.Init(nullptr, hInstance);
		App.Init();

		const int nFailures = IW::Test::RunAll(APP_VERSION_NAME);

		App.Free();
		_Module.Term();
		CoUninitialize();

		return nFailures;
	}

	IW::Crash::Install(L"imagewalker23");

	IW::Logging::Open(_T(APP_VERSION_NAME));

	int nRet = 0;

	try
	{
		HRESULT hRes = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

		if (FAILED(hRes))
		{
			IW::CMessageBoxIndirect mb;
			mb.Show(IDS_FAILEDTOSTART_OLE);
			return 0;
		}

		hRes = OleInitialize(nullptr);

		if (FAILED(hRes))
		{
			IW::CMessageBoxIndirect mb;
			mb.Show(IDS_FAILEDTOSTART_OLE);
			return 0;
		}

		// this resolves ATL window thunking problem when Microsoft Layer for Unicode (MSLU) is used
		::DefWindowProc(nullptr, 0, 0, 0L);

		DWORD dwCommonControls = ICC_COOL_CLASSES | ICC_BAR_CLASSES | ICC_USEREX_CLASSES | ICC_DATE_CLASSES;
		BOOL bInitCommonControls = AtlInitCommonControls(dwCommonControls);

		if (!bInitCommonControls)
		{
			IW::CMessageBoxIndirect mb;
			mb.Show(IDS_FAILEDTOREGISTERCOMMONCONTROLS);
			return 0;
		}

		App.Init();

		HRESULT hr = _Module.Init(nullptr, hInstance);

		if (FAILED(hr))
		{
			IW::CMessageBoxIndirect mb;
			mb.Show(IDS_FAILEDTOSTART_ATL);
			return 0;
		}

		AtlAxWinInit();

		nRet = RunMainWindow(lpCmdLine);

		App.Free();
		_Module.Term();

		::OleUninitialize();
		::CoUninitialize();
	}
	catch (IW::startup_exception& e)
	{
		CString str;

		str += App.LoadString(IDS_FAILEDTOSTART);
		str += g_szCRLF;
		str += g_szCRLF;
		str += e.what();
		str += g_szCRLF;
		str += g_szCRLF;
		str += App.LoadString(IDS_REINSTALL);

		IW::CMessageBoxIndirect mb;
		mb.Show(str);
	}
	catch (const std::exception& e)
	{
		const CString strWhat(CA2T(e.what()));

		CString str;
		str.Format(IDS_SERIOUSERROR, static_cast<LPCTSTR>(strWhat), _T(""), 0);

		IW::CMessageBoxIndirect mb;
		mb.Show(str);
	}

#ifdef _DEBUG
	_CrtDumpMemoryLeaks();
#endif // _DEBUG

	IW::Logging::Write(_T("exit code %d"), nRet);
	IW::Logging::Close();

	return nRet;
}

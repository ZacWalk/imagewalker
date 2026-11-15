// Proves the legacy harness keeps real popup and modal-dialog paths offscreen.
#include "iw/testrunner.h"

IW_TEST(NativePopupCannotMoveOntoTheDesktopOrTakeFocus)
{
	const HWND foreground = GetForegroundWindow();
	const HWND window = CreateWindowEx(WS_EX_APPWINDOW, _T("STATIC"), _T("Headless popup"),
		WS_POPUP, 0, 0, 240, 160, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
	IW_CHECK(window != nullptr);
	if (!window) return;
	IW_CHECK(IW::Test::OffscreenWindows::OutsideDesktop(window));
	IW_CHECK(SetWindowPos(window, HWND_TOP, 50, 50, 320, 200, SWP_NOACTIVATE) != FALSE);
	const bool nativeWindowsSafe = IW::Test::OffscreenWindows::OutsideDesktop(window);
	IW_CHECK(nativeWindowsSafe);
	if (!nativeWindowsSafe) { DestroyWindow(window); return; }
	ShowWindow(window, SW_SHOWNOACTIVATE);
	RECT bounds{};
	IW_CHECK(GetWindowRect(window, &bounds) != FALSE);
	IW_CHECK_EQ(bounds.right - bounds.left, 320);
	IW_CHECK_EQ(bounds.bottom - bounds.top, 200);
	SetActiveWindow(window);
	SetFocus(window);
	IW_CHECK(GetForegroundWindow() == foreground);
	IW_CHECK(GetFocus() != window);
	IW_CHECK((GetWindowLongPtr(window, GWL_EXSTYLE) & WS_EX_APPWINDOW) == 0);
	IW_CHECK(DestroyWindow(window) != FALSE);
}

namespace
{
	INT_PTR CALLBACK ModalDialog(HWND window, UINT message, WPARAM, LPARAM)
	{
		if (message == WM_INITDIALOG)
		{
			SetWindowPos(window, HWND_TOP, 100, 100, 240, 160, SWP_SHOWWINDOW);
			if (!SetTimer(window, 1, 10, nullptr)) EndDialog(window, -1);
			return TRUE;
		}
		if (message == WM_TIMER)
		{
			IW_CHECK(IsWindowVisible(window) != FALSE);
			IW_CHECK(IW::Test::OffscreenWindows::OutsideDesktop(window));
			KillTimer(window, 1);
			EndDialog(window, IDOK);
			return TRUE;
		}
		return FALSE;
	}
}

IW_TEST(NativeModalDialogKeepsItsMessageLoopWithoutVisibleUi)
{
	const HWND probe = CreateWindowEx(0, _T("STATIC"), _T("Headless policy probe"), WS_POPUP,
		0, 0, 240, 160, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
	IW_CHECK(probe != nullptr);
	if (!probe) return;
	SetWindowPos(probe, nullptr, 100, 100, 240, 160, SWP_NOZORDER | SWP_NOACTIVATE);
	const bool nativeWindowsSafe = IW::Test::OffscreenWindows::OutsideDesktop(probe);
	DestroyWindow(probe);
	IW_CHECK(nativeWindowsSafe);
	if (!nativeWindowsSafe) return;
	struct Template
	{
		DLGTEMPLATE dialog{};
		WORD menu = 0;
		WORD windowClass = 0;
		WORD title = 0;
	} resource;
	resource.dialog.style = WS_POPUP | WS_CAPTION | WS_VISIBLE | DS_MODALFRAME | DS_CENTER;
	resource.dialog.cx = 120;
	resource.dialog.cy = 80;
	const HWND foreground = GetForegroundWindow();
	const auto result = DialogBoxIndirectParam(GetModuleHandle(nullptr), &resource.dialog, nullptr, ModalDialog, 0);
	IW_CHECK_EQ(result, IDOK);
	IW_CHECK(GetForegroundWindow() == foreground);
}

IW_TEST(NativeHiddenGeometryPreservesRealPositionAndMaximize)
{
	const HWND window = CreateWindowEx(0, _T("STATIC"), _T("Hidden geometry"),
		WS_POPUP, 0, 0, 240, 160, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
	IW_CHECK(window != nullptr);
	if (!window) return;
	IW_CHECK(IW::Test::OffscreenWindows::PreserveGeometry(window));
	IW_CHECK(SetWindowPos(window, nullptr, 100, 100, 320, 200, SWP_SHOWWINDOW | SWP_NOZORDER) != FALSE);
	IW_CHECK(IsWindowVisible(window) == FALSE);
	RECT bounds{};
	IW_CHECK(GetWindowRect(window, &bounds) != FALSE);
	IW_CHECK(bounds.left == 100 && bounds.top == 100 && bounds.right == 420 && bounds.bottom == 300);
	ShowWindow(window, SW_MAXIMIZE);
	IW_CHECK(IsWindowVisible(window) == FALSE);
	IW_CHECK(IsZoomed(window) != FALSE);
	ShowWindow(window, SW_RESTORE);
	IW_CHECK(IsWindowVisible(window) == FALSE);
	IW_CHECK(IsZoomed(window) == FALSE);
	IW_CHECK(DestroyWindow(window) != FALSE);
}

int main()
{
	return IW::Test::RunAll("Native headless window policy");
}

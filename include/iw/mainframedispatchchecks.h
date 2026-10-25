#pragma once

#include "testrunner.h"

// Kept beside the complete frame type, rather than importing its many window
// implementation headers into test.cpp. Only the /test suite calls this.
void CheckMainFrameCommandDispatchForTests()
{
	struct RecordingView : ViewBase
	{
		std::vector<DWORD> commands;
		HWND Activate(HWND) override { return nullptr; }
		void Deactivate() override {}
		bool CanEditImages() const override { return false; }
		void OnTimer() override {}
		bool CanShowToolbar(DWORD) override { return false; }
		void OnOptionsChanged() override {}
		HWND GetImageWindow() override { return nullptr; }
		bool InvokeCommand(DWORD id) override
		{
			commands.push_back(id);
			return id == ID_FILE_PRINT;
		}
	} view;
	struct RestoreState
	{
		State *previous = g_pState;
		~RestoreState() { g_pState = previous; }
	} restore;
	{
		CMainFrame frame(nullptr, false);
		IW_CHECK_EQ(frame._state.Folder.GetFolder()->GetItemCount(), 0);
		frame._pView = &view;
		LRESULT result = 0;
		// Go through the actual MainFrame message map, not just a CRTP stand-in.
		IW_CHECK(frame.ProcessWindowMessage(nullptr, WM_COMMAND, MAKEWPARAM(ID_FILE_PRINT, 1), 0, result));
		IW_CHECK_EQ(frame._mode, CMainFrame::Mode::Normal);
		IW_CHECK_EQ(view.commands.back(), static_cast<DWORD>(ID_FILE_PRINT));
		IW_CHECK(frame.ProcessWindowMessage(nullptr, WM_COMMAND, MAKEWPARAM(ID_THUMBNAILS, 0), 0, result));
		IW_CHECK_EQ(view.commands.back(), static_cast<DWORD>(ID_THUMBNAILS));
		IW_CHECK(!frame.ProcessWindowMessage(nullptr, WM_COMMAND, MAKEWPARAM(0, 0), 0, result));
		IW_CHECK_EQ(view.commands.back(), 0u);
		frame._pView = nullptr;
		IW_CHECK(frame.ProcessWindowMessage(nullptr, WM_COMMAND, MAKEWPARAM(ID_VIEW_ARRANGEICONS, 0), 0, result));
		IW_CHECK_EQ(view.commands.size(), size_t{3});
		bool enabled = true, checked = false;
		IW_CHECK(frame.GetCommandState(ID_FILE_PRINT, enabled, checked));
		frame._mode = CMainFrame::Mode::Print;
		IW_CHECK(!frame.GetCommandState(ID_FILE_PRINT, enabled, checked));
		IW_CHECK(frame.GetCommandState(ID_VIEW_PRINT, enabled, checked));
		IW_CHECK(checked);
		frame._mode = CMainFrame::Mode::Normal;
	}
}

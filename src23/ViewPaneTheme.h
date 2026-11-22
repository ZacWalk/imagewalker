// ImageWalker by Zac Walker
//
// Purpose: Palette painting for the native controls in the Search and Edit panes.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

namespace IW::Skin
{
	class PaneTheme : public CMessageMap
	{
	public:
		BOOL ProcessWindowMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
		                          LRESULT &result, DWORD mapId = 0) override;

		static void AttachDropDownList(HWND combo);

	private:
		CBrush _surfaceBrush;
		CBrush _fieldBrush;
		COLORREF _surfaceColor = CLR_INVALID;
		COLORREF _fieldColor = CLR_INVALID;
	};
}

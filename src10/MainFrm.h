#pragma once

#include "AddressBar.h"
#include "ImageView.h"
#include "ItemsView.h"

#include <array>

class CMainFrame : public SdkWindow
{
public:
	enum { kStatusTimer = 1, kThumbnailTrackId = 0x7f21 };
	HWND Create();
	bool PreTranslateMessage(MSG* message);

private:
	LRESULT WindowMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
	bool Command(int id);
	LRESULT OnCreate();
	void OnDestroy();
	void OnInitMenuPopup(HMENU menu);
	void OnSelectionChanged();
	bool OnAddressCommand(int code);
	bool AddressNavigate(const CShellItem& item);
	void SetFullscreen(bool fullscreen);
	bool ResizeToMonitor(HMONITOR monitor);
	bool BackOut();
	void UpdateStatus();
	void UpdateLayout();
	void LayoutStatusBar(const CRect& client);
	void UpdateFolder();

	CToolBarCtrl m_toolBar;
	CAddressBar m_wndAddress;
	CStatusBarCtrl m_statusBar;
	CTrackBarCtrl m_thumbnailSlider;
	HACCEL m_accelerators{};
	CRect m_splitterBounds;
	int m_splitPosition = -1;
	int m_splitOffset = 0;
	bool m_splitDragging = false;
	bool m_ready = false;
	bool m_fullscreen = false;
	HMENU m_windowMenu{};
	DWORD m_windowStyle{};
	WINDOWPLACEMENT m_windowPlacement{sizeof(WINDOWPLACEMENT)};

	CImageView m_wndImage;
	CItemsView m_wndItems;

	bool m_bShowAddress = true;
	bool m_bShowToolBar = true;
	bool m_bShowStatusBar = true;

	CString m_strFolder;
	CString m_strStatusItems;
	CString m_strStatusImage;
	std::array<int, 4> m_statusParts{0, 0, 0, -1};
	CRect m_thumbnailSliderBounds{0, 0, 0, 0};
};

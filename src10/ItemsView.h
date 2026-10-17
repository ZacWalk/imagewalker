#pragma once

// The items pane: one folder, drawn either as a thumbnail grid or as detail
// rows, with the view-mode strip in the scroll bar gutter.
//
// The selection is the only state that says what is on screen. When the focus
// moves the pane tells the frame once, and the frame tells the image pane.

#include "ViewBase.h"
#include "ImageView.h"
#include "Selection.h"
#include "Thumb.h"

#include "iw/workqueue.h"

#include <memory>
#include <vector>

class CDib;

// Sent to the parent when the selection changes. wParam and lParam are
// unused: the frame asks the pane what it now holds.
#define WM_IW_SELECTIONCHANGED (WM_APP + 1)

class CItemsView : public CViewBase<CItemsView>
{
public:
	CItemsView();
	~CItemsView();

	DECLARE_WND_CLASS_EX(_T("IWItemsView"), CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, -1)

	enum
	{
		kToolBarId = 0x7f20
	};

	BEGIN_MSG_MAP(CItemsView)
		MSG_WM_CREATE(OnCreate)
		MSG_WM_DESTROY(OnDestroy)
		MSG_WM_SIZE(OnSize)
		MSG_WM_LBUTTONDOWN(OnLButtonDown)
		MSG_WM_LBUTTONDBLCLK(OnLButtonDblClk)
		MSG_WM_MOUSEMOVE(OnMouseMove)
		MSG_WM_KEYDOWN(OnKeyDown)
		MSG_WM_SETFOCUS(OnSetFocus)

		NOTIFY_CODE_HANDLER_EX(TTN_GETDISPINFO, OnToolTipText)

		COMMAND_ID_HANDLER_EX(ID_ITEMS_OPTIONS, OnItemsOptions)
		COMMAND_ID_HANDLER_EX(ID_BROWSE_PARENT, OnBrowseParent)
		COMMAND_ID_HANDLER_EX(ID_BROWSE_BACK, OnBrowseBack)
		COMMAND_ID_HANDLER_EX(ID_BROWSE_FORWARD, OnBrowseForward)
		COMMAND_ID_HANDLER_EX(ID_EDIT_COPY, OnEditCopy)
		COMMAND_ID_HANDLER_EX(ID_EDIT_SELECT_ALL, OnEditSelectAll)
		COMMAND_ID_HANDLER_EX(ID_EDIT_INVERTSELECTION, OnEditInvertSelection)
		COMMAND_ID_HANDLER_EX(ID_VIEW_REFRESH, OnViewRefresh)
		COMMAND_ID_HANDLER_EX(ID_VIEW_SORT_NAME, OnViewSort)
		COMMAND_ID_HANDLER_EX(ID_VIEW_SORT_TYPE, OnViewSort)
		COMMAND_ID_HANDLER_EX(ID_VIEW_SORT_SIZE, OnViewSort)
		COMMAND_ID_HANDLER_EX(ID_VIEW_SORT_DATE, OnViewSort)
		COMMAND_ID_HANDLER_EX(ID_ITEMS_THUMBNAILS, OnViewMode)
		COMMAND_ID_HANDLER_EX(ID_ITEMS_DETAILS, OnViewMode)
		COMMAND_ID_HANDLER_EX(ID_VIEW_NEXTIMAGE, OnNextImage)
		COMMAND_ID_HANDLER_EX(ID_VIEW_PREVIOUSIMAGE, OnPreviousImage)

		CHAIN_MSG_MAP(CViewBase<CItemsView>)
	END_MSG_MAP()

	// What the image pane should be showing: every selected file, each with the
	// thumbnail this pane has already decoded for it, plus what the whole
	// selection amounts to.
	CPreviewSelection SelectedItems() const;

	CShellItem FolderItem() const;
	void Navigate(const CShellItem& item);

	CString StatusText() const;

	bool CanGoBack() const { return m_nHistory > 0; }
	bool CanGoForward() const { return m_nHistory + 1 < m_history.size(); }
	bool CanGoParent() const;
	bool IsDetailView() const { return m_bDetail; }
	int SortCommand() const { return m_nSortCommand; }

	void OnDraw(CDibDC& ddc);
	void LayoutControls(const CRect& rectClient);

	// Called from the jobs.
	void OnThumbJobComplete(CDib* pDib, UINT uKey);
	void OnDirChanged();

private:
	struct CItem
	{
		CThumb* pThumb;
		UINT uKey;
		bool bScanned;
	};

	LRESULT OnCreate(LPCREATESTRUCT lpcs);
	void OnDestroy();
	void OnSize(UINT nType, CSize size);
	void OnLButtonDown(UINT nFlags, CPoint point);
	void OnLButtonDblClk(UINT nFlags, CPoint point);
	void OnMouseMove(UINT nFlags, CPoint point);
	void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	void OnSetFocus(CWindow wndOld);

	LRESULT OnToolTipText(LPNMHDR pnmh);

	void OnItemsOptions(UINT, int, CWindow);
	void OnBrowseParent(UINT, int, CWindow);
	void OnBrowseBack(UINT, int, CWindow);
	void OnBrowseForward(UINT, int, CWindow);
	void OnEditCopy(UINT, int, CWindow);
	void OnEditSelectAll(UINT, int, CWindow);
	void OnEditInvertSelection(UINT, int, CWindow);
	void OnViewRefresh(UINT, int, CWindow);
	void OnViewSort(UINT, int nID, CWindow);
	void OnViewMode(UINT, int nID, CWindow);
	void OnNextImage(UINT, int, CWindow);
	void OnPreviousImage(UINT, int, CWindow);

	void OpenFolder(std::unique_ptr<CFolder> pFolder);
	void Populate();
	void RebuildItems();
	void Reselect(const CString& strFocus, bool bTop);
	void Sort();
	void CountLoaded();
	void SetSortCommand(int nID);
	void SetViewMode(bool bDetail);
	void UpdateBars();

	void Select(int n, bool bControl, bool bShift);
	void SelectionChanged();
	int ItemFromPoint(CPoint point) const;
	CRect ItemRect(int n) const;
	void InvalidateItem(int n);
	void EnsureVisible(int n);
	void LayoutItems(bool bTop);

	CString DisplayName(int n, DWORD dwFlags = SHGDN_NORMAL) const;

	// The focused file, which is what a reorder puts the selection back on.
	CString FocusFileName() const;

	void PrimeThumbChain();
	void OnThumbLoaded(CDib* pDib, UINT uKey);
	void OpenItem(int n);

	void DrawThumbnail(CDibDC& ddc, int n);
	void DrawDetailRow(CDibDC& ddc, int n);
	void DrawItemIcon(CDibDC& ddc, int n, CPoint point, bool bSmall);

	std::unique_ptr<CFolder> m_pFolder;
	std::vector<CItem> m_items;
	CSelection m_selection;

	std::vector<CShellItem> m_history;
	size_t m_nHistory = 0;

	int (*m_Compare)(const CShellFolder&, CThumb&, CThumb&) = nullptr;
	int m_nSortCommand = ID_VIEW_SORT_NAME;

	bool m_bDetail = false;
	int m_nThumbsX = 1;
	int m_nRowHeight = 18;
	int m_nLoaded = 0;

	CToolBarCtrl m_toolBar;
	CToolTipCtrl m_toolTip;
	CFont m_font;
	int m_nToolTipItem = -1;
	CString m_strToolText;

	IW::CWorkerThread m_threadLoader;
	IW::CWorkerThread m_threadDirectory;

	// Handed to every job. It dies with this window, and the message loop drops
	// any job whose owner has gone rather than completing it into a dead object.
	std::shared_ptr<void> m_alive;

	// Bumped whenever the items a queued job could refer to are rebuilt, so an
	// in-flight decode both cancels itself and is dropped on completion.
	LONG m_nThumbOrder = 0;

	// Thumbnail jobs posted and not yet completed. UI thread only.
	int m_nThumbJobsInFlight = 0;
};

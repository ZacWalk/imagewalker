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
class CJobDirChanged;

// Sent to the parent when the selection changes. wParam and lParam are
// unused: the frame asks the pane what it now holds.
#define WM_IW_SELECTIONCHANGED (WM_APP + 1)

class CItemsView : public CViewBase<CItemsView>
{
public:
	CItemsView();
	~CItemsView();

	HWND Create(HWND parent, RECT bounds, LPCTSTR title, DWORD style)
	{
		return CreateNative("IWItemsView", parent, bounds, title, style, 0, CS_DBLCLKS);
	}

	enum
	{
		kToolBarId = 0x7f20
	};

	bool Command(int id);

	// What the image pane should be showing: every selected file, each with the
	// thumbnail this pane has already decoded for it, plus what the whole
	// selection amounts to.
	CPreviewSelection SelectedItems() const;

	CShellItem FolderItem() const;
	void Navigate(const CShellItem& item);
	void ActivateFocus();
	bool FocusPhoto();
	void StepPhoto(int direction);

	CString StatusText() const;

	bool CanGoBack() const { return m_nHistory > 0; }
	bool CanGoForward() const { return m_nHistory + 1 < m_history.size(); }
	bool CanGoParent() const;
	bool IsDetailView() const { return m_bDetail; }
	int ThumbnailSize() const { return m_nThumbnailSize; }
	void SetThumbnailSize(int size, bool tracking);
	int SortCommand() const { return m_nSortCommand; }

	void OnDraw(CDibDC& ddc);
	void LayoutControls(const CRect& rectClient);

	// Called from the jobs.
	void OnThumbJobComplete(CDib* pDib, UINT uKey, int edge);
	void OnDirChanged(const CJobDirChanged& change);

private:
	LRESULT WindowMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
	struct CItem
	{
		CThumb* pThumb;
		UINT uKey;
		int nAttemptedEdge;
	};

	LRESULT OnCreate(LPCREATESTRUCT lpcs);
	void OnDestroy();
	void OnSize(UINT nType, CSize size);
	void OnLButtonDown(UINT nFlags, CPoint point);
	void OnLButtonDblClk(UINT nFlags, CPoint point);
	void OnMouseMove(UINT nFlags, CPoint point);
	bool OnKeyDown(UINT nChar);

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
	void Populate(const CString& strFocus, bool bTop);
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
	void UpdateThumbnailDecodeSize();

	CString DisplayName(int n, DWORD dwFlags = SHGDN_NORMAL) const;
	bool CanPreview(int index) const;

	// The focused item, which is what a refresh or reorder puts the selection back on.
	CString FocusItemName() const;

	void PrimeThumbChain();
	void OnThumbLoaded(CDib* pDib, UINT uKey, int edge);
	bool OpenFolderItem(int n);

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
	CThumbnailGrid m_grid;
	int m_nThumbnailSize = IMAGE_X;
	int m_nDecodeSize = IMAGE_X;
	int m_nRowHeight = 18;
	int m_nLoaded = 0;

	CToolBarCtrl m_toolBar;
	CToolTipCtrl m_toolTip;
	HFONT m_font{};
	int m_nToolTipItem = -1;
	CString m_strToolText;

	IW::CWorkerThread m_threadLoader;
	IW::CWorkerThread m_threadDirectory;
	std::shared_ptr<CJobDirChanged> m_dirChange;

	// Handed to every job. It dies with this window, and the message loop drops
	// any job whose owner has gone rather than completing it into a dead object.
	std::shared_ptr<void> m_alive;

	// Bumped whenever the items a queued job could refer to are rebuilt, so an
	// in-flight decode both cancels itself and is dropped on completion.
	LONG m_nThumbOrder = 0;

	// Thumbnail jobs posted and not yet completed. UI thread only.
	int m_nThumbJobsInFlight = 0;
};

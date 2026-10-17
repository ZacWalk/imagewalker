#pragma once

// The image pane.
//
// It shows exactly what the frame last handed to Show(): one picture, a collage
// of the selection when more than one item is selected, or nothing. There is no
// other producer, so "what is selected" and "what is on screen" cannot drift
// apart.

#include "ViewBase.h"
#include "iw/workqueue.h"

#include <vector>

class CJobLoad;
class CJobScale;
class CJobCell;

// One thing the frame wants shown. The thumbnail is borrowed from the items
// pane for the duration of the call: it stands in, blown up to the size the
// decoded picture will occupy, until the decode lands.
//
// Folders and other things with no picture in them come through here too, so
// the pane can name what is selected rather than going blank.
struct CPreviewItem
{
	CString strName;
	CDib* pThumb = nullptr;
	LONGLONG nBytes = 0;
	bool bImage = false;
	bool bFocus = false;
};

// The whole selection. Only the first few items get a collage cell, so the
// count and the size are carried separately - they are what the pane reports
// about a selection larger than it can draw.
struct CPreviewSelection
{
	std::vector<CPreviewItem> items;
	int nSelected = 0;
	LONGLONG nBytes = 0;
};

class CImageView : public CViewBase<CImageView>
{
public:
	CImageView();
	~CImageView();

	DECLARE_WND_CLASS_EX(_T("IWImageView"), CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, -1)

	// 2.2's scale model: the three fit modes work out their own size on every
	// layout, eNormal keeps the percentage the user asked for.
	enum ScaleType { eNormal, eFit, eUp, eDown };

	enum
	{
		kToolBarId = 0x7f10,
		kScaleEditId = 0x7f11,
		kZoomTrackId = 0x7f12,

		kScaleMin = 1,
		kScaleMax = 1000,

		// What the zoom slider spans; the box and the menu are not limited to it.
		kTrackMin = 10,
		kTrackMax = 500,

		// More cells than this cannot be told apart in one pane, and each one
		// costs a decode. Matches 3.0's collage.
		kMaxCells = 24,
	};

	BEGIN_MSG_MAP(CImageView)
		MSG_WM_CREATE(OnCreate)
		MSG_WM_DESTROY(OnDestroy)
		MSG_WM_SIZE(OnSize)
		MSG_WM_HSCROLL(OnHScroll)
		MSG_WM_LBUTTONDOWN(OnLButtonDown)
		MSG_WM_LBUTTONUP(OnLButtonUp)
		MSG_WM_MOUSEMOVE(OnMouseMove)
		MSG_WM_CAPTURECHANGED(OnCaptureChanged)
		MSG_WM_SETCURSOR(OnSetCursor)
		COMMAND_ID_HANDLER_EX(ID_MODE_FITTOWINDOW, OnModeFit)
		COMMAND_ID_HANDLER_EX(ID_MODE_ACTUALSIZE, OnModeActualSize)
		COMMAND_ID_HANDLER_EX(ID_MODE_SCALEDOWNTOFIT, OnModeScaleDown)
		COMMAND_ID_HANDLER_EX(ID_MODE_SCALEUPTOFIT, OnModeScaleUp)
		COMMAND_ID_HANDLER_EX(ID_MODE_ZOOM, OnModeMenu)
		COMMAND_ID_HANDLER_EX(ID_NAVIGATE, OnNavigate)
		COMMAND_RANGE_HANDLER_EX(ID_MODE_25, ID_MODE_200, OnModeZoom)
		COMMAND_HANDLER_EX(kScaleEditId, EN_KILLFOCUS, OnScaleKillFocus)
		NOTIFY_CODE_HANDLER_EX(TTN_GETDISPINFO, OnToolTipText)
		CHAIN_MSG_MAP(CViewBase<CImageView>)

	// The strip, subclassed. The navigator has to open on the press, and a
	// toolbar reports a click only on the release.
	//
	// MESSAGE_HANDLER, not MSG_WM_LBUTTONDOWN: every WTL _EX macro calls
	// SetMsgHandled, which writes through this object's m_pCurrentMsg - and a
	// contained window's WindowProc sets that on itself, not on its owner, so
	// the write goes to null.
	ALT_MSG_MAP(1)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnToolBarDown)
	END_MSG_MAP()

	// The one entry point. An empty selection clears the pane.
	void Show(const CPreviewSelection& selection);

	// What the status bar reports about what is on screen.
	CString StatusText() const;

	ScaleType GetScaleType() const { return m_eScaleType; }
	bool IsFit() const { return m_eScaleType == eFit; }
	bool IsActualSize() const { return m_eScaleType == eNormal && m_nScaleSet == 100; }

	// Reads the scale edit box and applies it. The frame calls this on Enter.
	void ApplyScaleText();
	bool ScaleEditHasFocus() const { return ::GetFocus() == m_editScale.m_hWnd; }

	void OnDraw(CDibDC& ddc);
	void LayoutControls(const CRect& rectClient);

	void OnLoaded(CJobLoad& job);
	void OnScaled(CJobScale& job);
	void OnCellLoaded(CJobCell& job);

private:
	// One tile of the multi-selection collage. Each cell decodes at its own
	// size rather than borrowing the items pane's thumbnail, which is capped
	// far below the size a cell reaches.
	enum CellState { cell_none, cell_queued, cell_resident, cell_skipped };

	struct CCell
	{
		CString strName;
		CSize sizeSource{0, 0}; // the file's own pixels, which is what packs the layout
		CRect rectCell;
		CDib dib; // the thumbnail first, then the cell-sized decode
		CellState eState = cell_none;
		bool bFocus = false;
	};

	LRESULT OnCreate(LPCREATESTRUCT lpcs);
	void OnDestroy();
	void OnSize(UINT nType, CSize size);
	void OnHScroll(int nSBCode, short nPos, CScrollBar bar);
	void OnLButtonDown(UINT nFlags, CPoint point);
	void OnLButtonUp(UINT nFlags, CPoint point);
	void OnMouseMove(UINT nFlags, CPoint point);
	void OnCaptureChanged(CWindow wnd);
	BOOL OnSetCursor(CWindow wnd, UINT nHitTest, UINT message);

	void OnModeFit(UINT, int, CWindow);
	void OnModeActualSize(UINT, int, CWindow);
	void OnModeScaleDown(UINT, int, CWindow);
	void OnModeScaleUp(UINT, int, CWindow);
	void OnModeZoom(UINT, int nID, CWindow);
	void OnModeMenu(UINT, int, CWindow);
	void OnNavigate(UINT, int, CWindow);
	void OnScaleKillFocus(UINT, int, CWindow);
	LRESULT OnToolBarDown(UINT, WPARAM, LPARAM lParam, BOOL& bHandled);
	LRESULT OnToolTipText(LPNMHDR pnmh);

	CRect ButtonRect(int nID);
	void SetButtonChecked(int nID, bool bCheck);
	void SetButtonEnabled(int nID, bool bEnable);

	void Reset();
	void ShowOne(const CPreviewItem* pItem);
	void ShowCollage(const CPreviewSelection& selection);
	void SetLabel(bool bFailed);
	CString CaptionFor(const CPreviewSelection& selection) const;
	CRect CaptionRect() const;
	void ArrangeCollage();
	void QueueCells();
	void DrawCollage(CDibDC& ddc);
	void DrawMessage(CDibDC& ddc);

	bool CanPan() const;

	void SetScale(int nPercent);
	void SetScaleType(ScaleType eType);
	void ParseScaleText(LPCTSTR szScale);
	CString GetScaleText() const;
	void SaveScale() const;

	void TrackNavigate(const CRect& rectButton);
	void TrackModeMenu(const CRect& rectButton);

	void UpdateScale();
	void UpdateBars();
	CSize CalcScaledSize(CSize sizeImage) const;
	void StartScaleJob(const CDib& dib, CSize size, BOOL bPromote);

	CContainedWindowT<CToolBarCtrl> m_toolBar;
	CEdit m_editScale;
	CTrackBarCtrl m_trackZoom;

	// The tooltip control keeps the pointer, so the text has to outlive the
	// notification that supplied it.
	CString m_strToolText;

	IW::CWorkerThread m_threadLoader;

	// Handed to every job; it dies with this window, and the message loop drops
	// any job whose owner has gone rather than completing it into a dead object.
	std::shared_ptr<void> m_alive;

	// Bumped whenever a new picture is asked for, so an in-flight decode both
	// cancels itself and is dropped on completion.
	LONG m_nLoadOrder = 0;

	// The same for the scaled copies, which are asked for far more often - one
	// per WM_SIZE of a splitter drag - and are superseded rather than awaited.
	LONG m_nScaleOrder = 0;

	CDib m_dibPreview;
	CDibScale* m_pScale = nullptr;

	CString m_strFileName;

	std::vector<CCell> m_cells;

	// What the pane says when it has no picture to show: the selection in a
	// collage, the file's name and size otherwise.
	CString m_strCaption;
	LONGLONG m_nBytes = 0;
	int m_nCaptionHeight = 22;
	bool m_bCollage = false;

	// The longest cell edge the resident decodes were made for.
	int m_nCellSize = 0;

	ScaleType m_eScaleType = eFit;
	int m_nScaleSet = 100; // what the user asked for
	int m_nScale = 100;    // what is on screen, as a percentage, for display

	// The source picture's own pixel size, which is known from the stand-in
	// thumbnail before the decode lands.
	CSize m_sizeImage{0, 0};

	// Where the picture goes, cached: a fit derived from ViewRect() on every
	// read would chase the scroll bar it just made appear.
	CSize m_sizeScaled{0, 0};

	// m_dibPreview is the stand-in thumbnail rather than the picture itself.
	bool m_bPlaceholder = false;

	bool m_bPanning = false;
	CPoint m_pointPanStart{0, 0};
	CPoint m_pointPanOrigin{0, 0};

	// The zoom slider's thumb is being dragged.
	bool m_bTracking = false;
};

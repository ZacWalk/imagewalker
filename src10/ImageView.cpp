// The image pane.

#include "stdafx.h"
#include "ArtMate.h"
#include "ImageView.h"
#include "Jobs.h"

// iw::ui::layout_collage is what 3.0 packs its collage with.
#include "util_layout.h"

#include <memory>

extern CDibScale* CreateScale(CDib& dib, CSize sizeDst, CStatus* pStatus);

static constexpr TCHAR szSection[] = _T("Default");
static constexpr TCHAR szScaleKey[] = _T("Preview Window Scale");

namespace
{
	// Toolbar glyph indices into IDB_PREVIEW_MODE, as 2.0 used them.
	enum { kGlyphFit = 1, kGlyphActual = 4, kGlyphNavigate = 3, kGlyphZoom = 6 };

	// The zoom slider never takes more than this, however wide the pane is.
	constexpr int kTrackWidthMax = 200;

	// Longest edge of the navigator thumbnail, as 2.2 sized it.
	constexpr int kNavigateSize = 160;

	// The gap between collage cells.
	constexpr int kCellGap = 4;

	// Around the text of a label drawn in the middle of the pane.
	constexpr int kLabelPadding = 6;

	// A cell is never decoded smaller than this, nor larger than the ceiling for
	// its crowd: one size serves every cell, so it is what bounds the pixels a
	// collage holds resident.
	constexpr int kCellSizeMin = 192;

	int CellSizeMax(size_t nCells)
	{
		return nCells <= 4 ? 1024 : (nCells <= 9 ? 640 : 384);
	}

	CRect CenterIn(CSize size, const CRect& box)
	{
		const CSize fitted = FitToBox(size, box.Size());

		return CRect(CPoint(box.left + (box.Width() - fitted.cx) / 2,
		                    box.top + (box.Height() - fitted.cy) / 2), fitted);
	}

	// EnableWindow repaints whether or not the state changed, and this runs on
	// every scale change.
	void EnableControl(CWindow wnd, bool bEnable)
	{
		if (wnd.IsWindow() && (wnd.IsWindowEnabled() != FALSE) != bEnable)
			wnd.EnableWindow(bEnable);
	}

	// A line of white text on a wash: how the pane names anything it has no
	// picture for.
	void DrawLabel(CDibDC& ddc, const CString& str, const CRect& rect)
	{
		CRect r(rect);

		if (str.IsEmpty() || r.IsRectEmpty())
			return;

		ddc.Draw(r, 0x40000000);
		ddc.SetTextColor(RGB(255, 255, 255));
		ddc.DrawText(str, -1, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}

	iw::recti ToRect(const CRect& r)
	{
		return iw::recti(r.left, r.top, r.Width(), r.Height());
	}

	CRect FromRect(const iw::recti& r)
	{
		return CRect(r.x, r.y, r.right(), r.bottom());
	}

	struct ZoomEntry
	{
		UINT nID;
		int nPercent;
	};

	// The scale box reads and writes through one table, so the names it accepts
	// and the names it puts back cannot drift apart. Fit is first: an empty box
	// means it.
	struct ScaleName
	{
		LPCTSTR szName;
		CImageView::ScaleType eType;
	};

	const ScaleName kScaleNames[] =
	{
		{_T("Fit"), CImageView::eFit},
		{_T("Up"), CImageView::eUp},
		{_T("Down"), CImageView::eDown},
	};

	int ClampScale(int nPercent)
	{
		return max(static_cast<int>(CImageView::kScaleMin),
		           min(static_cast<int>(CImageView::kScaleMax), nPercent));
	}

	constexpr ZoomEntry kZooms[] =
	{
		{ID_MODE_25, 25}, {ID_MODE_50, 50}, {ID_MODE_75, 75},
		{ID_MODE_100, 100}, {ID_MODE_150, 150}, {ID_MODE_200, 200},
	};

	void ClampToWorkArea(CRect& rect)
	{
		CRect rectWork(0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN));

		HMONITOR hMonitor = ::MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = {sizeof(MONITORINFO)};

		if (hMonitor != nullptr && ::GetMonitorInfo(hMonitor, &mi))
			rectWork = mi.rcWork;

		CSize offset(0, 0);

		if (rect.left < rectWork.left)
			offset.cx = rectWork.left - rect.left;
		else if (rect.right > rectWork.right)
			offset.cx = rectWork.right - rect.right;

		if (rect.top < rectWork.top)
			offset.cy = rectWork.top - rect.top;
		else if (rect.bottom > rectWork.bottom)
			offset.cy = rectWork.bottom - rect.bottom;

		rect.OffsetRect(offset);
	}

	// The pan navigator, ported from 2.2's CImageNavigation: a thumbnail of the
	// whole picture with the visible rectangle drawn on it. It is dragged with
	// the button still down, so it holds the capture and pumps its own loop -
	// the toolbar owns the mouse until we take it.
	class CImageNavigate : public CWindowImpl<CImageNavigate>
	{
	public:
		DECLARE_WND_CLASS_EX(_T("IWImageNavigate"), CS_SAVEBITS, COLOR_WINDOW)

		BEGIN_MSG_MAP(CImageNavigate)
			MSG_WM_PAINT(OnPaint)
			MSG_WM_MOUSEMOVE(OnMouseMove)
			MSG_WM_LBUTTONUP(OnLButtonUp)
			MSG_WM_KEYDOWN(OnKeyDown)
			MSG_WM_DESTROY(OnDestroy)
		END_MSG_MAP()

		CImageNavigate(CImageView* pView, CDib& dibThumb)
			: m_pView(pView), m_dibThumb(dibThumb)
		{
		}

		void Track(const CRect& rectButton)
		{
			const CSize sizeThumb(static_cast<int>(m_dibThumb.Width()),
			                      static_cast<int>(m_dibThumb.Height()));

			const CRect rectView = m_pView->ViewRect();
			const CSize sizeAll = m_pView->m_sizeScroll;

			if (sizeAll.cx <= 0 || sizeAll.cy <= 0 || sizeThumb.cx <= 0 || sizeThumb.cy <= 0)
				return;

			m_pointRestore = m_pView->m_pointScroll;

			m_rectVisible.SetRect(
				MulDiv(m_pointRestore.x, sizeThumb.cx, sizeAll.cx),
				MulDiv(m_pointRestore.y, sizeThumb.cy, sizeAll.cy),
				MulDiv(m_pointRestore.x + rectView.Width(), sizeThumb.cx, sizeAll.cx),
				MulDiv(m_pointRestore.y + rectView.Height(), sizeThumb.cy, sizeAll.cy));

			CRect rect(0, 0, sizeThumb.cx, sizeThumb.cy);
			rect.OffsetRect(rectButton.CenterPoint() - rect.CenterPoint());
			ClampToWorkArea(rect);

			if (Create(m_pView->m_hWnd, rect, nullptr,
			           WS_POPUP | WS_VISIBLE | WS_BORDER, WS_EX_TOOLWINDOW) == nullptr)
				return;

			CPoint point;
			::GetCursorPos(&point);
			ScreenToClient(&point);

			// The grab point, so the rectangle does not jump under the cursor.
			m_sizeGrab = m_rectVisible.TopLeft() - point;

			SetCapture();
			PumpUntilClosed();
		}

	private:
		void PumpUntilClosed()
		{
			MSG msg;

			while (m_hWnd != nullptr)
			{
				const BOOL bRet = ::GetMessage(&msg, nullptr, 0, 0);

				if (bRet == 0)
				{
					// WM_QUIT belongs to the outer loop, not to this one.
					::PostQuitMessage(static_cast<int>(msg.wParam));
					break;
				}

				if (bRet == -1)
					break;

				if (::GetCapture() != m_hWnd)
				{
					DestroyWindow();
					break;
				}

				::TranslateMessage(&msg);
				::DispatchMessage(&msg);
			}

			if (::GetCapture() == m_hWnd)
				::ReleaseCapture();
		}

		void ScrollToRect()
		{
			const CSize sizeThumb(static_cast<int>(m_dibThumb.Width()),
			                      static_cast<int>(m_dibThumb.Height()));
			const CSize sizeAll = m_pView->m_sizeScroll;

			m_pView->ScrollTo(MulDiv(m_rectVisible.left, sizeAll.cx, sizeThumb.cx),
			                  MulDiv(m_rectVisible.top, sizeAll.cy, sizeThumb.cy));
		}

		void OnPaint(CDCHandle)
		{
			CPaintDC dc(m_hWnd);

			CRect rect;
			GetClientRect(&rect);

			m_dibThumb.Draw(dc.m_hDC, CPoint(0, 0),
			                CRect(0, 0, static_cast<int>(m_dibThumb.Width()),
			                      static_cast<int>(m_dibThumb.Height())));

			dc.DrawFocusRect(&m_rectVisible);
		}

		void OnMouseMove(UINT, CPoint point)
		{
			CClientDC dc(m_hWnd);
			dc.DrawFocusRect(&m_rectVisible);

			const CSize size = m_rectVisible.Size();
			const CPoint pointNew = point + m_sizeGrab;

			m_rectVisible.SetRect(pointNew.x, pointNew.y,
			                      pointNew.x + size.cx, pointNew.y + size.cy);

			CRect rectBounds(0, 0, static_cast<int>(m_dibThumb.Width()),
			                 static_cast<int>(m_dibThumb.Height()));

			CSize offset(0, 0);

			if (m_rectVisible.left < rectBounds.left)
				offset.cx = rectBounds.left - m_rectVisible.left;
			else if (m_rectVisible.right > rectBounds.right)
				offset.cx = rectBounds.right - m_rectVisible.right;

			if (m_rectVisible.top < rectBounds.top)
				offset.cy = rectBounds.top - m_rectVisible.top;
			else if (m_rectVisible.bottom > rectBounds.bottom)
				offset.cy = rectBounds.bottom - m_rectVisible.bottom;

			m_rectVisible.OffsetRect(offset);

			dc.DrawFocusRect(&m_rectVisible);
			ScrollToRect();
		}

		void OnLButtonUp(UINT, CPoint)
		{
			DestroyWindow();
		}

		void OnKeyDown(UINT nChar, UINT, UINT)
		{
			if (nChar != VK_ESCAPE)
				return;

			m_pView->ScrollTo(m_pointRestore.x, m_pointRestore.y);
			DestroyWindow();
		}

		void OnDestroy()
		{
			if (::GetCapture() == m_hWnd)
				::ReleaseCapture();

			SetMsgHandled(FALSE);
		}

		CImageView* m_pView;
		CDib& m_dibThumb;
		CRect m_rectVisible;
		CSize m_sizeGrab{0, 0};
		CPoint m_pointRestore{0, 0};
	};
}

CImageView::CImageView() : m_toolBar(this, 1)
{
	m_alive = std::make_shared<int>(0);

	ParseScaleText(Settings::GetString(szSection, szScaleKey, _T("Fit")));
}

CImageView::~CImageView()
{
	::InterlockedIncrement(&m_nLoadOrder);
	::InterlockedIncrement(&m_nScaleOrder);
	m_threadLoader.Stop();
	m_alive.reset();

	delete m_pScale;
}

LRESULT CImageView::OnCreate(LPCREATESTRUCT)
{
	InitViewBase();

	// The strip lives in the bottom scroll bar gutter, which is therefore
	// always present whether or not the bar itself is needed.
	m_sizeGutterMin.cy = m_cyBar;

	m_toolBar.Create(m_hWnd, CRect(0, 0, 0, 0), nullptr,
	                 WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | CCS_NORESIZE | CCS_NOPARENTALIGN |
	                 CCS_NODIVIDER | TBSTYLE_TOOLTIPS,
	                 0, kToolBarId);

	m_toolBar.SetButtonStructSize(sizeof(TBBUTTON));

	CImageList images;
	images.CreateFromImage(IDB_PREVIEW_MODE, 8, 1, RGB(0xff, 0x00, 0xff),
	                       IMAGE_BITMAP, LR_CREATEDIBSECTION);
	m_toolBar.SetImageList(images.Detach());

	// Plain buttons, so the strip is glyphs only: a drop-down arrow doubles the
	// width of a button in a gutter one scroll bar wide. The two that open a
	// popup do it from their command handler.
	TBBUTTON buttons[] =
	{
		{kGlyphFit, ID_MODE_FITTOWINDOW, TBSTATE_ENABLED, BTNS_CHECK, {0, 0}, 0, 0},
		{kGlyphActual, ID_MODE_ACTUALSIZE, TBSTATE_ENABLED, BTNS_CHECK, {0, 0}, 0, 0},
		{kGlyphZoom, ID_MODE_ZOOM, TBSTATE_ENABLED, BTNS_BUTTON, {0, 0}, 0, 0},
		{kGlyphNavigate, ID_NAVIGATE, TBSTATE_ENABLED, BTNS_BUTTON, {0, 0}, 0, 0},
	};

	m_toolBar.AddButtons(_countof(buttons), buttons);
	m_toolBar.SetBitmapSize(CSize(8, 8));
	m_toolBar.SetButtonSize(CSize(m_cyBar, m_cyBar));

	m_editScale.Create(m_hWnd, CRect(0, 0, 0, 0), nullptr,
	                   WS_CHILD | WS_VISIBLE | ES_CENTER | ES_AUTOHSCROLL,
	                   WS_EX_STATICEDGE, kScaleEditId);

	m_editScale.SetFont(AtlGetStockFont(DEFAULT_GUI_FONT));
	m_editScale.LimitText(8);

	m_trackZoom.Create(m_hWnd, CRect(0, 0, 0, 0), nullptr,
	                   WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TBS_HORZ | TBS_NOTICKS,
	                   0, kZoomTrackId);

	m_trackZoom.SetRange(kTrackMin, kTrackMax, FALSE);
	m_trackZoom.SetPageSize(10);
	m_trackZoom.SetPos(m_nScale);

	{
		CClientDC dc(m_hWnd);
		HFONT hOld = dc.SelectFont(AtlGetStockFont(DEFAULT_GUI_FONT));
		TEXTMETRIC tm = {0};
		dc.GetTextMetrics(&tm);
		dc.SelectFont(hOld);

		m_nCaptionHeight = max(tm.tmHeight + 8, 22);
	}

	if (!m_threadLoader.Start())
		return -1;

	UpdateBars();
	return 0;
}

void CImageView::OnDestroy()
{
	SetMsgHandled(FALSE);
}

void CImageView::OnSize(UINT, CSize)
{
	if (m_bCollage)
		ArrangeCollage();
	else if (m_eScaleType != eNormal)
		// A fit mode is a function of the client size, so it has to be recomputed.
		UpdateScale();
	else
		SetScrollSize(m_sizeScroll.cx, m_sizeScroll.cy);

	Invalidate();
	SetMsgHandled(FALSE);
}

// The zoom slider shares WM_HSCROLL with the pane's own horizontal bar.
void CImageView::OnHScroll(int nSBCode, short nPos, CScrollBar bar)
{
	if (bar.m_hWnd != m_trackZoom.m_hWnd)
	{
		SetMsgHandled(FALSE);
		return;
	}

	// nPos is only meaningful while the thumb is being moved.
	const int nScale = (nSBCode == TB_THUMBPOSITION || nSBCode == TB_THUMBTRACK)
		                   ? nPos
		                   : m_trackZoom.GetPos();

	m_bTracking = (nSBCode == TB_THUMBTRACK);

	if (nScale != m_nScale || m_eScaleType != eNormal)
		SetScale(nScale);

	// The drag is over, so the scaled copy the tracking passes skipped is worth
	// building now.
	if (nSBCode == TB_ENDTRACK)
		UpdateScale();
}

//////////////////////////////////////////////////////////////////////////////
// Panning

bool CImageView::CanPan() const
{
	// A collage is laid out to the pane, so there is never anything to scroll.
	if (m_bCollage || !m_dibPreview.IsOpen())
		return false;

	const CRect r = ViewRect();

	return m_sizeScroll.cx > r.Width() || m_sizeScroll.cy > r.Height();
}

void CImageView::OnLButtonDown(UINT, CPoint point)
{
	if (!CanPan() || !ViewRect().PtInRect(point))
	{
		SetMsgHandled(FALSE);
		return;
	}

	m_bPanning = true;
	m_pointPanStart = point;
	m_pointPanOrigin = m_pointScroll;

	SetCapture();
}

void CImageView::OnMouseMove(UINT, CPoint point)
{
	if (!m_bPanning)
	{
		SetMsgHandled(FALSE);
		return;
	}

	// The picture follows the cursor, so the scroll offset moves against it.
	ScrollTo(m_pointPanOrigin.x - (point.x - m_pointPanStart.x),
	         m_pointPanOrigin.y - (point.y - m_pointPanStart.y));
}

void CImageView::OnLButtonUp(UINT, CPoint)
{
	if (m_bPanning)
		ReleaseCapture(); // WM_CAPTURECHANGED clears the flag
	else
		SetMsgHandled(FALSE);
}

// The only place the flag is cleared, so a capture lost to anything at all -
// a modal dialog, another window taking it - leaves no drag stuck on.
void CImageView::OnCaptureChanged(CWindow)
{
	m_bPanning = false;
}

BOOL CImageView::OnSetCursor(CWindow wnd, UINT nHitTest, UINT)
{
	if (wnd == *this && nHitTest == HTCLIENT && CanPan())
	{
		::SetCursor(::LoadCursor(nullptr, IDC_SIZEALL));
		return TRUE;
	}

	SetMsgHandled(FALSE);
	return FALSE;
}

//////////////////////////////////////////////////////////////////////////////
// What is on show

void CImageView::Show(const CPreviewSelection& selection)
{
	if (selection.nSelected > 1)
		ShowCollage(selection);
	else
		ShowOne(selection.items.empty() ? nullptr : &selection.items.front());
}

// Everything one mode has to put back before the other takes over. Both modes
// went through this by hand, which is one edit away from leaving a stale cell
// list or a scale table built for a picture that has gone.
void CImageView::Reset()
{
	// Every previous request, single or collage, is stale from here on.
	::InterlockedIncrement(&m_nLoadOrder);
	::InterlockedIncrement(&m_nScaleOrder);

	delete m_pScale;
	m_pScale = nullptr;

	m_cells.clear();
	m_nCellSize = 0;

	m_dibPreview.Close();
	m_strFileName.Empty();
	m_strCaption.Empty();
	m_nBytes = 0;

	m_bCollage = false;
	m_bPlaceholder = false;

	m_pointScroll = CPoint(0, 0);
	m_sizeImage = CSize(0, 0);
	m_sizeScaled = CSize(0, 0);
}

void CImageView::ShowOne(const CPreviewItem* pItem)
{
	Reset();

	if (pItem == nullptr)
	{
		UpdateScale();
		return;
	}

	m_strFileName = pItem->strName;
	m_nBytes = pItem->nBytes;

	// The thumbnail stands in until the decode lands, blown up to the size the
	// picture will occupy, so the picture replaces it without the pane jumping.
	if (pItem->pThumb != nullptr && pItem->pThumb->IsOpen())
	{
		m_dibPreview.Set(pItem->pThumb);
		m_sizeImage = pItem->pThumb->OriginalSize();
		m_bPlaceholder = true;
	}

	// A folder is never going to decode, so say so now rather than after a load
	// that was never worth starting.
	SetLabel(!pItem->bImage);
	UpdateScale();

	if (!pItem->bImage)
		return;

	auto job = std::make_shared<CJobLoad>(this, m_strFileName, &m_nLoadOrder, "Loading %d%%");
	job->SetOwner(m_alive);
	m_threadLoader.Post(job);
}

// What the pane says when there is no picture: the name, the size, and once a
// decode has been tried and failed, that there will not be one.
void CImageView::SetLabel(bool bFailed)
{
	m_strCaption.Empty();

	if (m_strFileName.IsEmpty())
		return;

	m_strCaption = ::PathFindFileName(m_strFileName);

	if (m_nBytes > 0)
		m_strCaption += _T("      ") + FormatFileSize(m_nBytes);

	if (bFailed)
		m_strCaption += _T("      no preview");
}

void CImageView::OnLoaded(CJobLoad& job)
{
	if (job.Exit())
		return;

	if (!job.m_bLoaded || !job.m_dib.IsOpen())
	{
		// Nothing decoded. Drop the stand-in too rather than leave a thumbnail
		// blown up over a file that cannot be shown, and say what it was.
		m_dibPreview.Close();
		m_bPlaceholder = false;
		m_sizeImage = CSize(0, 0);
		SetLabel(true);
		UpdateScale();
		return;
	}

	// The size is settled here even though the pixels are not on show yet, so
	// the stand-in is already filling the rectangle the picture will.
	m_sizeImage = job.m_dib.Size();
	UpdateScale();

	if (m_sizeScaled == m_sizeImage)
	{
		m_dibPreview.Attach(job.m_dib);
		m_bPlaceholder = false;
		Invalidate();
		UpdateBars();
	}
	else
	{
		// Published only once its scaled copy is ready: scaling a full-size
		// picture during WM_PAINT stalls the pane for as long as it takes.
		StartScaleJob(job.m_dib, m_sizeScaled, TRUE);
	}
}

void CImageView::OnScaled(CJobScale& job)
{
	if (job.Exit())
		return;

	if (job.m_bPromote)
	{
		m_dibPreview.Attach(job.m_dibSrc);
		m_bPlaceholder = false;
	}

	delete m_pScale;

	// Ownership moves here; the job's destructor frees whatever it still holds.
	m_pScale = job.m_pScale;
	job.m_pScale = nullptr;

	// The pane may have been resized while this was in flight. Tested against
	// the size the job was given, not against the table it produced, so a scale
	// that failed cannot start another one for the same size for ever.
	if (!m_bPlaceholder && job.m_Size != m_sizeScaled && m_sizeScaled != m_sizeImage)
		StartScaleJob(m_dibPreview, m_sizeScaled, FALSE);

	UpdateBars();
	Invalidate();
}

void CImageView::StartScaleJob(const CDib& dib, CSize size, BOOL bPromote)
{
	// A superseded re-scale cancels itself: a splitter drag asks for one per
	// WM_SIZE. A promote is not one of those - it is the only thing that will
	// ever publish the decode, so it hangs off the load instead and a scale
	// change must not discard it.
	if (!bPromote)
		::InterlockedIncrement(&m_nScaleOrder);

	auto job = std::make_shared<CJobScale>(this, dib, size,
	                                       bPromote ? &m_nLoadOrder : &m_nScaleOrder,
	                                       "Scaling %d%%", bPromote);
	job->SetOwner(m_alive);
	m_threadLoader.Post(job);
}

//////////////////////////////////////////////////////////////////////////////
// The collage
//
// More than one selected item is shown as 3.0 shows it: the pane is split
// recursively into one cell per file, and each cell decodes at its own size
// rather than borrowing the items pane's thumbnail, which is capped far below
// the size a cell reaches.

// What the pane says about a selection it cannot show one picture of.
CString CImageView::CaptionFor(const CPreviewSelection& selection) const
{
	CString str;
	str.Format(_T("%d items selected"), selection.nSelected);

	if (selection.nBytes > 0)
		str += _T("      ") + FormatFileSize(selection.nBytes);

	// Folders, unreadable files and the cell cap all leave some of the
	// selection undrawn, and silently showing fewer tiles than were picked
	// looks like a bug.
	if (static_cast<int>(selection.items.size()) < selection.nSelected)
	{
		CString strShown;
		strShown.Format(_T("      showing %d"), static_cast<int>(selection.items.size()));
		str += strShown;
	}

	return str;
}

CRect CImageView::CaptionRect() const
{
	CRect r = ViewRect();

	// Nothing to say, or too little pane to give a whole strip away.
	if (m_strCaption.IsEmpty() || r.Height() < m_nCaptionHeight * 3)
		return CRect(r.left, r.top, r.right, r.top);

	r.bottom = r.top + m_nCaptionHeight;
	return r;
}

void CImageView::ShowCollage(const CPreviewSelection& selection)
{
	const int nWanted = min(static_cast<int>(selection.items.size()), static_cast<int>(kMaxCells));

	// A shift-click that only extends the selection must not throw away the
	// decodes already in flight for the cells it keeps.
	bool bSame = m_bCollage && static_cast<int>(m_cells.size()) == nWanted;

	for (int i = 0; bSame && i < nWanted; i++)
		bSame = m_cells[i].strName == selection.items[i].strName;

	if (bSame)
	{
		for (int i = 0; i < nWanted; i++)
			m_cells[i].bFocus = selection.items[i].bFocus;

		m_strCaption = CaptionFor(selection);
		Invalidate();
		return;
	}

	Reset();

	m_bCollage = true;
	m_strCaption = CaptionFor(selection);
	m_cells.resize(nWanted);

	for (int i = 0; i < nWanted; i++)
	{
		const CPreviewItem& item = selection.items[i];
		CCell& cell = m_cells[i];

		cell.strName = item.strName;
		cell.bFocus = item.bFocus;

		// A folder has no decode to wait for, so it goes straight to its name.
		if (!item.bImage)
			cell.eState = cell_skipped;

		if (item.pThumb != nullptr && item.pThumb->IsOpen())
		{
			cell.dib.Set(item.pThumb);
			cell.sizeSource = item.pThumb->OriginalSize();
		}
	}

	// A collage is laid out to the pane, so there is nothing to scroll and no
	// bars.
	SetScrollSize(0, 0);

	ArrangeCollage();
	UpdateBars();
	Invalidate();
}

void CImageView::ArrangeCollage()
{
	const int nCount = static_cast<int>(m_cells.size());

	if (nCount == 0)
		return;

	CRect rectView = ViewRect();
	rectView.top = CaptionRect().bottom;

	std::vector<iw::sizei> sizes;
	sizes.reserve(nCount);

	for (const CCell& cell : m_cells)
	{
		// An unknown shape packs as a 4:3 frame until its own decode says
		// otherwise.
		sizes.push_back(cell.sizeSource.cx > 0 && cell.sizeSource.cy > 0
			                ? iw::sizei(cell.sizeSource.cx, cell.sizeSource.cy)
			                : iw::sizei(4, 3));
	}

	const std::vector<iw::recti> packed = iw::ui::layout_collage(ToRect(rectView), sizes);

	for (int i = 0; i < nCount; i++)
		m_cells[i].rectCell = i < static_cast<int>(packed.size())
			                      ? FromRect(packed[i])
			                      : CRect(0, 0, 0, 0);

	QueueCells();
}

void CImageView::QueueCells()
{
	const int nCount = static_cast<int>(m_cells.size());
	int nLongest = 0;

	for (const CCell& cell : m_cells)
		nLongest = max(nLongest, max(cell.rectCell.Width(), cell.rectCell.Height()));

	if (nLongest <= 0)
		return;

	// The ceiling falls as the cell count rises - a crowded cell is small
	// anyway - so growing the selection has to lower it again, or two dozen new
	// cells inherit the size two cells earned.
	const int nCeiling = CellSizeMax(nCount);
	const int nWanted = min(max(nLongest, kCellSizeMin), nCeiling);

	m_nCellSize = min(m_nCellSize, nCeiling);

	if (nWanted > m_nCellSize * 3 / 2)
	{
		// A materially larger cell deserves a sharper decode, and what is on
		// screen stays there until the replacement lands. Shrinking never
		// re-decodes, so dragging the splitter cannot oscillate.
		m_nCellSize = nWanted;

		for (CCell& cell : m_cells)
			if (cell.eState == cell_resident && cell.dib.IsOpen() &&
				cell.dib.Size() != cell.sizeSource)
				cell.eState = cell_none;
	}

	for (int i = 0; i < nCount; i++)
	{
		if (m_cells[i].eState != cell_none)
			continue;

		auto job = std::make_shared<CJobCell>(this, m_cells[i].strName, i,
		                                      CSize(m_nCellSize, m_nCellSize), &m_nLoadOrder);
		job->SetOwner(m_alive);

		// Only claim the cell if the job is really queued: nothing would ever
		// complete to release it otherwise, and no one else would ask again.
		if (!m_threadLoader.Post(job))
			return;

		m_cells[i].eState = cell_queued;
	}
}

void CImageView::OnCellLoaded(CJobCell& job)
{
	if (job.Exit() || job.m_nCell < 0 || job.m_nCell >= static_cast<int>(m_cells.size()))
		return;

	CCell& cell = m_cells[job.m_nCell];

	if (!job.m_dib.IsOpen())
	{
		// Nothing to ask for again; the thumbnail, if there is one, stays.
		cell.eState = cell_skipped;
		return;
	}

	const bool bReshape = cell.sizeSource != job.m_sizeSource;

	cell.dib.Attach(job.m_dib);
	cell.sizeSource = job.m_sizeSource;
	cell.eState = cell_resident;

	// The packing is by aspect, so a cell whose real shape was not known from a
	// thumbnail changes the whole layout.
	if (bReshape)
		ArrangeCollage();

	Invalidate();
}

void CImageView::DrawCollage(CDibDC& ddc)
{
	CFontHandle* pOldFont = ddc.SelectObject(AtlGetStockFont(DEFAULT_GUI_FONT));

	DrawLabel(ddc, m_strCaption, CaptionRect());

	for (CCell& cell : m_cells)
	{
		CRect rect(cell.rectCell);
		rect.DeflateRect(kCellGap / 2, kCellGap / 2);

		if (rect.IsRectEmpty())
			continue;

		if (cell.dib.IsOpen())
		{
			ddc.Draw(cell.dib, CenterIn(cell.dib.Size(), rect));
		}
		else
		{
			// A folder, a file that is not a picture, or one whose turn has not
			// come. Either way the name is the only thing the pane can say.
			DrawLabel(ddc, ::PathFindFileName(cell.strName), rect);
		}

		if (cell.bFocus)
		{
			CBrush brush;
			brush.CreateSolidBrush(RGB(0, 255, 0));
			ddc.FrameRect(rect, brush);
		}
	}

	if (pOldFont != nullptr)
		ddc.SelectObject(*pOldFont);
}

//////////////////////////////////////////////////////////////////////////////
// Geometry and drawing

// One selected thing with no picture in it - a folder, a video, a raw file, or
// one still decoding. The pane names it rather than going blank.
void CImageView::DrawMessage(CDibDC& ddc)
{
	CFontHandle* pOldFont = ddc.SelectObject(AtlGetStockFont(DEFAULT_GUI_FONT));

	const CRect rectView = ViewRect();

	CRect rectText(0, 0, max(rectView.Width() - kLabelPadding * 4, 0), 0);
	ddc.DrawText(m_strCaption, -1, rectText, DT_CALCRECT | DT_CENTER | DT_SINGLELINE);

	CRect rect(rectText);
	rect.InflateRect(kLabelPadding * 2, kLabelPadding);
	rect.OffsetRect(rectView.CenterPoint() - rect.CenterPoint());

	DrawLabel(ddc, m_strCaption, rect);

	if (pOldFont != nullptr)
		ddc.SelectObject(*pOldFont);
}

void CImageView::OnDraw(CDibDC& ddc)
{
	if (m_bCollage)
	{
		DrawCollage(ddc);
		return;
	}

	if (!m_dibPreview.IsOpen() || m_sizeScaled.cx <= 0 || m_sizeScaled.cy <= 0)
	{
		DrawMessage(ddc);
		return;
	}

	// Centring cannot be done with a negative scroll offset - it is clamped to
	// zero - so a picture smaller than the pane is centred as it is drawn.
	const CRect rectView = ViewRect();
	const CPoint origin(max((rectView.Width() - m_sizeScaled.cx) / 2, 0),
	                    max((rectView.Height() - m_sizeScaled.cy) / 2, 0));

	if (m_sizeScaled == m_dibPreview.Size())
		ddc.Draw(m_dibPreview, origin);
	else
		ddc.Draw(m_dibPreview, CRect(origin, m_sizeScaled), m_pScale);
}

// 2.2's rule: "fit" always resizes, "up" only enlarges a picture smaller than
// the pane and "down" only shrinks one bigger than it. Neither touches an image
// that is already on the right side of the client.
CSize CImageView::CalcScaledSize(CSize sizeImage) const
{
	if (sizeImage.cx <= 0 || sizeImage.cy <= 0)
		return CSize(0, 0);

	if (m_eScaleType == eNormal)
		return CSize(max(MulDiv(sizeImage.cx, m_nScaleSet, 100), 1),
		             max(MulDiv(sizeImage.cy, m_nScaleSet, 100), 1));

	const CRect r = ViewRect();

	if (r.Width() <= 0 || r.Height() <= 0)
		return sizeImage;

	if (m_eScaleType == eDown && r.Width() >= sizeImage.cx && r.Height() >= sizeImage.cy)
		return sizeImage;

	if (m_eScaleType == eUp && !(r.Width() > sizeImage.cx && r.Height() > sizeImage.cy))
		return sizeImage;

	return FitToBox(sizeImage, CSize(r.Width(), r.Height()));
}

void CImageView::SetScale(int nPercent)
{
	m_eScaleType = eNormal;
	m_nScaleSet = ClampScale(nPercent);

	UpdateScale();
	SaveScale();
}

void CImageView::SetScaleType(ScaleType eType)
{
	m_eScaleType = eType;

	// "Up" and "down" fall back to 1:1 for the images they leave alone.
	if (eType != eNormal)
		m_nScaleSet = 100;

	UpdateScale();
	SaveScale();
}

// Accepts what 2.2's scale box accepted: a mode name, a ratio, or a percentage.
void CImageView::ParseScaleText(LPCTSTR szScale)
{
	CString str(szScale);
	str.Trim();

	if (str.IsEmpty())
		str = kScaleNames[0].szName;

	for (const ScaleName& mode : kScaleNames)
	{
		if (str.CompareNoCase(mode.szName) == 0)
		{
			m_eScaleType = mode.eType;

			// "Up" and "down" fall back to 1:1 for the images they leave alone.
			m_nScaleSet = 100;
			return;
		}
	}

	const int nColon = str.Find(_T(':'));

	const int nPercent = (nColon >= 0)
		                     ? MulDiv(_ttoi(str.Left(nColon)), 100, max(_ttoi(str.Mid(nColon + 1)), 1))
		                     : _ttoi(str);

	m_eScaleType = eNormal;
	m_nScaleSet = ClampScale(nPercent);
}

CString CImageView::GetScaleText() const
{
	for (const ScaleName& mode : kScaleNames)
		if (mode.eType == m_eScaleType)
			return CString(mode.szName);

	CString str;
	str.Format(_T("%d%%"), m_nScaleSet);
	return str;
}

void CImageView::SaveScale() const
{
	Settings::SetString(szSection, szScaleKey, GetScaleText());
}

void CImageView::UpdateScale()
{
	delete m_pScale;
	m_pScale = nullptr;

	m_sizeScaled = CalcScaledSize(m_sizeImage);

	if (m_sizeScaled.cx <= 0 || m_sizeScaled.cy <= 0)
	{
		m_nScale = m_nScaleSet;
		SetScrollSize(1, 1, j_center);
		UpdateBars();
		Invalidate();
		return;
	}

	// The percentage is what the box and the slider report; the size above is
	// what the pane is actually laid out to.
	m_nScale = max(MulDiv(m_sizeScaled.cx, 100, m_sizeImage.cx), kScaleMin);

	SetScrollSize(m_sizeScaled.cx, m_sizeScaled.cy, j_center);

	// The stand-in is stretched as it is drawn; a scaling table is only worth
	// building for the picture itself, and not for every frame of a slider drag
	// - each one is a full pass over the picture and a copy of it.
	if (!m_bPlaceholder && !m_bTracking && m_dibPreview.IsOpen() &&
		m_sizeScaled != m_dibPreview.Size())
		StartScaleJob(m_dibPreview, m_sizeScaled, FALSE);

	UpdateBars();
	Invalidate();
}

void CImageView::UpdateBars()
{
	// Everything on the strip acts on one picture. A collage, or an empty pane,
	// leaves nothing for any of it to act on.
	const bool bImage = !m_bCollage && m_dibPreview.IsOpen();

	if (m_toolBar.IsWindow())
	{
		SetButtonEnabled(ID_MODE_FITTOWINDOW, bImage);
		SetButtonEnabled(ID_MODE_ACTUALSIZE, bImage);
		SetButtonEnabled(ID_MODE_ZOOM, bImage);
		SetButtonEnabled(ID_NAVIGATE, CanPan());

		SetButtonChecked(ID_MODE_FITTOWINDOW, bImage && IsFit());
		SetButtonChecked(ID_MODE_ACTUALSIZE, bImage && IsActualSize());
	}

	EnableControl(m_trackZoom, bImage);
	EnableControl(m_editScale, bImage);

	// Not while the thumb is being dragged: the position on screen is already
	// the user's, and writing it back repaints the slider under the cursor.
	if (m_trackZoom.IsWindow() && !m_bTracking && m_trackZoom.GetPos() != m_nScale)
		m_trackZoom.SetPos(m_nScale);

	if (m_editScale.IsWindow() && !ScaleEditHasFocus())
	{
		const CString str = GetScaleText();
		CString strShown;
		m_editScale.GetWindowText(strShown);

		if (strShown != str)
			m_editScale.SetWindowText(str);
	}
}

void CImageView::SetButtonChecked(int nID, bool bCheck)
{
	if (((m_toolBar.GetState(nID) & TBSTATE_CHECKED) != 0) != bCheck)
		m_toolBar.CheckButton(nID, bCheck);
}

void CImageView::SetButtonEnabled(int nID, bool bEnable)
{
	if (((m_toolBar.GetState(nID) & TBSTATE_ENABLED) != 0) != bEnable)
		m_toolBar.EnableButton(nID, bEnable);
}

void CImageView::ApplyScaleText()
{
	CString str;
	m_editScale.GetWindowText(str);

	ParseScaleText(str);
	UpdateScale();
	SaveScale();
}

CString CImageView::StatusText() const
{
	// The caption is what the pane is showing whenever there is no picture.
	if (m_bCollage || !m_dibPreview.IsOpen())
		return m_strCaption;

	CString str = ::PathFindFileName(m_strFileName);

	if (m_sizeImage.cx > 0 && m_sizeImage.cy > 0)
	{
		CString strSize;
		strSize.Format(_T("  %d x %d  %d%%"), m_sizeImage.cx, m_sizeImage.cy, m_nScale);
		str += strSize;
	}

	return str;
}

// Bottom gutter, left to right: the horizontal scroll bar, the zoom slider, the
// scale box, then the mode strip hard against the right-hand gutter.
void CImageView::LayoutControls(const CRect& rectClient)
{
	const int cy = m_cyBar;
	const int y = rectClient.bottom - cy;

	// Measured from the buttons: the drop-down arrows widen two of them.
	CRect rectLast(0, 0, cy, cy);

	if (m_toolBar.GetButtonCount() > 0)
		m_toolBar.GetItemRect(m_toolBar.GetButtonCount() - 1, &rectLast);

	const int cxToolBar = rectLast.right;
	const int cxEdit = 48;

	// Stop short of the vertical gutter so the strip never lands in the corner
	// square the two bars share.
	const int xEnd = max(rectClient.right - m_sizeGutter.cx, 0L);
	const int xToolBar = max(xEnd - cxToolBar, 0);
	const int xEdit = max(xToolBar - cxEdit, 0);

	// Half of what is left over, capped, so the scroll bar keeps the rest.
	const int cxTrack = max(min(xEdit / 2, kTrackWidthMax), 0);
	const int xTrack = max(xEdit - cxTrack, 0);

	PlaceControl(m_trackZoom, CRect(xTrack, y, xTrack + cxTrack, y + cy));
	PlaceControl(m_editScale, CRect(xEdit, y, xEdit + cxEdit, y + cy));
	PlaceControl(m_toolBar, CRect(xToolBar, y, xToolBar + cxToolBar, y + cy));

	m_nReservedRight = cxToolBar + cxEdit + cxTrack;
}

void CImageView::OnModeFit(UINT, int, CWindow)
{
	SetScaleType(eFit);
}

void CImageView::OnModeActualSize(UINT, int, CWindow)
{
	SetScale(100);
}

void CImageView::OnModeScaleDown(UINT, int, CWindow)
{
	SetScaleType(eDown);
}

void CImageView::OnModeScaleUp(UINT, int, CWindow)
{
	SetScaleType(eUp);
}

void CImageView::OnModeZoom(UINT, int nID, CWindow)
{
	for (const ZoomEntry& zoom : kZooms)
		if (zoom.nID == static_cast<UINT>(nID))
			SetScale(zoom.nPercent);
}

// The popup is opened from the command handler rather than from TBN_DROPDOWN,
// so the button needs no drop-down arrow to be clickable.
CRect CImageView::ButtonRect(int nID)
{
	CRect r(0, 0, 0, 0);

	if (m_toolBar.IsWindow())
	{
		m_toolBar.GetRect(nID, &r);
		m_toolBar.ClientToScreen(&r);
	}

	return r;
}

void CImageView::OnModeMenu(UINT, int, CWindow)
{
	TrackModeMenu(ButtonRect(ID_MODE_ZOOM));
}

void CImageView::OnNavigate(UINT, int, CWindow)
{
	TrackNavigate(ButtonRect(ID_NAVIGATE));
}

// The navigator is dragged with the button still down, so it has to open on the
// press: a toolbar does not report a click until the release. Swallowing the
// press also means no WM_COMMAND follows and the button never latches down.
LRESULT CImageView::OnToolBarDown(UINT, WPARAM, LPARAM lParam, BOOL& bHandled)
{
	CPoint point(static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)));

	const int nHit = m_toolBar.HitTest(&point);

	if (nHit < 0 || nHit != m_toolBar.CommandToIndex(ID_NAVIGATE) || !CanPan())
	{
		bHandled = FALSE;
		return 0;
	}

	TrackNavigate(ButtonRect(ID_NAVIGATE));
	return 0;
}

void CImageView::OnScaleKillFocus(UINT, int, CWindow)
{
	ApplyScaleText();
}

// The strip's popup is the frame's own Image menu. One copy of the items, and
// the frame's WM_INITMENUPOPUP is what puts the marks on it.
void CImageView::TrackModeMenu(const CRect& rectButton)
{
	CMenu menu;

	if (!menu.LoadMenu(IDR_MAINFRAME))
		return;

	CMenuHandle popup = menu.GetSubMenu(kMenuImage);

	// The strip sits on the bottom edge, so the menu has to grow upwards.
	//
	// Owned by the frame, not by the splitter that happens to be our parent:
	// the ids are on its Image menu and it routes them back here.
	popup.TrackPopupMenu(TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_LEFTBUTTON,
	                     rectButton.left, rectButton.top, GetTopLevelParent());
}

void CImageView::TrackNavigate(const CRect& rectButton)
{
	if (!CanPan() || !m_dibPreview.IsOpen())
		return;

	// A thumbnail of the whole picture, longest edge kNavigateSize.
	const std::unique_ptr<CDibScale> pThumb(
		CreateScale(m_dibPreview, FitToBox(m_sizeImage, CSize(kNavigateSize, kNavigateSize)), nullptr));

	if (!pThumb)
		return;

	CImageNavigate nav(this, pThumb->m_dib);
	nav.Track(rectButton);
}

LRESULT CImageView::OnToolTipText(LPNMHDR pnmh)
{
	auto pTTT = reinterpret_cast<LPNMTTDISPINFO>(pnmh);

	// The strip asks by button command id; anything by window handle is not ours.
	if (pTTT->uFlags & TTF_IDISHWND)
		return 0;

	switch (static_cast<UINT>(pnmh->idFrom))
	{
	case ID_MODE_FITTOWINDOW: m_strToolText = _T("Fit to Window");
		break;
	case ID_MODE_ACTUALSIZE: m_strToolText = _T("Actual Size");
		break;
	case ID_MODE_ZOOM: m_strToolText = _T("Zoom");
		break;
	case ID_NAVIGATE: m_strToolText = _T("Navigate");
		break;
	default:
		return 0;
	}

	pTTT->lpszText = const_cast<LPTSTR>(static_cast<LPCTSTR>(m_strToolText));
	pTTT->hinst = nullptr;

	return 0;
}

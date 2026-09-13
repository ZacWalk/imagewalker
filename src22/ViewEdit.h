// ImageWalker by Zac Walker
//
// Purpose: Edit mode: one picture, the crop rectangle on it, and the panel
//          of adjustments beside it.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

//
// EditView: the image editing mode.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "ImagingEdits.h"
#include "ViewBackBuffer.h"
#include "ViewSplitter.h"
#include "ViewYesNoDlg.h"

class EditView;

///////////////////////////////////////////////////////////////////////
// The canvas: the preview, the crop rectangle drawn over it, and the
// before/after comparison.

class EditCanvas : public CWindowImpl<EditCanvas>
{
public:

	DECLARE_WND_CLASS_EX(_T("IWEditCanvas"), CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, NULL)

	enum
	{
		HandleSize = 9,		// drawn size of a crop handle
		HandleGrab = 11,	// how close the mouse has to be to take one
		DividerGrab = 10,
		DividerGrip = 28,	// height of the block drawn on the divider
		DragSlop = 3,		// below this a press is a click, not a drag
		MinCrop = 8,		// in source pixels
		Margin = 8
	};

	// Which part of the crop the mouse took hold of. Corners and edges resize
	// and the middle moves; the crop is always on the picture, so there is no
	// gesture that starts one from nothing.
	enum class Grab
	{
		None, TopLeft, Top, TopRight, Right,
		BottomRight, Bottom, BottomLeft, Left,
		Move, Divider
	};

	// Off shows the edited image alone. Split runs a divider across one frame
	// so the two halves line up; Side puts the whole of each side by side.
	enum class Compare { Off, Split, Side };

	EditView *_pView;

	IW::Image _preview;			// source, scaled, with the edits applied
	IW::Image _before;			// the same source scaled but untouched

	// The edit stack runs over a scaled copy, and every slider tick rebuilds it.
	// Scaling the full-resolution file each time is what made dragging the
	// straighten slider crawl, so the copy is kept until the size it was made
	// for changes -- or until the view is handed a different file.
	IW::Image _scaled;
	CSize _sizeScaledFor;
	CSize _sizeBeforeFor;

	CSize _sizeSource;			// the file's own extent
	CSize _sizeTransformed;		// full-resolution extent the crop is measured in
	CRect _rectCropBounds;		// where a crop is allowed to be, in those pixels
	CRect _rectPreview;			// where the edited image lands in client coords
	CRect _rectBefore;			// where the untouched image lands
	double _scale = 1.0;		// _rectPreview / _sizeTransformed

	Compare _compare = Compare::Off;
	int _nDivider = 50;			// percent across _rectPreview

	Grab _grab = Grab::None;
	bool _bDragging = false;	// the press has travelled far enough to count
	CPoint _ptGrab;				// in image coordinates
	CPoint _ptGrabClient;
	CRect _rectGrabStart;

	// A solid dark bitmap the size of the preview, blended over what the crop
	// throws away. Kept because it is rebuilt on every paint otherwise, and the
	// crop repaints on every mouse move.
	CDC _dcDim;
	CBitmap _bmDim;
	CSize _sizeDim;

	explicit EditCanvas(EditView *pView) : _pView(pView)
	{
		_sizeSource.cx = _sizeSource.cy = 0;
		_sizeScaledFor.cx = _sizeScaledFor.cy = 0;
		_sizeBeforeFor.cx = _sizeBeforeFor.cy = 0;
		_sizeTransformed.cx = _sizeTransformed.cy = 0;
		_rectCropBounds.SetRectEmpty();
		_rectPreview.SetRectEmpty();
		_rectBefore.SetRectEmpty();
		_ptGrab.x = _ptGrab.y = 0;
		_ptGrabClient.x = _ptGrabClient.y = 0;
		_rectGrabStart.SetRectEmpty();
		_sizeDim.cx = _sizeDim.cy = 0;
	}

	BEGIN_MSG_MAP(EditCanvas)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
	END_MSG_MAP()

	LRESULT OnEraseBackground(UINT, WPARAM, LPARAM, BOOL&) { return 1; }

	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL& bHandled);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&);
	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM wParam, LPARAM lParam, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled);
	LRESULT OnSetCursor(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

	void Rebuild();
	void Invalidate() { if (m_hWnd) InvalidateRect(nullptr, FALSE); }

	// The scaled copies are keyed on the size they were built for, and nothing
	// about a size says which file it came from.
	void OnSourceChanged()
	{
		_scaled.Free();
		_before.Free();
		_sizeScaledFor.cx = _sizeScaledFor.cy = 0;
		_sizeBeforeFor.cx = _sizeBeforeFor.cy = 0;
	}

	void SetCompare(Compare compare);
	bool IsComparing() const { return _compare != Compare::Off; }

	// The area a crop may occupy. Straightening turns the picture inside the
	// same frame, so this is smaller than the frame as soon as it is used.
	CRect ImageBounds() const { return _rectCropBounds; }

	// What the crop is right now. An unset crop keeps the whole frame, so the
	// rectangle and its handles are always on the picture and always ready to
	// be dragged -- there is never a mode to enter first.
	CRect EffectiveCrop() const;

	CPoint ClientToImage(CPoint pt) const
	{
		if (_rectPreview.Width() < 1 || _rectPreview.Height() < 1)
			return CPoint(0, 0);

		return CPoint(
			MulDiv(pt.x - _rectPreview.left, _sizeTransformed.cx, _rectPreview.Width()),
			MulDiv(pt.y - _rectPreview.top, _sizeTransformed.cy, _rectPreview.Height()));
	}

	CRect ImageToClient(const CRect &rc) const
	{
		if (_sizeTransformed.cx < 1 || _sizeTransformed.cy < 1)
			return CRect(0, 0, 0, 0);

		return CRect(
			_rectPreview.left + MulDiv(rc.left, _rectPreview.Width(), _sizeTransformed.cx),
			_rectPreview.top + MulDiv(rc.top, _rectPreview.Height(), _sizeTransformed.cy),
			_rectPreview.left + MulDiv(rc.right, _rectPreview.Width(), _sizeTransformed.cx),
			_rectPreview.top + MulDiv(rc.bottom, _rectPreview.Height(), _sizeTransformed.cy));
	}

	int DividerX() const
	{
		return _rectPreview.left + MulDiv(_nDivider, _rectPreview.Width(), 100);
	}

	// The part of the canvas the edited image occupies, which is the only part
	// the crop applies to.
	CRect AfterRegion() const
	{
		CRect rc = _rectPreview;

		if (_compare == Compare::Split)
			rc.left = IW::Max(rc.left, DividerX());

		return rc;
	}

	Grab HitTest(CPoint pt) const;
	void Draw(CDCHandle dc, const CRect &rcClient);
	void DrawHandle(CDCHandle dc, CPoint pt) const;
	void DrawLabel(CDCHandle dc, CPoint pt, UINT nAlign, const CString &str) const;
	void NudgeCrop(int dx, int dy, bool bResize);
	bool PrepareDim(CDCHandle dc, CSize size);
};

///////////////////////////////////////////////////////////////////////
// The panel: one control per value on the edit stack.

class CEditPanelDlg : public CDialogImpl<CEditPanelDlg>
{
public:

	typedef CEditPanelDlg ThisClass;

	enum { IDD = IDD_EDIT_PANEL };

	EditView *_pView;

	// Set while the panel is writing to its own controls, so the change
	// notifications that causes do not read half-updated state back out.
	bool _bSetting;

	CEditPanelDlg() : _pView(nullptr), _bSetting(false)
	{
	}

	BEGIN_MSG_MAP(ThisClass)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_HSCROLL, OnScroll)

		COMMAND_ID_HANDLER(IDC_EDIT_COMPARE_OFF, OnCompareChanged)
		COMMAND_ID_HANDLER(IDC_EDIT_COMPARE_SPLIT, OnCompareChanged)
		COMMAND_ID_HANDLER(IDC_EDIT_COMPARE_SIDE, OnCompareChanged)

		COMMAND_ID_HANDLER(IDC_EDIT_ROTATE_LEFT, OnButton)
		COMMAND_ID_HANDLER(IDC_EDIT_ROTATE_RIGHT, OnButton)
		COMMAND_ID_HANDLER(IDC_EDIT_AUTO_STRAIGHTEN, OnButton)
		COMMAND_ID_HANDLER(IDC_EDIT_AUTO_COLOR, OnButton)
		COMMAND_ID_HANDLER(IDC_EDIT_RESET_GEOMETRY, OnButton)
		COMMAND_ID_HANDLER(IDC_EDIT_RESET_COLOR, OnButton)
		COMMAND_ID_HANDLER(IDC_EDIT_CROP, OnButton)
		COMMAND_ID_HANDLER(ID_EDIT_SAVE, OnButton)
		COMMAND_ID_HANDLER(ID_EDIT_SAVEAS, OnButton)
		COMMAND_ID_HANDLER(ID_EDIT_CANCEL_EDITS, OnButton)

	ALT_MSG_MAP(1)

		COMMAND_ID_HANDLER(IDC_EDIT_COMPARE_OFF, OnCompareChanged)
		COMMAND_ID_HANDLER(IDC_EDIT_COMPARE_SPLIT, OnCompareChanged)
		COMMAND_ID_HANDLER(IDC_EDIT_COMPARE_SIDE, OnCompareChanged)

		COMMAND_ID_HANDLER(IDC_EDIT_ROTATE_LEFT, OnButton)
		COMMAND_ID_HANDLER(IDC_EDIT_ROTATE_RIGHT, OnButton)
		COMMAND_ID_HANDLER(IDC_EDIT_AUTO_STRAIGHTEN, OnButton)
		COMMAND_ID_HANDLER(IDC_EDIT_AUTO_COLOR, OnButton)
		COMMAND_ID_HANDLER(IDC_EDIT_RESET_GEOMETRY, OnButton)
		COMMAND_ID_HANDLER(IDC_EDIT_RESET_COLOR, OnButton)
		COMMAND_ID_HANDLER(IDC_EDIT_CROP, OnButton)
		COMMAND_ID_HANDLER(ID_EDIT_SAVE, OnButton)
		COMMAND_ID_HANDLER(ID_EDIT_SAVEAS, OnButton)
		COMMAND_ID_HANDLER(ID_EDIT_CANCEL_EDITS, OnButton)
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL& bHandled);
	LRESULT OnScroll(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnButton(WORD, WORD wID, HWND, BOOL&);
	LRESULT OnCompareChanged(WORD, WORD wID, HWND, BOOL&);

	// Which of the three compare buttons is down.
	void ShowCompare(int nMode);

	bool PreTranslateMessage(MSG *pMsg)
	{
		if (m_hWnd == nullptr || !IsWindowVisible())
			return false;

		const HWND hWndFocus = ::GetFocus();

		if (hWndFocus != m_hWnd && !IsChild(hWndFocus))
			return false;

		return IsDialogMessage(pMsg) != FALSE;
	}

	// Positions every control for the width the pane actually has, and returns
	// the height it needs. The dialog template only says which controls exist:
	// stretching it sideways would leave each one at its design width with a
	// band of dead space beside it.
	int Layout(int cx, bool bApply);

	void SetupSlider(int id, int nMin, int nMax);

	// SetWindowText invalidates whether or not the text moved, and these are
	// rewritten on every mouse move of a crop drag.
	void SetTextIfChanged(int id, const CString &str);

	void ReadFrom(const ImageEdits &edits);
	void WriteTo(ImageEdits &edits) const;
	void ShowSliderValues(const ImageEdits &edits);
	void UpdateButtons(const ImageEdits &edits);
	void SetInfo(const CString &strName, const CString &strInfo);
};

///////////////////////////////////////////////////////////////////////
// The mode: canvas on the left, panel on the right.

class EditView :
	public CWindowImpl<EditView>,
	public CSplitter2Impl<EditView>,
	public ViewBase
{
public:

	typedef EditView ThisClass;

	DECLARE_WND_CLASS_EX(_T("IWEditView"), CS_HREDRAW | CS_VREDRAW, NULL)

	Coupling *_pCoupling;
	State &_state;

	EditCanvas _canvas;
	IW::CDialogScroll<CEditPanelDlg> _panel;

	// The edits sit here, not on the image: the source is reloaded untouched
	// and the stack is replayed, which is what makes every change undoable.
	ImageEdits _edits;
	IW::Image _source;
	CString _strSourcePath;

	// Set while Save is publishing its own result, so the image-changed callback
	// that causes does not replay the stack over an already-edited image.
	bool _bSaving = false;

	int _nDefaultSplitterPos;

	EditView(Coupling *pCoupling, State &state) :
		_pCoupling(pCoupling),
		_state(state),
		_canvas(this),
		_nDefaultSplitterPos(App.Settings.EditSplitterPos)
	{
	}

	BEGIN_MSG_MAP(EditView)

		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		MESSAGE_HANDLER(WM_SIZE, OnSize)

		CHAIN_MSG_MAP(CSplitter2Impl<EditView>)
		CHAIN_MSG_MAP_ALT_MEMBER(_panel, 1)

	END_MSG_MAP()

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL& bHandled)
	{
		_panel.GetDialog()._pView = this;

		// The sunken border round each pane is 2.2's, the same as the browse
		// panes either side of the splitter.
		_canvas.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
		               WS_EX_CLIENTEDGE);
		_panel.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
		              WS_EX_CLIENTEDGE);

		// The scroller scrolls the dialog template's own height. The panel lays
		// itself out instead, and every row of it is one line tall, so the
		// height it needs does not depend on the width it is given.
		_panel._rectClient.bottom = _panel._rectClient.top + _panel.GetDialog().Layout(200, false);

		SetSplitterPanes(_canvas, _panel);
		SetProportionalPos(_nDefaultSplitterPos);

		bHandled = FALSE;
		return 0;
	}

	LRESULT OnEraseBackground(UINT, WPARAM, LPARAM, BOOL&) { return 1; }

	LRESULT OnSize(UINT, WPARAM wParam, LPARAM, BOOL& bHandled)
	{
		if (wParam != SIZE_MINIMIZED)
			SetSplitterRect();

		bHandled = FALSE;
		return 1;
	}

	// ViewBase
	HWND Activate(HWND hWndParent) override
	{
		if (m_hWnd == 0)
			Create(hWndParent, rcDefault, nullptr, IW_WS_CHILD, 0);

		Reload();

		ShowWindow(SW_SHOW);
		_canvas.SetFocus();

		return m_hWnd;
	}

	void Deactivate() override
	{
		ShowWindow(SW_HIDE);
	}

	bool CanEditImages() const override { return true; }
	void OnTimer() override {}
	HWND GetImageWindow() override { return _canvas; }
	void OnOptionsChanged() override {}

	bool CanShowToolbar(DWORD id) override
	{
		// The main toolbar and the address bar are items-mode navigation, so
		// edit mode replaces them with a bar of its own.
		return id == IDC_EDIT_TOOLBAR || id == IDC_COMMAND_BAR || id == IDC_LOGO;
	}

	BOOL PreTranslateMessage(MSG *pMsg) override
	{
		return _panel.GetDialog().PreTranslateMessage(pMsg) ? TRUE : FALSE;
	}

	void OnNewImage(bool /*bScrollToCenter*/) override { if (!_bSaving) Reload(); }

	bool InvokeCommand(DWORD id) override;
	bool GetCommandState(DWORD id, bool &bEnabled, bool &bChecked) override;
	bool OnEscape() override;
	bool QueryLeave() override;

	// Takes a fresh copy of whatever the items mode has displayed and replays
	// the stack over it.
	void Reload();

	// Rebuilds the preview and the panel's summary line after any change.
	void OnEditsChanged();
	void ClampCrop();

	// The panel stacks its rows differently at different widths, so how far it
	// scrolls is not known until it has laid itself out.
	void PanelHeightChanged(int cy)
	{
		if (_panel.m_hWnd == nullptr || _panel._rectClient.Height() == cy)
			return;

		_panel._rectClient.bottom = _panel._rectClient.top + cy;
		_panel.DoSize();
	}

	// The crop is not baked into the preview bitmap, so moving it only needs a
	// repaint and a new readout -- not the decode a full rebuild costs.
	void OnCropChanged();

	void UpdateInfo();

	bool HasImage() const { return !_source.IsEmpty(); }
	bool HasChanges() const;

	bool Save(bool bSaveAs);
	void CancelEdits();
	void ResetCrop();
	void ToggleCompare();
	void Rotate(int nQuarterTurns);
	void AutoStraighten();
	void AutoColor();

	void LoadDefaultSettings()
	{
		_nDefaultSplitterPos = App.Settings.EditSplitterPos;
	}

	void SaveDefaultSettings()
	{
		App.Settings.EditSplitterPos = m_hWnd ? GetProportionalPos() : _nDefaultSplitterPos;
	}
};

///////////////////////////////////////////////////////////////////////
// EditCanvas

inline CRect EditCanvas::EffectiveCrop() const
{
	const CRect &crop = _pView->_edits._crop;
	return crop.IsRectEmpty() ? ImageBounds() : crop;
}

inline void EditCanvas::Rebuild()
{
	_preview.Free();
	_rectPreview.SetRectEmpty();
	_rectBefore.SetRectEmpty();
	_scale = 1.0;

	const IW::Image &source = _pView->_source;

	if (source.IsEmpty() || m_hWnd == nullptr)
		return;

	CRect rcClient;
	GetClientRect(rcClient);
	rcClient.DeflateRect(Margin, Margin);

	if (rcClient.Width() < 32 || rcClient.Height() < 32)
		return;

	const IW::Page page = source.GetFirstPage();
	_sizeSource = CSize(page.GetWidth(), page.GetHeight());

	if (_sizeSource.cx < 1 || _sizeSource.cy < 1)
		return;

	_sizeTransformed = _pView->_edits.TransformedSize(_sizeSource);
	_rectCropBounds = _pView->_edits.CropBounds(_sizeSource);

	// Side by side gives each image half the canvas; the other two modes share
	// one frame, so the divider reveals the same framing on both.
	CRect rcAfter = rcClient;
	CRect rcBefore = rcClient;

	if (_compare == Compare::Side)
	{
		const int nHalf = rcClient.Width() / 2;
		rcBefore.right = rcClient.left + nHalf - Margin / 2;
		rcAfter.left = rcClient.left + nHalf + Margin / 2;
	}

	_rectPreview = IW::FitToRect(_sizeTransformed, rcAfter, _scale);

	// The stack runs over a scaled copy, so a slider move costs the preview
	// rather than the full image.
	const CSize sizeScaled(
		IW::Max(1, static_cast<int>(_sizeSource.cx * _scale + 0.5)),
		IW::Max(1, static_cast<int>(_sizeSource.cy * _scale + 0.5)));

	if (_scaled.IsEmpty() || _sizeScaledFor != sizeScaled)
	{
		// At 100% there is nothing to resample: running the picture through the
		// scaler anyway costs a pass and softens every edge in the preview.
		// Below it the copy has to keep 8 bits a channel -- the default preview
		// is PF555, and five bits a channel bands under any tone curve.
		_scaled = (sizeScaled == _sizeSource) ? source : IW::CreatePreview(source, sizeScaled, true);
		_sizeScaledFor = sizeScaled;
	}

	ImageEdits edits = _pView->_edits;
	edits._crop.SetRectEmpty();

	if (!IW::ApplyEdits(_scaled, _preview, edits, IW::CNullStatus::Instance) || _preview.IsEmpty())
	{
		_preview.Free();
		_rectPreview.SetRectEmpty();
		return;
	}

	// Straighten and perspective leave the corners transparent, and those are
	// filled with the page background when drawn. Left at its default the frame
	// gained bright white wedges as soon as the slider moved.
	//
	// Only then: with nothing on the stack ApplyEdits hands back the source
	// itself, and the pages are refcount-shared with the picture the items mode
	// is displaying.
	if (edits.HasWarp())
		_preview.GetFirstPage().SetBackGround(IW::Style::Color::Window);

	if (_compare == Compare::Off)
		return;

	// Split lays the untouched image over exactly the frame the edited one
	// occupies, so only a rotation makes the two rectangles differ.
	double scaleBefore = 1.0;
	_rectBefore = (_compare == Compare::Side)
		              ? IW::FitToRect(_sizeSource, rcBefore, scaleBefore)
		              : IW::FitToRect(_sizeSource, _rectPreview, scaleBefore);

	if (_before.IsEmpty() || _sizeBeforeFor != _rectBefore.Size())
	{
		// Same depth as the edited side, or the comparison shows a difference
		// the edits did not make.
		_before = (_rectBefore.Size() == _sizeSource)
			          ? source
			          : IW::CreatePreview(source, _rectBefore.Size(), true);
		_sizeBeforeFor = _rectBefore.Size();
	}

	if (_before.IsEmpty())
		_rectBefore.SetRectEmpty();
}

inline void EditCanvas::SetCompare(Compare compare)
{
	if (_compare == compare)
		return;

	_compare = compare;
	_nDivider = 50;

	Rebuild();
	Invalidate();

	// Side by side halves the room the edited image gets, so the zoom figure
	// changes with the mode.
	_pView->UpdateInfo();
}

inline LRESULT EditCanvas::OnSize(UINT, WPARAM, LPARAM, BOOL& bHandled)
{
	Rebuild();

	// The zoom figure comes out of Rebuild, so the readout is only right if it
	// is written again after one.
	_pView->UpdateInfo();

	bHandled = FALSE;
	return 0;
}

inline void EditCanvas::DrawHandle(CDCHandle dc, CPoint pt) const
{
	const int n = HandleSize / 2;
	const CRect rc(pt.x - n, pt.y - n, pt.x + n + 1, pt.y + n + 1);

	dc.FillSolidRect(rc, RGB(255, 255, 255));
	dc.FrameRect(rc, static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
}

// A caption drawn straight onto the canvas. nAlign is a DT_ combination naming
// which corner of the text the point is.
inline void EditCanvas::DrawLabel(CDCHandle dc, CPoint pt, UINT nAlign, const CString &str) const
{
	if (str.IsEmpty())
		return;

	const HFONT hFontOld = dc.SelectFont(IW::Style::GetFont(IW::Style::Font::Small));

	CSize size;
	dc.GetTextExtent(str, str.GetLength(), &size);

	CRect rc(pt.x, pt.y, pt.x + size.cx + 8, pt.y + size.cy + 4);

	if (nAlign & DT_RIGHT) rc.OffsetRect(-rc.Width(), 0);
	if (nAlign & DT_BOTTOM) rc.OffsetRect(0, -rc.Height());

	dc.FillSolidRect(rc, RGB(0, 0, 0));

	const int nModeOld = dc.SetBkMode(TRANSPARENT);
	const COLORREF clrOld = dc.SetTextColor(RGB(255, 255, 255));

	dc.DrawText(str, str.GetLength(), rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

	dc.SetTextColor(clrOld);
	dc.SetBkMode(nModeOld);
	dc.SelectFont(hFontOld);
}

inline bool EditCanvas::PrepareDim(CDCHandle dc, CSize size)
{
	if (size.cx < 1 || size.cy < 1)
		return false;

	if (!_bmDim.IsNull() && _sizeDim == size)
		return true;

	// The DC goes first. A bitmap still selected into a DC cannot be deleted,
	// so freeing them the other way round leaks one preview-sized bitmap every
	// time the pane changes size.
	if (!_dcDim.IsNull())
		_dcDim.DeleteDC();

	if (!_bmDim.IsNull())
		_bmDim.DeleteObject();

	_sizeDim.cx = _sizeDim.cy = 0;

	if (_dcDim.CreateCompatibleDC(dc) == NULL)
		return false;

	if (_bmDim.CreateCompatibleBitmap(dc, size.cx, size.cy) == NULL)
		return false;

	_dcDim.SelectBitmap(_bmDim);
	_dcDim.FillSolidRect(0, 0, size.cx, size.cy, RGB(8, 8, 8));
	_sizeDim = size;

	return true;
}

inline LRESULT EditCanvas::OnPaint(UINT, WPARAM, LPARAM, BOOL&)
{
	CPaintDC dc(m_hWnd);

	CRect rcClient;
	GetClientRect(rcClient);

	// A paint here is the canvas colour, then the picture, then the dimming, then
	// the handles -- and the crop repaints on every mouse move, so straight to
	// the window that stack of layers is what the reader sees.
	BackBuffer buffer(dc.m_hDC);

	if (buffer.GetDC() == NULL)
	{
		Draw(dc.m_hDC, rcClient);
	}
	else if (!buffer.IsRectEmpty())
	{
		Draw(buffer.GetDC(), rcClient);
		buffer.Flip();
	}

	return 0;
}

inline void EditCanvas::Draw(CDCHandle dc, const CRect &rcClient)
{
	dc.FillSolidRect(rcClient, IW::Style::Color::Window);

	if (_preview.IsEmpty())
	{
		// An empty canvas with no explanation is the one state a viewer must
		// never leave a reader in.
		DrawLabel(dc.m_hDC, CPoint(rcClient.CenterPoint().x, rcClient.CenterPoint().y), DT_CENTER | DT_BOTTOM,
		          App.LoadString(IDS_EDIT_NO_IMAGE));
		return;
	}

	const int xDivider = DividerX();

	if (_compare == Compare::Split && !_before.IsEmpty())
	{
		const int nSaved = dc.SaveDC();
		dc.IntersectClipRect(_rectPreview.left, _rectPreview.top, xDivider, _rectPreview.bottom);
		IW::CRender::DrawToDC(dc, _before.GetFirstPage(), _rectBefore);
		dc.RestoreDC(nSaved);

		const int nSaved2 = dc.SaveDC();
		dc.IntersectClipRect(xDivider, _rectPreview.top, _rectPreview.right, _rectPreview.bottom);
		IW::CRender::DrawToDC(dc, _preview.GetFirstPage(), _rectPreview);
		dc.RestoreDC(nSaved2);
	}
	else
	{
		if (_compare == Compare::Side && !_before.IsEmpty())
			IW::CRender::DrawToDC(dc, _before.GetFirstPage(), _rectBefore);

		IW::CRender::DrawToDC(dc, _preview.GetFirstPage(), _rectPreview);
	}

	const CRect crop = EffectiveCrop();

	if (!crop.IsRectEmpty())
	{
		const CRect rc = ImageToClient(crop);
		const CRect rcAfter = AfterRegion();

		// Darken what the crop throws away rather than painting over it: you
		// cannot compose a crop against a flat grey block. Only the edited side
		// is dimmed -- the original is shown as it is.
		if (PrepareDim(dc.m_hDC, _rectPreview.Size()))
		{
			const CRect rcOutside[] =
			{
				CRect(_rectPreview.left, _rectPreview.top, _rectPreview.right, rc.top),
				CRect(_rectPreview.left, rc.bottom, _rectPreview.right, _rectPreview.bottom),
				CRect(_rectPreview.left, rc.top, rc.left, rc.bottom),
				CRect(rc.right, rc.top, _rectPreview.right, rc.bottom)
			};

			BLENDFUNCTION bf = {AC_SRC_OVER, 0, 170, 0};

			for (int i = 0; i < countof(rcOutside); i++)
			{
				CRect rcDim;

				if (!rcDim.IntersectRect(rcAfter, rcOutside[i]))
					continue;

				// Source and destination the same size. Stretching the source
				// instead left a bright seam along the edge of the crop.
				dc.AlphaBlend(rcDim.left, rcDim.top, rcDim.Width(), rcDim.Height(),
				              _dcDim,
				              rcDim.left - _rectPreview.left, rcDim.top - _rectPreview.top,
				              rcDim.Width(), rcDim.Height(), bf);
			}
		}

		dc.FrameRect(rc, static_cast<HBRUSH>(::GetStockObject(WHITE_BRUSH)));

		// Thirds and sixths while the crop is being moved, the way a camera
		// shows them: useful for placing an edge, clutter the rest of the time.
		if (_grab != Grab::None && _grab != Grab::Divider)
		{
			const COLORREF clrGuide = RGB(170, 170, 170);

			for (int div = 6; div >= 3; div /= 2)
			{
				for (int i = 1; i < div; i++)
				{
					if (div == 6 && (i % 2) == 0)
						continue;

					const int x = rc.left + MulDiv(i, rc.Width(), div);
					const int y = rc.top + MulDiv(i, rc.Height(), div);

					dc.FillSolidRect(x, rc.top, 1, rc.Height(), clrGuide);
					dc.FillSolidRect(rc.left, y, rc.Width(), 1, clrGuide);
				}
			}
		}

		const CPoint ptMid((rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2);

		DrawHandle(dc.m_hDC, CPoint(rc.left, rc.top));
		DrawHandle(dc.m_hDC, CPoint(ptMid.x, rc.top));
		DrawHandle(dc.m_hDC, CPoint(rc.right, rc.top));
		DrawHandle(dc.m_hDC, CPoint(rc.right, ptMid.y));
		DrawHandle(dc.m_hDC, CPoint(rc.right, rc.bottom));
		DrawHandle(dc.m_hDC, CPoint(ptMid.x, rc.bottom));
		DrawHandle(dc.m_hDC, CPoint(rc.left, rc.bottom));
		DrawHandle(dc.m_hDC, CPoint(rc.left, ptMid.y));

		CString strSize;
		strSize.Format(_T("%d x %d"), crop.Width(), crop.Height());

		// Above the rectangle, unless the rectangle is against the top edge --
		// which it is whenever the crop covers the whole frame.
		if (rc.top - 2 > rcClient.top + 14)
			DrawLabel(dc.m_hDC, CPoint(rc.left, rc.top - 2), DT_BOTTOM, strSize);
		else
			DrawLabel(dc.m_hDC, CPoint(rc.left + 2, rc.top + 2), 0, strSize);
	}

	if (_compare == Compare::Split && !_before.IsEmpty())
	{
		dc.FillSolidRect(xDivider - 1, _rectPreview.top, 2, _rectPreview.Height(), RGB(255, 255, 255));

		// Something to aim at. A bare hairline reads as decoration, and the band
		// around it is the only part of the picture that is not a crop gesture.
		const int yMid = (_rectPreview.top + _rectPreview.bottom) / 2;
		const CRect rcGrip(xDivider - 4, yMid - DividerGrip / 2, xDivider + 5, yMid + DividerGrip / 2);

		dc.FillSolidRect(rcGrip, RGB(255, 255, 255));
		dc.FrameRect(rcGrip, static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));

		DrawLabel(dc.m_hDC, CPoint(_rectPreview.left + 4, _rectPreview.bottom - 4), DT_BOTTOM,
		          App.LoadString(IDS_EDIT_BEFORE));
		DrawLabel(dc.m_hDC, CPoint(_rectPreview.right - 4, _rectPreview.bottom - 4), DT_RIGHT | DT_BOTTOM,
		          App.LoadString(IDS_EDIT_AFTER));
	}
	else if (_compare == Compare::Side && !_before.IsEmpty())
	{
		DrawLabel(dc.m_hDC, CPoint(_rectBefore.left, _rectBefore.top - 2), DT_BOTTOM,
		          App.LoadString(IDS_EDIT_BEFORE));
		DrawLabel(dc.m_hDC, CPoint(_rectPreview.left, _rectPreview.top - 2), DT_BOTTOM,
		          App.LoadString(IDS_EDIT_AFTER));
	}
}

inline EditCanvas::Grab EditCanvas::HitTest(CPoint pt) const
{
	if (_preview.IsEmpty())
		return Grab::None;

	if (_compare == Compare::Split && !_before.IsEmpty() &&
		abs(pt.x - DividerX()) <= DividerGrab && _rectPreview.PtInRect(CPoint(DividerX(), pt.y)))
		return Grab::Divider;

	const CRect crop = EffectiveCrop();

	if (crop.IsRectEmpty())
		return Grab::None;

	const CRect rc = ImageToClient(crop);
	const CPoint ptMid((rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2);
	const int n = HandleGrab;

	const bool bLeft = abs(pt.x - rc.left) <= n;
	const bool bRight = abs(pt.x - rc.right) <= n;
	const bool bTop = abs(pt.y - rc.top) <= n;
	const bool bBottom = abs(pt.y - rc.bottom) <= n;
	const bool bMidX = abs(pt.x - ptMid.x) <= n;
	const bool bMidY = abs(pt.y - ptMid.y) <= n;
	const bool bSpanX = pt.x >= rc.left - n && pt.x <= rc.right + n;
	const bool bSpanY = pt.y >= rc.top - n && pt.y <= rc.bottom + n;

	// Corners first: at a small crop every handle overlaps every other one, and
	// a corner is the one a reader means.
	if (bLeft && bTop) return Grab::TopLeft;
	if (bRight && bTop) return Grab::TopRight;
	if (bLeft && bBottom) return Grab::BottomLeft;
	if (bRight && bBottom) return Grab::BottomRight;

	if (bTop && bMidX) return Grab::Top;
	if (bBottom && bMidX) return Grab::Bottom;
	if (bLeft && bMidY) return Grab::Left;
	if (bRight && bMidY) return Grab::Right;

	if (bTop && bSpanX) return Grab::Top;
	if (bBottom && bSpanX) return Grab::Bottom;
	if (bLeft && bSpanY) return Grab::Left;
	if (bRight && bSpanY) return Grab::Right;

	if (rc.PtInRect(pt)) return Grab::Move;

	return Grab::None;
}

inline LRESULT EditCanvas::OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&)
{
	if (_preview.IsEmpty())
		return 0;

	SetFocus();

	const CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

	_grab = HitTest(pt);

	if (_grab == Grab::None)
		return 0;

	_bDragging = false;
	_ptGrabClient = pt;
	_ptGrab = ClientToImage(pt);
	_rectGrabStart = EffectiveCrop();

	SetCapture();

	return 0;
}

inline LRESULT EditCanvas::OnMouseMove(UINT, WPARAM wParam, LPARAM lParam, BOOL&)
{
	if (_grab == Grab::None || GetCapture() != m_hWnd)
		return 0;

	if ((wParam & MK_LBUTTON) == 0)
	{
		ReleaseCapture();
		return 0;
	}

	const CPoint ptClient(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

	if (!_bDragging &&
		abs(ptClient.x - _ptGrabClient.x) < DragSlop &&
		abs(ptClient.y - _ptGrabClient.y) < DragSlop)
		return 0;

	const bool bFirst = !_bDragging;
	_bDragging = true;

	if (_grab == Grab::Divider)
	{
		if (_rectPreview.Width() > 0)
		{
			_nDivider = IW::Clamp(MulDiv(ptClient.x - _rectPreview.left, 100, _rectPreview.Width()), 0, 100);
			Invalidate();
		}

		return 0;
	}

	const CRect rcBounds = ImageBounds();
	const CPoint ptImage = ClientToImage(ptClient);
	const CPoint pt(IW::Clamp(ptImage.x, 0, rcBounds.right), IW::Clamp(ptImage.y, 0, rcBounds.bottom));

	CRect rc = _rectGrabStart;

	switch (_grab)
	{
	case Grab::TopLeft: rc.left = pt.x; rc.top = pt.y; break;
	case Grab::Top: rc.top = pt.y; break;
	case Grab::TopRight: rc.right = pt.x; rc.top = pt.y; break;
	case Grab::Right: rc.right = pt.x; break;
	case Grab::BottomRight: rc.right = pt.x; rc.bottom = pt.y; break;
	case Grab::Bottom: rc.bottom = pt.y; break;
	case Grab::BottomLeft: rc.left = pt.x; rc.bottom = pt.y; break;
	case Grab::Left: rc.left = pt.x; break;

	case Grab::Move:
		{
			// Clamp the offset, not the rectangle. Intersecting a moved crop
			// with the frame shrinks it against the edge instead of stopping.
			// The bounds do not start at the origin once anything is warped.
			const int dx = IW::Clamp(pt.x - _ptGrab.x, rcBounds.left - rc.left, rcBounds.right - rc.right);
			const int dy = IW::Clamp(pt.y - _ptGrab.y, rcBounds.top - rc.top, rcBounds.bottom - rc.bottom);
			rc.OffsetRect(dx, dy);
		}
		break;

	default:
		return 0;
	}

	rc.NormalizeRect();
	rc.IntersectRect(rc, rcBounds);

	if (rc.Width() < MinCrop || rc.Height() < MinCrop)
		return 0;

	_pView->_edits._crop = rc;
	_pView->OnCropChanged();

	if (bFirst)
		Invalidate();

	return 0;
}

inline LRESULT EditCanvas::OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&)
{
	if (_grab == Grab::None)
		return 0;

	_grab = Grab::None;
	_bDragging = false;

	if (GetCapture() == m_hWnd)
		ReleaseCapture();

	// The guides are only drawn while a drag is in progress.
	Invalidate();

	return 0;
}

inline LRESULT EditCanvas::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&)
{
	// Losing the capture to somebody else has to end the drag, or the next
	// move over the canvas carries on resizing with no button held.
	_grab = Grab::None;
	_bDragging = false;

	return 0;
}

inline void EditCanvas::NudgeCrop(int dx, int dy, bool bResize)
{
	CRect crop = EffectiveCrop();

	if (crop.IsRectEmpty())
		return;

	const CRect rcBounds = ImageBounds();

	if (bResize)
	{
		crop.right = IW::Clamp(crop.right + dx, crop.left + MinCrop, rcBounds.right);
		crop.bottom = IW::Clamp(crop.bottom + dy, crop.top + MinCrop, rcBounds.bottom);
	}
	else
	{
		crop.OffsetRect(IW::Clamp(dx, rcBounds.left - crop.left, rcBounds.right - crop.right),
		                IW::Clamp(dy, rcBounds.top - crop.top, rcBounds.bottom - crop.bottom));
	}

	_pView->_edits._crop = crop;
	_pView->OnCropChanged();
}

inline LRESULT EditCanvas::OnKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled)
{
	if (_preview.IsEmpty())
	{
		bHandled = FALSE;
		return 0;
	}

	const bool bResize = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
	const int nStep = (::GetKeyState(VK_CONTROL) & 0x8000) != 0 ? 10 : 1;

	switch (wParam)
	{
	case VK_LEFT: NudgeCrop(-nStep, 0, bResize); return 0;
	case VK_RIGHT: NudgeCrop(nStep, 0, bResize); return 0;
	case VK_UP: NudgeCrop(0, -nStep, bResize); return 0;
	case VK_DOWN: NudgeCrop(0, nStep, bResize); return 0;
	default: break;
	}

	bHandled = FALSE;
	return 0;
}

inline LRESULT EditCanvas::OnSetCursor(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	if (reinterpret_cast<HWND>(wParam) != m_hWnd || LOWORD(lParam) != HTCLIENT)
	{
		bHandled = FALSE;
		return 0;
	}

	CPoint pt;
	GetCursorPos(&pt);
	ScreenToClient(&pt);

	switch (_grab != Grab::None ? _grab : HitTest(pt))
	{
	case Grab::TopLeft:
	case Grab::BottomRight:
		::SetCursor(::LoadCursor(nullptr, IDC_SIZENWSE));
		break;
	case Grab::TopRight:
	case Grab::BottomLeft:
		::SetCursor(::LoadCursor(nullptr, IDC_SIZENESW));
		break;
	case Grab::Top:
	case Grab::Bottom:
		::SetCursor(::LoadCursor(nullptr, IDC_SIZENS));
		break;
	case Grab::Left:
	case Grab::Right:
	case Grab::Divider:
		::SetCursor(::LoadCursor(nullptr, IDC_SIZEWE));
		break;
	case Grab::Move:
		::SetCursor(::LoadCursor(nullptr, IDC_SIZEALL));
		break;
	default:
		::SetCursor(::LoadCursor(nullptr, IDC_ARROW));
		break;
	}

	return 1;
}

///////////////////////////////////////////////////////////////////////
// CEditPanelDlg

// Every slider, with the label on its left and the value on its right. One
// table drives the layout, the readouts and the transfer to and from the edits,
// so the three can never fall out of step.
struct EditSliderRow
{
	int _idLabel;
	int _idSlider;
	int _idValue;
	int _nMin;
	int _nMax;
	int ImageEdits::*_pValue;
};

static const EditSliderRow s_editRows[] =
{
	{IDC_EDIT_STRAIGHTEN_LBL, IDC_EDIT_STRAIGHTEN, IDC_EDIT_STRAIGHTEN_VAL,
	 ImageEdits::StraightenMin, ImageEdits::StraightenMax, &ImageEdits::_straighten},
	{IDC_EDIT_PERSPECTIVE_H_LBL, IDC_EDIT_PERSPECTIVE_H, IDC_EDIT_PERSPECTIVE_H_VAL,
	 ImageEdits::PerspectiveMin, ImageEdits::PerspectiveMax, &ImageEdits::_perspectiveH},
	{IDC_EDIT_PERSPECTIVE_V_LBL, IDC_EDIT_PERSPECTIVE_V, IDC_EDIT_PERSPECTIVE_V_VAL,
	 ImageEdits::PerspectiveMin, ImageEdits::PerspectiveMax, &ImageEdits::_perspectiveV},
	{IDC_EDIT_VIBRANCE_LBL, IDC_EDIT_VIBRANCE, IDC_EDIT_VIBRANCE_VAL,
	 ImageEdits::ColorMin, ImageEdits::ColorMax, &ImageEdits::_vibrance},
	{IDC_EDIT_DARKS_LBL, IDC_EDIT_DARKS, IDC_EDIT_DARKS_VAL,
	 ImageEdits::ColorMin, ImageEdits::ColorMax, &ImageEdits::_darks},
	{IDC_EDIT_MIDTONES_LBL, IDC_EDIT_MIDTONES, IDC_EDIT_MIDTONES_VAL,
	 ImageEdits::ColorMin, ImageEdits::ColorMax, &ImageEdits::_midtones},
	{IDC_EDIT_LIGHTS_LBL, IDC_EDIT_LIGHTS, IDC_EDIT_LIGHTS_VAL,
	 ImageEdits::ColorMin, ImageEdits::ColorMax, &ImageEdits::_lights},
	{IDC_EDIT_CONTRAST_LBL, IDC_EDIT_CONTRAST, IDC_EDIT_CONTRAST_VAL,
	 ImageEdits::ColorMin, ImageEdits::ColorMax, &ImageEdits::_contrast},
	{IDC_EDIT_BRIGHTNESS_LBL, IDC_EDIT_BRIGHTNESS, IDC_EDIT_BRIGHTNESS_VAL,
	 ImageEdits::ColorMin, ImageEdits::ColorMax, &ImageEdits::_brightness},
	{IDC_EDIT_SATURATION_LBL, IDC_EDIT_SATURATION, IDC_EDIT_SATURATION_VAL,
	 ImageEdits::ColorMin, ImageEdits::ColorMax, &ImageEdits::_saturation},
	{IDC_EDIT_TEMPERATURE_LBL, IDC_EDIT_TEMPERATURE, IDC_EDIT_TEMPERATURE_VAL,
	 ImageEdits::ColorMin, ImageEdits::ColorMax, &ImageEdits::_temperature},
	{IDC_EDIT_TINT_LBL, IDC_EDIT_TINT, IDC_EDIT_TINT_VAL,
	 ImageEdits::ColorMin, ImageEdits::ColorMax, &ImageEdits::_tint}
};

// The three sliders above the Colour heading are the geometry ones.
enum { EditGeometryRows = 3 };

inline void CEditPanelDlg::SetupSlider(int id, int nMin, int nMax)
{
	CTrackBarCtrl slider = GetDlgItem(id);

	// The redraw flag has to be set on the last of these. A trackbar caches the
	// thumb rectangle and only recomputes it when something asks it to redraw,
	// so with both range calls silent the thumb stayed where the default 0..100
	// range had put it -- hard left -- while the value underneath was 0, which
	// on a -100..100 slider is the middle.
	slider.SetRangeMin(nMin, FALSE);
	slider.SetRangeMax(nMax, TRUE);
	slider.SetPageSize((nMax - nMin) / 10);

	// One tick, at neutral. Without it every slider looks like it is sitting at
	// its minimum when it is in fact in the middle doing nothing.
	slider.ClearTics(FALSE);
	slider.SetTic(0);

	slider.SetPos(0);
}

inline int CEditPanelDlg::Layout(int cx, bool bApply)
{
	if (m_hWnd == nullptr)
		return 0;

	CClientDC dc(m_hWnd);
	const HFONT hFontOld = dc.SelectFont(GetFont());

	TEXTMETRIC tm;
	dc.GetTextMetrics(&tm);

	const auto measure = [&](int id)
	{
		CString str;
		GetDlgItemText(id, str);

		CSize size;
		dc.GetTextExtent(str, str.GetLength(), &size);
		return static_cast<int>(size.cx);
	};

	int cxLabel = 0;

	for (int i = 0; i < countof(s_editRows); i++)
		cxLabel = IW::Max(cxLabel, measure(s_editRows[i]._idLabel));

	CSize sizeValue;
	dc.GetTextExtent(_T("-100.0"), 6, &sizeValue);

	// pad is the horizontal gap; the vertical rhythm is padY between rows and
	// padSection above each section title.
	const int pad = 6;
	const int padY = 10;
	const int padSection = 18;
	const int cyText = tm.tmHeight;
	const int cyRow = IW::Max(cyText + 10, 24);
	const int cyButton = IW::Max(cyText + 14, 28);

	const int left = pad;
	const int width = IW::Max(60, cx - pad * 2);
	const int cxValue = IW::Min(sizeValue.cx + 4, width / 4);

	// Below this the label, the slider and the value cannot share a line
	// without one of them being unreadable, so the slider moves to its own row.
	const bool bStacked = width < cxLabel + cxValue + 90;

	HDWP hdwp = bApply ? ::BeginDeferWindowPos(64) : nullptr;

	int y = pad;

	const auto place = [&](int id, int x, int yItem, int cxItem, int cyItem)
	{
		if (!bApply || hdwp == nullptr)
			return;

		const HWND hWnd = GetDlgItem(id);

		if (hWnd != nullptr)
			hdwp = ::DeferWindowPos(hdwp, hWnd, nullptr, x, yItem, cxItem, cyItem,
			                        SWP_NOZORDER | SWP_NOACTIVATE);
	};

	const auto fullRow = [&](int id, int cyItem)
	{
		place(id, left, y, width, cyItem);
		y += cyItem + padY;
	};

	// A title opens a section, so the air belongs above it, not below.
	const auto titleRow = [&](int id)
	{
		y += padSection - padY;
		place(id, left, y, width, cyText);
		y += cyText + padY;
	};

	// Buttons share a row while their captions still fit; below that they take
	// fewer per row rather than being clipped to "Rotate lef".
	const auto buttonRow = [&](int id1, int id2, int id3)
	{
		const int ids[] = {id1, id2, id3};
		const int n = id3 == 0 ? 2 : 3;

		int cxCaption = 0;

		for (int i = 0; i < n; i++)
			cxCaption = IW::Max(cxCaption, measure(ids[i]));

		int nPerRow = n;

		while (nPerRow > 1 && (width - pad * (nPerRow - 1)) / nPerRow < cxCaption + 10)
			nPerRow--;

		for (int i = 0; i < n; i += nPerRow)
		{
			const int nThis = IW::Min(nPerRow, n - i);
			const int cxOne = (width - pad * (nThis - 1)) / nThis;

			for (int j = 0; j < nThis; j++)
			{
				const int x = left + (cxOne + pad) * j;
				place(ids[i + j], x, y, j == nThis - 1 ? width - (cxOne + pad) * j : cxOne, cyButton);
			}

			y += cyButton + padY;
		}
	};

	const auto sliderRow = [&](const EditSliderRow &row)
	{
		if (bStacked)
		{
			place(row._idLabel, left, y, width - cxValue - pad, cyText);
			place(row._idValue, left + width - cxValue, y, cxValue, cyText);
			y += cyText + 3;
			place(row._idSlider, left, y, width, cyRow);
			y += cyRow + padY;
			return;
		}

		const int cxSlider = width - cxLabel - pad - cxValue - pad;
		const int dy = (cyRow - cyText) / 2;

		place(row._idLabel, left, y + dy, cxLabel, cyText);
		place(row._idSlider, left + cxLabel + pad, y, cxSlider, cyRow);
		place(row._idValue, left + width - cxValue, y + dy, cxValue, cyText);
		y += cyRow + padY / 2;
	};

	fullRow(IDC_EDIT_FILENAME, cyText);
	buttonRow(ID_EDIT_SAVE, ID_EDIT_SAVEAS, ID_EDIT_CANCEL_EDITS);
	fullRow(IDC_EDIT_INFO, cyText * 2 + 2);

	titleRow(IDC_EDIT_TITLE_COMPARE);
	buttonRow(IDC_EDIT_COMPARE_OFF, IDC_EDIT_COMPARE_SPLIT, IDC_EDIT_COMPARE_SIDE);

	titleRow(IDC_EDIT_TITLE_GEOMETRY);
	buttonRow(IDC_EDIT_CROP, IDC_EDIT_ROTATE_LEFT, IDC_EDIT_ROTATE_RIGHT);

	for (int i = 0; i < EditGeometryRows; i++)
		sliderRow(s_editRows[i]);

	y += padY / 2;
	buttonRow(IDC_EDIT_AUTO_STRAIGHTEN, IDC_EDIT_RESET_GEOMETRY, 0);

	titleRow(IDC_EDIT_TITLE_COLOR);

	for (int i = EditGeometryRows; i < countof(s_editRows); i++)
		sliderRow(s_editRows[i]);

	y += padY / 2;
	buttonRow(IDC_EDIT_AUTO_COLOR, IDC_EDIT_RESET_COLOR, 0);

	// Only now: buttonRow measures its captions as it goes, and it has to do
	// that in the font the panel is drawn in.
	dc.SelectFont(hFontOld);

	if (hdwp != nullptr)
		::EndDeferWindowPos(hdwp);

	if (bApply)
		Invalidate();

	return y + padY;
}

inline LRESULT CEditPanelDlg::OnSize(UINT, WPARAM, LPARAM lParam, BOOL& bHandled)
{
	const int cy = Layout(LOWORD(lParam), true);

	if (_pView != nullptr)
		_pView->PanelHeightChanged(cy);

	bHandled = FALSE;
	return 0;
}

inline LRESULT CEditPanelDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&)
{
	IW::ScopeLockedBool lockSetting(_bSetting);

	for (int i = 0; i < countof(s_editRows); i++)
		SetupSlider(s_editRows[i]._idSlider, s_editRows[i]._nMin, s_editRows[i]._nMax);

	SetDlgItemText(IDC_EDIT_COMPARE_OFF, App.LoadString(IDS_EDIT_COMPARE_OFF));
	SetDlgItemText(IDC_EDIT_COMPARE_SPLIT, App.LoadString(IDS_EDIT_COMPARE_SPLIT));
	SetDlgItemText(IDC_EDIT_COMPARE_SIDE, App.LoadString(IDS_EDIT_COMPARE_SIDE));
	ShowCompare(0);

	return 0;
}

inline void CEditPanelDlg::ShowSliderValues(const ImageEdits &edits)
{
	if (m_hWnd == nullptr)
		return;

	for (int i = 0; i < countof(s_editRows); i++)
	{
		const int value = edits.*(s_editRows[i]._pValue);
		CString str;

		if (value == 0)
			str = _T("0");
		else if (s_editRows[i]._idSlider == IDC_EDIT_STRAIGHTEN)
			// Straighten is stored in tenths of a degree; a reader wants degrees.
			str.Format(_T("%+.1f\u00b0"), value / 10.0);
		else
			str.Format(_T("%+d"), value);

		SetTextIfChanged(s_editRows[i]._idValue, str);
	}
}

inline void CEditPanelDlg::ReadFrom(const ImageEdits &edits)
{
	if (m_hWnd == nullptr)
		return;

	IW::ScopeLockedBool lockSetting(_bSetting);

	for (int i = 0; i < countof(s_editRows); i++)
		CTrackBarCtrl(GetDlgItem(s_editRows[i]._idSlider)).SetPos(edits.*(s_editRows[i]._pValue));

	ShowSliderValues(edits);
	UpdateButtons(edits);
}

// Save and Discard mean nothing until something has been changed, and a live
// Save would recompress the file for no reason.
inline void CEditPanelDlg::UpdateButtons(const ImageEdits &edits)
{
	if (m_hWnd == nullptr || _pView == nullptr)
		return;

	const bool bChanges = _pView->HasChanges();
	const bool bImage = _pView->HasImage();

	::EnableWindow(GetDlgItem(ID_EDIT_SAVE), bChanges);
	::EnableWindow(GetDlgItem(ID_EDIT_CANCEL_EDITS), bChanges);
	::EnableWindow(GetDlgItem(ID_EDIT_SAVEAS), bImage);
	::EnableWindow(GetDlgItem(IDC_EDIT_CROP), bImage && edits.HasCrop());
}

inline void CEditPanelDlg::WriteTo(ImageEdits &edits) const
{
	if (m_hWnd == nullptr)
		return;

	for (int i = 0; i < countof(s_editRows); i++)
		edits.*(s_editRows[i]._pValue) = CTrackBarCtrl(GetDlgItem(s_editRows[i]._idSlider)).GetPos();
}

inline void CEditPanelDlg::SetInfo(const CString &strName, const CString &strInfo)
{
	if (m_hWnd == nullptr)
		return;

	SetTextIfChanged(IDC_EDIT_FILENAME, strName);
	SetTextIfChanged(IDC_EDIT_INFO, strInfo);
}

inline void CEditPanelDlg::SetTextIfChanged(int id, const CString &str)
{
	CWindow ctrl = GetDlgItem(id);

	if (ctrl.m_hWnd == nullptr)
		return;

	CString strOld;
	ctrl.GetWindowText(strOld);

	if (strOld != str)
		ctrl.SetWindowText(str);
}

inline LRESULT CEditPanelDlg::OnScroll(UINT, WPARAM, LPARAM, BOOL&)
{
	if (_bSetting || _pView == nullptr)
		return 0;

	WriteTo(_pView->_edits);
	ShowSliderValues(_pView->_edits);
	_pView->OnEditsChanged();

	return 0;
}

inline void CEditPanelDlg::ShowCompare(int nMode)
{
	if (m_hWnd == nullptr)
		return;

	CheckRadioButton(IDC_EDIT_COMPARE_OFF, IDC_EDIT_COMPARE_SIDE,
	                 nMode == 1 ? IDC_EDIT_COMPARE_SPLIT : nMode == 2 ? IDC_EDIT_COMPARE_SIDE : IDC_EDIT_COMPARE_OFF);
}

inline LRESULT CEditPanelDlg::OnCompareChanged(WORD, WORD wID, HWND, BOOL&)
{
	if (_bSetting || _pView == nullptr)
		return 0;

	_pView->_canvas.SetCompare(wID == IDC_EDIT_COMPARE_SPLIT
		                           ? EditCanvas::Compare::Split
		                           : wID == IDC_EDIT_COMPARE_SIDE
		                           ? EditCanvas::Compare::Side
		                           : EditCanvas::Compare::Off);

	return 0;
}

inline LRESULT CEditPanelDlg::OnButton(WORD, WORD wID, HWND, BOOL&)
{
	if (_pView == nullptr)
		return 0;

	switch (wID)
	{
	case IDC_EDIT_ROTATE_LEFT: _pView->Rotate(-1); break;
	case IDC_EDIT_ROTATE_RIGHT: _pView->Rotate(1); break;
	case IDC_EDIT_AUTO_STRAIGHTEN: _pView->AutoStraighten(); break;
	case IDC_EDIT_AUTO_COLOR: _pView->AutoColor(); break;
	case IDC_EDIT_CROP: _pView->ResetCrop(); break;
	case ID_EDIT_SAVE: _pView->Save(false); break;
	case ID_EDIT_SAVEAS: _pView->Save(true); break;
	case ID_EDIT_CANCEL_EDITS: _pView->CancelEdits(); break;

	case IDC_EDIT_RESET_GEOMETRY:
		_pView->_edits.ResetGeometry();
		ReadFrom(_pView->_edits);
		_pView->OnEditsChanged();
		break;

	case IDC_EDIT_RESET_COLOR:
		_pView->_edits.ResetColor();
		ReadFrom(_pView->_edits);
		_pView->OnEditsChanged();
		break;

	default:
		break;
	}

	return 0;
}

///////////////////////////////////////////////////////////////////////
// EditView

inline void EditView::Reload()
{
	CString strPath = _state.Image.GetImageFileName();

	// Edit mode does not depend on the items mode having displayed anything:
	// it takes the focused item and loads the file itself, the way Diffractor's
	// edit view does.
	if (strPath.IsEmpty())
	{
		IW::FolderPtr pFolder = _state.Folder.GetFolder();
		const int nFocus = pFolder->GetFocusItem();

		if (nFocus != -1 && pFolder->IsItemImage(nFocus))
			strPath = pFolder->GetItemPath(nFocus);
	}

	// A different file starts a fresh stack; the same one keeps what is set.
	if (strPath.CompareNoCase(_strSourcePath) != 0)
	{
		_strSourcePath = strPath;
		_edits.Reset();
		_source.Free();
		_canvas.OnSourceChanged();
	}

	if (_source.IsEmpty() && !strPath.IsEmpty())
	{
		const IW::Image &shown = _state.Image.GetImage();

		// IsImageReady, not IsImageShown: while the decode is in flight what is
		// on screen is the items pane's thumbnail, and editing that would put
		// 160 pixels back over the file.
		if (_state.Image.IsImageReady() && !shown.IsEmpty() &&
			_state.Image.GetImageFileName().CompareNoCase(strPath) == 0)
		{
			_source = shown;
		}
		else
		{
			CWaitCursor wait;
			CLoadAny loader(_state.Loaders);
			CImageLoad info(strPath, true, 0, 0, 0);

			if (info.Load(loader, IW::CNullStatus::Instance))
				_source = info._image;
		}
	}

	_panel.GetDialog().ReadFrom(_edits);
	OnEditsChanged();
}

inline void EditView::OnEditsChanged()
{
	ClampCrop();
	_canvas.Rebuild();
	_canvas.Invalidate();
	_panel.GetDialog().UpdateButtons(_edits);
	UpdateInfo();
}

// Straightening shrinks the area a crop may occupy, so a crop set before it has
// to be pulled back in rather than left hanging over the empty corners.
inline void EditView::ClampCrop()
{
	if (_source.IsEmpty() || !_edits.HasCrop())
		return;

	const IW::Page page = _source.GetFirstPage();
	const CRect bounds = _edits.CropBounds(CSize(page.GetWidth(), page.GetHeight()));

	CRect rc;

	if (!rc.IntersectRect(_edits._crop, bounds) || rc.Width() < 8 || rc.Height() < 8)
		_edits._crop.SetRectEmpty();
	else
		_edits._crop = rc;
}

inline void EditView::OnCropChanged()
{
	// The crop is drawn over the preview rather than baked into it, so nothing
	// has to be decoded again -- only redrawn and re-measured.
	_canvas.Invalidate();
	_panel.GetDialog().UpdateButtons(_edits);
	UpdateInfo();
}

// A crop that keeps everything the picture covers is where the view starts: a
// rectangle on screen with handles to drag, not yet an edit worth saving or
// worth stopping the reader on the way out.
inline bool EditView::HasChanges() const
{
	if (_source.IsEmpty())
		return false;

	ImageEdits edits = _edits;

	if (edits.HasCrop())
	{
		const IW::Page page = _source.GetFirstPage();

		if (edits._crop == edits.CropBounds(CSize(page.GetWidth(), page.GetHeight())))
			edits._crop.SetRectEmpty();
	}

	return !edits.IsEmpty();
}

inline void EditView::UpdateInfo()
{
	CString str;

	if (!_source.IsEmpty())
	{
		const IW::Page page = _source.GetFirstPage();
		const CSize sizeSource(page.GetWidth(), page.GetHeight());

		// What Save is going to write, which with no crop of its own is the
		// usable area rather than the frame -- the same rectangle the handles
		// are drawn around. Reporting the frame meant the readout disagreed
		// with the handles, and with the file, the moment anything was warped.
		const CSize sizeOut = _edits.HasCrop()
			                      ? _edits._crop.Size()
			                      : _edits.CropBounds(sizeSource).Size();

		// The saved size first, because that is the number being decided; then
		// how much of the canvas one saved pixel is worth.
		str.Format(_T("%d x %d\r\n%s %d%%"),
		           sizeOut.cx, sizeOut.cy,
		           static_cast<LPCTSTR>(App.LoadString(IDS_EDIT_ZOOM)),
		           static_cast<int>(_canvas._scale * 100.0 + 0.5));
	}

	_panel.GetDialog().SetInfo(IW::Path::FindFileName(_strSourcePath), str);

	// The status bar is one line and draws a break as two glyph boxes.
	CString strStatus(str);
	strStatus.Replace(_T("\r\n"), _T("  "));
	_pCoupling->SetStatusText(strStatus);
}

inline void EditView::Rotate(int nQuarterTurns)
{
	if (_source.IsEmpty())
		return;

	const IW::Page page = _source.GetFirstPage();
	CSize size = _edits.TransformedSize(CSize(page.GetWidth(), page.GetHeight()));

	// The crop and the two perspective sliders are expressed in the transformed
	// frame, so a quarter turn moves them with the picture. Throwing the crop
	// away instead made every rotation lose the framing that led to it.
	const int nTurns = ((nQuarterTurns % 4) + 4) % 4;

	for (int i = 0; i < nTurns; i++)
	{
		if (_edits.HasCrop())
		{
			const CRect rc = _edits._crop;
			_edits._crop = CRect(size.cy - rc.bottom, rc.left, size.cy - rc.top, rc.right);
		}

		const int nPerspectiveH = _edits._perspectiveH;
		_edits._perspectiveH = -_edits._perspectiveV;
		_edits._perspectiveV = nPerspectiveH;

		const int cx = size.cx;
		size.cx = size.cy;
		size.cy = cx;
	}

	_edits._rotate = (_edits._rotate + nTurns) % 4;

	_panel.GetDialog().ReadFrom(_edits);
	OnEditsChanged();
}

// The crop is always on the picture, so there is nothing to turn on: this puts
// it back around the whole frame.
inline void EditView::ResetCrop()
{
	_edits._crop.SetRectEmpty();
	OnCropChanged();
}

inline void EditView::ToggleCompare()
{
	const bool bOn = !_canvas.IsComparing();

	_canvas.SetCompare(bOn ? EditCanvas::Compare::Split : EditCanvas::Compare::Off);
	_panel.GetDialog().ShowCompare(bOn ? 1 : 0);
}

inline void EditView::AutoStraighten()
{
	if (_source.IsEmpty())
		return;

	CWaitCursor wait;
	IW::AutoStraighten(_source, _edits);

	_panel.GetDialog().ReadFrom(_edits);
	OnEditsChanged();
}

inline void EditView::AutoColor()
{
	if (_source.IsEmpty())
		return;

	CWaitCursor wait;
	IW::AutoColor(_source, _edits);

	_panel.GetDialog().ReadFrom(_edits);
	OnEditsChanged();
}

inline void EditView::CancelEdits()
{
	_edits.Reset();
	_panel.GetDialog().ReadFrom(_edits);
	OnEditsChanged();
}

inline bool EditView::Save(bool bSaveAs)
{
	if (_source.IsEmpty())
		return false;

	IW::Image result;

	{
		CWaitCursor wait;

		// What the handles enclose is what gets saved. With no crop of its own
		// that is the usable area, which straightening makes smaller than the
		// frame -- otherwise the file would keep the empty corners the view was
		// careful not to show inside the crop.
		ImageEdits edits = _edits;

		if (!edits.HasCrop())
		{
			const IW::Page page = _source.GetFirstPage();
			edits._crop = edits.CropBounds(CSize(page.GetWidth(), page.GetHeight()));
		}

		if (!IW::ApplyEdits(_source, result, edits, IW::CNullStatus::Instance))
		{
			// Silence here read as "Save did nothing", and QueryLeave turned it
			// into "Yes, save" being ignored.
			IW::CMessageBoxIndirect mb;
			mb.Show(IDS_FAILEDTO_SAVE);
			return false;
		}
	}

	bool bSaved = false;

	{
		IW::ScopeLockedBool lockSaving(_bSaving);

		_state.Image.SetImageWithHistory(result, _T("Edit"));
		bSaved = bSaveAs ? _state.Image.SaveAs() : _state.Image.Save();

		// A save the user cancelled, or one the loader refused, would otherwise
		// leave the edited picture in the image state marked unsaved -- and the
		// question this mode asks on the way out would be asked again at the
		// next image, long after the mode was left.
		if (!bSaved)
			_state.Image.UndoWithoutPrompt();
	}

	if (bSaved)
	{
		// The saved file is the new source, so the stack starts over rather
		// than being applied twice.
		_edits.Reset();
		_strSourcePath.Empty();
		Reload();
	}

	return bSaved;
}

inline bool EditView::InvokeCommand(DWORD id)
{
	switch (id)
	{
	case ID_EDIT_SAVE: Save(false); return true;
	case ID_EDIT_SAVEAS: Save(true); return true;
	case ID_EDIT_CANCEL_EDITS: CancelEdits(); return true;
	case ID_EDIT_ROTATELEFT: Rotate(-1); return true;
	case ID_EDIT_ROTATERIGHT: Rotate(1); return true;
	case ID_EDIT_CROP: ResetCrop(); return true;
	case ID_EDIT_COMPARE: ToggleCompare(); return true;
	default: break;
	}

	return false;
}

inline bool EditView::GetCommandState(DWORD id, bool &bEnabled, bool &bChecked)
{
	switch (id)
	{
	case ID_EDIT_SAVE:
		bEnabled = HasImage() && HasChanges();
		return true;

	case ID_EDIT_SAVEAS:
	case ID_EDIT_ROTATELEFT:
	case ID_EDIT_ROTATERIGHT:
		bEnabled = HasImage();
		return true;

	case ID_EDIT_CROP:
		bEnabled = HasImage() && _edits.HasCrop();
		return true;

	case ID_EDIT_COMPARE:
		bEnabled = HasImage();
		bChecked = _canvas.IsComparing();
		return true;

	case ID_EDIT_CANCEL_EDITS:
		bEnabled = HasChanges();
		return true;

	default:
		break;
	}

	return false;
}

// Escape climbs down: the comparison is a way of looking, the crop is a piece
// of work, and only when neither is up does the frame get to leave the mode.
inline bool EditView::OnEscape()
{
	if (_canvas.IsComparing())
	{
		ToggleCompare();
		return true;
	}

	if (!_edits.HasCrop())
		return false;

	ResetCrop();
	return true;
}

// The edit stack is the only unsaved state edit mode holds, so leaving it is
// the moment to ask. Cancel keeps the mode; No throws the stack away.
inline bool EditView::QueryLeave()
{
	if (!HasImage() || !HasChanges())
		return true;

	CString strChanges;
	if (_edits.HasGeometry()) IW::AddToList(strChanges, App.LoadString(IDS_EDIT_GEOMETRY));
	if (_edits.HasColor()) IW::AddToList(strChanges, App.LoadString(IDS_EDIT_COLOR));

	const CString strName = IW::Path::FindFileName(_strSourcePath);
	const CString str = IW::Format(IDS_SAVE_CHANGES, static_cast<LPCTSTR>(strName),
	                               static_cast<LPCTSTR>(strChanges));

	CYesNoDlg dlg(_canvas._preview.IsEmpty() ? _source : _canvas._preview, str);
	const int nRet = static_cast<int>(dlg.DoModal());

	if (nRet == IDCANCEL)
		return false;

	if (nRet == IDYES)
		return Save(false);

	CancelEdits();
	return true;
}

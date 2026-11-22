// ImageWalker by Zac Walker
//
// Purpose: Skin drawing implementation.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"
#include "ViewPaneTheme.h"
#include "ViewSkin.h"
#include "ViewShellMenu.h"

namespace
{
	bool UseDarkPaneColors()
	{
		HIGHCONTRAST contrast = {};
		contrast.cbSize = sizeof(contrast);
		return App.Settings.BlackSkin &&
			::SystemParametersInfo(SPI_GETHIGHCONTRAST, contrast.cbSize, &contrast, 0) &&
			!(contrast.dwFlags & HCF_HIGHCONTRASTON);
	}

	struct PaneColors
	{
		COLORREF surface = IW::Style::Color::TaskBackground;
		COLORREF text = IW::Style::Color::TaskText;
		COLORREF field = IW::Average(surface, RGB(0, 0, 0));
		COLORREF button = IW::Average(surface, field);
		COLORREF border = IW::Average(field, text);
		COLORREF muted = IW::Average(surface, text);
		COLORREF accent = IW::Style::Color::Highlight;
		COLORREF selected = IW::Average(field, accent);
	};

	bool IsControl(HWND hWnd, LPCTSTR className)
	{
		TCHAR name[32] = {};
		return ::GetClassName(hWnd, name, countof(name)) != 0 && _tcsicmp(name, className) == 0;
	}

	void PaintPaneBox(CDCHandle dc, CRect rect, COLORREF fill, COLORREF border)
	{
		if (rect.IsRectEmpty())
			return;
		dc.FillSolidRect(rect, border);
		rect.DeflateRect(1, 1);
		if (!rect.IsRectEmpty())
			dc.FillSolidRect(rect, fill);
	}

	void DrawPaneText(CDCHandle dc, HWND control, CRect rect, COLORREF color, UINT flags)
	{
		CWindow window(control);
		CString text;
		window.GetWindowText(text);
		const HFONT font = window.GetFont();
		const HFONT oldFont = font != nullptr ? dc.SelectFont(font) : nullptr;
		const COLORREF oldColor = dc.SetTextColor(color);
		const int oldMode = dc.SetBkMode(TRANSPARENT);
		if (!(flags & DT_NOPREFIX) && (::SendMessage(control, WM_QUERYUISTATE, 0, 0) & UISF_HIDEACCEL))
			flags |= DT_HIDEPREFIX;

		dc.DrawText(text, text.GetLength(), rect, flags | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
		dc.SetBkMode(oldMode);
		dc.SetTextColor(oldColor);
		if (oldFont != nullptr)
			dc.SelectFont(oldFont);
	}

	void DrawPaneFocus(CDCHandle dc, HWND control, CRect rect, int inset)
	{
		if (::GetFocus() != control || (::SendMessage(control, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS))
			return;

		rect.DeflateRect(inset, inset);
		if (!rect.IsRectEmpty())
			dc.DrawFocusRect(rect);
	}

	LRESULT DrawPaneButton(const NMCUSTOMDRAW &draw, const PaneColors &colors)
	{
		const HWND hWnd = draw.hdr.hwndFrom;
		const DWORD style = CWindow(hWnd).GetStyle();
		const DWORD type = style & BS_TYPEMASK;
		const bool checkBox = type == BS_AUTOCHECKBOX;
		const bool toggle = type == BS_AUTORADIOBUTTON && (style & BS_PUSHLIKE);
		if ((!checkBox && !toggle && type != BS_PUSHBUTTON && type != BS_DEFPUSHBUTTON) ||
			draw.dwDrawStage != CDDS_PREPAINT)
			return CDRF_DODEFAULT;

		CDCHandle dc(draw.hdc);
		const CRect rect(draw.rc);
		const bool enabled = ::IsWindowEnabled(hWnd) != FALSE;
		const bool checked = (checkBox || toggle) && ::SendMessage(hWnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
		const bool pressed = (draw.uItemState & CDIS_SELECTED) != 0;
		const bool hot = (draw.uItemState & CDIS_HOT) != 0;
		const COLORREF ink = enabled ? colors.text : colors.muted;
		const COLORREF edge = !enabled ? IW::Average(colors.field, colors.muted) :
			checked || type == BS_DEFPUSHBUTTON ? colors.accent : hot ? colors.text : colors.border;

		if (checkBox)
		{
			dc.FillSolidRect(rect, colors.surface);
			const int size = IW::Min(rect.Height() - 2, ::MulDiv(13, dc.GetDeviceCaps(LOGPIXELSX), 96));
			if (size < 5 || rect.Width() < size + 2)
				return CDRF_SKIPDEFAULT;

			const int x = rect.left + 1;
			const int y = rect.top + (rect.Height() - size) / 2;
			PaintPaneBox(dc, CRect(x, y, x + size, y + size), pressed ? colors.button : colors.field, edge);
			if (checked)
			{
				const POINT tick[] = {
					{x + size / 4, y + size / 2}, {x + size * 5 / 12, y + size * 3 / 4},
					{x + size * 4 / 5, y + size / 4}
				};
				CPen pen;
				if (pen.CreatePen(PS_SOLID, IW::Max(1, size / 6), ink))
				{
					const HPEN oldPen = dc.SelectPen(pen);
					dc.Polyline(tick, countof(tick));
					dc.SelectPen(oldPen);
				}
			}

			CRect label(rect);
			label.left = x + size + 5;
			DrawPaneText(dc, hWnd, label, ink, DT_LEFT);
			DrawPaneFocus(dc, hWnd, rect, 0);
		}
		else
		{
			const COLORREF fill = !enabled || pressed ? colors.field :
				checked ? colors.selected : hot ? colors.surface : colors.button;
			PaintPaneBox(dc, rect, fill, edge);
			CRect label(rect);
			label.DeflateRect(4, 1);
			DrawPaneText(dc, hWnd, label, ink, DT_CENTER);
			DrawPaneFocus(dc, hWnd, rect, 3);
		}
		return CDRF_SKIPDEFAULT;
	}

	LRESULT DrawPaneTrackbar(const NMCUSTOMDRAW &draw, const PaneColors &colors)
	{
		CDCHandle dc(draw.hdc);
		const CRect rect(draw.rc);
		const HWND hWnd = draw.hdr.hwndFrom;
		switch (draw.dwDrawStage)
		{
		case CDDS_PREPAINT:
			dc.FillSolidRect(rect, colors.surface);
			return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
		case CDDS_POSTPAINT:
			DrawPaneFocus(dc, hWnd, rect, 2);
			break;
		case CDDS_ITEMPREPAINT:
			if (draw.dwItemSpec == TBCD_CHANNEL)
			{
				PaintPaneBox(dc, rect, colors.field, colors.border);
				return CDRF_SKIPDEFAULT;
			}
			if (draw.dwItemSpec == TBCD_THUMB)
			{
				const bool enabled = ::IsWindowEnabled(hWnd) != FALSE;
				const COLORREF fill = !enabled ? colors.muted :
					(draw.uItemState & (CDIS_HOT | CDIS_SELECTED)) ? colors.accent : colors.text;
				PaintPaneBox(dc, rect, fill, colors.field);
				return CDRF_SKIPDEFAULT;
			}
			if (draw.dwItemSpec == TBCD_TICS)
			{
				// Edit uses TBS_BOTH. Get the native position of its neutral tick
				// instead of guessing at the thumb's travel after a pane resize.
				CRect channel, thumb;
				CTrackBarCtrl slider(hWnd);
				slider.GetChannelRect(channel);
				slider.GetThumbRect(thumb);
				const auto tick = [&](int x)
				{
					dc.FillSolidRect(x, thumb.top - 3, 1, 3, colors.border);
					dc.FillSolidRect(x, thumb.bottom, 1, 3, colors.border);
				};
				tick(channel.left + thumb.Width() / 2);
				tick(channel.right - thumb.Width() / 2 - 1);
				const int ticCount = static_cast<int>(slider.GetNumTics());
				for (int i = 0; i + 2 < ticCount; ++i)
				{
					const int x = slider.GetTicPos(i);
					if (x >= 0)
						tick(x);
				}
				return CDRF_SKIPDEFAULT;
			}
			break;
		}
		return CDRF_DODEFAULT;
	}

	void PaintPaneCombo(HWND combo, CDCHandle dc)
	{
		COMBOBOXINFO info = {};
		info.cbSize = sizeof(info);
		if (!::GetComboBoxInfo(combo, &info))
			return;

		const PaneColors colors;
		CRect rect;
		::GetClientRect(combo, rect);
		PaintPaneBox(dc, rect, colors.field, colors.border);

		CRect button(info.rcButton);
		button.IntersectRect(button, rect);
		if (!button.IsRectEmpty())
		{
			const bool pressed = (::SendMessage(combo, CB_GETDROPPEDSTATE, 0, 0) != FALSE);
			dc.FillSolidRect(button, pressed ? colors.selected : colors.button);
			const CPoint center = button.CenterPoint();
			const COLORREF ink = ::IsWindowEnabled(combo) ? colors.text : colors.muted;
			for (int i = 0; i != 4; ++i)
				dc.FillSolidRect(center.x - 3 + i, center.y - 2 + i, 7 - i * 2, 1, ink);
		}

		CRect label(info.rcItem);
		label.IntersectRect(label, rect);
		label.DeflateRect(2, 0);
		DrawPaneText(dc, combo, label, ::IsWindowEnabled(combo) ? colors.text : colors.muted, DT_LEFT | DT_NOPREFIX);
		DrawPaneFocus(dc, combo, info.rcItem, 1);
	}

	LRESULT CALLBACK PaneComboProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR)
	{
		switch (msg)
		{
		case WM_NCDESTROY:
			::RemoveWindowSubclass(hWnd, PaneComboProc, id);
			break;
		case WM_CTLCOLORLISTBOX:
			// The popup can ask the combo instead of the dialog. Its selection,
			// keyboard handling and scrollbar still belong to Windows.
			if (UseDarkPaneColors())
				return ::SendMessage(::GetParent(hWnd), msg, wParam, lParam);
			break;
		case WM_ERASEBKGND:
			if (UseDarkPaneColors())
				return TRUE;
			break;
		case WM_PAINT:
			if (UseDarkPaneColors())
			{
				CPaintDC dc(hWnd);
				PaintPaneCombo(hWnd, static_cast<HDC>(dc));
				return 0;
			}
			break;
		case WM_PRINTCLIENT:
			if (UseDarkPaneColors())
			{
				PaintPaneCombo(hWnd, reinterpret_cast<HDC>(wParam));
				return 0;
			}
			break;
		case WM_SETFOCUS:
		case WM_KILLFOCUS:
		case WM_UPDATEUISTATE:
		case WM_ENABLE:
		case CB_SETCURSEL:
		case CB_SHOWDROPDOWN:
			// Some native state changes paint immediately, without WM_PAINT.
			// Let Windows update its state and then repaint the closed field.
			{
				const LRESULT result = ::DefSubclassProc(hWnd, msg, wParam, lParam);
				if (UseDarkPaneColors())
					::InvalidateRect(hWnd, nullptr, FALSE);
				return result;
			}
		}
		return ::DefSubclassProc(hWnd, msg, wParam, lParam);
	}
}

void IW::Skin::PaneTheme::AttachDropDownList(HWND combo)
{
	if (combo != nullptr)
		::SetWindowSubclass(combo, PaneComboProc, 1, 0);
}

BOOL IW::Skin::PaneTheme::ProcessWindowMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                               LRESULT &result, DWORD mapId)
{
	if (mapId != 0)
		return FALSE;

	switch (msg)
	{
	case WM_SETTINGCHANGE:
	case WM_SYSCOLORCHANGE:
	case WM_THEMECHANGED:
		::RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
		return FALSE;
	case WM_COMMAND:
		{
			HWND control = reinterpret_cast<HWND>(lParam);
			const UINT code = HIWORD(wParam);
			if ((code == CBN_SELCHANGE || code == CBN_DROPDOWN || code == CBN_CLOSEUP) &&
				IsControl(control, WC_COMBOBOX) && UseDarkPaneColors())
				::InvalidateRect(control, nullptr, FALSE);
			return FALSE;
		}
	case WM_CTLCOLORDLG:
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORLISTBOX:
		{
			if (!UseDarkPaneColors())
				return FALSE;

			const bool field = msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX ||
				(msg == WM_CTLCOLORSTATIC && IsControl(reinterpret_cast<HWND>(lParam), WC_EDIT));
			const PaneColors colors;
			const COLORREF fill = field ? colors.field : colors.surface;
			CBrush &brush = field ? _fieldBrush : _surfaceBrush;
			COLORREF &previous = field ? _fieldColor : _surfaceColor;
			if (brush.IsNull() || previous != fill)
			{
				if (!brush.IsNull())
					brush.DeleteObject();
				if (!brush.CreateSolidBrush(fill))
					return FALSE;
				previous = fill;
			}

			CDCHandle dc(reinterpret_cast<HDC>(wParam));
			dc.SetTextColor(colors.text);
			dc.SetBkColor(fill);
			dc.SetBkMode(field ? OPAQUE : TRANSPARENT);
			result = reinterpret_cast<LRESULT>(static_cast<HBRUSH>(brush));
			return TRUE;
		}
	case WM_NOTIFY:
		{
			const auto *draw = reinterpret_cast<const NMCUSTOMDRAW *>(lParam);
			if (draw == nullptr || draw->hdr.code != NM_CUSTOMDRAW ||
				::GetParent(draw->hdr.hwndFrom) != hWnd || !UseDarkPaneColors())
				return FALSE;

			if (IsControl(draw->hdr.hwndFrom, WC_BUTTON))
				result = DrawPaneButton(*draw, PaneColors());
			else if (IsControl(draw->hdr.hwndFrom, TRACKBAR_CLASS))
				result = DrawPaneTrackbar(*draw, PaneColors());
			else
				return FALSE;
			return TRUE;
		}
	}
	return FALSE;
}

void IW::Skin::DrawGradient(CDCHandle dc, const CRect& r, DWORD c1, DWORD c2)
{
	TRIVERTEX vert[2];
	GRADIENT_RECT gRect;
	vert[0].x = r.left;
	vert[0].y = r.top;
	vert[0].Red = static_cast<COLOR16>(GetR(c1) << 8);
	vert[0].Green = static_cast<COLOR16>(GetG(c1) << 8);
	vert[0].Blue = static_cast<COLOR16>(GetB(c1) << 8);
	vert[0].Alpha = 0x0000;
	vert[1].x = r.right;
	vert[1].y = r.bottom;
	vert[1].Red = static_cast<COLOR16>(GetR(c2) << 8);
	vert[1].Green = static_cast<COLOR16>(GetG(c2) << 8);
	vert[1].Blue = static_cast<COLOR16>(GetB(c2) << 8);
	vert[1].Alpha = 0x0000;

	gRect.UpperLeft = 0;
	gRect.LowerRight = 1;
	GradientFill(dc, vert, 2, &gRect, 1, GRADIENT_FILL_RECT_V);
}

void IW::Skin::DrawArrow(CDCHandle& dc, const CRect& rc)
{
	// Draw arrow
	CPen pen;
	pen.CreatePen(PS_SOLID, 0, Emphasize(Style::Color::WindowText));
	CBrush brush;
	brush.CreateSolidBrush(Style::Color::WindowText);

	HPEN hOldPen = dc.SelectPen(pen);
	HBRUSH hOldBrush = dc.SelectBrush(brush);
	int xMiddle = rc.left - 1 + (rc.right - rc.left) / 2;
	int yMiddle = rc.top + (rc.bottom - rc.top) / 2;
	constexpr int ARROW_SIZE = 2;
	POINT pt[] = {
		{xMiddle - ARROW_SIZE, yMiddle - ARROW_SIZE},
		{xMiddle + ARROW_SIZE, yMiddle - ARROW_SIZE},
		{xMiddle, yMiddle + 1}
	};
	dc.Polygon(pt, 3);
	dc.SelectPen(hOldPen);
	dc.SelectBrush(hOldBrush);
}

void IW::Skin::DrawBitmapDisabled(CDCHandle& dc, HIMAGELIST hImageList, POINT point, int nImage)
{
	IMAGELISTDRAWPARAMS ildp = {0};
	ildp.cbSize = sizeof(IMAGELISTDRAWPARAMS);
	ildp.himl = hImageList;
	ildp.i = nImage;
	ildp.hdcDst = dc;
	ildp.x = point.x;
	ildp.y = point.y;
	ildp.cx = 0;
	ildp.cy = 0;
	ildp.xBitmap = 0;
	ildp.yBitmap = 0;
	ildp.fStyle = ILD_TRANSPARENT;
	ildp.fState = ILS_SATURATE;
	ildp.Frame = 0;
	ImageList_DrawIndirect(&ildp);
}

void IW::Skin::DrawButton(LPNMTBCUSTOMDRAW lpTBCustomDraw, HIMAGELIST hImageList, const CSize& sizeImageIn,
                          bool bDrawArrows)
{
	CRect r = lpTBCustomDraw->nmcd.rc;
	CToolBarCtrl tb(lpTBCustomDraw->nmcd.hdr.hwndFrom);
	CDCHandle dc = lpTBCustomDraw->nmcd.hdc;

	// The caller's size is a fallback: ask the list, so the glyph stays centred
	// when the button size and the image size disagree.
	CSize sizeImage = sizeImageIn;

	if (hImageList != nullptr)
	{
		int cx = 0, cy = 0;
		if (::ImageList_GetIconSize(hImageList, &cx, &cy) && cx > 0 && cy > 0)
			sizeImage = CSize(cx, cy);
	}

	constexpr int cchText = 200;
	TCHAR szText[cchText] = {0};
	TBBUTTONINFO tbbi = {0};
	tbbi.cbSize = sizeof(TBBUTTONINFO);
	tbbi.dwMask = TBIF_TEXT | TBIF_IMAGE | TBIF_STYLE | TBIF_STATE;
	tbbi.pszText = szText;
	tbbi.cchText = cchText;
	tb.GetButtonInfo(static_cast<int>(lpTBCustomDraw->nmcd.dwItemSpec), &tbbi);

	UINT uItemState = lpTBCustomDraw->nmcd.uItemState;
	bool bHotlight = (uItemState & ODS_HOTLIGHT) != 0;
	bool bChecked = (tbbi.fsState & TBSTATE_CHECKED) != 0;
	bool bPressed = (tbbi.fsState & TBSTATE_PRESSED) != 0;
	bool bDisabled = (tbbi.fsState & TBSTATE_ENABLED) == 0;
	bool bDropDown = (tbbi.fsStyle & TBSTYLE_DROPDOWN) != 0;
	bool bDropWhole = (tbbi.fsStyle & BTNS_WHOLEDROPDOWN) != 0;

	CString str = tbbi.pszText;

	COLORREF clrHighlightText = Style::Color::HighlightText;
	COLORREF clrText = Style::Color::WindowText;

	int cxThumb = 10;
	CRect rArrow = r;
	rArrow.left = rArrow.right - cxThumb;

	if (bDisabled)
	{
		lpTBCustomDraw->clrText = 0x808080;
	}
	else if (bPressed)
	{
		dc.FillSolidRect(r, Style::Color::Highlight);
		dc.FrameRect(r, Style::Brush::EmphasizedHighlight);
		lpTBCustomDraw->clrText = clrHighlightText;
	}
	else if (bHotlight)
	{
		dc.FillSolidRect(r, Average(Style::Color::Highlight, Style::Color::Window));
		dc.FrameRect(r, Style::Brush::EmphasizedHighlight);
		lpTBCustomDraw->clrText = clrHighlightText;
	}
	else if (bChecked)
	{
		dc.FillSolidRect(r, Average(Style::Color::Highlight, Style::Color::Window));
		lpTBCustomDraw->clrText = clrText;
	}
	else
	{
		lpTBCustomDraw->clrText = clrText;
	}

	HFONT hFont = tb.GetFont();
	HFONT hFontOld = nullptr;
	if (hFont != nullptr)
		hFontOld = dc.SelectFont(hFont);

	if (bDrawArrows)
	{
		if (bDropDown && (bPressed || bHotlight))
		{
			dc.FrameRect(rArrow, Style::Brush::EmphasizedHighlight);
		}

		if (bDropWhole || bDropDown)
		{
			// Paint arrow				
			DrawArrow(dc, rArrow);
			r.right -= cxThumb;
		}
	}

	int y = r.top + ((r.Height() - sizeImage.cy) / 2);

	if (!str.IsEmpty())
	{
		CRect rText(r);
		dc.DrawText(str, -1, rText, DT_SINGLELINE | DT_VCENTER | DT_CALCRECT);
		int width = rText.Width();
		if (tbbi.iImage != 0) width += sizeImage.cx + 2;
		if (width < r.Width()) r.left += (r.Width() - width) / 2;
	}
	else
	{
		r.left += ((r.Width() - sizeImage.cx) / 2);
	}

	if (hImageList != nullptr)
	{
		if (bDisabled)
		{
			DrawBitmapDisabled(dc, hImageList, CPoint(r.left, y), tbbi.iImage);
		}
		else
		{
			ImageList_Draw(hImageList, tbbi.iImage, dc, r.left, y, ILD_TRANSPARENT);
		}

		r.left += sizeImage.cx + 2;
	}

	if (!str.IsEmpty())
	{
		dc.SetTextColor(lpTBCustomDraw->clrText);
		dc.SetBkMode(lpTBCustomDraw->nStringBkMode);
		dc.DrawText(str, -1, r, DT_SINGLELINE | DT_VCENTER);
	}

	if (hFont != nullptr)
		dc.SelectFont(hFontOld);
}

struct _MenuItemData // menu item data
{
	DWORD dwMagic;
	LPTSTR lpstrText;
	UINT fType;
	UINT fState;
	int iButton;

	_MenuItemData() { dwMagic = 0x1313; }
	bool IsCmdBarMenuItem() { return (dwMagic == 0x1313); }
};

void DrawMenuText(CDCHandle& dc, RECT& rc, LPCTSTR lpstrText, COLORREF color)
{
	constexpr bool m_bShowKeyboardCues = true;
	int nTab = -1;
	for (int i = 0; i < lstrlen(lpstrText); i++)
	{
		if (lpstrText[i] == _T('\t'))
		{
			nTab = i;
			break;
		}
	}
	dc.SetTextColor(color);
	dc.DrawText(lpstrText, nTab, &rc, DT_SINGLELINE | DT_LEFT | DT_VCENTER | (m_bShowKeyboardCues ? 0 : DT_HIDEPREFIX));
	if (nTab != -1)
		dc.DrawText(&lpstrText[nTab + 1], -1, &rc,
		            DT_SINGLELINE | DT_RIGHT | DT_VCENTER | (m_bShowKeyboardCues ? 0 : DT_HIDEPREFIX));
}

void IW::Skin::DrawMenuItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	auto pMI = (_MenuItemData*)lpDrawItemStruct->itemData;
	auto pSMI = (ShellMenuItem*)lpDrawItemStruct->itemData;

	CDCHandle dc = lpDrawItemStruct->hDC;
	const RECT& rcItem = lpDrawItemStruct->rcItem;

	CSize m_szBitmap(16, 16);
	CSize m_szButton(m_szBitmap.cx + 2 * 3, m_szBitmap.cy + 2 * 3);


	BOOL bDisabled = lpDrawItemStruct->itemState & ODS_GRAYED;
	BOOL bSelected = lpDrawItemStruct->itemState & ODS_SELECTED;
	BOOL bChecked = lpDrawItemStruct->itemState & ODS_CHECKED;

	// paint background
	if (bSelected)
	{
		dc.FillSolidRect(&rcItem, Style::Color::Highlight);
		dc.FrameRect(&rcItem, Style::Brush::EmphasizedHighlight);
	}
	else
	{
		dc.FillSolidRect(&rcItem, Style::Color::MenuBackground);
	}

	if (pMI->fType & MFT_SEPARATOR)
	{
		// draw separator
		CRect rc = rcItem;
		rc.bottom = rc.top = (rc.bottom + rc.top) / 2;
		rc.InflateRect(-2, 1);

		dc.FillSolidRect(&rc, Emphasize(Style::Color::MenuBackground, 64)); // draw separator line
	}
	else // not a separator
	{
		if (LOWORD(lpDrawItemStruct->itemID) == static_cast<WORD>(-1))
			bSelected = FALSE;
		CRect rcButn(rcItem.left, rcItem.top, rcItem.left + m_szButton.cx, rcItem.top + m_szButton.cy); // button rect
		rcButn.OffsetRect(0, ((rcItem.bottom - rcItem.top) - (rcButn.bottom - rcButn.top)) / 2); // center vertically

		// draw background and border for checked items
		if (bChecked)
		{
			CRect rcCheck = rcButn;
			rcCheck.DeflateRect(1, 1);
			dc.FrameRect(&rcCheck, Style::Brush::EmphasizedHighlight);
		}

		// calc drawing point
		SIZE sz = {rcButn.right - rcButn.left - m_szBitmap.cx, rcButn.bottom - rcButn.top - m_szBitmap.cy};
		sz.cx /= 2;
		sz.cy /= 2;
		POINT point = {rcButn.left + sz.cx, rcButn.top + sz.cy};

		HIMAGELIST hImageList = App.GetGlobalBitmap();

		if (pSMI->IsValid())
		{
			hImageList = App.GetShellImageList(true);
		}

		int iButton = pMI->iButton;
		if (iButton >= 0)
		{
			// draw disabled or normal
			if (!bDisabled)
			{
				ImageList_Draw(hImageList, iButton, dc, point.x, point.y, ILD_TRANSPARENT);
			}
			else
			{
				DrawBitmapDisabled(dc, hImageList, point, iButton);
			}
		}
		else
		{
			if (bChecked)
			{
				CRect r(point, m_szBitmap);
				dc.FillSolidRect(r, Average(Style::Color::Highlight, Style::Color::Window));
			}
		}

		constexpr int s_kcxGap = 1;
		constexpr int s_kcxTextMargin = 2;


		// draw item text
		int cxButn = m_szButton.cx;
		// calc text rectangle and colors
		RECT rcText = rcItem;
		rcText.left += cxButn + s_kcxGap + s_kcxTextMargin;
		rcText.right -= cxButn;
		dc.SetBkMode(TRANSPARENT);

		COLORREF colorText = bDisabled
			                     ? Emphasize(Style::Color::MenuBackground, 100)
			                     : (bSelected ? Style::Color::HighlightText : Style::Color::WindowText);
		DrawMenuText(dc, rcText, pMI->lpstrText, colorText); // finally!
	}
}

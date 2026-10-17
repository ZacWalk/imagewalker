// ImageWalker by Zac Walker
//
// Purpose: The navigation thumbnail shown when a zoomed image is larger than
//          its pane.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

template<class TParent>
class CImageNavigation : 
	public CWindowImpl<CImageNavigation<TParent> >  
{
public:

	CImageNavigation(TParent &parent) : m_parent(parent)
	{
	}

	~CImageNavigation()
	{
	}	

	static void Track(TParent &parent, HWND hwndParent, const CRect &rect)
	{
		CImageNavigation nav(parent);

		const CRect rectImage = nav.m_parent.GetDrawnImageRect();
		const CRect rectView(nav.m_parent.GetScrollOffset(), nav.m_parent.GetClientSize());
		nav._image = parent._state.Image.GetThumbnailImage();

		if (nav._image.IsEmpty() || rectImage.Width() <= 0 || rectImage.Height() <= 0)
			return;

		const CSize sizeThumb = nav._image.GetBoundingRect().Size();

		if (sizeThumb.cx <= 0 || sizeThumb.cy <= 0)
			return;

		// sizeThumb is the client the thumbnail needs; the border goes outside it.
		CRect rectWindow(CPoint(0, 0), sizeThumb);
		::AdjustWindowRectEx(&rectWindow, WS_POPUP | WS_BORDER, FALSE, 0);
		rectWindow.OffsetRect(rect.CenterPoint() - rectWindow.CenterPoint());

		HDC hdcScreen = CreateDC(_T("DISPLAY"), NULL, NULL, NULL); 
		CRect rectScreen(0,0, GetDeviceCaps(hdcScreen, HORZRES),  GetDeviceCaps(hdcScreen, VERTRES)); 
		DeleteDC(hdcScreen);

		rectWindow  = IW::ClampRect(rectWindow, rectScreen);

		// Everything here is measured against the picture, not against the
		// canvas: the canvas also holds the pane's own margin and the gutter, so
		// scaling by it drew the thumbnail short and off to one side.
		nav._pointImage = rectImage.TopLeft();
		nav._sizeImage = rectImage.Size();
		nav._rectDrawImage = CRect(CPoint(0, 0), sizeThumb);

		CRect rectViewInImage(rectView);
		rectViewInImage.OffsetRect(-nav._pointImage);

		nav._rectSroller = IW::ClampRect(IW::MulDivRect(rectViewInImage, sizeThumb, nav._sizeImage),
		                                 nav._rectDrawImage);

		if (nav.Create(hwndParent, rectWindow))
		{
			nav.SetCapture();

			CPoint pointFirst;
			GetCursorPos(&pointFirst); 
			nav.ScreenToClient(&pointFirst);		

			CRect rectClient;
			nav.GetClientRect(rectClient);
			nav._pointFirst = pointFirst;
			nav._sizeOffset = nav._rectSroller.TopLeft() - nav._pointFirst;

			// get messages until capture lost or cancelled/accepted
			MSG msg;
			while (nav.m_hWnd != 0)
			{
				int nRet = ::GetMessage(&msg, NULL, 0, 0);

				if (nRet == -1)
					break;

				if (nRet == 0)
				{
					::PostQuitMessage(static_cast<int>(msg.wParam));
					break;
				}

				if (::GetCapture() != nav.m_hWnd)
					nav.PostMessage(WM_CLOSE);

				::TranslateMessage(&msg);
				::DispatchMessage(&msg); 			
			}
		}
	}	

protected:

	IW::Image _image;
	TParent &m_parent;
	CRect _rectSroller;
	CRect _rectDrawImage;
	CPoint _pointFirst;
	CSize _sizeOffset;
	CPoint _pointImage;
	CSize _sizeImage;

public:

	BEGIN_MSG_MAP(CImageNavigation)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
	END_MSG_MAP()


	HWND Create(HWND hWndParent,  const CRect &rect)
	{
		CWindowImpl<CImageNavigation>::Create(hWndParent, (CRect)rect, NULL,
			WS_VISIBLE | WS_POPUP | WS_BORDER | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);

		return m_hWnd;
	}

	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		ATLTRACE(_T("Destroy CImageNavigation\n"));
		if (::GetCapture() == m_hWnd) ::ReleaseCapture();
		return 0;
	}

	LRESULT OnMouseMove(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

		CClientDC dc(m_hWnd);
		dc.DrawFocusRect(_rectSroller);
		CPoint pointOffset = point + _sizeOffset;

		_rectSroller.SetRect(pointOffset, pointOffset + _rectSroller.Size());

		CRect rectClient;
		GetClientRect(rectClient);
		_rectSroller = IW::ClampRect(_rectSroller, rectClient);

		// draw new rect
		dc.DrawFocusRect(_rectSroller);

		if (rectClient.Width() <= 0 || rectClient.Height() <= 0)
			return 0;

		const CPoint pointToScrollTo(
			_pointImage.x + MulDiv(_rectSroller.left, _sizeImage.cx, rectClient.Width()),
			_pointImage.y + MulDiv(_rectSroller.top, _sizeImage.cy, rectClient.Height()));

		m_parent.ScrollTo(pointToScrollTo);
		return 0;
	}

	LRESULT OnKeyDown(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		if (wParam == VK_ESCAPE)
		{
			CPoint ptOffset = m_parent.GetScrollOffset();
			m_parent.ScrollTo(ptOffset);
			PostMessage(WM_CLOSE);
		}
		return 0;
	}

	LRESULT OnLButtonUp(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		PostMessage(WM_CLOSE);
		return 0;
	}


	LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		ATLASSERT(::IsWindow(m_hWnd));
		CPaintDC dc(m_hWnd);
		IW::CRender render;

		if (render.Create(dc))
		{
			render.Fill(IW::Style::Color::Window);

			if (!_image.IsEmpty())
			{
				render.DrawImage(_image.GetFirstPage(), _rectDrawImage);      
			}

			render.Flip();
			dc.DrawFocusRect(_rectSroller);
		}
		return 0;
	}

	LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		// handled, no background painting needed
		return 1;
	}
};


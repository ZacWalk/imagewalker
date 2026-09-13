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

		CSize sizeAll = nav.m_parent.GetCanvasSize();
		CRect rectImage = nav.m_parent.GetDrawnImageRect();
		CRect rectClient(nav.m_parent.GetScrollOffset(), nav.m_parent.GetClientSize());
		nav._image = parent._state.Image.GetThumbnailImage();

		if (nav._image.IsEmpty())
			return;

		CSize sizeWindow = nav._image.GetBoundingRect().Size();
		CRect rectWindow(CPoint(0,0), sizeWindow);
		rectWindow.OffsetRect(rect.CenterPoint() - rectWindow.CenterPoint());

		HDC hdcScreen = CreateDC(_T("DISPLAY"), NULL, NULL, NULL); 
		CRect rectScreen(0,0, GetDeviceCaps(hdcScreen, HORZRES),  GetDeviceCaps(hdcScreen, VERTRES)); 
		DeleteDC(hdcScreen);

		rectWindow  = IW::ClampRect(rectWindow, rectScreen);

		// Work out the normalized
		// client area on our nav win
		nav._rectSroller = IW::MulDivRect(rectClient, sizeWindow, sizeAll);
		nav._rectDrawImage = IW::MulDivRect(rectImage, sizeWindow, sizeAll);

		if (nav.Create(hwndParent, rectWindow))
		{
			nav.SetCapture();

			CPoint pointFirst;
			GetCursorPos(&pointFirst); 
			nav.ScreenToClient(&pointFirst);		

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

		CSize sizeAll = m_parent.GetCanvasSize();

		CPoint pointToScrollTo(
			MulDiv(_rectSroller.left, sizeAll.cx, rectClient.Width()),
			MulDiv(_rectSroller.top, sizeAll.cy, rectClient.Height()));

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

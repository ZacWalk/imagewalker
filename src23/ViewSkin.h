// ImageWalker by Zac Walker
//
// Purpose: The skinned frame - the gradient chrome, the custom-drawn toolbar
//          buttons and the window shape.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#define WM_NCMOUSELEAVE                 0x02A2
#define TME_NONCLIENT   0x00000010

namespace IW
{
	namespace Skin
	{
		template<class T>
		class WindowImpl
		{
		public:

			typedef WindowImpl<T> ThisClass;
			CBitmap captionBitmap;
			

			enum 
			{
				SKINANCHOR_ICON, SKINANCHOR_SYSTEM,
				SKINANCHOR_MINIMISE, SKINANCHOR_MAXIMISE, SKINANCHOR_CLOSE,
				SKINANCHOR_COUNT
			};

			enum
			{
				TitleHeight = 28
			};

			struct Anchor
			{
				bool _bUse;
				int _nImage;
				RECT _rect;

				void SetAnchor(const RECT &rect, int nImage)
				{
					_bUse = true;
					_nImage = nImage;
					_rect = rect;
				}

				bool HitTestAnchor(const RECT& rcClient, const POINT &point)
				{
					if (_bUse)
					{
						RECT rcAnchor;
						ResolveAnchor( rcClient, rcAnchor );

						if ( ::PtInRect(&rcAnchor, point) ) 
							return true;
					}

					return false;
				}

				void ResolveAnchor(const RECT& rcClient, RECT& rcAnchor)
				{
					rcAnchor = _rect;
					::OffsetRect(&rcAnchor, rcAnchor.left < 0 ? rcClient.right : rcClient.left, 0 );
					::OffsetRect(&rcAnchor, 0, rcAnchor.top < 0 ? rcClient.bottom : rcClient.top );
				}

				void DrawCaptionButton(HDC hDC, bool bHover, bool bPressed)
				{
					static bool bNeedtInit = true;
					static CImageList captionButtons;
					static CImageList captionButtonsHover;

					if (bNeedtInit)
					{
						captionButtons.CreateFromImage(IDB_CAPTION_BUTTONS, 12, 1, RGB(255, 0, 255), IMAGE_BITMAP);
						captionButtonsHover.CreateFromImage(IDB_CAPTION_BUTTONS_HOVER, 12, 1, RGB(255, 0, 255), IMAGE_BITMAP);
						bNeedtInit = false;
					}


					int cx = ((_rect.left + _rect.right) / 2) - 6;
					int cy = ((_rect.top + _rect.bottom) / 2) - 6;

					if (bPressed && bHover)
					{
						cx += 2;
						cy += 2;
					}

					if (bHover)
					{
						captionButtonsHover.Draw(hDC, _nImage, cx, cy, ILD_NORMAL);
					}
					else
					{
						captionButtons.Draw(hDC, _nImage, cx, cy, ILD_NORMAL);
					}
				}
			};

			void DrawCaption(CDCHandle dc, CRect r, int nTop = 0)
			{

				CDC dcBm;
				if (dcBm.CreateCompatibleDC(dc))
				{
					HBITMAP hbmOld = dcBm.SelectBitmap(captionBitmap);

					if (hbmOld)
					{
						BITMAP bitmapStats;
						captionBitmap.GetBitmap(&bitmapStats);

						int nCurv = 3;

						int nOldSetStretchBltMode = dc.SetStretchBltMode(HALFTONE);
						::SetBrushOrgEx(dc, 0, 0, NULL);

						dc.StretchBlt(r.left, r.top, nCurv, r.Height(), dcBm, 0, nTop, nCurv, bitmapStats.bmHeight - nTop, SRCCOPY);
						dc.StretchBlt(r.left + nCurv, r.top, r.Width(), r.Height(), dcBm, nCurv, nTop, bitmapStats.bmWidth - (nCurv * 2), bitmapStats.bmHeight - nTop, SRCCOPY);
						dc.StretchBlt(r.right - nCurv, r.top, nCurv, r.Height(), dcBm, bitmapStats.bmWidth - nCurv, nTop, nCurv, bitmapStats.bmHeight - nTop, SRCCOPY);

						dc.SetStretchBltMode(nOldSetStretchBltMode);
						dcBm.SelectBitmap(hbmOld);
					}			
				}
			}

			Anchor m_anchors[ SKINANCHOR_COUNT ];

			int				m_nHoverAnchor;
			int				m_nDownAnchor;
			int BORDER_WIDTH;
			int SIZEBOX_WIDTH;		
			bool m_bTracking;

			CSize			m_szMinSize;
			CRect			m_rcMaximise;
			CRect			m_rcResize;

			WindowImpl()
			{
				m_bTracking = false;

				BORDER_WIDTH = GetSystemMetrics( SM_CXSIZEFRAME );
				SIZEBOX_WIDTH = GetSystemMetrics( SM_CXSIZE );

				m_nHoverAnchor	= 0;
				m_nDownAnchor	= 0;

				m_szMinSize.cx = m_szMinSize.cy = 0;
				m_rcMaximise.SetRect( -1, 0, -1, -1 );
				m_rcResize.SetRect( BORDER_WIDTH, BORDER_WIDTH, BORDER_WIDTH, BORDER_WIDTH );

				captionBitmap.LoadBitmap(IDB_CAPTION_BACKGROUND);			

				ResetAnchors();
			}

			~WindowImpl()
			{
				ATLTRACE("Delete Skin::WindowImpl\n");
			}

			

			// Message map
			BEGIN_MSG_MAP(ThisClass)

				if (App.Settings.BlackSkin)
				{
					MESSAGE_HANDLER(WM_CREATE, OnCreate)
					MESSAGE_HANDLER(WM_SIZE, OnSize)
					MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
					MESSAGE_HANDLER(WM_GETMINMAXINFO, OnGetMinMaxInfo)
					MESSAGE_HANDLER(WM_NCACTIVATE, OnNCActivate)
					MESSAGE_HANDLER(WM_NCCALCSIZE, OnNCCalcSize)
					MESSAGE_HANDLER(WM_NCHITTEST, OnNCHitTest)
					MESSAGE_HANDLER(WM_NCLBUTTONDOWN, OnNCLButtonDown)
					MESSAGE_HANDLER(WM_NCLBUTTONUP, OnNCLButtonUp)
					MESSAGE_HANDLER(WM_NCMOUSELEAVE, OnNCMouseLeave)
					MESSAGE_HANDLER(WM_NCMOUSEMOVE, OnNCMouseMove)
					MESSAGE_HANDLER(WM_NCPAINT, OnNCPaint)
					MESSAGE_HANDLER(WM_SETTEXT, OnSetText)
				}

			END_MSG_MAP()
			
		private:			

			/////////////////////////////////////////////////////////////////////////////////////////////////////
			/////////////////////////////////////////////////////////////////////////////////////////////////////

			LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
			{
				OnSize();
				bHandled = false;
				return 0;
			}
			
			LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
			{
				OnSize();

				bHandled = false;

				return 0;
			}

			
			LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
			{
				OnEraseBackground((HDC)wParam);
				return 1;
			}
			

			LRESULT OnNCLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
			{
				CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); 
				T *pT = static_cast<T*>(this);

				int nHitTest = static_cast<int>(wParam);
				int nAnchor = HitCodeToAnchor(nHitTest);
				if (nAnchor == 0) return ::DefWindowProc(pT->m_hWnd, uMsg, wParam, lParam); 

				m_nHoverAnchor = m_nDownAnchor = nAnchor;

				if ( nAnchor == SKINANCHOR_SYSTEM )
				{
					CRect rcWindow, rcSystem;

					pT->GetWindowRect( &rcWindow );
					m_anchors[SKINANCHOR_SYSTEM].ResolveAnchor(rcWindow, rcSystem);
					CMenuHandle popup(pT->GetSystemMenu( FALSE ));

					Paint( true );

					DWORD nTime = GetTickCount();

					UINT nCmdID = popup.TrackPopupMenu( TPM_LEFTALIGN|TPM_TOPALIGN|TPM_RETURNCMD, rcSystem.left, rcSystem.bottom, pT->m_hWnd, NULL );

					m_nHoverAnchor = m_nDownAnchor = 0;

					if ( nCmdID )
					{
						pT->PostMessage( WM_SYSCOMMAND, nCmdID, MAKELONG( rcSystem.left, rcSystem.bottom ) );
					}
					else if ( GetTickCount() - nTime < 300 )
					{
						GetCursorPos( &point );
						if ( OnNcHitTest( point ) == HTSYSMENU )
						{
							pT->PostMessage( WM_SYSCOMMAND, SC_CLOSE, MAKELONG( rcSystem.left, rcSystem.bottom ) );
						}
					}
				}
				
				Paint( true );
				Track();


				return 0;
			}

			LRESULT OnNCLButtonUp(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
			{
				if ( ! m_nDownAnchor ) return 1;

				if ( m_nDownAnchor == m_nHoverAnchor )
				{
					T *pT = static_cast<T*>(this);

					switch ( m_nDownAnchor )
					{
					case SKINANCHOR_MINIMISE:
						pT->PostMessage( WM_SYSCOMMAND, SC_MINIMIZE );
						break;
					case SKINANCHOR_MAXIMISE:
						pT->PostMessage( WM_SYSCOMMAND, pT->IsZoomed() ? SC_RESTORE : SC_MAXIMIZE );
						break;
					case SKINANCHOR_CLOSE:
						pT->PostMessage( WM_SYSCOMMAND, SC_CLOSE );
						break;
					}
				}

				m_nHoverAnchor = 0;
				m_nDownAnchor = 0;

				Paint( true );

				return 0;
			}

			LRESULT OnNCMouseMove(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
			{
				int nHitTest = static_cast<int>(wParam);
				int nAnchor = HitCodeToAnchor(nHitTest);
				
				if ( m_nDownAnchor && m_nDownAnchor != nAnchor )
				{
					if ( ( GetAsyncKeyState( VK_LBUTTON ) & 0x8000 ) == 0 )
						m_nDownAnchor = 0;

					nAnchor = 0;
				}

				if ( nAnchor != m_nHoverAnchor )
				{
					m_nHoverAnchor = nAnchor;
					Paint( true );	
					Track();
				}	

				return 1;
			}

			void Track()
			{
				if (!m_bTracking)
				{
					T *pT = static_cast<T*>(this);

					TRACKMOUSEEVENT track;
					track.cbSize = sizeof(TRACKMOUSEEVENT);
					track.dwFlags = TME_NONCLIENT | TME_LEAVE;
					track.hwndTrack = pT->m_hWnd;
					track.dwHoverTime = 100;
					m_bTracking = TrackMouseEvent(&track) != 0;
				}
			}

			LRESULT OnNCMouseLeave(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
			{
				if (m_nHoverAnchor != 0)
				{
					m_nHoverAnchor = 0;
					m_nDownAnchor = 0;

					Paint( true );
				}

				m_bTracking = false;

				return 0;
			};

			LRESULT OnNCHitTest(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
			{
				CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); 
				return OnNcHitTest(point);
			}

			LRESULT OnNCCalcSize(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
			{
				NCCALCSIZE_PARAMS * lpncsp = (NCCALCSIZE_PARAMS*)(lParam);
				CalcWindowRect( &lpncsp->rgrc[0], TRUE );
				return 0;
			}

			LRESULT OnNCPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
			{
				Paint(false);
				return 0;
			}

			LRESULT OnGetMinMaxInfo(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
			{
				OnGetMinMaxInfo((MINMAXINFO*)lParam);
				return 0;
			}

			LRESULT OnNCActivate(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
			{
				bool bActive = wParam != 0;
				Paint( bActive);
				return bActive ? 0 : 1;
			}

			LRESULT OnSetText(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
			{
				Paint(true);
				return 0;
			}


			/////////////////////////////////////////////////////////////////////////////////////////////////////
			/////////////////////////////////////////////////////////////////////////////////////////////////////

		

			void CalcWindowRect(RECT* pRect, BOOL bToClient)
			{
				CRect rcAdjust( BORDER_WIDTH, TitleHeight, BORDER_WIDTH, BORDER_WIDTH );

				if ( bToClient )
				{
					pRect->left		+= rcAdjust.left;
					pRect->top		+= rcAdjust.top;
					pRect->right	-= rcAdjust.right;
					pRect->bottom	-= rcAdjust.bottom;
				}
				else
				{
					pRect->left		-= rcAdjust.left;
					pRect->top		-= rcAdjust.top;
					pRect->right	+= rcAdjust.right;
					pRect->bottom	+= rcAdjust.bottom;
				}
			}

		protected:

			void OnGetMinMaxInfo(MINMAXINFO* lpMMI)
			{
				CRect rcWork;
				SystemParametersInfo( SPI_GETWORKAREA, 0, &rcWork, 0 );
				rcWork.OffsetRect( -rcWork.left, -rcWork.top );

				CRect rcAdjusted( &rcWork );
				CalcWindowRect( &rcAdjusted, FALSE );

				if ( m_rcMaximise.left < 0 )
					rcWork.left = rcAdjusted.left;
				else
					rcWork.left -= m_rcMaximise.left;

				if ( m_rcMaximise.top < 0 )
					rcWork.top = rcAdjusted.top;
				else
					rcWork.top -= m_rcMaximise.top;

				if ( m_rcMaximise.right < 0 )
					rcWork.right = rcAdjusted.right;
				else
					rcWork.right += m_rcMaximise.right;

				if ( m_rcMaximise.bottom < 0 )
					rcWork.bottom = rcAdjusted.bottom;
				else
					rcWork.bottom += m_rcMaximise.bottom;

				lpMMI->ptMaxPosition.x	= rcWork.left;
				lpMMI->ptMaxPosition.y	= rcWork.top;
				lpMMI->ptMaxSize.x		= rcWork.right;
				lpMMI->ptMaxSize.y		= rcWork.bottom;

				lpMMI->ptMinTrackSize.x	= max( lpMMI->ptMinTrackSize.x, m_szMinSize.cx );
				lpMMI->ptMinTrackSize.y	= max( lpMMI->ptMinTrackSize.y, m_szMinSize.cy );
			}

		private:			

			UINT OnNcHitTest(CPoint point)
			{
				T *pT = static_cast<T*>(this);
				bool bResizable = true;

				CRect rc, rcAnchor;
				pT->GetWindowRect(&rc);

				UINT codes[] = { HTSYSMENU, HTSYSMENU, HTREDUCE, HTZOOM, HTCLOSE };

				for(int i = 0; i < SKINANCHOR_COUNT; i++)
				{
					if (m_anchors[i].HitTestAnchor(rc, point))
						return codes[i];
				}

				if ( bResizable && ! pT->IsZoomed() )
				{
					if ( point.x >= rc.right - SIZEBOX_WIDTH && point.y >= rc.bottom - SIZEBOX_WIDTH )
						return HTBOTTOMRIGHT;

					if ( point.x < rc.left + m_rcResize.left )
					{
						if ( point.y < rc.top + m_rcResize.top ) return HTTOPLEFT;
						else if ( point.y >= rc.bottom - m_rcResize.bottom ) return HTBOTTOMLEFT;
						else return HTLEFT;
					}
					else if ( point.x >= rc.right - m_rcResize.right )
					{
						if ( point.y < rc.top + m_rcResize.top ) return HTTOPRIGHT;
						else if ( point.y >= rc.bottom - m_rcResize.bottom ) return HTBOTTOMRIGHT;
						else return HTRIGHT;
					}
					else if ( point.y < rc.top + m_rcResize.top )
					{
						if ( point.x < rc.left + m_rcResize.left ) return HTTOPLEFT;
						else if ( point.x >= rc.right - m_rcResize.right ) return HTTOPRIGHT;
						return HTTOP;
					}
					else if ( point.y >= rc.bottom - m_rcResize.bottom )
					{
						if ( point.x < rc.left + m_rcResize.left ) return HTBOTTOMLEFT;
						else if ( point.x >= rc.right - m_rcResize.right ) return HTBOTTOMRIGHT;
						return HTBOTTOM;
					}
				}

				CalcWindowRect(rc, TRUE );
				if ( point.y < rc.top ) return HTCAPTION;
				return rc.PtInRect( point ) ? HTCLIENT : HTBORDER;
			}

			

			void ResetAnchors()
			{
				for(int i = 0; i < SKINANCHOR_COUNT; i++)
				{
					m_anchors[i]._bUse = false;
				}
			}

			void CalcSkinSize()
			{
				T *pT = static_cast<T*>(this);

				ResetAnchors();				

				RECT  rCap;
				CalcCaptionRect( pT->m_hWnd, rCap );

				// Get window style
				DWORD dStyle   = pT->GetWindowLong( GWL_STYLE );

				// Check if we have a caption
				if ( ( dStyle & WS_CAPTION ) == WS_CAPTION )
				{
					// Get button dimensions
					int cxBtn = GetSystemMetrics( SM_CXSIZE );

					// Calc position and draw close button
					CRect  rPos;

					rPos.top    = rCap.top + 2;
					rPos.bottom = rCap.bottom - 2;
					rPos.right  = rCap.right - 2;
					rPos.left   = rCap.right - 2 - ( cxBtn - 2);

					m_anchors[SKINANCHOR_CLOSE].SetAnchor(rPos, 3);

					// Calc position and draw maximize<->restore/help button
					if ( ( dStyle & WS_MAXIMIZEBOX ) == WS_MAXIMIZEBOX )
					{
						rPos.OffsetRect(-cxBtn, 0);
						m_anchors[SKINANCHOR_MAXIMISE].SetAnchor(rPos, pT->IsZoomed() ? 2 : 1);
					}

					// Calc position and draw minimize and iconize button
					if ( ( dStyle & WS_MINIMIZEBOX ) == WS_MINIMIZEBOX )
					{
						// Minimize button
						rPos.OffsetRect(-cxBtn, 0);
						m_anchors[SKINANCHOR_MINIMISE].SetAnchor(rPos, 0);
					}
				}
			}
			
			
			
			void OnSize()
			{
				T *pT = static_cast<T*>(this);
				if ( pT->IsIconic() ) return;
				
				CRect rect;

				pT->GetWindowRect(rect);
				rect.OffsetRect( -rect.left, -rect.top );
				//rcWnd.right++; rcWnd.bottom++;

				HRGN hRgn = CreateRectRgnIndirect(rect);
				pT->SetWindowRgn( hRgn, TRUE );

				/*if ( pT->IsIconic() ) return;

				if ( pT->IsZoomed() )
				{
					pT->SetWindowRgn( NULL, TRUE );
				}
				else if ( m_pRegionXML )
				{
					SelectRegion( pWnd );
				}
				else if ( CoolInterface.IsNewWindows() )
				{
					CRect rcWnd;

					pT->GetWindowRect( &rcWnd );
					rcWnd.OffsetRect( -rcWnd.left, -rcWnd.top );
					rcWnd.right++; rcWnd.bottom++;

					HRGN hRgn = CreateRectRgnIndirect( &rcWnd );
					pT->SetWindowRgn( hRgn, TRUE );
				}
				*/
			}

			void OnEraseBackground(CDCHandle dc)
			{
			}


			void CalcCaptionRect( HWND hWnd, RECT& Rect )
			{	
				DWORD dStyle		;
				SIZE  sFrame		;
				int	  Icon			;	

				// Get frame size of window
				dStyle    = GetWindowLong( hWnd, GWL_STYLE );
				sFrame.cx = GetSystemMetrics( ( dStyle & WS_THICKFRAME ) ? SM_CXSIZEFRAME : SM_CXFIXEDFRAME );
				sFrame.cy = GetSystemMetrics( ( dStyle & WS_THICKFRAME ) ? SM_CYSIZEFRAME : SM_CYFIXEDFRAME );

				// Get width of icon/button in caption
				Icon = GetSystemMetrics( SM_CXSIZE );

				// Calculate rectangle dimensions
				::GetWindowRect( hWnd, &Rect );
				Rect.bottom -= Rect.top;
				Rect.right  -= Rect.left;
				Rect.top     = 0;
				Rect.left    = 0;

				Rect.left   += sFrame.cx;
				Rect.right  -= sFrame.cx;
				Rect.top    += sFrame.cy;
				Rect.bottom  = Rect.top + GetSystemMetrics( SM_CYCAPTION )
					- GetSystemMetrics( SM_CYBORDER );
			}

			


			void Paint(bool bActive)
			{
				T *pT = static_cast<T*>(this);
				CWindowDC dc(pT->m_hWnd);	

				CalcSkinSize();

				CRect r;
				pT->GetWindowRect(r);
				r.OffsetRect( -r.left, -r.top );

				COLORREF clr = 0x000000;

				CRect rectFrame(r);
				rectFrame.top += TitleHeight;

				dc.FillSolidRect(CRect(rectFrame.left, rectFrame.top, rectFrame.left + BORDER_WIDTH, rectFrame.bottom), clr);
				dc.FillSolidRect(CRect(rectFrame.right - BORDER_WIDTH, rectFrame.top, rectFrame.right, rectFrame.bottom), clr);
				dc.FillSolidRect(CRect(rectFrame.left, rectFrame.bottom - BORDER_WIDTH, rectFrame.right, rectFrame.bottom), clr);

				{
					CRect rectCaption(r);
					rectCaption.bottom = rectCaption.top + TitleHeight;
					CMemoryDC memdc(dc, rectCaption);
					PaintCaption(memdc, rectCaption);
				}

				//CRect rectInsideFrame(rectFrame.left + BORDER_WIDTH, rectFrame.top,rectFrame.right - BORDER_WIDTH, rectFrame.bottom - BORDER_WIDTH);
				//rectInsideFrame.InflateRect(1,1);
				//dc.Draw3dRect(rectInsideFrame, ::GetSysColor(COLOR_3DSHADOW),::GetSysColor(COLOR_3DHILIGHT));
			}

			void PaintCaption(HDC hdc, const CRect &r)
			{
				T *pT = static_cast<T*>(this);
				CDCHandle dc(hdc);

				DrawCaption(dc, r);			

				// Horiz text
				dc.SetTextColor(RGB(255,255,255));
				dc.SetBkMode(TRANSPARENT);

				HFONT hFontOld = dc.SelectFont(IW::Style::GetFont(IW::Style::Font::Heading));

				int nIndent = TitleHeight;

				CRect rectText(r.left + nIndent, r.top, r.Width() - nIndent, TitleHeight);

				TCHAR sz[100];
				pT->GetWindowText(sz, 100);

				TCHAR szClass[200];
				::GetClassName(pT->m_hWnd, szClass, countof(szClass));

				WNDCLASSEX wc;
				wc.cbSize = sizeof(WNDCLASSEX);

				// try process local class first
				BOOL bRet = ::GetClassInfoEx(_AtlBaseModule.GetModuleInstance(), szClass, &wc);

				if(!bRet)    // try global class

					bRet = ::GetClassInfoEx(NULL, szClass, &wc);

				if (bRet)
				{
					HICON hIcon = wc.hIconSm;
					int nOffset = (TitleHeight - 16) / 2;
					dc.DrawIconEx(nOffset, nOffset, hIcon, 16, 16, 0, NULL, DI_NORMAL | DI_COMPAT);
				}

				dc.DrawText(sz, -1, &rectText, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
				dc.SelectFont(hFontOld);

				for(int i = 0; i < SKINANCHOR_COUNT; i++)
				{
					if (m_anchors[i]._bUse)
					{
						m_anchors[i].DrawCaptionButton(dc, m_nHoverAnchor == i, m_nDownAnchor == i);
					}
				}
			}

			static int HitCodeToAnchor(int nHitTest)
			{
				int nAnchor = 0;
				if ( nHitTest == HTSYSMENU ) nAnchor = SKINANCHOR_SYSTEM;
				else if ( nHitTest == HTREDUCE ) nAnchor = SKINANCHOR_MINIMISE;
				else if ( nHitTest == HTZOOM ) nAnchor = SKINANCHOR_MAXIMISE;
				else if ( nHitTest == HTCLOSE ) nAnchor = SKINANCHOR_CLOSE;
				return nAnchor;
			}
		};

		void DrawGradient(CDCHandle dc, const CRect &r, DWORD c1, DWORD c2);
		void DrawArrow(CDCHandle& dc, const CRect &rc);
		void DrawBitmapDisabled(CDCHandle& dc, HIMAGELIST hImageList, POINT point, int nImage);
		void DrawButton(LPNMTBCUSTOMDRAW lpTBCustomDraw, HIMAGELIST hImageList, const CSize &sizeImage, bool bDrawArrows = true);
		void DrawMenuItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
	}
}

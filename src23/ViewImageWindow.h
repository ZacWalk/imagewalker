// ImageWalker by Zac Walker
//
// Purpose: The behaviour the image panes share: zoom, scroll, the tool under
//          the mouse, and the frames drawn over the picture.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "iw/cursorautohide.h"

#include "ViewImageTools.h"
#include "ViewImageFrames.h"
#include "ImagingFilter.h"

class FrameVisitor;


// The image and file commands shared by the items view and full screen.
// Both hosts chain their own switch here.
template<class T>
class ImageCommandImpl
{
public:

	void CopyOrMoveTo(const CString &strDestination, bool bMove)
	{
		T *pT = static_cast<T*>(this);
		IW::Focus preserveFocus;
		IW::FileOperation operation(pT->GetSelectedFileList());

		if (operation.Copy(strDestination, bMove))
		{
			pT->OnAfterCopy(bMove);
		}
		else if (!operation.WasAborted())
		{
			IW::CMessageBoxIndirect mb;
			mb.Show(IDS_FAILEDTOCOPYORMOVE);
		}
	}

	// Ask for a folder, remember it, then copy or move the selection into it.
	void CopyOrMoveToNewLocation(bool bMove)
	{
		T *pT = static_cast<T*>(this);
		IW::CShellItem item;

		if (item.Open(pT->m_hWnd, CSIDL_MYPICTURES))
		{
			CString strPath;
			FavouriteState &favourite = pT->_state.Favourite;

			if (!favourite.IsEmpty())
				item = favourite.GetTop();

			if (IW::CShellDesktop::GetDirectory(pT->m_hWnd, item)
				&& item.GetPath(strPath))
			{
				favourite.Add(item);
				CopyOrMoveTo(strPath, bMove);
			}
		}
	}

	void TrackCopyOrMoveToMenu(bool bMove)
	{
		T *pT = static_cast<T*>(this);

		if (bMove ? pT->CanMove() : pT->CanCopy())
		{
			pT->OnBeforeFileOperation();

			CRect rc;
			pT->GetClientRect(rc);
			pT->MapWindowPoints(HWND_DESKTOP, rc);
			CPoint point = rc.CenterPoint();

			IW::CShellMenu cmdbar;

			if (bMove)
				pT->_state.Favourite.GetMoveToMenu(cmdbar);
			else
				pT->_state.Favourite.GetCopyToMenu(cmdbar);

			pT->_pCoupling->TrackPopupMenu(cmdbar, TPM_CENTERALIGN | TPM_VCENTERALIGN, point.x, point.y);
		}
	}

	void DeleteSelectedItems()
	{
		T *pT = static_cast<T*>(this);
		IW::Focus preserveFocus;
		pT->OnBeforeFileOperation();

		IW::FileOperation operation(pT->GetSelectedFileList());

		if (operation.Delete(true))
		{
			pT->OnAfterDelete();
		}
		else if (!operation.WasAborted())
		{
			IW::CMessageBoxIndirect mb;
			mb.Show(IDS_COULD_NOT_DELETE);
		}
	}

	void ShowDescription()
	{
		T *pT = static_cast<T*>(this);
		ImageState &imageState = pT->_state.Image;

		if (imageState.CanEditImage())
		{
			imageState.StopLoading();
			CDescriptionDlg dlg(imageState);
			dlg.DoModal();
		}
	}

	bool InvokeImageCommand(DWORD id)
	{
		T *pT = static_cast<T*>(this);
		ImageState &imageState = pT->_state.Image;

		switch (id)
		{
		// Image
		case ID_IMAGE_COPY:
			imageState.Copy();
			return true;

		case ID_IMAGE_EDITDESCRIPTION:
			ShowDescription();
			return true;

		case ID_SCALE_100: pT->SetScale(_T("100%")); return true;
		case ID_SCALE_200: pT->SetScale(_T("200%")); return true;
		case ID_SCALE_50: pT->SetScale(_T("50%")); return true;
		case ID_SCALE_DOWN: pT->SetScale(_T("Down")); return true;
		case ID_SCALE_FIT: pT->SetScale(_T("Fit")); return true;
		case ID_SCALE_FILL: pT->SetScale(_T("Fill")); return true;
		case ID_SCALE_UP: pT->SetScale(_T("Up")); return true;
		case ID_SCALE_TOGGLE: pT->ToggleScale(); return true;

		case ID_EDIT_UNDO_IMAGE:
			pT->OnBeforeFileOperation();
			imageState.Undo();
			return true;

		case ID_EDIT_SAVE:
			{
				IW::Focus preserveFocus;
				pT->OnBeforeFileOperation();
				imageState.Save();
			}
			return true;

		case ID_EDIT_SAVE_MOVENEXT:
			{
				IW::Focus preserveFocus;

				if (imageState.Save())
				{
					pT->NextImage();
				}
			}
			return true;

		case ID_EDIT_SAVEAS:
			{
				IW::Focus preserveFocus;
				imageState.SaveAs();
			}
			return true;

		// File
		case ID_EDIT_DELETE: DeleteSelectedItems(); return true;

		case ID_FILE_COPYTO:
		case ID_FILE_COPYTO_POPUP:
			TrackCopyOrMoveToMenu(false);
			return true;

		case ID_FILE_MOVETO:
		case ID_FILE_MOVETO_POPUP:
			TrackCopyOrMoveToMenu(true);
			return true;

		case ID_MOVECOPY_CLEARLOCATIONS:
			pT->_state.Favourite.RemoveAll();
			return true;

		case ID_MOVETO_NEWLOCATION:
			pT->OnBeforeFileOperation();
			CopyOrMoveToNewLocation(true);
			return true;

		case ID_COPYTO_NEWLOCATION:
			pT->OnBeforeFileOperation();
			CopyOrMoveToNewLocation(false);
			return true;
		}

		return false;
	}

	bool GetImageCommandState(DWORD id, bool &bEnabled, bool &bChecked)
	{
		T *pT = static_cast<T*>(this);
		ImageState &imageState = pT->_state.Image;

		switch (id)
		{
		case ID_IMAGE_COPY:
		case ID_IMAGE_EDITDESCRIPTION:
		case ID_EDIT_SAVEAS:
			bEnabled = imageState.IsImageReady();
			return true;

		// Saving replaces the file the picture came from and re-encodes it on the
		// way, so it is offered only when there is a change worth that.
		case ID_EDIT_SAVE:
		case ID_EDIT_SAVE_MOVENEXT:
			bEnabled = imageState.IsImageReady() && imageState.IsDirty();
			return true;

		// A zoom is a way of looking at a picture, so the whole submenu goes
		// grey together when there is not one.
		case ID_SCALE_100: bChecked = IsScale(_T("100%")); bEnabled = imageState.IsImageShown(); return true;
		case ID_SCALE_200: bChecked = IsScale(_T("200%")); bEnabled = imageState.IsImageShown(); return true;
		case ID_SCALE_50: bChecked = IsScale(_T("50%")); bEnabled = imageState.IsImageShown(); return true;
		case ID_SCALE_DOWN: bChecked = IsScale(_T("Down")); bEnabled = imageState.IsImageShown(); return true;
		case ID_SCALE_FIT: bChecked = IsScale(_T("Fit")); bEnabled = imageState.IsImageShown(); return true;
		case ID_SCALE_FILL: bChecked = IsScale(_T("Fill")); bEnabled = imageState.IsImageShown(); return true;
		case ID_SCALE_UP: bChecked = IsScale(_T("Up")); bEnabled = imageState.IsImageShown(); return true;
		case ID_SCALE_TOGGLE: bEnabled = imageState.IsImageShown(); return true;

		case ID_EDIT_UNDO_IMAGE:
			bEnabled = imageState.CanUndo();
			return true;

		case ID_EDIT_DELETE:
			bEnabled = pT->CanDelete();
			return true;

		case ID_MOVECOPY_CLEARLOCATIONS:
		case ID_MOVETO_NEWLOCATION:
			return true;

		case ID_FILE_COPYTO:
		case ID_FILE_COPYTO_POPUP:
		case ID_COPYTO_NEWLOCATION:
			bEnabled = pT->CanCopy();
			return true;

		case ID_FILE_MOVETO:
		case ID_FILE_MOVETO_POPUP:
			bEnabled = pT->CanMove();
			return true;
		}

		return false;
	}

private:

	bool IsScale(LPCTSTR szScale)
	{
		T *pT = static_cast<T*>(this);
		return 0 == _tcsicmp(pT->GetScaleText(), szScale);
	}
};

template<class T>
class CImageWindowImpl
{
private:
	
	bool _bPauseAnimation;	

	CPoint _pointOffset;
	CSize _sizeDrawnImage;

	CPoint _pointScroll;
	CSize _sizeClient;
	CSize _sizeAll;

	CRect _rectSelected;

	int _nFrame;
	int _nTimer;	
	
	ImageTool *_pToolCurrent;		

	enum { _delay = 5000 };

	IW::CCursorAutoHide _cursorAutoHide;

	Scale _scale;
	State &_state;

	ImageToolSelect<T> _toolSelect;
	ImageToolMoveSelection<T> _toolMoveSelection;
	ImageToolMoveImage<T> _toolMoveImage;

public:

	Coupling *_pCoupling;
	FrameImageNavigate<T> _frameNavigation;

	CImageWindowImpl(Coupling *pCoupling, State &state) : 
	      _pCoupling(pCoupling),
		  _state(state),
		  _bPauseAnimation(false),
		  _nFrame(0),
		  _nTimer(0),
		  _pToolCurrent(0),
		  _toolSelect(*static_cast<T*>(this)),
		  _toolMoveSelection(*static_cast<T*>(this)),
		  _toolMoveImage(*static_cast<T*>(this)),
		  _frameNavigation(static_cast<T*>(this)->_frameParent, static_cast<T*>(this), state)
	  {
		  _sizeDrawnImage.cx = 1; 
		  _sizeDrawnImage.cy = 1;
		  _pointOffset.x = 0;
		  _pointOffset.y = 0;	
	  }

	  CString GetImageFileName() const { return _state.Image.GetImageFileName(); };
	  bool CanAnimate() const { return _state.Image.GetImage().CanAnimate(); };
	  const IW::Image &GetRenderImage() const { return _state.Image.GetRenderImage(); };

	  CSize GetDrawnImageSize() const
	  {
		  return _sizeDrawnImage;
	  }  	  

	  bool CanNavigate() const
	  {
		  const T *pT = static_cast<const T*>(this);
		  return _state.Image.IsImageShown() &&
			  (_sizeDrawnImage.cx > pT->_sizeClient.cx || _sizeDrawnImage.cy > pT->_sizeClient.cy);
	  };

	  BEGIN_MSG_MAP(CImageWindowImpl<T>)

		  MESSAGE_HANDLER(WM_CREATE, OnCreate)
		  MESSAGE_HANDLER(WM_SIZE, OnSize)
		  MESSAGE_HANDLER(WM_PAINT, OnPaint)
		  MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		  MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		  MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
		  MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)

		  MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		  MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		  MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		  MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)	
		  MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDblClk)
		  MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
		  MESSAGE_HANDLER(WM_XBUTTONUP, OnXButtonUp)		

		  COMMAND_ID_HANDLER(ID_MODE_FITTOWINDOW, OnModeFitToWindow)
		  COMMAND_ID_HANDLER(ID_MODE_SCALEDOWNTOFIT, OnModeScaleDown)
		  COMMAND_ID_HANDLER(ID_MODE_SCALEUPTOFIT, OnModeScaleUp)
		  COMMAND_ID_HANDLER(ID_MODE_ACTUALSIZE, OnModeActualSize)

		  NOTIFY_CODE_HANDLER(TBN_DROPDOWN, OnToolbarDropDown)

	  END_MSG_MAP()

	  LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	  {
		  T *pT = static_cast<T*>(this);
		  pT->_frames.push_back(&_frameNavigation);
		  _state.Image.EditModeDelegates.Bind<T>(pT, &T::OnEditModeChanged);
		  _state.Folder.RefreshDelegates.Bind(this, &T::OnFolderRefresh);
		  bHandled = false;
		  return 0;
	  }

	  void OnEditModeChanged()
	  {
		  T *pT = static_cast<T*>(this);
		  pT->OnResetFrames();
	  }

	  void OnFolderRefresh()
	  {
		  if (!_state.Image.UpdateFileTime())
			  return;

		  // Our own saves refresh the file time, so anything that gets here was
		  // written by something else -- a tool, another app -- and the picture
		  // on screen is simply out of date. Only unsaved work is worth a
		  // question, because reloading is what would throw it away.
		  if (_state.Image.IsDirty())
		  {
			  CString str;
			  str.Format(_T("Image '%s' has changed on disk.\nDo you want to reload it and lose your changes?"),
			             IW::Path::FindFileName(_state.Image.GetImageFileName()));

			  T *pT = static_cast<T*>(this);

			  if (pT->MessageBox(str, _T("ImageWalker"), MB_YESNO | MB_ICONQUESTION) != IDYES)
				  return;
		  }

		  CWaitCursor wait;
		  _state.Image.Reload();
	  }
	  
	  LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	  {
		  // handled, no background painting needed
		  return 1;
	  }

	  LRESULT OnSetCursor(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	  {
		  T *pT = static_cast<T*>(this);

		  if (_cursorAutoHide.OnSetCursor(pT->m_hWnd, wParam, lParam))
			  return TRUE;

		  bHandled = false;
		  return 0;
	  }

	  LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	  {
		  T *pT = static_cast<T*>(this);
		  pT->DoSize(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		  bHandled = FALSE;
		  return 0;
	  }

	  LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	  {
		  T *pT = static_cast<T*>(this);
		  if (wParam != 0)
		  {
			  pT->OnPaint((HDC)wParam);		
		  }
		  else
		  {
			  CPaintDC dc(pT->m_hWnd);
			  pT->OnPaint((HDC)dc);
		  }

		  return 0;
	  }

	  void OnPaint(CDCHandle dc)
	  {
		  T *pT = static_cast<T*>(this);
	  }	

	  LRESULT OnKeyDown(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	  {
		  T *pT = static_cast<T*>(this);
		  int nChar = (int) wParam;
		  pT->OnKeyDown(nChar);
		  return 0;
	  }

	  LRESULT OnMouseWheel(UINT uMsg, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	  {
		  T* pT = static_cast<T*>(this);
		  ATLASSERT(::IsWindow(pT->m_hWnd));

		  int zDelta = ((short)HIWORD(wParam)) / 2;

		  if (App.ControlKeyDown)
		  {
			  pT->SetScale(GetScale() + (zDelta / 10));
		  }
		  else
		  {
			  if(_sizeDrawnImage.cy > _sizeDrawnImage.cx)
			  {
				  pT->ScrollTo(CPoint(_pointScroll.x, _pointScroll.y + zDelta));
			  }
			  else		
			  {
				  pT->ScrollTo(CPoint(_pointScroll.x + zDelta, _pointScroll.y));
			  }
		  }

		  return 0;
	  }

	  LRESULT OnMouseMove(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
	  {
		  T *pT = static_cast<T*>(this);		
		  CPoint point (GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

		  if (_pToolCurrent != 0)
		  {
			  _pToolCurrent->OnMouseMove(point);
		  }
		  else
		  {
			  pT->ShowMouseMoveFeedback(point);	
		  }

		  bHandled = false;
		  return 0;
	  }

	  LRESULT OnMouseLeave(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	  {
		  T* pT = static_cast<T*>(this);
		  pT->ShowMouseMoveFeedback(CPoint(-1,-1));
		  bHandled = FALSE;
		  return 0;
	  }	  

	  LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	  {
		  T *pT = static_cast<T*>(this);
		  CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

		  if (_pToolCurrent != 0)
		  {
			  _pToolCurrent->OnLButtonUp(point);
			  _pToolCurrent = 0;

			  pT->Invalidate();
			  ReleaseCapture();			

			  pT->ShowMouseMoveFeedback(point);
		  }
		  else
		  {
			  bHandled = false;
		  }

		  _state.Image.UpdateStatusText(); 

		  return 0;
	  }	

	  LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	  {
		  T *pT = static_cast<T*>(this);
		  CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		  pT->SetFocus();

		  bool bHandled = false;
		  pT->Accept(FrameVisitorMouseLeftButtonDown(point, bHandled));

		  if (!bHandled)
		  {
			  ImageTool *pTool = pT->GetToolFromLButtonDown(point);

			  if (pTool)
			  {
				  _pToolCurrent = pTool;
				  SetCursor(_pToolCurrent->GetCursor());
				  _pToolCurrent->OnLButtonDown(point);
				  pT->SetCapture();
			  }
		  }

		  return 0;
	  }


	  LRESULT OnLButtonDblClk(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
	  {
		  T *pT = static_cast<T*>(this);
		  pT->ToggleScale();
		  return 0;
	  }

	  LRESULT OnXButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	  {
		  T *pT = static_cast<T*>(this);
		  int fwButton = GET_XBUTTON_WPARAM (wParam); 

		  switch(fwButton)
		  {
		  case XBUTTON1:
			  pT->OnCommand(ID_IMAGE_PREVIOUS);
			  break;
		  case XBUTTON2:
			  pT->OnCommand(ID_IMAGE_NEXT);
			  break;	
		  }

		  return 0;
	  }



	  void DrawControls(CDCHandle &dc)
	  {
		  if (!_rectSelected.IsRectEmpty())
		  {
			  CRect rect = GetDeviceRect();
			  dc.DrawFocusRect(rect);
			  dc.DrawFocusRect(GetCentreThirdRect(rect));
		  }
	  }

	  void OnTimer()
	  {
		  T *pT = static_cast<T*>(this);

		  // Is the image animated?
		  if (!IsAnimationPaused() && pT->m_hWnd &&  CanAnimate())
		  {
			  _nTimer -= 100;

			  if (_nTimer <= 0)
			  {
				  // Work out page
				  int nPage = (_nFrame + 1) % pT->GetRenderImage().GetPageCount();
				  IW::Page page = pT->GetRenderImage().GetPage(nPage); 

				  _nTimer = page.GetTimeDelay();
				  _nFrame = nPage;

				  InvalidateDib();
			  }
		  }

		  if (_cursorAutoHide.Update(pT->m_hWnd,
			  pT->CanHideCursor() && !App.Settings._bDontHideCursor, _delay))
		  {
			  pT->OnShowCursor();
		  }

		  pT->Accept(FrameVisitorTimer()); 
	  }

	  CRect GetImageRect() const
	  {
		  return CRect(_pointOffset, _sizeDrawnImage);
	  }

	  void InvalidateDib()
	  {
		  T *pT = static_cast<T*>(this);
		  pT->InvalidateRect(GetImageRect());
	  }

	  void DoAnimation()
	  {
		  T *pT = static_cast<T*>(this);

		  if (_state.Image.IsImageShown())
		  {
			  int nPageCount = pT->GetRenderImage().GetPageCount();

			  if (nPageCount > 1)
			  {
				  // C++ leaves a negative dividend negative, and GetPage does not
				  // bounds check -- PreviousFrame on frame 0 would index Pages[-1].
				  _nFrame %= nPageCount;
				  if (_nFrame < 0) _nFrame += nPageCount;

				  InvalidateDib();
			  }
		  }
	  }

	  void DrawImage(IW::CRender &render, const CRect &rectClip)
	  {
		  T *pT = static_cast<T*>(this);		  

		  if (_state.Image.IsImageShown())
		  {
			  const IW::Image &image = pT->GetRenderImage();
			  IW::Page page = image.GetFirstPage();

			  if (image.CanAnimate())
			  {
				  page = image.GetPage(_nFrame);
			  }

			  pT->DrawImage(render, page, rectClip);
		  }
		  else
		  {
			  //DrawLogo(render);
		  }		  

		  pT->DrawTools(render);
	  }	 

	  void DrawTools(IW::CRender &render)
	  {
	  }


	  CRect GetDrawnImageRect() const
	  {
		  const T *pT = static_cast<const T*>(this);
		  const IW::Image &image = pT->GetRenderImage();
		  return GetDrawnImageRect(image.GetBoundingRect(), image.GetFirstPage().GetPageRect());
	  }

	  CRect GetDrawnImageRect(const CRect &rcBounding, const CRect &rcPage) const
	  {
		  const T *pT = static_cast<const T*>(this);

		  const CPoint pt(
			  _pointOffset.x + MulDiv(rcPage.left - rcBounding.left, _sizeDrawnImage.cx, rcBounding.Width()),
			  _pointOffset.y + MulDiv(rcPage.top - rcBounding.top, _sizeDrawnImage.cy, rcBounding.Height()));

		  const CSize size(
			  MulDiv(rcPage.Width(), _sizeDrawnImage.cx, rcBounding.Width()),
			  MulDiv(rcPage.Height(), _sizeDrawnImage.cy, rcBounding.Height()));

		  return CRect(pt - CSize(pT->GetScrollOffset()), size);
	  }


	  void DrawImage(IW::CRender &render, IW::Page &page, const CRect &rectClip) const
	  {
		  const T *pT = static_cast<const T*>(this);

		  const CRect rcPage = page.GetPageRect();
		  const CRect rcBounding = pT->GetRenderImage().GetBoundingRect();
		  const CRect rcDraw = GetDrawnImageRect(rcBounding, rcPage);

		  render.DrawImage(page, rcDraw);			
	  }

	  void NextFrame()
	  {
		  if (CanAnimate())
		  {
			  _bPauseAnimation = true;
			  _nFrame++;
			  DoAnimation();
		  }
	  }

	  void PreviousFrame()
	  {
		  if (CanAnimate())
		  {
			  _bPauseAnimation = true;
			  _nFrame -= 1;
			  DoAnimation();
		  }
	  }

	  bool IsAnimationPaused() const
	  {
		  return _bPauseAnimation;
	  }	

	  void SetAnimationPaused(bool bPause)
	  {
		  _bPauseAnimation = bPause;
	  }

	  void ResetAnimation()
	  {
		  _nTimer = 0;
		  _nFrame = 0;
	  }

	  void OnShowCursor()
	  {
	  }	

	  // Overridden per view: the pointer only auto-hides where the chrome is gone.
	  bool CanHideCursor() const
	  {
		  return false;
	  }


	  void SetScale(ScaleMode::Type type)
	  {
		  T *pT = static_cast<T*>(this);

		  _scale.SetScale(type);
		  SetScrollSizes(true);	
		  pT->OnScaleChange();
	  }

	  void SetScale(LPCTSTR szScale)
	  {
		  T *pT = static_cast<T*>(this);

		  if (_scale.Parse(szScale))
		  {
			  SetScrollSizes(true);
			  pT->OnScaleChange();
		  }
	  }

	  void SetScale(int s)
	  {
		  T *pT = static_cast<T*>(this);

		  _scale.SetScale(s);
		  SetScrollSizes(true);
		  pT->OnScaleChange();
	  }	  

	  ScaleMode::Type GetScaleType() const 
	  {
		  return _scale.GetScaleType();
	  }

	  int GetScale() const
	  {
		  const T *pT = static_cast<const T*>(this);
		  CSize sizeImage(0,0);

		  if (_state.Image.IsImageShown())
		  {
			  sizeImage = _state.Image.GetDisplaySize();
		  }

		  return _scale.CalcScalePercent(sizeImage, pT->GetImageClientRect().Size(), _sizeClient);
	  }

	  void SetScrollSizes(bool bScrollToCenter)
	  {	
		  T *pT = static_cast<T*>(this);

		  CRect rectClient; pT->GetClientRect(rectClient);
		  CRect rectImageDisplay = pT->GetImageDisplayRect();

		  CSize sizeDrawnImage = _scale.CalcSize(_state.Image.GetDisplaySize(), pT->GetImageClientRect().Size(), rectClient.Size());

		  _sizeAll = rectImageDisplay.TopLeft() + sizeDrawnImage;
		  _sizeDrawnImage = sizeDrawnImage;
		  _pointOffset = rectImageDisplay.TopLeft();

		  int x = ((rectImageDisplay.left + rectImageDisplay.right) - sizeDrawnImage.cx) / 2;
		  int y = ((rectImageDisplay.top + rectImageDisplay.bottom) - sizeDrawnImage.cy) / 2;

		  CPoint pointScrollOffset(0,0);

		  if (x < rectImageDisplay.left)
		  {
			  _pointOffset.x = rectImageDisplay.left;
			  pointScrollOffset.x = rectImageDisplay.left - x;
		  }
		  else
		  {
			  _pointOffset.x = x;
		  }

		  if (y < rectImageDisplay.top)
		  {
			  _pointOffset.y = rectImageDisplay.top;
			  pointScrollOffset.y = rectImageDisplay.top - y;
		  }
		  else
		  {
			  _pointOffset.y = y;
		  }

		  ScrollTo(bScrollToCenter ? pointScrollOffset : _pointScroll);
	  }

	  void DoSize(int cx, int cy)
	  {
		  T *pT = static_cast<T*>(this);

		  _sizeClient.cx = cx;
		  _sizeClient.cy = cy;	

		  SetScrollSizes(_scale.IsResizing());
		  pT->OnSizeChanged();
	  }

	  void DoSize()
	  {
		  SetScrollSizes(true);
	  }

	  void OnSizeChanged()
	  {
		  T *pT = static_cast<T*>(this);

		  if (_scale.IsResizing())
		  {
			  SetScrollSizes(false);
			  pT->OnScaleChange();
		  }
	  }

	  inline int ClampRange(int v, int l, int h)	
	  {
		  if (l > h)
		  {
			  h = l;
		  }

		  return v < l ? l : (v > h ? h : v);
	  }

	  void ScrollTo(const CPoint &point)
	  {
		  T *pT = static_cast<T*>(this);

		  CSize sizeMax = _sizeAll - _sizeClient;

		  _pointScroll.x = ClampRange(point.x, 0, sizeMax.cx);
		  _pointScroll.y = ClampRange(point.y, 0, sizeMax.cy);

		  pT->Invalidate();
	  }

	  bool CanImageBeScrolled() const
	  {
		  if (_state.Image.IsImageShown())
		  {
			  CSize sizeMax = _sizeAll - _sizeClient;
			  return sizeMax.cx > 0 || sizeMax.cy > 0;
		  }

		  return false;
	  }




	  LRESULT OnModeScaleUp(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	  {
		  T *pT = static_cast<T*>(this);
		  pT->SetScale(ScaleMode::Up);
		  return 0;
	  }

	  LRESULT OnModeScaleDown(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	  {
		  T *pT = static_cast<T*>(this);
		  pT->SetScale(ScaleMode::Down);
		  return 0;
	  }

	  LRESULT OnModeFitToWindow(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	  {
		  T *pT = static_cast<T*>(this);
		  pT->SetScale(GetScaleType() == ScaleMode::Fit ? ScaleMode::Normal : ScaleMode::Fit);
		  return 0;
	  }

	  LRESULT OnModeActualSize(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	  {
		  T *pT = static_cast<T*>(this);
		  pT->SetScale(100);
		  return 0;
	  }

	  LRESULT OnToolbarDropDown(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/)
	  {
		  NMTOOLBAR* ptb = (NMTOOLBAR *) pnmh;
		  CRect rc;

		  CToolBarCtrl tbar(pnmh->hwndFrom);
		  tbar.GetItemRect(tbar.CommandToIndex(ptb->iItem), rc);
		  tbar.MapWindowPoints(HWND_DESKTOP, rc);

		  if (ptb->iItem == ID_NAVIGATE) // Navigation
		  {
			  DoNavigate(rc);
		  }

		  return 0;
	  }

	  void DoNavigate(const CRect &rect)
	  {
		  T *pT = static_cast<T*>(this);

		  if (_state.Image.IsImageShown())
		  {
			  CImageNavigation<T>::Track(*pT, pT->m_hWnd, rect);
		  }
	  }

	  void SetDeviceRect(const CRect &rectIn)
	  {
		  // Add both offsets together
		  CRect rectImage = GetImageRect();
		  rectImage.OffsetRect(-_pointScroll.x, -_pointScroll.y);

		  IW::Page page = GetRenderImage().GetFirstPage(); 

		  // Calculate the new rect
		  _rectSelected.left = MulDiv(rectIn.left - rectImage.left, page.GetWidth(), rectImage.Width());
		  _rectSelected.top = MulDiv(rectIn.top - rectImage.top, page.GetHeight(), rectImage.Height());
		  _rectSelected.right = MulDiv(rectIn.right - rectImage.left, page.GetWidth(), rectImage.Width());
		  _rectSelected.bottom = MulDiv(rectIn.bottom - rectImage.top, page.GetHeight(), rectImage.Height());
	  }

	  CRect GetDeviceRect() const
	  {
		  if (_rectSelected.IsRectEmpty())
		  {
			  const CRect rectEmpty(0,0,0,0);
			  return rectEmpty;
		  }
		  else
		  {
			  const CSize sizeDrawnImage = GetDrawnImageSize();
			  const CPoint pointDrawnImageOffset = _pointOffset;

			  // Add both offsets together
			  CPoint pointOffset;
			  pointOffset.x = pointDrawnImageOffset.x - _pointScroll.x;
			  pointOffset.y = pointDrawnImageOffset.y - _pointScroll.y;

			  IW::Page page = GetRenderImage().GetFirstPage(); 

			  CRect r;

			  r.left = pointOffset.x + MulDiv(_rectSelected.left,
				  sizeDrawnImage.cx, page.GetWidth());

			  r.top = pointOffset.y + MulDiv(_rectSelected.top,
				  sizeDrawnImage.cy, page.GetHeight());

			  r.right = pointOffset.x + MulDiv(_rectSelected.right, 
				  sizeDrawnImage.cx, page.GetWidth());

			  r.bottom = pointOffset.y + MulDiv(_rectSelected.bottom, 
				  sizeDrawnImage.cy, page.GetHeight());

			  return r;
		  }

	  }


	  void ToggleScale(bool canDown = false)
	  {
		  T *pT = static_cast<T*>(this);

		  _scale.Toggle(canDown);
		  SetScrollSizes(true);
		  pT->OnScaleChange();
	  }

	  CPoint GetScrollOffset() const
	  {
		  return _pointScroll;
	  }

	  CSize GetClientSize() const
	  {
		  return _sizeClient;
	  }

	  CSize GetCanvasSize() const
	  {
		  return _sizeAll;
	  }

	  void Refresh(bool bScrollToCenter)
	  {
		  ResetAnimation();
		  _rectSelected.SetRectEmpty();
		  SetScrollSizes(bScrollToCenter);
	  }

	  CString GetScaleText()
	  {
		  return _scale.GetScaleText();
	  }

	  CRect GetImageRectSelected() const
	  {
		  CRect r(0, 0, 0, 0);

		  if (!_rectSelected.IsRectEmpty())
		  {
			  return _rectSelected;
		  }
		  else if (_state.Image.IsImageShown())
		  {
			  r = GetRenderImage().GetBoundingRect();
		  }

		  return r;
	  }

	  bool HasSelection() const
	  {
		  return _state.Image.IsImageShown() && !_rectSelected.IsRectEmpty();
	  };

	  BOOL PreTranslateMessage(MSG* pMsg)
	  {	
		  return FALSE;
	  }

	  void ShowMouseMoveFeedback()
	  {
		  T *pT = static_cast<T*>(this);
		  CPoint point;
		  ::GetCursorPos(&point);

		  pT->ScreenToClient(&point);
		  pT->ShowMouseMoveFeedback(point);
	  }

	  void ShowMouseMoveFeedback(const CPoint &point)
	  {
		  T *pT = static_cast<T*>(this);

		  if (pT->PointOnFrame(point))
			  return;
		  
		  CRect r = pT->GetDeviceRect();

		  if (r.PtInRect(point))
		  {
			  SetCursor(IW::Style::Cursor::Move);
		  }
		  else if (IsInDragMode())
		  {
			  SetCursor(IW::Style::Cursor::HandUp);
		  }
		  else
		  {					
			  SetCursor(IW::Style::Cursor::Normal);
		  }
	  }

	  ImageTool *GetToolFromLButtonDown(const CPoint &point)
	  {
		  T *pT = static_cast<T*>(this);

		  if (_state.Image.IsImageShown())
		  {
			  CRect r = pT->GetDeviceRect();

			  if (r.PtInRect(point))
			  {					
				  return &_toolMoveSelection;				  
			  }
			  else if (IsInDragMode())
			  {
				  return &_toolMoveImage;				  
			  }
			  else
			  {
				  return &_toolSelect;
			  }			  
		  }

		  return 0;
	  }

	  bool IsInDragMode() const
	  {
		  const T *pT = static_cast<const T*>(this);
		  return !App.ControlKeyDown && _state.Image.IsImageShown() && pT->CanImageBeScrolled();
	  }

	  void ShowFrames()
	  {
		  _frameNavigation.SetVisible(CanShowNavigation());
	  }

	  CRect GetImageDisplayRect() const
	  {
		  const T *pT = static_cast<const T*>(this);
		  CRect rectClient;		
		  pT->GetClientRect(rectClient);
		  return rectClient;
	  }

	  CRect GetImageClientRect() const
	  {
		  const T *pT = static_cast<const T*>(this);
		  CRect rectClient;		
		  pT->GetClientRect(rectClient);
		  return rectClient;
	  }

	  bool CanShowNavigation() const 
	  {
		  return _state.Image.IsImageShown() && 
			  CanNavigate();
	  }
};

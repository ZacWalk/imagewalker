// ImageWalker by Zac Walker
//
// Purpose: Drop handling for the items pane - what a drop over an item or
//          the background means.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once


template<typename TWindow>
class CFolderWindowDropAdapter :
	public CComCoClass<CFolderWindowDropAdapter<TWindow> >,
	public CComObjectRootEx<CComSingleThreadModel>,
	public IDropTarget,
	public IDropSource
{
public:

	TWindow *_pWindow;

	bool m_bDragStarted;    // has drag really started yet?		
	bool m_bInDragOver;
	DWORD m_dwButtonCancel; // which button will cancel (going down)
	DWORD m_dwButtonDrop;   // which button will confirm (going up)	

	CComPtr<IDataObject> m_spDataObject;
	CComPtr<IDropTarget> m_spShellDropTarget;

	CFolderWindowDropAdapter(TWindow *pWindow = 0) : 
		_pWindow(pWindow),
		m_bDragStarted(false), 
		m_dwButtonCancel(0),
		m_dwButtonDrop(0),
		m_bInDragOver(false)
	{
	}

	BEGIN_COM_MAP(CFolderWindowDropAdapter<TWindow>)
		COM_INTERFACE_ENTRY(IDropSource)
		COM_INTERFACE_ENTRY(IDropTarget)
	END_COM_MAP()	

	void SetDragOverItem(int nNewDragOverItem)
	{
		int nCurrentDragOverItem = _pWindow->_nDragOverItem;

		if (nNewDragOverItem != nCurrentDragOverItem)
		{
			IW::FolderPtr pFolder = GetFolder();

			if (nCurrentDragOverItem >= 0) _pWindow->InvalidateThumb(pFolder, nCurrentDragOverItem);
			_pWindow->_nDragOverItem = nNewDragOverItem;
			if (nNewDragOverItem >= 0) _pWindow->InvalidateThumb(pFolder, nNewDragOverItem);
		}
	}

	int GetDragOverItem() const
	{
		return _pWindow->_nDragOverItem;
	}

	IW::FolderPtr GetFolder()
	{
		return _pWindow->_state.Folder.GetFolder();
	}

	bool IsDraging() const
	{
		return _pWindow->_bDraging;
	}

	// metrics for drag start determination
	static const UINT nDragDelay = 500;    // delay before drag starts

	BOOL RegisterDropTarget()
	{
		LPUNKNOWN lpUnknown = GetUnknown();
		ATLASSERT(lpUnknown != NULL);

		// Has create been called window?
		ATLASSERT(_pWindow->IsWindow());


		// the object must be locked externally to keep LRPC connections alive
		HRESULT hr = CoLockObjectExternal(lpUnknown, TRUE, FALSE);

		if (hr != S_OK)
			return FALSE;

		// connect the HWND to the IDropTarget implementation
		hr = RegisterDragDrop(_pWindow->GetHWnd(), (LPDROPTARGET)this);

		if (hr != S_OK)
		{
			CoLockObjectExternal(lpUnknown, FALSE, FALSE);
			return FALSE;
		}


		return TRUE;
	}

	void RevokeDropTarget()
	{
		// disconnect from OLE
		RevokeDragDrop(_pWindow->GetHWnd());
		CoLockObjectExternal((LPUNKNOWN)GetUnknown(), FALSE, TRUE);
	}

	BOOL OnBeginDrag()
	{

		m_bDragStarted = false;

		// opposite button cancels drag operation
		m_dwButtonCancel = 0;
		m_dwButtonDrop = 0;
		if (GetKeyState(VK_LBUTTON) < 0)
		{
			m_dwButtonDrop |= MK_LBUTTON;
			m_dwButtonCancel |= MK_RBUTTON;
		}
		else if (GetKeyState(VK_RBUTTON) < 0)
		{
			m_dwButtonDrop |= MK_RBUTTON;
			m_dwButtonCancel |= MK_LBUTTON;
		}

		DWORD dwLastTick = GetTickCount();
		::SetCapture(_pWindow->GetHWnd());

		while (!m_bDragStarted)
		{
			// some applications steal capture away at random times
			if (::GetCapture() != _pWindow->GetHWnd())
				break;

			// peek for next input message
			MSG msg;
			if (PeekMessage(&msg, NULL, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE) ||
				PeekMessage(&msg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE))
			{
				// check for button cancellation (any button down will cancel)
				if (msg.message == WM_LBUTTONUP || msg.message == WM_RBUTTONUP ||
					msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN)
					break;

				// check for keyboard cancellation
				if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
					break;

				// check for drag start transition
				m_bDragStarted = !PtInRect(&(_pWindow->_rectStartDrag), msg.pt);
			}

			// if the user sits here long enough, we eventually start the drag
			if (GetTickCount() - dwLastTick > nDragDelay)
				m_bDragStarted = true;
		}
		ReleaseCapture();

		return m_bDragStarted;
	}

	STDMETHODIMP Drop(IDataObject  *pDataObj, DWORD grfKeyState, POINTL pt, DWORD  *pdwEffect) 
	{
		if (m_spShellDropTarget != NULL)
		{
			HRESULT hr = m_spShellDropTarget->Drop(pDataObj,  grfKeyState,  pt, pdwEffect);

			// Drop ends the drag session. DragLeave is the *other* ending, not a
			// follow-up, so only our own state is taken down here.
			m_bInDragOver = false;
			m_spShellDropTarget.Release();
			m_spDataObject.Release();

			if (GetDragOverItem() != -1)
			{
				SetDragOverItem(-1);
			}

			return hr;
		}
		else if (GetDragOverItem() != -1 && IsDraging())
		{
			// Drag selection to new position
			IW::FolderPtr pFolder = _pWindow->_state.Folder.GetFolder();
			pFolder->DragSelection(GetDragOverItem());
			
			DragLeave();
			_pWindow->Invalidate();			
		}

		return S_OK;
	}

	STDMETHODIMP DragEnter(IDataObject  *pDataObj, DWORD grfKeyState, POINTL pt, DWORD  *pdwEffect) 
	{
		SetDragOverItem(-2);
		m_spDataObject = pDataObj;

		m_bInDragOver = true;

		return DragOver(grfKeyState, pt, pdwEffect);
	}	

	STDMETHODIMP DragLeave() 
	{
		m_bInDragOver = false;

		if (m_spShellDropTarget != NULL)
		{
			m_spShellDropTarget->DragLeave();
			m_spShellDropTarget.Release();
		}

		if (m_spDataObject != NULL)
		{
			m_spDataObject.Release();
		}	

		if (GetDragOverItem() != -1)
		{
			SetDragOverItem(-1);
		}

		return S_OK;
	}

	STDMETHODIMP DragOver(DWORD grfKeyState, POINTL pt, DWORD  *pdwEffect) 
	{	
		IW::FolderPtr pFolder = GetFolder();

		HRESULT hr = E_FAIL;	
		CPoint pointScreen(pt.x, pt.y);
		_pWindow->ScreenToClient(&pointScreen);

		const CPoint pointWithOffset = pointScreen + CSize(_pWindow->GetScrollOffset());

		int i = _pWindow->GetLayout()->ThumbFromPoint(pointWithOffset);

		if (i != -1)
		{
			if (!_pWindow->GetImageRect(i).PtInRect(pointWithOffset))
				i = -1;
		}

		if (i != GetDragOverItem())
		{
			if (i != -1)
			{
				if (pFolder->IsItemDropTarget(i))
				{
					i = -1;
				}				
			}

			if (GetDragOverItem() != i)
			{
				SetDragOverItem(i);

				if (m_spShellDropTarget != NULL)
				{
					m_spShellDropTarget->DragLeave();
					m_spShellDropTarget.Release();
				}

				if (GetDragOverItem() != -1)
				{				
					hr = pFolder->GetUIObjectOf(GetDragOverItem(), IID_IDropTarget, (LPVOID *)&m_spShellDropTarget);
				}
				else
				{
					hr = pFolder->CreateViewObject(IW::GetMainWindow(), IID_IDropTarget, (LPVOID *)&m_spShellDropTarget);
				}


				if (SUCCEEDED(hr))
				{
					if ((IsDraging() || _pWindow->_state.Folder.IsSearchMode) &&
						(GetDragOverItem() == -1 || pFolder->IsItemSelected(GetDragOverItem())))
					{
						*pdwEffect = DROPEFFECT_NONE;
					}

					hr = m_spShellDropTarget->DragEnter(m_spDataObject, grfKeyState, pt, pdwEffect);

					if (SUCCEEDED(hr))
					{
						return hr;
					}

					// A target that refused DragEnter is not in a drag session,
					// so it must not be sent DragOver or Drop either. The item goes
					// with it, or Drop falls through to the internal reorder.
					m_spShellDropTarget.Release();
					SetDragOverItem(-1);
				}
			}
		}

		if (m_spShellDropTarget != NULL)
		{
			if ((IsDraging() || _pWindow->_state.Folder.IsSearchMode) && 
				(GetDragOverItem() == -1 ||
				pFolder->IsItemSelected(GetDragOverItem())))
			{
				*pdwEffect = DROPEFFECT_NONE;
			}

			return m_spShellDropTarget->DragOver(grfKeyState, pt, pdwEffect);
		}
		else
		{
			*pdwEffect = IsDraging() ? DROPEFFECT_MOVE : DROPEFFECT_NONE;
		}

		return S_OK;
	}

	// Drop source stuff
	STDMETHODIMP QueryContinueDrag(BOOL bEscapePressed, DWORD dwKeyState)
	{

		// check escape key or right button -- and cancel
		if (bEscapePressed || (dwKeyState & m_dwButtonCancel) != 0)
		{
			m_bDragStarted = false; // avoid unnecessary cursor setting
			return DRAGDROP_S_CANCEL;
		}

		// check left-button up to end drag/drop and do the drop
		if ((dwKeyState & m_dwButtonDrop) == 0)
			return m_bDragStarted ? DRAGDROP_S_DROP : DRAGDROP_S_CANCEL;

		// otherwise, keep polling...
		return S_OK;
	}

	STDMETHODIMP GiveFeedback(DROPEFFECT /*dropEffect*/)
	{

		//CPoint point;
		//GetCursorPos(&point);
		//if (_rectStartDrag.PtInRect(point))
		//return S_OK;

		if (m_bDragStarted && m_spShellDropTarget == NULL && GetDragOverItem() != -1 && IsDraging())
		{
			SetCursor(IW::Style::Cursor::Insert);
			return S_OK;
		}

		// don't change the cursor until drag is officially started
		return m_bDragStarted ? DRAGDROP_S_USEDEFAULTCURSORS : S_OK;
	}

	void OnTimer()
	{
		// Do drag scroll if we
		// Are in a drag mode
		if (m_bInDragOver)
		{
			CPoint			pt;
			CRect rc;
			CSize sizeThumb =  App.Settings._sizeThumbImage;

			::GetCursorPos((LPPOINT)&pt);
			_pWindow->ScreenToClient(&pt);
			_pWindow->GetClientRect(&rc);

			int cy = sizeThumb.cy / 2;

			if (pt.y < (rc.top + cy))
			{
				// Scroll Up
				_pWindow->ScrollLineUp();
			}
			else if (pt.y > (rc.bottom - cy))
			{
				_pWindow->ScrollLineDown();
			}
		}
	}
};

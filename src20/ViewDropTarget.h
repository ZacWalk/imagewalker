// ImageWalker by Zac Walker
//
// Purpose: The IDropTarget implementation the panes register.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once


template<class T>
class CDropTargetImpl : public IDropTarget
{
	IW::RefPtr<IDataObject>  m_spDataObject;

public:


	BOOL RegisterDropTarget()
	{
		T* pT = static_cast<T*>(this);

		// Has create been called window?
		ATLASSERT(pT->IsWindow());

		// connect the HWND to the IDropTarget implementation
		return SUCCEEDED(RegisterDragDrop(pT->m_hWnd, this));
	}

	void RevokeDropTarget()
	{
		T* pT = static_cast<T*>(this);

		// disconnect from OLE
		RevokeDragDrop(pT->m_hWnd);
	}

protected:

	STDMETHODIMP Drop(IDataObject  *pDataObj, DWORD grfKeyState, POINTL pt, DWORD  *pdwEffect) 
	{
		static FORMATETC fmteDib = {(CLIPFORMAT) CF_DIB, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
		static FORMATETC fmteFile = {(CLIPFORMAT) CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};

		T* pT = static_cast<T*>(this);

		STGMEDIUM stgMedium;
		HRESULT hr = pDataObj->GetData(&fmteDib, &stgMedium);
		DWORD  dwEffect = DROPEFFECT_NONE;

		if (SUCCEEDED(hr) && stgMedium.hGlobal)
		{
			pT->LoadImageFromHGlobal(stgMedium.hGlobal);			
			dwEffect = DROPEFFECT_COPY;

			::ReleaseStgMedium(&stgMedium);
		}
		/*else
		{
		// DragQueryFile

		HRESULT hr = pDataObj->GetData(&fmteDib, &stgMedium);
		if (SUCCEEDED(hr))
		{
		//HGLOBAL gmem = stgMedium.hGlobal;
		//TCHAR* str = (TCHAR*)GlobalLock(gmem);
		// use str
		//GlobalUnlock(gmem);

		if (hglb)
		{
		IW::Image dib;
		dib.Copy(stgMedium.hGlobal);

		_pFolderWindow->SaveNewImage(dib);
		}

		::ReleaseStgMedium(&stgMedium);
		}
		}*/


		*pdwEffect = dwEffect;

		return hr;
	}


	STDMETHODIMP DragEnter(IDataObject  *pDataObj, DWORD grfKeyState, POINTL pt, DWORD  *pdwEffect) 
	{
		static FORMATETC fmteDib = {(CLIPFORMAT) CF_DIB, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
		static FORMATETC fmteFile = {(CLIPFORMAT) CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};

		if (S_OK == pDataObj->QueryGetData(&fmteDib))// ||
			//S_OK == pDataObj->QueryGetData(&fmteFile))
		{
			*pdwEffect = DROPEFFECT_COPY;
			m_spDataObject = pDataObj;
		}
		else 
		{
			*pdwEffect = DROPEFFECT_NONE;
		}


		return S_OK;

	}

	STDMETHODIMP DragLeave() 
	{
		if (m_spDataObject != NULL)
		{
			m_spDataObject.Release();
		}	

		return S_OK;
	}

	STDMETHODIMP DragOver( DWORD grfKeyState, POINTL pt, DWORD  *pdwEffect) 
	{
		*pdwEffect = (m_spDataObject != 0) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
		return S_OK;
	}

	// Registered with RegisterDragDrop, so OLE and every drag source that
	// touches the pane call these. The object outlives every drag -- it is a
	// member of the window -- so the count is nominal, but it still has to be a
	// count, and COM identity still has to answer IUnknown.
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject)
	{
		if (ppvObject == nullptr)
			return E_POINTER;

		*ppvObject = nullptr;

		if (IsEqualIID(riid, IID_IDropTarget) || IsEqualIID(riid, IID_IUnknown))
		{
			*ppvObject = static_cast<IDropTarget*>(this);
			AddRef();
			return S_OK;
		}

		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef( void)
	{
		return static_cast<ULONG>(::InterlockedIncrement(&_cRef));
	}

	ULONG STDMETHODCALLTYPE Release( void)
	{
		const LONG n = ::InterlockedDecrement(&_cRef);
		return static_cast<ULONG>(n < 0 ? 0 : n);
	}

private:

	LONG _cRef = 1;
};

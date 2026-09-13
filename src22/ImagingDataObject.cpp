// ImageWalker by Zac Walker
//
// Purpose: Image data object implementation - DIB, file contents and HDROP
//          formats.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"
#include "ViewModelState.h"
#include "ImagingDataObject.h"
#include "FileFormatAny.h"

//////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
//
// CEnumFORMATETCImpl::
//

using FORMATETCLIST = std::vector<FORMATETC>;
static UINT cfPDE = RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT);


class CEnumFORMATETCImpl :
	public CComObjectRootEx<CComSingleThreadModel>,
	public CComCoClass<CEnumFORMATETCImpl>,
	public IEnumFORMATETC
{
	FORMATETCLIST m_pFmtEtc;
	DWORD m_iCur;

public:
	CEnumFORMATETCImpl();
	CEnumFORMATETCImpl(FORMATETCLIST& ArrFE);
	CEnumFORMATETCImpl(const FORMATETCLIST& ArrFE);

	BEGIN_COM_MAP(CEnumFORMATETCImpl)
		COM_INTERFACE_ENTRY(IEnumFORMATETC)
	END_COM_MAP()

	void Add(const FORMATETC&);
	void Copy(const FORMATETCLIST& ArrFE);
	void Copy(const CEnumFORMATETCImpl& ef);

	//IEnumFORMATETC members
	STDMETHOD(Next)(ULONG, LPFORMATETC, ULONG FAR *);
	STDMETHOD(Skip)(ULONG);
	STDMETHOD(Reset)(void);
	STDMETHOD(Clone)(IEnumFORMATETC FAR * FAR*);
};


////////////////////
//////   CEnumFORMATETCImpl
///////////////////////////////

CEnumFORMATETCImpl::CEnumFORMATETCImpl() : m_iCur(0)
{
}


CEnumFORMATETCImpl::CEnumFORMATETCImpl(const FORMATETCLIST& ArrFE) : m_iCur(0)
{
	for (DWORD i = 0; i < ArrFE.size(); ++i)
		m_pFmtEtc.push_back(ArrFE[i]);
}

void CEnumFORMATETCImpl::Copy(const CEnumFORMATETCImpl& ef)
{
	m_iCur = ef.m_iCur;

	for (DWORD i = 0; i < ef.m_pFmtEtc.size(); ++i)
		m_pFmtEtc.push_back(ef.m_pFmtEtc[i]);
}

void CEnumFORMATETCImpl::Copy(const FORMATETCLIST& ArrFE)
{
	m_iCur = 0;

	for (DWORD i = 0; i < ArrFE.size(); ++i)
		m_pFmtEtc.push_back(ArrFE[i]);
}


void CEnumFORMATETCImpl::Add(const FORMATETC& fmtetc)
{
	m_pFmtEtc.push_back(fmtetc);
}


////////////////////
//////   CEnumFORMATETCImpl
///////////////////////////////

STDMETHODIMP CEnumFORMATETCImpl::Next(ULONG celt, LPFORMATETC lpFormatEtc, ULONG FAR * pceltFetched)
{
	if (pceltFetched != nullptr)
		*pceltFetched = 0;

	ULONG cReturn = celt;

	if (celt <= 0 || lpFormatEtc == nullptr || m_iCur >= m_pFmtEtc.size())
		return S_FALSE;

	if (pceltFetched == nullptr && celt != 1) // pceltFetched can be NULL only for 1 item request
		return S_FALSE;

	while (m_iCur < m_pFmtEtc.size() && cReturn > 0)
	{
		*lpFormatEtc++ = m_pFmtEtc[m_iCur++];
		--cReturn;
	}
	if (pceltFetched != nullptr)
		*pceltFetched = celt - cReturn;

	return (cReturn == 0) ? S_OK : S_FALSE;
}


////////////////////
//////   CEnumFORMATETCImpl
    ///////////////////////////////

STDMETHODIMP CEnumFORMATETCImpl::Skip(ULONG celt)
{
	if ((m_iCur + static_cast<int>(celt)) >= m_pFmtEtc.size())
		return S_FALSE;

	m_iCur += celt;
	return S_OK;
}

////////////////////
//////   CEnumFORMATETCImpl
///////////////////////////////

STDMETHODIMP CEnumFORMATETCImpl::Reset(void)
{
	m_iCur = 0;
	return S_OK;
}

////////////////////
//////   CEnumFORMATETCImpl
///////////////////////////////

STDMETHODIMP CEnumFORMATETCImpl::Clone(IEnumFORMATETC FAR * FAR* ppCloneEnumFormatEtc)
{
	if (ppCloneEnumFormatEtc == nullptr)
		return E_POINTER;

	CComPtr<CEnumFORMATETCImpl> p = IW::CreateComObject<CEnumFORMATETCImpl>();
	p->Copy(*this);

	return p->QueryInterface(IID_IEnumFORMATETC, (LPVOID*)ppCloneEnumFormatEtc);
}

//////////////////////////////////////////////////////////////////////
// CImageDataObject
//////////////////////////////////////////////////////////////////////

CImageDataObject::CImageDataObject()
{
	m_bCachedItemImage = false;
	m_bMove = false;
	m_bHasImage = false;
	m_bSetForMove = false;
}

CImageDataObject::~CImageDataObject()
{
}

void CImageDataObject::AggregateDataObject(IDataObject* pDataObject)
{
	m_spShellDataObject = pDataObject;
}

void CImageDataObject::Cache(const IW::CShellItem& item)
{
	_itemCachedItem = item;
	m_bCachedItemImage = true;
}

void CImageDataObject::Cache(const IW::Image& image)
{
	_image.Copy(image);
	m_bHasImage = true;
}

void CImageDataObject::SetForMove(bool bMove)
{
	m_bMove = bMove;
	m_bSetForMove = true;
}

// Methods of the IDataObject Interface
//
STDMETHODIMP CImageDataObject::GetData(FORMATETC* pformatetcIn, STGMEDIUM* pmedium)
{
	if (pformatetcIn->cfFormat == CF_DIB ||
		pformatetcIn->cfFormat == CF_METAFILEPICT)
	{
		if (m_bCachedItemImage && !m_bHasImage)
		{
			// A render request serviced after the frame has gone has nothing to
			// render from.
			if (g_pState == nullptr)
				return E_FAIL;

			CLoadAny loader(g_pState->Loaders);
			CString strPath;

			if (_itemCachedItem.GetPath(strPath) &&
				loader.LoadImage(strPath, _image, IW::CNullStatus::Instance))
			{
				m_bHasImage = true;
			}
		}

		if (m_bHasImage)
		{
			if (pformatetcIn->cfFormat == CF_DIB)
			{
				if ((pformatetcIn->tymed & TYMED_HGLOBAL) == 0)
					return DV_E_TYMED;

				pmedium->tymed = TYMED_HGLOBAL;
				pmedium->hGlobal = _image.CopyToHandle();
				pmedium->pUnkForRelease = nullptr;
			}
			else
			{
				if ((pformatetcIn->tymed & TYMED_MFPICT) == 0)
					return DV_E_TYMED;

				CDCHandle mfdc = CreateMetaFile(nullptr);
				const CRect rect = _image.GetBoundingRect();

				//mfdc.SetMapMode(MM_ANISOTROPIC);
				mfdc.FillSolidRect(rect, RGB(255, 255, 255));

				IW::CRender::DrawToDC(mfdc, _image.GetFirstPage(), rect);

				HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, sizeof(METAFILEPICT));

				if (hMem == nullptr)
					return E_OUTOFMEMORY;

				auto lpMFP = static_cast<LPMETAFILEPICT>(GlobalLock(hMem));

				if (lpMFP == nullptr)
				{
					GlobalFree(hMem);
					return E_OUTOFMEMORY;
				}

				lpMFP->mm = MM_TEXT;
				lpMFP->xExt = rect.Width();
				lpMFP->yExt = rect.Height();
				lpMFP->hMF = CloseMetaFile(mfdc);

				GlobalUnlock(hMem);

				pmedium->tymed = TYMED_MFPICT;
				pmedium->hMetaFilePict = hMem;
				pmedium->pUnkForRelease = nullptr;
			}

			return S_OK;
		}
	}

	if (m_bSetForMove && pformatetcIn->cfFormat == cfPDE)
	{
		if ((pformatetcIn->tymed & TYMED_HGLOBAL) == 0)
			return DV_E_TYMED;

		HGLOBAL h = GlobalAlloc(GMEM_ZEROINIT | GMEM_MOVEABLE | GMEM_DDESHARE, sizeof(DWORD));

		if (h == nullptr)
			return E_OUTOFMEMORY;

		auto p = static_cast<DWORD*>(GlobalLock(h));

		if (p == nullptr)
		{
			GlobalFree(h);
			return E_OUTOFMEMORY;
		}

		*p = m_bMove ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
		GlobalUnlock(h);

		pmedium->tymed = TYMED_HGLOBAL;
		pmedium->hGlobal = h;
		pmedium->pUnkForRelease = nullptr;

		return S_OK;
	}

	if (m_spShellDataObject != nullptr)
	{
		return m_spShellDataObject->GetData(pformatetcIn, pmedium);
	}

	return DV_E_FORMATETC;
}

STDMETHODIMP CImageDataObject::GetDataHere(FORMATETC* pformatetc, STGMEDIUM* pmedium)
{
	if (m_spShellDataObject != nullptr)
	{
		return m_spShellDataObject->GetDataHere(pformatetc, pmedium);
	}

	return E_NOTIMPL;
}

STDMETHODIMP CImageDataObject::QueryGetData(FORMATETC* pformatetc)
{
	if (pformatetc->cfFormat == CF_DIB && m_bCachedItemImage)
	{
		return S_OK;
	}

	if (m_spShellDataObject != nullptr)
	{
		return m_spShellDataObject->QueryGetData(pformatetc);
	}

	return E_NOTIMPL;
}

STDMETHODIMP CImageDataObject::GetCanonicalFormatEtc(FORMATETC* pformatectIn,
                                                     FORMATETC* pformatetcOut)
{
	if (m_spShellDataObject != nullptr)
	{
		return m_spShellDataObject->GetCanonicalFormatEtc(pformatectIn, pformatetcOut);
	}

	return E_NOTIMPL;
}

STDMETHODIMP CImageDataObject::SetData(FORMATETC* pformatetc,
                                       STGMEDIUM* pmedium,
                                       BOOL fRelease)
{
	if (m_spShellDataObject != nullptr)
	{
		return m_spShellDataObject->SetData(pformatetc, pmedium, fRelease);
	}

	return E_NOTIMPL;
}


STDMETHODIMP CImageDataObject::EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppenumFormatEtc)
{
	std::vector<FORMATETC> vfmtetc;

	static FORMATETC fmteDib = {static_cast<CLIPFORMAT>(CF_DIB), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
	static FORMATETC fmteMF = {static_cast<CLIPFORMAT>(CF_METAFILEPICT), nullptr, DVASPECT_CONTENT, -1, TYMED_MFPICT};

	static FORMATETC fmtePDE = {static_cast<CLIPFORMAT>(cfPDE), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};


	if (ppenumFormatEtc == nullptr)
		return E_POINTER;

	vfmtetc.clear();

	if (m_bCachedItemImage || m_bHasImage)
	{
		vfmtetc.push_back(fmteDib);
		vfmtetc.push_back(fmteMF);
	}

	if (m_bSetForMove && m_bMove)
	{
		vfmtetc.push_back(fmtePDE);
	}


	if (m_spShellDataObject != nullptr)
	{
		IW::RefPtr<IEnumFORMATETC> pEnumFmt;
		// enumerate the available formats supported by the object
		HRESULT hr = m_spShellDataObject->EnumFormatEtc(DATADIR_GET, pEnumFmt.GetPtr());

		// A namespace extension is allowed to answer E_NOTIMPL here.
		if (SUCCEEDED(hr) && pEnumFmt != nullptr)
		{
			FORMATETC fmt;

			while (S_OK == pEnumFmt->Next(1, &fmt, nullptr))
			{
				vfmtetc.push_back(fmt);
			}
		}

		//return m_spShellDataObject->EnumFormatEtc(dwDirection, ppenumFormatEtc);
	}

#ifdef _DEBUG

	for (auto it = vfmtetc.begin();
	     it != vfmtetc.end(); ++it)
	{
		TCHAR szBuf[MAX_PATH];

		if (GetClipboardFormatName(it->cfFormat, szBuf, MAX_PATH))
		{
			// remaining entries read from "fmt" members
			ATLTRACE(_T("EnumFormatEtc %s\n"), szBuf);
		}
		else
		{
			ATLTRACE(_T("EnumFormatEtc (Unknown)\n"));
		}
	}

#endif //_DEBUG

	*ppenumFormatEtc = nullptr;
	switch (dwDirection)
	{
	case DATADIR_GET:
		{
			CComPtr<CEnumFORMATETCImpl> p = IW::CreateComObject<CEnumFORMATETCImpl>();
			p->Copy(vfmtetc);
			p->QueryInterface(IID_IEnumFORMATETC, (LPVOID*)ppenumFormatEtc);
		}
		break;

	case DATADIR_SET:
	default:
		return E_NOTIMPL;
		break;
	}

	return S_OK;
}

STDMETHODIMP CImageDataObject::DAdvise(FORMATETC* pformatetc,
                                       DWORD advf,
                                       IAdviseSink* pAdvSink,
                                       DWORD* pdwConnection)
{
	if (m_spShellDataObject != nullptr)
	{
		return m_spShellDataObject->DAdvise(pformatetc, advf, pAdvSink, pdwConnection);
	}

	return E_NOTIMPL;
}

STDMETHODIMP CImageDataObject::DUnadvise(DWORD dwConnection)
{
	if (m_spShellDataObject != nullptr)
	{
		return m_spShellDataObject->DUnadvise(dwConnection);
	}

	return E_NOTIMPL;
}

STDMETHODIMP CImageDataObject::EnumDAdvise(IEnumSTATDATA** ppenumAdvise)
{
	if (m_spShellDataObject != nullptr)
	{
		return m_spShellDataObject->EnumDAdvise(ppenumAdvise);
	}

	return E_NOTIMPL;
}

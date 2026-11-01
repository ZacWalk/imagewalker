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
#include "iw/formatenum.h"

static UINT cfPDE = RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT);

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
	std::vector<std::unique_ptr<DVTARGETDEVICE, decltype(&::CoTaskMemFree)>> targetDevices;

	static FORMATETC fmteDib = {static_cast<CLIPFORMAT>(CF_DIB), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
	static FORMATETC fmteMF = {static_cast<CLIPFORMAT>(CF_METAFILEPICT), nullptr, DVASPECT_CONTENT, -1, TYMED_MFPICT};

	static FORMATETC fmtePDE = {static_cast<CLIPFORMAT>(cfPDE), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};


	if (ppenumFormatEtc == nullptr)
		return E_POINTER;

	*ppenumFormatEtc = nullptr;
	if (dwDirection != DATADIR_GET)
		return E_NOTIMPL;

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
				std::unique_ptr<DVTARGETDEVICE, decltype(&::CoTaskMemFree)> device(fmt.ptd, ::CoTaskMemFree);
				vfmtetc.push_back(fmt);
				targetDevices.push_back(std::move(device));
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

	return IW::CreateFormatEnumerator(static_cast<UINT>(vfmtetc.size()), vfmtetc.data(), ppenumFormatEtc);
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

// ImageWalker by Zac Walker
//
// Purpose: OLE compound document implementation.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"
#include "FileCompoundDoc.h"
#include "FileMetaFile.h"

#include <ole2.h>
#include <locale.h>


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CLoadCompoundDoc::CLoadCompoundDoc()
{
	//::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
}

CLoadCompoundDoc::~CLoadCompoundDoc()
{
	//CoUninitialize();
}

#include <pshpack1.h>

using PACKEDMETA = struct tagPACKEDMETA
{
	WORD mm;
	WORD xExt;
	WORD yExt;
	WORD reserved;
};

#include <poppack.h>

bool CLoadCompoundDoc::Read(
	const CString& str,
	IW::IStreamIn* pStreamIn,
	IW::IImageStream* pImageOut,
	IW::IStatus* pStatus)
{
	CComPtr<IStorage> pStorage;
	CComPtr<ILockBytes> pLockBytes;
	CComPtr<IPropertySetStorage> pPropSetStg;

	HRESULT hr;
	IW::FileSize size = pStreamIn->GetFileSize();
	HGLOBAL hg = GlobalAlloc(GMEM_FIXED, size);

	if (hg == nullptr)
		return false;

	//  STEP 4.b:
	//  GlobalLock the handle returned from the GlobalAlloc call.
	auto p = static_cast<LPBYTE>(GlobalLock(hg));

	if (p != nullptr)
	{
		pStreamIn->Read(p, size);
		GlobalUnlock(hg);
	}

	hr = CreateILockBytesOnHGlobal(hg, TRUE, &pLockBytes);
	if (FAILED(hr))
	{
		pStatus->SetError(App.LoadString(IDS_FAILEDTOLOCKBYTES));
		return false;
	}

	hr = StgOpenStorageOnILockBytes(pLockBytes, nullptr,
	                                STGM_DIRECT | STGM_READ | STGM_SHARE_EXCLUSIVE, nullptr,
	                                0, &pStorage);

	if (FAILED(hr))
	{
		pStatus->SetError(App.LoadString(IDS_FAILEDTOOPENSTORAGE));
		return false;
	}

	// Obtain the IPropertySetStorage interface.
	hr = pStorage->QueryInterface(IID_IPropertySetStorage, (void**)&pPropSetStg);

	if (FAILED(hr))
	{
		pStatus->SetError(App.LoadString(IDS_QIFAILEDFORIPROPERTYSETSTORAGE));
		return false;
	}


	CComPtr<IPropertyStorage> pPropStg;

	// Open summary information, getting an IpropertyStorage.
	hr = pPropSetStg->Open(FMTID_SummaryInformation, STGM_READ | STGM_SHARE_EXCLUSIVE, &pPropStg);

	if (FAILED(hr))
	{
		pStatus->SetError(App.LoadString(IDS_NOSUMMARYINFO));
		return false;
	}

	// Initialize PROPSPEC for the properties you want.
	PROPSPEC spec;
	PROPVARIANT var;

	IW::MemZero(&spec, sizeof(PROPSPEC));
	PropVariantInit(&var);
	spec.ulKind = PRSPEC_PROPID;
	spec.propid = PIDSI_THUMBNAIL;

	// Read properties.
	hr = pPropStg->ReadMultiple(1, &spec, &var);

	if (FAILED(hr))
	{
		pStatus->SetError(App.LoadString(IDS_READMULTIPLEFAILED));
		return false;
	}

	bool bSuccess = false;

	if (VT_CF == var.vt &&
		var.pclipdata &&
		var.pclipdata->cbSize > sizeof(BITMAPINFO))
	{
		//CF_METAFILEPICT;

		// cbSize counts ulClipFmt as well, so pClipData is four bytes shorter.
		const DWORD nSize = var.pclipdata->cbSize - sizeof(var.pclipdata->ulClipFmt);
		LPBYTE pByte = var.pclipdata->pClipData;
		const DWORD dwOffset = sizeof(PACKEDMETA) + 4;

		if (pByte != nullptr && nSize > dwOffset)
		{
			IW::ScopeObj<CLoadMetaFile> ml;
			bSuccess = ml.Read(pByte + dwOffset, nSize - dwOffset, pImageOut, pStatus);
		}
	}

	PropVariantClear(&var);

	if (bSuccess)
	{
		pImageOut->SetStatistics(GetTitle());
		pImageOut->SetLoaderName(GetKey());
	}


	// Dump properties.
	//DumpBuiltInProps(pPropSetStg);
	//DumpCustomProps(pPropSetStg);

	return bSuccess;
}

bool CLoadCompoundDoc::Write(const CString& str, IW::IStreamOut* pStreamOut, const IW::Image& imageIn,
                             const IW::CodecSettings& settings, IW::IStatus* pStatus)
{
	return false;
}

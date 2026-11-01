// ImageWalker by Zac Walker
//
// Purpose: OLE compound document implementation.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "LegacyPch.h"
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
try
{
	CComPtr<IStorage> pStorage;
	CComPtr<ILockBytes> pLockBytes;
	CComPtr<IPropertySetStorage> pPropSetStg;

	HRESULT hr;
	const auto size = pStreamIn->GetFileSize().GetByteCount();
	if (!size || size > INT_MAX)
	{
		const CString message(L"The compound document is empty or exceeds the input limit.");
		if (size > INT_MAX) pStatus->SetResourceError(message);
		else pStatus->SetError(message);
		return false;
	}
	struct Memory
	{
		HGLOBAL handle;
		~Memory() { if (handle) GlobalFree(handle); }
	} memory{GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(size))};
	if (!memory.handle) throw std::bad_alloc();

	{
		auto* bytes = static_cast<BYTE*>(GlobalLock(memory.handle));
		if (!bytes)
		{
			pStatus->SetError(L"The compound document input could not be locked.");
			return false;
		}
		struct Unlock
		{
			HGLOBAL handle;
			~Unlock() { GlobalUnlock(handle); }
		} unlock{memory.handle};
		DWORD read{};
		if (pStreamIn->Seek(IW::IStreamCommon::eBegin, 0) != 0 ||
			!pStreamIn->Read(bytes, static_cast<DWORD>(size), &read) || read != size)
		{
			pStatus->SetError(L"The compound document could not be read completely.");
			return false;
		}
	}

	hr = CreateILockBytesOnHGlobal(memory.handle, TRUE, &pLockBytes);
	if (FAILED(hr))
	{
		pStatus->SetError(CString(IW::Text::FAILEDTOLOCKBYTES));
		return false;
	}
	memory.handle = nullptr;

	hr = StgOpenStorageOnILockBytes(pLockBytes, nullptr,
	                                STGM_DIRECT | STGM_READ | STGM_SHARE_EXCLUSIVE, nullptr,
	                                0, &pStorage);

	if (FAILED(hr))
	{
		pStatus->SetError(CString(IW::Text::FAILEDTOOPENSTORAGE));
		return false;
	}

	// Obtain the IPropertySetStorage interface.
	hr = pStorage->QueryInterface(IID_IPropertySetStorage, (void**)&pPropSetStg);

	if (FAILED(hr))
	{
		pStatus->SetError(CString(IW::Text::QIFAILEDFORIPROPERTYSETSTORAGE));
		return false;
	}


	CComPtr<IPropertyStorage> pPropStg;

	// Open summary information, getting an IpropertyStorage.
	hr = pPropSetStg->Open(FMTID_SummaryInformation, STGM_READ | STGM_SHARE_EXCLUSIVE, &pPropStg);

	if (FAILED(hr))
	{
		pStatus->SetError(CString(IW::Text::NOSUMMARYINFO));
		return false;
	}

	// Initialize PROPSPEC for the properties you want.
	PROPSPEC spec;
	PROPVARIANT var;

	IW::MemZero(&spec, sizeof(PROPSPEC));
	PropVariantInit(&var);
	struct Property
	{
		PROPVARIANT& value;
		~Property() { PropVariantClear(&value); }
	} property{var};
	spec.ulKind = PRSPEC_PROPID;
	spec.propid = PIDSI_THUMBNAIL;

	// Read properties.
	hr = pPropStg->ReadMultiple(1, &spec, &var);

	if (FAILED(hr))
	{
		pStatus->SetError(CString(IW::Text::READMULTIPLEFAILED));
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
catch (const std::bad_alloc&)
{
	pStatus->SetResourceError(L"Not enough memory to read the compound document.");
	return false;
}
catch (const std::exception& error)
{
	pStatus->SetError(CString(error.what()));
	return false;
}

bool CLoadCompoundDoc::Write(const CString& str, IW::IStreamOut* pStreamOut, const IW::Image& imageIn,
                             const IW::CodecSettings& settings, IW::IStatus* pStatus)
{
	return false;
}

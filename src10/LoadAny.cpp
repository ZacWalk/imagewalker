// LoadAny.cpp: implementation of the CLoadAny class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "artmate.h"
#include "LoadAny.h"
#include "LoadJpg.h"
#include "LoadTiff.h"
#include "LoadGif.h"
#include "LoadPcx.h"
#include "LoadBmp.h"
#include "LoadPng.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CLoadAny::CLoadAny()
{
	m_pLoadJpg = nullptr;
	m_pLoadTiff = nullptr;
	m_pLoadGif = nullptr;
	m_pLoadPcx = nullptr;
	m_pLoadPng = nullptr;
	m_pLoadBmp = nullptr;
}

CLoadAny::~CLoadAny()
{
	if (m_pLoadJpg)
		delete m_pLoadJpg;

	if (m_pLoadTiff)
		delete m_pLoadTiff;

	if (m_pLoadGif)
		delete m_pLoadGif;

	if (m_pLoadPcx)
		delete m_pLoadPcx;

	if (m_pLoadPng)
		delete m_pLoadPng;

	if (m_pLoadBmp)
		delete m_pLoadBmp;
}

BOOL CLoadAny::Load(CDib* pDib, LPBYTE pByte, DWORD nSize, CStatus* pStatus, BOOL bThumb)
{
	CLoad* pLoader = nullptr;

	// Every loader below starts by reading a header out of this buffer, and the
	// switch itself reads the first two bytes.
	if (pByte == nullptr || nSize < sizeof(unsigned short))
		return FALSE;

	switch (*((unsigned short*)pByte))
	{
	case 0x4D42: // bmp
		if (!m_pLoadBmp)
			m_pLoadBmp = new CLoadBmp();

		pLoader = m_pLoadBmp;
		break;

	case 0xD8FF: // Jpeg
		if (!m_pLoadJpg)
			m_pLoadJpg = new CLoadJpg();

		pLoader = m_pLoadJpg;
		break;

	case 0x4947: // Gif
		if (!m_pLoadGif)
			m_pLoadGif = new CLoadGif();

		pLoader = m_pLoadGif;
		break;

	case 0x050A: // Pcx
		if (!m_pLoadPcx)
			m_pLoadPcx = new CLoadPcx();

		pLoader = m_pLoadPcx;
		break;

	case 0x4949: // Tif
	case 0x4D4D: // Tif, big endian
		if (!m_pLoadTiff)
			m_pLoadTiff = new CLoadTiff();

		pLoader = m_pLoadTiff;
		break;

	case 0x5089: // Tif
		if (!m_pLoadPng)
			m_pLoadPng = new CLoadPng();

		if (m_pLoadPng->IsPng(pByte))
			pLoader = m_pLoadPng;

		break;
	}


	return pLoader && pLoader->Load(pDib, pByte, nSize, pStatus, bThumb);
}

BOOL CLoadAny::Load(CDib* pDib, LPCSTR szFileName, CStatus* pStatus, BOOL bThumb)
{
	BOOL b = FALSE;

	// Open the file
	HANDLE hFile = CreateFile(szFileName, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
	                          FILE_ATTRIBUTE_READONLY, nullptr);

	if (hFile != (HANDLE)-1)
	{
		// Create a file mapping of the opened file
		HANDLE hFileMap = CreateFileMapping(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);

		if (hFileMap != nullptr)
		{
			//CreateFileMapping
			// Map a view of the whole file
			LPVOID pFileMap = MapViewOfFile(hFileMap, FILE_MAP_READ, 0, 0, 0);

			if (pFileMap != nullptr)
			{
				const DWORD nSize = GetFileSize(hFile, nullptr);

				if (nSize != INVALID_FILE_SIZE)
					b = Load(pDib, static_cast<LPBYTE>(pFileMap), nSize, pStatus, bThumb);

				UnmapViewOfFile(pFileMap);
			}

			CloseHandle(hFileMap);
		}
		CloseHandle(hFile);
	}

	return b;
}


BOOL CLoadAny::Load(CDib* pDib, int nID, CStatus* pStatus, BOOL bThumb)
{
	HINSTANCE hInst = _AtlBaseModule.GetResourceInstance();
	HRSRC hrsrc = ::FindResource(hInst, MAKEINTRESOURCE(nID), "image");
	if (!hrsrc)
	{
		TRACE("BITMAP resource not found\n");
		return FALSE;
	}
	HGLOBAL hg = LoadResource(hInst, hrsrc);
	if (!hg)
	{
		TRACE("Failed to load BITMAP resource\n");
		return FALSE;
	}

	DWORD nSize = SizeofResource(hInst, hrsrc);
	auto pByte = static_cast<LPBYTE>(LockResource(hg));

	if (nSize == 0 || pByte == nullptr)
	{
		TRACE("Empty or unlockable BITMAP resource\n");
		return FALSE;
	}

	BOOL b = Load(pDib, pByte, nSize, pStatus, bThumb);

	FreeResource(hg);

	return b;
}

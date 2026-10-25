///////////////////////////////////////////////////////////////////////
//
// This file is part of the ImageWalker code base.
// For more information on ImageWalker see www.ImageWalker.com
//
// Copyright (C) 1998-2001 Zac Walker.  All rights reserved.
//
///////////////////////////////////////////////////////////////////////
//
// The one shell-namespace item representation, shared by the WTL apps.
//
// 1.0 through 2.3 each carried their own copy of this: src10/shell.h,
// src20/shell.h, src22/IWShell.h and src23/Shell.h. The 2.x
// three were the same design under three different smart-pointer aliases; the
// surface below is their union, so no app loses a method it was calling.
//
// 3.0 is deliberately NOT a client of this header. Its iw::platform layer
// exposes shell navigation as std::filesystem::path and states that only
// platform backends may interpret an OS handle; it also builds /permissive-
// without ATL. See docs/layout.md.
//
// Two things vary per app and cannot live here:
//
//   * the exception type. 1.0, 2.0 and 2.2 throw hierarchies with no
//     std::exception base, so anything thrown from a shared header would sail
//     straight past their catch sites. The three failure paths call
//     ShellApiFailed / ShellOutOfMemory, which each app defines.
//   * the archive format. 2.2 persists a shell item as the raw PIDL bytes,
//     2.3 as its parsed display name. Both are here, under different
//     names, because changing either would orphan the .ini files already
//     written by that version.
//
///////////////////////////////////////////////////////////////////////

#pragma once

#include <shtypes.h>
#include <comdef.h>
#include <shlobj.h>
#include <shlguid.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <tchar.h>

// Self-contained rather than relying on the includer: 2.0's PCH stops at
// atlbase.h and never pulls in a CString.
#include <atlbase.h>
#include <atlstr.h>

namespace IW
{
	// Defined once per app, next to that app's exception hierarchy.
	[[noreturn]] void ShellApiFailed(LPCTSTR szApi, HRESULT hr);
	[[noreturn]] void ShellOutOfMemory();

	class CShellFolder;

	class CShellMalloc : public CComPtr<IMalloc>
	{
	public:
		CShellMalloc()
		{
			LPMALLOC pMalloc = 0;
			HRESULT hr = SHGetMalloc(&pMalloc);

			if (hr != NOERROR)
				ShellApiFailed(_T("SHGetMalloc"), hr);

			Attach(pMalloc);
		}
	};


	class CShellItem
	{
	public:

		CShellItem() : m_pItem(0)
		{
		}

		CShellItem(LPCITEMIDLIST pItemSrc) : m_pItem(0)
		{
			Copy(pItemSrc);
		}

		CShellItem(LPITEMIDLIST pItemSrc) : m_pItem(pItemSrc)
		{
		}

		CShellItem(const CShellItem& item) : m_pItem(0)
		{
			Copy(item);
		}

		// 2.0 spelling: construct straight from a CSIDL.
		CShellItem(HWND hwnd, const int nSpecialFolder) : m_pItem(0)
		{
			HRESULT hr = SHGetSpecialFolderLocation(hwnd, nSpecialFolder, &m_pItem);

			if (FAILED(hr))
				ShellApiFailed(_T("SHGetSpecialFolderLocation"), hr);
		}

		virtual ~CShellItem()
		{
			Free();
		}

		bool IsNull() const
		{
			return m_pItem == 0;
		}

		bool Open(HWND hwnd, const int nSpecialFolder)
		{
			LPITEMIDLIST pItem = 0;
			HRESULT hr = SHGetSpecialFolderLocation(hwnd, nSpecialFolder, &pItem);

			if (FAILED(hr))
				return false;

			Attach(pItem);
			return true;
		}

		// LPCTSTR rather than const CString&: 2.0 through 2.2 pass IW::CFilePath,
		// which converts to LPCTSTR but cannot reach CString without a second
		// user-defined conversion.
		bool Open(LPCTSTR strPath)
		{
			LPITEMIDLIST pItem = 0;
			ULONG chEaten = 0;
			USES_CONVERSION;

			CComPtr<IShellFolder> spDesktopFolder;

			HRESULT hr = ::SHGetDesktopFolder(&spDesktopFolder);

			if (FAILED(hr))
				return false;

			hr = spDesktopFolder->ParseDisplayName(NULL, NULL, CT2OLE(strPath), &chEaten, &pItem, NULL);

			if (FAILED(hr))
				return false;

			Attach(pItem);
			return true;
		}

		bool GetPath(::CString& str) const
		{
			bool bRet = SHGetPathFromIDList(GetItem(), str.GetBuffer(MAX_PATH)) != 0;
			str.ReleaseBuffer();
			return bRet;
		}

		HRESULT Copy(const CShellItem& item)
		{
			return Copy(item.m_pItem);
		}

		HRESULT Copy(LPCITEMIDLIST pItemSrc)
		{
			if (pItemSrc != 0)
			{
				UINT cbTotal = GetSize(pItemSrc);
				LPITEMIDLIST pItem = Create(cbTotal);

				if (pItem == 0)
					return E_OUTOFMEMORY;

				memcpy(pItem, pItemSrc, cbTotal);

				Attach(pItem);

				ATLASSERT((m_pItem == 0) || IsDesktop() || CShellMalloc()->DidAlloc(m_pItem));
			}
			else
			{
				Free();
			}

			return S_OK;
		}

		HRESULT StripToTail()
		{
			ATLASSERT(m_pItem != NULL);
			ATLASSERT(CShellMalloc()->DidAlloc(m_pItem));

			LPCITEMIDLIST p = m_pItem;
			LPCITEMIDLIST pLast = p;

			while (p->mkid.cb)
			{
				pLast = p;
				p = Next(p);
			}

			return Copy(pLast);
		}

		LPCITEMIDLIST GetTailItem() const
		{
			return GetTailItem(m_pItem);
		}

		static LPCITEMIDLIST GetTailItem(LPCITEMIDLIST p)
		{
			ATLASSERT(p != NULL);
			ATLASSERT(CShellMalloc()->DidAlloc((LPVOID)p));

			LPCITEMIDLIST pLast = p;

			while (p->mkid.cb)
			{
				pLast = p;
				p = Next(p);
			}

			return pLast;
		}

		HRESULT CopyTo(long** ppItemLongOut) const
		{
			ATLASSERT(*ppItemLongOut == 0);

			CShellItem item;
			HRESULT hr = item.Copy(m_pItem);

			if (SUCCEEDED(hr))
				*ppItemLongOut = reinterpret_cast<long*>(item.Detach());

			return hr;
		}

		HRESULT CopyTo(LPITEMIDLIST* ppidl) const
		{
			ATLASSERT(*ppidl == 0);

			CShellItem item;
			HRESULT hr = item.Copy(m_pItem);

			if (SUCCEEDED(hr))
				*ppidl = item.Detach();

			return hr;
		}

		// The assert usually indicates a bug. If taking the address really is
		// what is needed, take the address of m_pItem explicitly.
		LPITEMIDLIST* GetPtr()
		{
			ATLASSERT(m_pItem == 0);
			return &m_pItem;
		}

		long** GetLongPtr()
		{
			ATLASSERT(m_pItem == 0);
			return reinterpret_cast<long**>(&m_pItem);
		}

		long* GetLong()
		{
			return reinterpret_cast<long*>(m_pItem);
		}

		void Free()
		{
			if (m_pItem)
			{
				ATLASSERT(CShellMalloc()->DidAlloc(m_pItem));
				CShellMalloc()->Free(m_pItem);
			}

			m_pItem = NULL;
		}

		void Attach(LPITEMIDLIST pItem)
		{
			ATLASSERT((m_pItem == 0) || CShellMalloc()->DidAlloc(m_pItem));

			Free();

			m_pItem = pItem;
		}

		LPITEMIDLIST Detach()
		{
			ATLASSERT((m_pItem == 0) || IsDesktop() || CShellMalloc()->DidAlloc(m_pItem));

			LPITEMIDLIST p = m_pItem;
			m_pItem = NULL;

			return p;
		}

		inline const CShellItem& operator=(const CShellItem& item)
		{
			if (this != &item)
				Copy(item);

			return *this;
		}

		inline const CShellItem& operator=(LPCITEMIDLIST pidl)
		{
			ATLASSERT(pidl != NULL);
			Copy(pidl);
			return *this;
		}

		short Compare(const CShellItem& item) const;
		short Compare(IShellFolder* pFolder, const CShellItem& item) const;

		inline bool operator>(const CShellItem& item) const
		{
			ATLASSERT(m_pItem != NULL);
			return Compare(item) > 0;
		}

		inline bool operator<(const CShellItem& item) const
		{
			ATLASSERT(m_pItem != NULL);
			return Compare(item) < 0;
		}

		inline bool operator==(const CShellItem& item) const
		{
			ATLASSERT(m_pItem != NULL);
			return Compare(item) == 0;
		}

		inline bool operator!=(const CShellItem& item) const
		{
			return Compare(item) != 0;
		}

		operator LPCITEMIDLIST*() const
		{
			ATLASSERT(m_pItem != NULL);
			return (LPCITEMIDLIST*)&m_pItem;
		}

		operator LPCITEMIDLIST() const
		{
			ATLASSERT(m_pItem != NULL);
			return m_pItem;
		}

		LPCITEMIDLIST GetItem() const
		{
			ATLASSERT(m_pItem != NULL);
			return m_pItem;
		}

		bool IsDesktop() const
		{
			ATLASSERT(m_pItem != NULL);
			return (m_pItem->mkid.cb == 0);
		}

		static bool IsDesktop(LPCITEMIDLIST pItem)
		{
			ATLASSERT(pItem != NULL);
			return (pItem->mkid.cb == 0);
		}

		int Depth() const
		{
			return Depth(m_pItem);
		}

		static int Depth(LPCITEMIDLIST p)
		{
			ATLASSERT(p != NULL);
			ATLASSERT(CShellMalloc()->DidAlloc((LPVOID)p));

			int i = 0;

			while (p->mkid.cb)
			{
				p = Next(p);
				i += 1;
			}

			return i;
		}

		void Cat(LPCITEMIDLIST pItem1, LPCITEMIDLIST pItem2)
		{
			ATLASSERT((m_pItem == 0) || IsDesktop() || CShellMalloc()->DidAlloc(m_pItem));

			if (pItem1 == 0 || IsDesktop(pItem1))
			{
				Copy(pItem2);
			}
			else if (pItem2 == 0 || IsDesktop(pItem2))
			{
				Copy(pItem1);
			}
			else
			{
				UINT cb1 = GetSize(pItem1) - sizeof(pItem1->mkid.cb);
				UINT cb2 = GetSize(pItem2);

				LPITEMIDLIST pidlNew = Create(cb1 + cb2);

				if (pidlNew)
				{
					memcpy(((LPBYTE)pidlNew), pItem1, cb1);
					memcpy(((LPBYTE)pidlNew) + cb1, pItem2, cb2);

					Attach(pidlNew);
				}
			}
		}

		void Cat(LPCITEMIDLIST pItem)
		{
			ATLASSERT((m_pItem == 0) || IsDesktop() || CShellMalloc()->DidAlloc(m_pItem));

			if (m_pItem == 0 || IsDesktop())
			{
				Copy(pItem);
			}
			else
			{
				UINT cb1 = GetSize(m_pItem) - sizeof(m_pItem->mkid.cb);
				UINT cb2 = GetSize(pItem);

				LPITEMIDLIST pidlNew = Create(cb1 + cb2);

				if (pidlNew)
				{
					memcpy(((LPBYTE)pidlNew), m_pItem, cb1);
					memcpy(((LPBYTE)pidlNew) + cb1, pItem, cb2);

					Free();
					m_pItem = pidlNew;
				}
			}
		}

		// 2.2 / 2.3 archive format: the parsed display name.
		// TArchive is deduced by reference so both a raw IPropertyArchive* and an
		// IW::RefPtr<IPropertyArchive> bind without a conversion.
		template <class TArchive>
		bool Read(LPCTSTR szValueName, const TArchive& pArchive)
		{
			::CString str;

			if (pArchive->Read(szValueName, str) && !str.IsEmpty())
				return Open(str);

			return false;
		}

		template <class TArchive>
		bool Write(LPCTSTR szValueName, const TArchive& pArchive) const;

		// 2.2 archive format: the raw PIDL bytes. A different on-disk shape, so
		// it keeps a different name rather than silently replacing the above.
		template <class TArchive>
		bool ReadBinary(LPCTSTR szValueName, const TArchive& pArchive)
		{
			DWORD dwCount = 1024;
			BYTE buffer[1024];

			if (pArchive->Read(szValueName, buffer, dwCount) && dwCount)
			{
				Attach(Create(dwCount));
				memcpy(m_pItem, buffer, dwCount);
				return true;
			}

			return false;
		}

		template <class TArchive>
		bool WriteBinary(LPCTSTR szValueName, const TArchive& pArchive) const
		{
			ATLASSERT(m_pItem != 0);
			ATLASSERT(CShellMalloc()->DidAlloc(m_pItem));

			return pArchive->Write(szValueName, (LPCVOID)m_pItem, GetSize(m_pItem));
		}

		// Strip the final item to make this the parent.
		bool StripToParent()
		{
			ATLASSERT(m_pItem != 0);
			ATLASSERT(CShellMalloc()->DidAlloc(m_pItem));

			if (m_pItem && m_pItem->mkid.cb)
			{
				SHITEMID* pmkid = (SHITEMID*)m_pItem;
				SHITEMID* pmkid2 = pmkid;

				while (pmkid->cb != 0)
				{
					pmkid2 = pmkid;
					pmkid = (SHITEMID*)((LPBYTE)pmkid + pmkid->cb);
				}

				pmkid2->cb = 0;
			}
			else
			{
				return false;
			}

			return true;
		}

		// Keep the first item only.
		bool StripToFirst()
		{
			ATLASSERT(m_pItem != 0);
			ATLASSERT(CShellMalloc()->DidAlloc(m_pItem));

			if (m_pItem && m_pItem->mkid.cb)
			{
				SHITEMID* pmkid = (SHITEMID*)m_pItem;
				pmkid = (SHITEMID*)((LPBYTE)pmkid + pmkid->cb);
				pmkid->cb = 0;
			}
			else
			{
				return false;
			}

			return true;
		}

		// 2.0 spelling. 2.2 onwards reach the same place through CShellFolder::Open.
		HRESULT GetAsFolder(LPSHELLFOLDER* ppFolder) const;

		// 2.2 spelling. 2.3 dropped these in favour of the CString
		// GetDisplayNameOf below; both are kept so neither side has to change.
		HRESULT BuildDisplayNameOf(LPTSTR pszBuf, UINT cchBuf) const;
		HRESULT BuildDisplayNameOf(LPTSTR pszBuf, UINT cchBuf, CShellFolder& pFolder, bool bFirstTrue = false) const;

	private:
		LPITEMIDLIST m_pItem;

	protected:
		static LPITEMIDLIST Create(UINT cbSize)
		{
			CShellMalloc pMalloc;
			LPITEMIDLIST pidl = (LPITEMIDLIST)pMalloc->Alloc(cbSize);

			if (pidl == NULL)
				ShellOutOfMemory();

			return pidl;
		}

	public:
		static LPCITEMIDLIST Next(LPCITEMIDLIST pidl)
		{
			LPBYTE lpMem = (LPBYTE)pidl;
			lpMem += pidl->mkid.cb;
			return (LPCITEMIDLIST)lpMem;
		}

		static UINT GetSize(LPCITEMIDLIST pidl)
		{
			UINT cbTotal = 0;

			if (pidl)
			{
				cbTotal += sizeof(pidl->mkid.cb); // Null terminator

				while (pidl->mkid.cb)
				{
					cbTotal += pidl->mkid.cb;
					pidl = Next(pidl);
				}
			}

			return cbTotal;
		}
	};


	class CShellDesktopItem : public CShellItem
	{
	public:
		CShellDesktopItem()
		{
			static ITEMIDLIST itemDesktop = {0};
			Copy(&itemDesktop);
		}
	};


	class CShellFolder : public CComPtr<IShellFolder>
	{
	public:

		CShellFolder() throw()
		{
		}

		CShellFolder(IShellFolder* pIn) throw() : CComPtr<IShellFolder>(pIn)
		{
		}

		IShellFolder* operator=(IShellFolder* pIn)
		{
			CComPtr<IShellFolder>::operator=(pIn);
			return p;
		}

		// IW::RefPtr spellings the 2.x call sites use.
		IShellFolder** GetPtr()
		{
			return &p;
		}

		void Copy(IShellFolder* pIn)
		{
			CComPtr<IShellFolder>::operator=(pIn);
		}

		HRESULT Open(const CShellItem& item, bool bBuildUpFolder = false);
		HRESULT Open(CShellFolder& pFolder, const CShellItem& item, bool bBuildUpFolder = false);

		DWORD GetAttributes(const CShellFolder& pFolder, LPCITEMIDLIST pItem, DWORD dwAttributes) const;

		DWORD GetAttributes(LPCITEMIDLIST pItem, DWORD dwAttributes) const
		{
			return GetAttributes(*this, pItem, dwAttributes);
		}

		HRESULT BindToStorage(const CShellFolder& pFolder, const CShellItem& item, REFIID riid, void** ppv) const;

		HRESULT BindToStorage(const CShellItem& item, REFIID riid, void** ppv) const
		{
			return BindToStorage(*this, item, riid, ppv);
		}

		bool IsFolder(LPCITEMIDLIST pItem) const
		{
			if (GetAttributes(pItem, SFGAO_REMOVABLE) & SFGAO_REMOVABLE)
				return true;

			return (GetAttributes(pItem, SFGAO_FOLDER) & SFGAO_FOLDER) != 0;
		}

		bool IsBrowsable(LPCITEMIDLIST pItem) const
		{
			if (GetAttributes(pItem, SFGAO_REMOVABLE) & SFGAO_REMOVABLE)
				return true;

			DWORD u = GetAttributes(pItem, SFGAO_FOLDER);
			bool bIsFolder = ((u & SFGAO_FOLDER) != 0);

			// Shell ZIP folders are not browsable without archive support.
			if (bIsFolder)
			{
				::CString str = GetDisplayNameOf(pItem, SHGDN_INFOLDER | SHGDN_FORPARSING);
				LPCTSTR szExt = ::PathFindExtension(str);
				bIsFolder = (0 != _tcsicmp(szExt, _T(".zip")));
			}

			return bIsFolder;
		}

		CShellItem GetShellItem() const
		{
			CComQIPtr<IPersistFolder2> pExtInit = *this;
			CShellItem item;

			if (pExtInit != 0)
				pExtInit->GetCurFolder(item.GetPtr());

			return item;
		}

		inline ::CString GetDisplayNameOf(LPCITEMIDLIST pItem, unsigned long uFlags) const
		{
			STRRET strReturn;
			HRESULT hr = p->GetDisplayNameOf(pItem, uFlags, &strReturn);

			if (SUCCEEDED(hr))
				return ConvertStrRetToString(strReturn, pItem);

			return _T("");
		}

		// 2.2 spelling.
		inline HRESULT GetDisplayNameOf(LPTSTR pszBuf, UINT cchBuf, LPCITEMIDLIST pItem, unsigned long uFlags) const
		{
			pszBuf[0] = 0;
			STRRET strReturn;
			HRESULT hr = p->GetDisplayNameOf(pItem, uFlags, &strReturn);

			if (SUCCEEDED(hr))
				_tcscpy_s(pszBuf, cchBuf, ConvertStrRetToString(strReturn, pItem));

			return hr;
		}

		static ::CString ConvertStrRetToString(STRRET& strReturn, LPCITEMIDLIST pidl)
		{
			::CString str;

			switch (strReturn.uType)
			{
			case STRRET_WSTR:

				str = strReturn.pOleStr;

				// Release the ole string
				{
					CShellMalloc spMalloc;
					ATLASSERT(spMalloc->DidAlloc(strReturn.pOleStr));

					if (spMalloc)
						spMalloc->Free(strReturn.pOleStr);
				}
				break;

			case STRRET_OFFSET:
				str = ((LPCSTR)pidl) + strReturn.uOffset;
				break;

			case STRRET_CSTR:
				str = (LPCSTR)strReturn.cStr;
				break;

			default:
				ATLASSERT(0);
				break;
			}

			return str;
		}

		// 2.2 spelling. Keeps the E_FAIL-on-unknown-STRRET contract that
		// IW::Folder::GetFolderPath tests.
		static HRESULT ConvertStrRetToBuffer(STRRET* pstr, LPCITEMIDLIST pidl, LPTSTR pszBuf, UINT cchBuf)
		{
			if (pstr->uType != STRRET_WSTR && pstr->uType != STRRET_OFFSET && pstr->uType != STRRET_CSTR)
				return E_FAIL;

			_tcscpy_s(pszBuf, cchBuf, ConvertStrRetToString(*pstr, pidl));
			return S_OK;
		}

		HRESULT ResolveLink(LPCITEMIDLIST pItemIn, CShellItem& itemOut) const
		{
			CComPtr<IStream> pStream;
			HRESULT hr = BindToStorage(pItemIn, IID_IStream, (LPVOID*)&pStream);

			if (FAILED(hr))
				return hr;

			// Get a pointer to the IShellLink interface.
			CComPtr<IShellLink> psl;
			hr = psl.CoCreateInstance(CLSID_ShellLink);

			if (SUCCEEDED(hr))
			{
				// Get a pointer to the IPersistFile interface.
				CComQIPtr<IPersistStream> pps(psl);

				if (pps != 0)
				{
					hr = pps->Load(pStream);
				}
				else
				{
					hr = E_FAIL;
				}

				if (FAILED(hr))
				{
					::CString strFileName = GetDisplayNameOf(pItemIn, SHGDN_FORPARSING);

					if (!strFileName.IsEmpty())
					{
						USES_CONVERSION;
						CComQIPtr<IPersistFile> ppf(psl);

						if (ppf != 0)
							hr = ppf->Load(CT2OLE(strFileName), STGM_WRITE);
					}
				}

				if (SUCCEEDED(hr))
				{
					LPITEMIDLIST pidl = 0;
					hr = psl->GetIDList(&pidl);

					if (SUCCEEDED(hr))
						itemOut.Attach(pidl);
				}
			}

			return hr;
		}

		// 2.0 spelling: resolve the link in place.
		HRESULT ResolveLink(CShellItem& item) const
		{
			CShellItem itemIn(item);
			return ResolveLink(itemIn, item);
		}

		HRESULT BindToStorage(LPCITEMIDLIST pItem, REFIID riid, void** ppv) const
		{
			return BindToStorage(CShellItem(pItem), riid, ppv);
		}
	};


	class CShellDesktop : public CShellFolder
	{
	public:
		CShellDesktop()
		{
			LPSHELLFOLDER pshf = 0;
			HRESULT hr = SHGetDesktopFolder(&pshf);

			if (hr != NOERROR)
				ShellApiFailed(_T("SHGetDesktopFolder"), hr);

			Attach(pshf);
		}

		static int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lp, LPARAM pData)
		{
			switch (uMsg)
			{
			case BFFM_INITIALIZED:
				// FALSE: pData is a pidl, not a path.
				SendMessage(hwnd, BFFM_SETSELECTION, FALSE, pData);
				return 1;

			default:
				break;
			}

			return 0;
		}

		static bool GetDirectory(HWND hWndParent, ::CString& strDir)
		{
			CShellItem item;
			ParseStartingDirectory(strDir, item);

			return GetDirectory(hWndParent, item) && item.GetPath(strDir);
		}

		// 2.0 through 2.2 spelling: they pass an IW::CFilePath / TCHAR buffer.
		static bool GetDirectory(HWND hWndParent, LPTSTR szDir)
		{
			CShellItem item;
			ParseStartingDirectory(szDir, item);

			return GetDirectory(hWndParent, item) && SHGetPathFromIDList(item, szDir) != 0;
		}

		// Best-effort: an unparseable starting directory just means the browser
		// opens with nothing preselected.
		static void ParseStartingDirectory(LPCTSTR szDir, CShellItem& item)
		{
			USES_CONVERSION;

			::CString strDirCorrected = szDir;
			strDirCorrected.Replace(_T('/'), _T('\\'));

			LPITEMIDLIST pidl = 0;
			ULONG chEaten = 0;
			ULONG dwAttributes = 0;

			HRESULT hr = CShellDesktop()->ParseDisplayName(
				NULL,
				NULL,
				(LPOLESTR)CT2OLE(strDirCorrected),
				&chEaten,
				&pidl,
				&dwAttributes);

			if (SUCCEEDED(hr))
				item.Attach(pidl);
		}

		static bool GetDirectory(HWND hWndParent, CShellItem& item)
		{
			TCHAR szPath[MAX_PATH] = {0};

			BROWSEINFO bi;
			bi.hwndOwner = hWndParent;
			bi.pidlRoot = NULL;
			bi.pszDisplayName = szPath;
			bi.lpszTitle = _T("Select Folder...");
			bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
			bi.lpfn = BrowseCallbackProc;
			bi.lParam = item.IsNull() ? 0 : (LPARAM)(LPCITEMIDLIST)item;
			bi.iImage = 0;

			LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
			item.Attach(pidl);

			return (NULL != pidl);
		}
	};


	class CShellItemEnum : public CComPtr<IEnumIDList>
	{
	public:
		HRESULT Create(HWND hwnd, IShellFolder* pFolder, UINT nFlags)
		{
			LPENUMIDLIST lpe = 0;

			// Get the IEnumIDList object for the given folder.
			HRESULT hr = pFolder->EnumObjects(hwnd, nFlags, &lpe);

			// Cant bind?
			if (FAILED(hr))
				return hr;

			Attach(lpe);

			return S_OK;
		}
	};


	inline short CShellItem::Compare(IShellFolder* pFolder, const CShellItem& item) const
	{
		if (IsDesktop())
			return item.IsDesktop() ? 0 : -1;

		if (item.IsDesktop())
			return 1;

		HRESULT hr = pFolder->CompareIDs(0, *this, item);

		if (SUCCEEDED(hr))
			return static_cast<short>(SCODE_CODE(hr));

		return -1;
	}

	inline short CShellItem::Compare(const CShellItem& item) const
	{
		return Compare(CShellDesktop(), item);
	}

	inline HRESULT CShellItem::GetAsFolder(LPSHELLFOLDER* ppFolder) const
	{
		ATLASSERT(ppFolder != NULL);

		CShellDesktop pDesktop;

		if (IsDesktop())
		{
			*ppFolder = pDesktop.Detach();
			return S_OK;
		}

		return pDesktop->BindToObject(m_pItem, NULL, IID_IShellFolder, (LPVOID*)ppFolder);
	}

	inline HRESULT CShellFolder::Open(const CShellItem& item, bool bBuildUpFolder)
	{
		HRESULT hr = S_OK;

		CShellDesktop pDesktop;

		if (item.IsDesktop())
		{
			Copy(pDesktop);
		}
		else
		{
			hr = Open(pDesktop, item, bBuildUpFolder);
		}

		return hr;
	}

	inline HRESULT CShellFolder::Open(CShellFolder& pFolder, const CShellItem& item, bool bBuildUpFolder)
	{
		HRESULT hr = S_OK;

		if (bBuildUpFolder && item.Depth() > 1)
		{
			CShellItem itemFirst(item);

			if (!itemFirst.StripToFirst())
				return E_FAIL;

			CShellFolder pFolderLocal;
			hr = pFolder->BindToObject(itemFirst, NULL, IID_IShellFolder, (LPVOID*)pFolderLocal.GetPtr());

			if (SUCCEEDED(hr))
			{
				CShellItem itemRest(CShellItem::Next(item));
				hr = Open(pFolderLocal, itemRest, bBuildUpFolder);
			}
		}
		else
		{
			hr = pFolder->BindToObject(item, NULL, IID_IShellFolder, (LPVOID*)GetPtr());
		}

		return hr;
	}

	inline DWORD CShellFolder::GetAttributes(const CShellFolder& pFolder, LPCITEMIDLIST pItem, DWORD dwAttributes) const
	{
		HRESULT hr = S_OK;
		ULONG ulAttrs = dwAttributes;

		if (CShellItem::Depth(pItem) > 1)
		{
			CShellItem itemFirst(pItem);

			// Verbatim from the four app copies, including the two defects: this
			// returns an HRESULT from a DWORD attribute mask, and it binds
			// pFolderLocal and then asks pFolder - the parent - about a pidl that
			// is relative to the child. Left alone so the consolidation does not
			// change what IsFolder/IsBrowsable answer. The deep branch is close to
			// dead in practice; every caller passes a depth-1 tail item.
			if (!itemFirst.StripToFirst())
				return E_FAIL;

			CShellFolder pFolderLocal;
			hr = pFolder->BindToObject(itemFirst, NULL, IID_IShellFolder, (LPVOID*)pFolderLocal.GetPtr());

			if (SUCCEEDED(hr))
			{
				CShellItem itemRest(CShellItem::Next(pItem));
				hr = pFolder->GetAttributesOf(1, itemRest, &ulAttrs);
			}
		}
		else
		{
			hr = pFolder->GetAttributesOf(1, &pItem, &ulAttrs);
		}

		ATLASSERT(SUCCEEDED(hr));

		return ulAttrs;
	}

	inline HRESULT CShellFolder::BindToStorage(const CShellFolder& pFolder, const CShellItem& item, REFIID riid,
	                                          void** ppv) const
	{
		HRESULT hr = S_OK;

		if (item.Depth() > 1)
		{
			CShellItem itemFirst(item);

			if (!itemFirst.StripToFirst())
				return E_FAIL;

			CShellFolder pFolderLocal;
			hr = pFolder->BindToObject(itemFirst, NULL, IID_IShellFolder, (LPVOID*)pFolderLocal.GetPtr());

			if (SUCCEEDED(hr))
			{
				CShellItem itemRest(CShellItem::Next(item));
				hr = BindToStorage(pFolderLocal, itemRest, riid, ppv);
			}
		}
		else
		{
			hr = pFolder->BindToStorage(item, 0, riid, ppv);
		}

		return hr;
	}

	inline HRESULT CShellItem::BuildDisplayNameOf(LPTSTR pszBuf, UINT cchBuf) const
	{
		CShellDesktop pDesktop;

		if (IsDesktop())
			return pDesktop.GetDisplayNameOf(pszBuf, cchBuf, m_pItem, SHGDN_FORPARSING);

		return BuildDisplayNameOf(pszBuf, cchBuf, pDesktop, true);
	}

	inline HRESULT CShellItem::BuildDisplayNameOf(LPTSTR pszBuf, UINT cchBuf, CShellFolder& pFolder, bool bFirst) const
	{
		HRESULT hr = S_OK;
		DWORD dwFlags = (bFirst) ? SHGDN_FORPARSING : SHGDN_INFOLDER | SHGDN_FORPARSING;

		if (Depth() > 1)
		{
			CShellItem itemFirst;
			itemFirst.Copy(m_pItem);

			if (!itemFirst.StripToFirst())
				return E_FAIL;

			pFolder.GetDisplayNameOf(pszBuf, cchBuf, itemFirst, dwFlags);

			CShellFolder pFolderLocal;
			hr = pFolder->BindToObject(itemFirst, NULL, IID_IShellFolder, (LPVOID*)pFolderLocal.GetPtr());

			if (SUCCEEDED(hr))
			{
				_tcscat_s(pszBuf, cchBuf, _T("\\"));
				size_t n = _tcsclen(pszBuf);

				CShellItem itemRest(CShellItem::Next(m_pItem));
				hr = itemRest.BuildDisplayNameOf(pszBuf + n, static_cast<UINT>(cchBuf - n), pFolderLocal);

				if (FAILED(hr))
					hr = pFolder.GetDisplayNameOf(pszBuf + n, static_cast<UINT>(cchBuf - n), m_pItem, SHGDN_NORMAL);
			}
		}
		else
		{
			hr = pFolder.GetDisplayNameOf(pszBuf, cchBuf, m_pItem, dwFlags);

			if (FAILED(hr))
				hr = pFolder.GetDisplayNameOf(pszBuf, cchBuf, m_pItem, SHGDN_NORMAL);
		}

		return hr;
	}

	template <class TArchive>
	bool CShellItem::Write(LPCTSTR szValueName, const TArchive& pArchive) const
	{
		ATLASSERT(m_pItem != 0);
		ATLASSERT(CShellMalloc()->DidAlloc(m_pItem));

		::CString strPath = CShellDesktop().GetDisplayNameOf(*this, SHGDN_FORPARSING);
		return pArchive->Write(szValueName, strPath);
	}


	typedef CSimpleValArray<CShellItem> ShellItemList;

	template <class TProperties>
	void LoadShellItemList(ShellItemList& list, LPCTSTR szValueName, const TProperties& pArchive)
	{
		if (pArchive->StartSection(szValueName))
		{
			list.RemoveAll();

			::CString str;
			CShellItem item;
			unsigned int i = 0;
			DWORD nMax = 0;

			if (!pArchive->Read(_T("Count"), nMax))
				nMax = 0;

			while (i < nMax)
			{
				str.Format(_T("Item %d"), i);

				if (!item.Read(str, pArchive))
					break;

				list.Add(item);
				i++;
			}

			pArchive->EndSection();
		}
	}

	template <class TProperties>
	void SaveShellItemList(const ShellItemList& list, LPCTSTR szValueName, const TProperties& pArchive)
	{
		if (pArchive->StartSection(szValueName))
		{
			::CString str;
			pArchive->Write(_T("Count"), list.GetSize());

			for (int i = 0; i < list.GetSize(); i++)
			{
				str.Format(_T("Item %d"), i);
				list[i].Write(str, pArchive);
			}

			pArchive->EndSection();
		}
	}
} // namespace IW

// ArtMate's shell layer. The item, folder, desktop and enumerator wrappers are
// the shared ones in include/iw/shell.h; what stays here is what only 1.0 has.
//
// 1.0 used to carry its own copy with an MFC shape - CObject bases, an
// enumerator that derived from the item, a CShellReturnString scratch object and
// AfxGetMainWnd() baked into every call. All of that is gone; the owner window
// is now passed in, and display names come back as a CString.

#pragma once

#include "iw/shell.h"

namespace IW
{
	// Declared [[noreturn]] by iw/shell.h.
	[[noreturn]] inline void ShellApiFailed(LPCTSTR szApi, HRESULT hr)
	{
		TRACE("%s failed, hr=%lx\n", szApi, hr);
		Throw("shell call failed");
	}

	[[noreturn]] inline void ShellOutOfMemory()
	{
		ThrowOutOfMemory();
	}
}

using IW::CShellDesktop;
using IW::CShellDesktopItem;
using IW::CShellFolder;
using IW::CShellItem;
using IW::CShellItemEnum;

// The thumbnail map is keyed on this. A file item's PIDL carries its name, size
// and write time, so an edited file hashes to a new key and gets a new entry.
inline UINT ShellHashKey(const CShellItem& item)
{
	LPCITEMIDLIST p = item.GetItem();
	const BYTE* pBytes = p->mkid.abID;
	const int nSize = p->mkid.cb - sizeof(p->mkid.cb);

	UINT nHash = 0;

	for (int i = 0; i < nSize; i++)
		nHash = (nHash << 5) + nHash + pBytes[i];

	return nHash;
}

inline CString ShellItemType(const CShellItem& item)
{
	SHFILEINFO sfi;
	ZeroMemory(&sfi, sizeof(sfi));

	SHGetFileInfo((LPCTSTR)item.GetItem(), 0, &sfi, sizeof(sfi), SHGFI_PIDL | SHGFI_TYPENAME);

	return sfi.szTypeName;
}

// SFGAO flags for a *fully qualified* item. CShellFolder::GetAttributes cannot
// answer for one - for a pidl deeper than one level it binds to the first
// component and then asks the desktop about a pidl relative to that child - and
// its failure path leaves the requested mask in place, so everything looked
// like a folder except when it did not.
inline DWORD ShellItemAttributes(const CShellItem& item, DWORD dwMask)
{
	SHFILEINFO sfi;
	ZeroMemory(&sfi, sizeof(sfi));
	sfi.dwAttributes = dwMask;

	if (0 == SHGetFileInfo((LPCTSTR)item.GetItem(), 0, &sfi, sizeof(sfi),
	                       SHGFI_PIDL | SHGFI_ATTRIBUTES | SHGFI_ATTR_SPECIFIED))
		return 0;

	return sfi.dwAttributes;
}

inline HRESULT ShellParseDisplayName(const CShellFolder& folder, HWND hwndOwner,
                                     LPCTSTR szDisplayName, CShellItem& itemOut)
{
	USES_CONVERSION;

	LPITEMIDLIST pidl = nullptr;
	ULONG chEaten = 0;

	HRESULT hr = folder->ParseDisplayName(hwndOwner, nullptr, T2W(const_cast<LPTSTR>(szDisplayName)),
	                                      &chEaten, &pidl, nullptr);

	if (SUCCEEDED(hr))
		itemOut.Attach(pidl);

	return hr;
}

// The largest PIDL the ini is allowed to describe.
const UINT nMaxItemListBytes = 64 * 1024;

// A PIDL is a chain of SHITEMIDs ending in a zero cb. The ini is user-supplied,
// so the chain has to be walked against the buffer it arrived in before
// anything else follows mkid.cb.
inline BOOL IsValidItemList(const void* pBytes, UINT nSize)
{
	if (pBytes == nullptr || nSize < sizeof(USHORT) || nSize > nMaxItemListBytes)
		return FALSE;

	auto p = static_cast<const BYTE*>(pBytes);

	for (UINT nOffset = 0;;)
	{
		if (nOffset + sizeof(USHORT) > nSize)
			return FALSE;

		USHORT cb;
		CopyMemory(&cb, p + nOffset, sizeof(cb));

		if (cb == 0)
			return TRUE;

		if (cb < sizeof(SHITEMID) || nOffset + static_cast<UINT>(cb) > nSize)
			return FALSE;

		nOffset += cb;
	}
}

inline void ShellItemAttach(CShellItem& item, const void* pBytes, UINT nSize)
{
	auto pNew = static_cast<LPITEMIDLIST>(IW::CShellMalloc()->Alloc(nSize));

	if (pNew == nullptr)
		IW::ThrowOutOfMemory();

	CopyMemory(pNew, pBytes, nSize);
	item.Attach(pNew);
}

inline BOOL ShellItemProfileRead(CShellItem& item, LPCTSTR szSection, LPCTSTR szEntry)
{
	std::vector<BYTE> data;

	if (!Settings::GetBinary(szSection, szEntry, data) ||
		!IsValidItemList(data.data(), static_cast<UINT>(data.size())))
		return FALSE;

	ShellItemAttach(item, data.data(), static_cast<UINT>(data.size()));
	return TRUE;
}

inline void ShellItemProfileWrite(const CShellItem& item, LPCTSTR szSection, LPCTSTR szEntry)
{
	Settings::SetBinary(szSection, szEntry, (const BYTE*)item.GetItem(),
	                    CShellItem::GetSize(item.GetItem()));
}

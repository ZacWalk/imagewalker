#pragma once

#include <shlobj.h>
#include <shlwapi.h>
#include <algorithm>
#include <memory>
#include "SdkUi.h"

template<class T> struct ReleaseInterface
{
	void operator()(T* value) const { value->Release(); }
};
template<class T> using ShellPtr = std::unique_ptr<T, ReleaseInterface<T>>;

class CShellItem
{
public:
	CShellItem() = default;
	explicit CShellItem(LPCITEMIDLIST item)
	{
		if (item)
		{
			item_ = ::ILCloneFull(item);
			if (!item_) throw std::bad_alloc();
		}
	}
	CShellItem(const CShellItem& item) : CShellItem(item.GetItem()) {}
	CShellItem(CShellItem&& item) noexcept : item_(item.Detach()) {}
	~CShellItem() { ::CoTaskMemFree(item_); }
	CShellItem& operator=(CShellItem other) noexcept { std::swap(item_, other.item_); return *this; }
	operator LPCITEMIDLIST() const { return item_; }
	LPCITEMIDLIST GetItem() const { return item_; }
	bool IsNull() const { return !item_; }
	bool IsDesktop() const { return item_ && item_->mkid.cb == 0; }
	static UINT GetSize(LPCITEMIDLIST item) { return ::ILGetSize(item); }
	void Attach(LPITEMIDLIST item) { ::CoTaskMemFree(item_); item_ = item; }
	LPITEMIDLIST Detach() { return std::exchange(item_, nullptr); }
	void StripToParent() { if (item_) ::ILRemoveLastID(item_); }
	void Cat(const CShellItem& parent, const CShellItem& child)
	{
		const auto item = ::ILCombine(parent, child);
		if (!item) throw std::bad_alloc();
		Attach(item);
	}
	bool operator==(const CShellItem& other) const
	{
		return IsNull() || other.IsNull() ? item_ == other.item_ : ::ILIsEqual(item_, other.item_) != FALSE;
	}
	int Depth() const
	{
		int depth = 0;
		for (auto p = GetItem(); p && p->mkid.cb; p = ::ILGetNext(p)) ++depth;
		return depth;
	}
	bool Open(HWND owner, int folder)
	{
		LPITEMIDLIST item{};
		if (FAILED(::SHGetSpecialFolderLocation(owner, folder, &item)) || !item) return false;
		Attach(item);
		return true;
	}
	bool GetPath(CText& text) const
	{
		PWSTR path{};
		const auto hr = item_ ? ::SHGetNameFromIDList(item_, SIGDN_FILESYSPATH, &path) : E_INVALIDARG;
		const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(path, &CoTaskMemFree);
		text = SUCCEEDED(hr) && owned ? NarrowText(owned.get()) : CText{};
		return !text.IsEmpty();
	}
private:
	LPITEMIDLIST item_{};
};

struct CShellDesktopItem : CShellItem
{
	CShellDesktopItem() { Open(nullptr, CSIDL_DESKTOP); }
};

class CShellFolder
{
public:
	operator IShellFolder*() const { return folder_.get(); }
	IShellFolder* operator->() const { return folder_.get(); }
	HRESULT Open(const CShellItem& item)
	{
		if (item.IsNull()) return E_INVALIDARG;
		IShellFolder* raw{};
		const auto hr = ::SHGetDesktopFolder(&raw);
		if (FAILED(hr)) return hr;
		ShellPtr<IShellFolder> desktop(raw);
		if (item.IsDesktop())
		{
			folder_ = std::move(desktop);
			return S_OK;
		}
		raw = nullptr;
		const auto bound = desktop->BindToObject(item, nullptr, IID_PPV_ARGS(&raw));
		if (SUCCEEDED(bound)) folder_.reset(raw);
		return bound;
	}
	CText GetDisplayNameOf(const CShellItem& item, DWORD flags) const
	{
		STRRET name{};
		if (!folder_ || FAILED(folder_->GetDisplayNameOf(item, static_cast<SHGDNF>(flags), &name))) return {};
		PWSTR text{};
		const auto hr = ::StrRetToStrW(&name, item, &text);
		const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(text, &CoTaskMemFree);
		return SUCCEEDED(hr) && owned ? NarrowText(text) : CText{};
	}
	void ResolveLink(CShellItem& item) const
	{
		IShellFolder* raw{};
		PCUITEMID_CHILD child{};
		if (FAILED(::SHBindToParent(item, IID_PPV_ARGS(&raw), &child))) return;
		const ShellPtr<IShellFolder> parent(raw);
		IShellLinkW* shellLink{};
		if (FAILED(parent->GetUIObjectOf(nullptr, 1, &child, IID_IShellLinkW, nullptr,
		                                 reinterpret_cast<void**>(&shellLink)))) return;
		const ShellPtr<IShellLinkW> link(shellLink);
		LPITEMIDLIST target{};
		if (SUCCEEDED(link->GetIDList(&target)) && target) item.Attach(target);
	}
private:
	ShellPtr<IShellFolder> folder_;
};

struct CShellDesktop : CShellFolder
{
	CShellDesktop() { Open(CShellDesktopItem()); }
};

class CShellItemEnum
{
public:
	explicit operator bool() const { return bool(items_); }
	IEnumIDList* operator->() const { return items_.get(); }
	HRESULT Create(HWND owner, IShellFolder* folder, UINT flags)
	{
		IEnumIDList* raw{};
		const auto hr = folder ? folder->EnumObjects(owner, static_cast<SHCONTF>(flags), &raw) : E_INVALIDARG;
		if (SUCCEEDED(hr)) items_.reset(raw);
		return hr;
	}
private:
	ShellPtr<IEnumIDList> items_;
};

namespace IW
{
	template<class Open>
	bool OpenStartupFolder(HWND owner, const CShellItem& remembered, Open open)
	{
		if (!remembered.IsNull() && open(remembered)) return true;
		CShellItem item;
		if (item.Open(owner, CSIDL_MYPICTURES) && open(item)) return true;
		if (item.Open(owner, CSIDL_PERSONAL) && open(item)) return true;
		return open(CShellDesktopItem());
	}
}

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
	const auto wide = WideText(szDisplayName);
	if (wide.empty() || !static_cast<IShellFolder*>(folder)) return E_INVALIDARG;
	std::wstring name(wide);
	LPITEMIDLIST pidl{};
	ULONG chEaten = 0;
	const HRESULT hr = folder->ParseDisplayName(hwndOwner, nullptr, name.data(), &chEaten, &pidl, nullptr);

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
	auto pNew = static_cast<LPITEMIDLIST>(::CoTaskMemAlloc(nSize));

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

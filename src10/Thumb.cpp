// The folder model: enumerate and sort.

#include "stdafx.h"
#include "Thumb.h"

void CThumb::Refresh(CShellFolder& folder)
{
	m_ulAttribs = SFGAO_LINK | SFGAO_FOLDER | SFGAO_FILESYSTEM | SFGAO_GHOSTED | SFGAO_DROPTARGET;

	auto item = GetItem();
	if (FAILED(folder->GetAttributesOf(1, &item, &m_ulAttribs)))
		m_ulAttribs = 0;

	if (!(m_ulAttribs & SFGAO_FILESYSTEM))
		return;

	WIN32_FIND_DATA findFileData;

	if (FAILED(SHGetDataFromIDList(folder, *this, SHGDFIL_FINDDATA,
	                               &findFileData, sizeof(findFileData))))
		return;

	m_timeFile = findFileData.ftLastWriteTime;
	m_sizeFile = (static_cast<LONGLONG>(findFileData.nFileSizeHigh) << 32) |
		findFileData.nFileSizeLow;

	m_uExtension = 0;

	if (const char* szType = strrchr(findFileData.cFileName, '.'))
	{
		char sz[4] = {0};
		strncpy(sz, szType + 1, sizeof(sz) - 1);
		_strlwr_s(sz);

		for (int i = 0; i < 3; i++)
			m_uExtension = (m_uExtension << 8) + sz[i];
	}
}

int CThumb::CompareName(const CShellFolder& folder, CThumb& a, CThumb& b)
{
	// CompareIDs puts a signed short in the HRESULT's code field: 0xFFFF is "before".
	return static_cast<short>(SCODE_CODE(folder->CompareIDs(0, a, b)));
}

int CThumb::CompareSize(const CShellFolder& folder, CThumb& a, CThumb& b)
{
	const int i = (a.m_sizeFile < b.m_sizeFile) ? -1 : (a.m_sizeFile > b.m_sizeFile) ? 1 : 0;

	if (i == 0 || a.IsFolder() || b.IsFolder())
		return CompareName(folder, a, b);

	return i;
}

int CThumb::CompareType(const CShellFolder& folder, CThumb& a, CThumb& b)
{
	const int i = static_cast<int>(a.m_uExtension) - static_cast<int>(b.m_uExtension);

	if (i == 0 || a.IsFolder() || b.IsFolder())
		return CompareName(folder, a, b);

	return i;
}

int CThumb::CompareDate(const CShellFolder& folder, CThumb& a, CThumb& b)
{
	const int i = CompareFileTime(&(a.m_timeFile), &(b.m_timeFile));

	if (i == 0 || a.IsFolder() || b.IsFolder())
		return CompareName(folder, a, b);

	return i;
}

CFolder::CFolder(HWND hwndOwner, const CShellItem& item)
	: m_Item(item), m_hwndOwner(hwndOwner)
{
	const HRESULT hr = Open(item);

	if (FAILED(hr))
		IW::Throw("cannot open folder");

	if (!Refresh())
		IW::Throw("cannot read folder");
}

UINT CFolder::FindItemKey(const std::map<UINT, std::unique_ptr<CThumb>>& thumbs, const CShellItem& item)
{
	UINT key = ShellHashKey(item);
	const UINT size = CShellItem::GetSize(item.GetItem());
	for (;; ++key)
	{
		const auto found = thumbs.find(key);
		if (found == thumbs.end())
			return key;
		const auto& existing = *found->second;
		if (CShellItem::GetSize(existing.GetItem()) == size &&
			memcmp(existing.GetItem(), item.GetItem(), size) == 0)
			return key;
	}
}

bool CFolder::Refresh()
{
	for (auto& entry : m_mapThumbs)
		entry.second->m_bDelete = TRUE;

	CShellItemEnum enumitem;

	if (FAILED(enumitem.Create(m_hwndOwner, *this,
	                           SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN)))
		return false;

	LPITEMIDLIST pItemRaw = nullptr;
	ULONG ulFetched = 0;
	CShellItem itemEnum;

	HRESULT next = S_FALSE;
	while (enumitem && (next = enumitem->Next(1, &pItemRaw, &ulFetched)) == S_OK)
	{
		itemEnum.Attach(pItemRaw);

		const UINT uKey = FindItemKey(m_mapThumbs, itemEnum);
		auto it = m_mapThumbs.find(uKey);

		if (it == m_mapThumbs.end())
		{
			auto pThumb = std::make_unique<CThumb>();
			pThumb->Attach(itemEnum.Detach());
			pThumb->Refresh(*this);

			it = m_mapThumbs.emplace(uKey, std::move(pThumb)).first;
		}

		it->second->m_bDelete = FALSE;
	}

	if (FAILED(next))
		return false;

	for (auto it = m_mapThumbs.begin(); it != m_mapThumbs.end();)
		it = it->second->m_bDelete ? m_mapThumbs.erase(it) : std::next(it);
	return true;
}

BOOL CFolder::GetPath(CString& strPath) const
{
	if (!m_Item.GetPath(strPath))
		return FALSE;

	if (strPath.Right(1) != "\\")
		strPath += "\\";

	return TRUE;
}

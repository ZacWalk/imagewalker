// The folder model: enumerate and sort.

#include "stdafx.h"
#include "Thumb.h"

void CThumb::Refresh(CShellFolder& folder)
{
	m_ulAttribs = SFGAO_LINK | SFGAO_FOLDER | SFGAO_FILESYSTEM | SFGAO_GHOSTED | SFGAO_DROPTARGET;

	folder->GetAttributesOf(1, *this, &m_ulAttribs);

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
		strlwr(sz);

		for (int i = 0; i < 3; i++)
			m_uExtension = (m_uExtension << 8) + sz[i];
	}
}

int CThumb::CompareName(const CShellFolder& folder, CThumb& a, CThumb& b)
{
	return SCODE_CODE(folder->CompareIDs(0, a, b));
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

	Refresh();
}

void CFolder::Refresh()
{
	for (auto& entry : m_mapThumbs)
		entry.second->m_bDelete = TRUE;

	CShellItemEnum enumitem;

	if (FAILED(enumitem.Create(m_hwndOwner, *this,
	                           SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN)))
		return;

	LPITEMIDLIST pItemRaw = nullptr;
	ULONG ulFetched = 0;
	CShellItem itemEnum;

	while (enumitem->Next(1, &pItemRaw, &ulFetched) == S_OK)
	{
		itemEnum.Attach(pItemRaw);

		const UINT uKey = ShellHashKey(itemEnum);
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

	for (auto it = m_mapThumbs.begin(); it != m_mapThumbs.end();)
		it = it->second->m_bDelete ? m_mapThumbs.erase(it) : std::next(it);
}

BOOL CFolder::GetPath(CString& strPath) const
{
	LPTSTR psz = strPath.GetBuffer(MAX_PATH);
	const BOOL b = SHGetPathFromIDList(m_Item, psz);
	strPath.ReleaseBuffer();

	if (!b)
		return FALSE;

	if (strPath.Right(1) != _T('\\'))
		strPath += _T('\\');

	return TRUE;
}

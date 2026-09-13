#pragma once

// One folder's worth of items, and the thumbnail each carries.
//
// The map is keyed on the shell item hash rather than on an index, because a
// refresh renumbers everything and a background decode has to name the item it
// was started for.

#include "shell.h"
#include "Dib.h"
#include "dibthumb.h"

#include <map>
#include <memory>

#define THUMB_PADDING 2

#define THUMB_X (IMAGE_X + (THUMB_PADDING * 4))
#define THUMB_Y (IMAGE_Y + (THUMB_PADDING * 4))

class CThumb : public CShellItem
{
public:
	CThumb() = default;

	explicit CThumb(const CShellItem& item) : CShellItem(item)
	{
	}

	void Refresh(CShellFolder& folder);

	FILETIME m_timeFile{0, 0};
	LONGLONG m_sizeFile = 0;
	ULONG m_ulAttribs = 0;
	BOOL m_bOpen = FALSE;
	BOOL m_bDelete = FALSE;
	UINT m_uExtension = 0;
	CDib m_Dib;

	bool IsFolder() const { return (m_ulAttribs & SFGAO_FOLDER) != 0; }

	bool IsLoadable() const
	{
		return (m_ulAttribs & SFGAO_FILESYSTEM) && !(m_ulAttribs & SFGAO_FOLDER);
	}

	static int CompareDate(const CShellFolder& folder, CThumb& a, CThumb& b);
	static int CompareType(const CShellFolder& folder, CThumb& a, CThumb& b);
	static int CompareName(const CShellFolder& folder, CThumb& a, CThumb& b);
	static int CompareSize(const CShellFolder& folder, CThumb& a, CThumb& b);
};

class CFolder : public CShellFolder
{
public:
	CFolder(HWND hwndOwner, const CShellItem& item);

	CFolder(const CFolder&) = delete;
	CFolder& operator=(const CFolder&) = delete;

	BOOL GetPath(CString& strPath) const;
	void Refresh();

	std::map<UINT, std::unique_ptr<CThumb>> m_mapThumbs;

	CShellItem m_Item;
	HWND m_hwndOwner;
};

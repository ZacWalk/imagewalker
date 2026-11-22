#pragma once

// One folder's worth of items, and the thumbnail each carries.
//
// The map uses collision-resolved shell item hashes rather than indices: a
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

inline CRect ThumbnailImageRect(const CRect& cell, CSize size, int edge = IMAGE_X)
{
	const CRect box(cell.TopLeft() + CSize(2 * THUMB_PADDING, 2 * THUMB_PADDING), CSize(edge, edge));
	if (size.cx > box.Width() || size.cy > box.Height())
		size = FitToBox(size, box.Size());
	return CRect(CPoint(box.left + (box.Width() - size.cx) / 2, box.top + (box.Height() - size.cy) / 2), size);
}

struct CThumbnailGrid
{
	enum { minEdge = 64, maxEdge = 256 };
	int edge = IMAGE_X;
	int columns = 1;
	int left = 0;
	int rowHeight = THUMB_Y + 18;

	int CellWidth() const { return edge + 4 * THUMB_PADDING; }

	void Layout(int width, int count, int size, int captionHeight)
	{
		edge = min(size, max(1, width - 4 * THUMB_PADDING));
		rowHeight = CellWidth() + captionHeight;
		columns = max(1, min(count, width / CellWidth()));
		left = max(0, (width - columns * CellWidth()) / 2);
	}

	CRect Cell(int index) const
	{
		return CRect(CPoint(left + (index % columns) * CellWidth(), (index / columns) * rowHeight),
		             CSize(CellWidth(), rowHeight));
	}

	int Hit(CPoint point, int count) const
	{
		if (point.x < left || point.x >= left + columns * CellWidth() ||
			point.y < 0 || count == 0 || point.y / rowHeight > (count - 1) / columns)
			return -1;
		const int index = (point.y / rowHeight) * columns + (point.x - left) / CellWidth();
		return index < count ? index : -1;
	}
};

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
	int m_nDecodedEdge = 0;
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
	bool Refresh();

	static UINT FindItemKey(const std::map<UINT, std::unique_ptr<CThumb>>& thumbs, const CShellItem& item);

	std::map<UINT, std::unique_ptr<CThumb>> m_mapThumbs;

	CShellItem m_Item;
	HWND m_hwndOwner;
};

// ImageWalker by Zac Walker
//
// Purpose: Where each thumbnail goes and how it is drawn, for the thumbnail,
//          list and detail layouts.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "ViewLayout.h"

template<class TParent>
class FolderLayout
{
public:

	TParent &_parent;

	CSize _sizeThumb;
	CSize _sizeThumbImage;
	CSize _sizeThumbCenter;

	int _nTextHeight;	


	enum { sizeFolderOffsetX = 10,
		sizeFolderOffsetY = 4 };

	FolderLayout(TParent &parent) : _parent(parent), _sizeThumb(1,1)
	{
	}

	virtual void Init()
	{
		const CSize sizeThumbDefault = _parent.GetThumbnailSize();

		_sizeThumb.cx = sizeThumbDefault.cx + (THUMB_PADDING * 4);
		_sizeThumb.cy = sizeThumbDefault.cy + (THUMB_PADDING * 4);
		_sizeThumbImage = sizeThumbDefault;
		_nTextHeight = App.Settings.m_annotations.GetSize() * App.m_nTextExtent;
		_sizeThumb.cy += _nTextHeight;
		_sizeThumbCenter.cx = _sizeThumb.cx / 2;
		_sizeThumbCenter.cy = (_sizeThumb.cy - _nTextHeight) /2;

		if (_nTextHeight > 0)
		{
			// introduce a little overlap
			_sizeThumb.cy -= App.m_nTextExtent;
		}
	}

	virtual void DoSize()
	{
		const int nOldThumbsY = _parent._nThumbsY;

		SetScrollSizeList(true);

		CPoint pointOffset = _parent.GetScrollOffset();
		const int nFirstThumb = ClosestThumbFromPoint(pointOffset);
		const int nThumbCount = _parent.GetItemCount();

		if (nThumbCount > 0)
		{
			CPoint pointThumb = GetThumbPoint(nFirstThumb);
			
			if (_parent._nThumbsY != nOldThumbsY)
			{
				pointOffset = pointThumb;
			}

			_parent.SetScrollOffset(pointOffset);
		}
	}

	virtual void SetScrollSizeList(bool bChangeOffset)
	{
		const CPoint pointOffset = _parent.GetScrollOffset();
		const CSize sizeClient = GetClientSize();
		const CPoint pointOrigin = _parent.GetScreenOrigin();

		int cx = sizeClient.cx;
		int cy = sizeClient.cy;
		int x = pointOffset.x;
		int y = pointOffset.y;

		const CSize sizeThumb = _sizeThumb;
		const int nThumbCount = _parent.GetItemCount();

		if (nThumbCount > 0)
		{
			int cxAvailable = cx;

			int nThumbsX = (cxAvailable < sizeThumb.cx) ? 1 : (cxAvailable / sizeThumb.cx);
			int nThumbsY = ((nThumbCount - 1) / nThumbsX) + 1;
			x = ((nThumbsX * sizeThumb.cx) - cx) / 2;

			cx = (nThumbsX * sizeThumb.cx) + pointOrigin.x;
			cy = (nThumbsY * sizeThumb.cy) + pointOrigin.y;		

			_parent._nThumbsX = nThumbsX;
			_parent._nThumbsY = nThumbsY;
		}

		_parent.SetScrollSize(IW::LowerLimit<1>(cx), IW::LowerLimit<1>(cy), true, bChangeOffset);

		// SetScrollSize resets both to a fraction of the total height.
		_parent.SetScrollLine(0, sizeThumb.cy);
		_parent.SetScrollPage(0, IW::LowerLimit<1>(sizeClient.cy - sizeThumb.cy));

		if (bChangeOffset)
		{
			_parent.SetScrollOffset(x, y);
		}
	}

	int ThumbColumnCount() const { return _parent._nThumbsX; }
	const CSize GetThumbSize() const { return _sizeThumb; }
	const CSize GetClientSize() const { return _parent.GetClientSize(); }	

	// Keyboard stepping asks the layout, because a layout that does not show
	// every item cannot be walked by adding one to the index.
	virtual bool IsSingleRow() const { return false; }
	virtual int StepItem(int nItem, int nStep) const { return nItem + nStep; }
	virtual int NearestItem(int nItem) const { return nItem; }

	virtual void HoverChanged(int nHover)
	{
	}

	virtual void OnTimer()
	{
	}

	virtual CPoint GetThumbPoint(const int nThumb) const
	{
		const CPoint pointOrigin = _parent.GetScreenOrigin();
		const CSize sizeThumb = GetThumbSize();
		const int nThumbColumnCount = ThumbColumnCount();

		int nCol = (nThumb % nThumbColumnCount);
		int nRow = (nThumb / nThumbColumnCount);

		return CPoint(pointOrigin.x + (nCol * sizeThumb.cx), pointOrigin.y + (nRow * sizeThumb.cy));
	}

	int ThumbFromPoint(const CPoint &pt) const { return ThumbFromPoint(pt.x, pt.y); };
	int ClosestThumbFromPoint(const CPoint &pt) const { return ClosestThumbFromPoint(pt.x, pt.y); };	

	virtual void UnionTextRectIfFocusItem(int nThumb, CRect &rectThumb) const
	{
		const int nFocusItem = _parent.GetFocusItem();

		if (nThumb == nFocusItem)
		{
			CString str;
			_parent.GetThumbText(str, nThumb, true);

			CRect rectText = GetTextRect(nThumb, str);
			UnionRect(&rectThumb, &rectThumb, &rectText);
		}
	}

	virtual int ClosestThumbFromPoint(int x, int y) const
	{
		const CPoint pointOrigin = _parent.GetScreenOrigin();
		const CSize sizeThumb = GetThumbSize();
		const int nThumbCount = _parent.GetItemCount();
		const int nThumbColumnCount = ThumbColumnCount();

		x -= pointOrigin.x;
		y -= pointOrigin.y;

		int i = IW::Clamp(x / sizeThumb.cx, 0, nThumbColumnCount - 1);
		i += (y / sizeThumb.cy) * nThumbColumnCount;

		int nMaxThumb = nThumbCount - 1;
		return IW::Clamp(i, 0, nMaxThumb);
	}

	virtual int ThumbFromPoint(int x, int y) const
	{
		const CPoint pointOrigin = _parent.GetScreenOrigin();
		const CSize sizeThumb = GetThumbSize();
		const int nThumbCount = _parent.GetItemCount();
		const int nThumbColumnCount = ThumbColumnCount();

		x -= pointOrigin.x;
		y -= pointOrigin.y;

		if (x < 0 || x >= nThumbColumnCount * sizeThumb.cx)
			return -1;

		int i = ((x / sizeThumb.cx) + ((y / sizeThumb.cy) * nThumbColumnCount));
		return ((i < 0) || (i >= nThumbCount)) ? -1 : i;
	}

	virtual CRect GetThumbRect(int nThumb) const
	{
		CRect rectThumb(GetThumbPoint(nThumb), GetThumbSize());
		UnionTextRectIfFocusItem(nThumb, rectThumb);
		return rectThumb;
	}

	virtual CRect GetTextRect(int i, LPCTSTR sz) const
	{
		const CSize sizeThumb = GetThumbSize();
		const CPoint pointThumb = GetThumbPoint(i);

		const CRect rectIn(0, 0, sizeThumb.cx - (THUMB_PADDING * 2), 0);
		CRect rectOut = IW::Style::MeasureString(sz, rectIn, IW::Style::Font::Standard, IW::Style::Text::SelectedThumbnail);

		rectOut.OffsetRect(
			pointThumb.x + (sizeThumb.cx - rectOut.Width()) / 2, 
			pointThumb.y + sizeThumb.cy - (_nTextHeight + THUMB_PADDING));

		return rectOut;
	}
	
	virtual void DrawItemText(IW::CRender &render, int nItem, const IW::FolderItemAttributes &attributes, const CRect &rectThumb, bool bFocus, LPCTSTR szName, const CRect &rectAlpha, const COLORREF clrText, const COLORREF clrBG)
	{
		if (szName && szName[0] != 0)
		{
			CRect rectText(
				rectThumb.left + THUMB_PADDING, 
				rectThumb.bottom - (_nTextHeight + THUMB_PADDING), 
				rectThumb.right - THUMB_PADDING, 
				rectThumb.bottom); 

			if (bFocus)
			{
				rectText.bottom = rectText.top + 10000;
			}

			const bool bDrawImage = attributes.bIsImage;
			const bool bIsFolder = attributes.bIsFolder;
			CRect r;

			if (bDrawImage && bIsFolder)
			{
				render.Blend(clrBG, rectText);
			}
			else if (r.IntersectRect(&rectAlpha, rectText))
			{
				render.Blend(clrBG, r);
			}

			IW::Style::Text::Style style = bFocus ? IW::Style::Text::SelectedThumbnail : IW::Style::Text::Thumbnail;
			render.DrawString(szName, rectText, IW::Style::Font::Standard, style, clrText);
		}
	}
	
	virtual void DrawItemMarkers(IW::CRender &render, const IW::FolderItemAttributes &attributes, const CRect &rectThumb, const COLORREF clrText, const COLORREF clrBG)
	{
		CRect rectMarkerText(rectThumb);
		rectMarkerText.DeflateRect(THUMB_PADDING, THUMB_PADDING);

		bool bShowMarkers = App.Settings.m_bShowMarkers;
		bool bSmallMode = IW::Min(rectMarkerText.Width(), rectMarkerText.Height()) < 40;

		if (!bSmallMode && bShowMarkers)
		{
			if (attributes.HasAttribute())
			{
				const IW::Style::Text::Style markersTextStyle = IW::Style::Text::NormalRight;
				const CString strMarkers = attributes.ToString();
				CRect rectMarkerTextOut = render.MeasureString(strMarkers, rectMarkerText, IW::Style::Font::Small, markersTextStyle);

				rectMarkerTextOut.left = rectMarkerText.right - rectMarkerTextOut.Width();
				rectMarkerTextOut.right = rectMarkerText.right;

				CRect rectMarkerTextBG(rectMarkerTextOut);
				rectMarkerTextBG.InflateRect(THUMB_PADDING, THUMB_PADDING);

				render.Blend(clrBG, rectMarkerTextBG);
				render.DrawString(strMarkers, rectMarkerTextOut, IW::Style::Font::Small, markersTextStyle, IW::Emphasize(clrText));
			}
		}
	}

	// Per item, because the detail layout's rows are not all the same height.
	virtual CSize GetThumbSizeForItem(int /*nItem*/) const { return _sizeThumb; }
	virtual CSize GetThumbCenterForItem(int /*nItem*/) const { return _sizeThumbCenter; }

	virtual void AdjustThumbRect(CRect &rectThumb, CPoint &pointThumb, const CRect &rectClip, bool bFocus, int nItem)
	{
		const CPoint pointOffset = _parent.GetScrollOffset();
		const CSize sizeClient = GetClientSize();
		const CSize sizeThumb = _sizeThumb;

		pointThumb.x -= pointOffset.x;
		pointThumb.y -= pointOffset.y;		

		rectThumb.left = pointThumb.x;
		rectThumb.top = pointThumb.y;
		rectThumb.right = pointThumb.x + sizeThumb.cx;
		rectThumb.bottom = pointThumb.y + sizeThumb.cy;
	}

	

	void DrawThumb(IW::CRender &render, IW::Folder &folder, int nItem, const CRect &rectClip)
	{
		CRect  rectThumb;

		CPoint pointThumb = GetThumbPoint(nItem);
		const CPoint pointOffset = _parent.GetScrollOffset();	
		const CSize sizeThumb = GetThumbSizeForItem(nItem);

		// If this thumb is selected we draw the selected rect 
		const int nFocusItem = folder.GetFocusItem();
		const bool bFocus = (nItem == nFocusItem);
		const bool bHighLight = (nItem == _parent._nDragOverItem);
		const bool bIsSelected = folder.IsItemSelected(nItem);		

		AdjustThumbRect(rectThumb, pointThumb, rectClip, bFocus, nItem);

		// One fetch for the whole item: this takes the folder lock and resolves the
		// page to draw, and the three draw helpers below all used to do it separately.
		IW::FolderItemAttributes attributes;
		folder.GetItemAttributes(nItem, attributes, true);

		CString str;		
		folder.GetItemFormatText(nItem, str, App.Settings.m_annotations, true);

		CRect rectBack = GetThumbRect(nItem);
		rectBack.OffsetRect(-pointOffset);

		COLORREF clrText = 0;
		COLORREF clrBG = IW::Style::Color::HighlightText;

		// In the drag drop
		if (bHighLight)
		{
			clrBG = IW::Style::Color::Highlight;
			//render.Fill(clrBG, &rectBack);
			render.Fill(IW::Emphasize(IW::SwapRB(clrBG)), rectBack);
			clrText = IW::Style::Color::HighlightText;
		}
		else if (bIsSelected) // Selected?
		{
			clrBG = IW::Style::Color::Highlight;

			if (bFocus)
			{
				render.Fill(clrBG, rectBack);
			}
			else
			{
				render.Fill(IW::Average(IW::Style::Color::Highlight, IW::Style::Color::Window), rectBack);
			}

			clrText = IW::Style::Color::HighlightText;
		}
		else
		{
			clrBG = _parent.GetBackGroundColor();
			clrText = _parent.GetTextColor();
		}

		CRect rectAlpha(0,0,0,0);

		// Render Image
		DrawThumbnail(
			render, 
			nItem, 
			attributes, 
			pointThumb, 
			sizeThumb, 
			_sizeThumbImage, 
			GetThumbCenterForItem(nItem), 
			rectAlpha);

		DrawItemText(
			render, 
			nItem, 
			attributes, 
			rectThumb, 
			bFocus,
			str,
			rectAlpha,
			clrText, 
			clrBG);

		DrawItemMarkers(
			render, 
			attributes, 
			rectThumb, 
			clrText, 
			clrBG);

		if (bFocus)
		{
			//render.DrawFocusRect(rectBack);
		}
	}


	void DrawThumbnail(IW::CRender &render, const int nItem, IW::FolderItemAttributes &attributes, const CPoint &pointThumb, const CSize &sizeThumb, const CSize &sizeThumbImageCurrent, const CSize &sizeThumbCenter, CRect &rectAlpha)
	{
		CPoint pointCenterImage;
		pointCenterImage.x = pointThumb.x + sizeThumbCenter.cx;
		pointCenterImage.y = pointThumb.y + sizeThumbCenter.cy;

		bool bDrawAsImage = (attributes.bIsImage || attributes.bIsImageIcon) && !attributes.page.empty();	

		// Draw the bitmap
		if (bDrawAsImage)
		{
			DrawImage(render,
				attributes, 
				pointCenterImage, 
				sizeThumbImageCurrent,
				rectAlpha,
				nItem == _parent.GetHoverItem());
		}

		bool bSmallMode = IW::Min(sizeThumb.cx, sizeThumb.cy) < 40;

		if (!bDrawAsImage || attributes.bIsFolder)
		{
			int cxy = bSmallMode ? 16 : 32;		
			CPoint pointDib(pointCenterImage - CSize(cxy/2, cxy/2));

			if (attributes.bIsImageIcon)
			{
				pointDib.x -= sizeFolderOffsetX;
				pointDib.y -= sizeFolderOffsetY;
			}

			if (attributes.nImage == -1)
			{
				HICON hIcon = attributes.bIsFolder ? IW::Style::Icon::Folder : IW::Style::Icon::Default;
				render.DrawIcon(hIcon, pointDib.x, pointDib.y, cxy, cxy);
			}
			else
			{
				render.DrawImageList(App.GetShellImageList(false), attributes.nImage, pointDib.x,  pointDib.y, cxy, cxy);
			}
		}	
	}


	virtual void DrawImage(IW::CRender &render, IW::FolderItemAttributes& attributes, const CPoint &pointCenterImage, const CSize &sizeThumbImageCurrent, CRect& rectAlpha, bool bHover)
	{
		const IW::Page &page = attributes.page;

		// Image Scaled correctly?
		// We only work if the thumb is larger
		// than the current settings
		const CRect rectBounding = attributes.rectBounding;

		if ((int)rectBounding.Width() > sizeThumbImageCurrent.cx ||
			(int)rectBounding.Height() > sizeThumbImageCurrent.cy)
		{
			long icx = rectBounding.Width();
			long icy = rectBounding.Height();
			long nDiv = 0x1000;


			// Scale the image
			long sh = MulDiv(sizeThumbImageCurrent.cx, nDiv, icx);
			long sw = MulDiv(sizeThumbImageCurrent.cy, nDiv, icy);

			long s =  IW::Min(sh, sw);

			CSize sizeImage;
			sizeImage.cx = MulDiv(page.GetWidth(), s, nDiv);
			sizeImage.cy = MulDiv(page.GetHeight(), s, nDiv);

			// Image offset 
			CPoint pointOffset = page.GetOffset();
			pointOffset.x = MulDiv(pointOffset.x, s, nDiv);
			pointOffset.y = MulDiv(pointOffset.y, s, nDiv);

			// Draw the image
			int oy = pointCenterImage.y - (sizeImage.cy / 2) + pointOffset.y;
			int ox = pointCenterImage.x - (sizeImage.cx / 2) + pointOffset.x;

			if (attributes.bIsFolder)
			{
				ox += sizeFolderOffsetX;
				oy += sizeFolderOffsetY;
			}

			// Draw the image
			CRect rectImage(ox, oy, ox + sizeImage.cx, oy + sizeImage.cy);
			render.DrawImage(page, rectImage);

			rectAlpha = rectImage;
		}
		else
		{
			CPoint pointOffset = page.GetOffset();

			int ox = pointCenterImage.x + pointOffset.x - (rectBounding.Width() / 2);
			int oy = pointCenterImage.y + pointOffset.y - (rectBounding.Height() / 2);

			if (attributes.bIsFolder)
			{
				ox += sizeFolderOffsetX;
				oy += sizeFolderOffsetY;
			}

			render.DrawImage(page, ox, oy);

			// May need to draw alpha block for text
			rectAlpha.left = ox;
			rectAlpha.top = oy;              
			rectAlpha.right = ox  + page.GetWidth();
			rectAlpha.bottom = oy + page.GetHeight();
		}
	}

	virtual void MakeItemVisible(int nThumb)
	{
		const CRect rc = GetThumbRect(nThumb);
		CPoint pointOffset = _parent.GetScrollOffset();
		const CSize sizeClient = GetClientSize();

		if (rc.top <  pointOffset.y)
		{
			pointOffset.y = rc.top;
		}
		else if (rc.bottom > (pointOffset.y + sizeClient.cy))
		{
			pointOffset.y = rc.bottom - sizeClient.cy;
		}

		if (rc.left < pointOffset.x)
		{
			pointOffset.x = rc.left;
		}
		else if (rc.right > (pointOffset.x + sizeClient.cx))
		{
			pointOffset.x = rc.right - sizeClient.cx;
		}

		_parent.SetScrollOffset(pointOffset);
		_parent.UpdateBars();
	}
};


template<class TParent>
class FolderLayoutNormal : public FolderLayout<TParent>
{
public:
	typedef FolderLayout<TParent> BaseClass;
	typedef FolderLayoutNormal<TParent> ThisClass;

	FolderLayoutNormal(TParent &parent) : BaseClass(parent)
	{
	}
};


// The full screen filmstrip, following src30: one row, and only the items that
// can actually be displayed. Folders and non-images keep their index -- the
// paint loop walks an index range -- but are parked outside the strip so they
// clip away, and the slot map is what decides horizontal position.
template<class TParent>
class FolderLayoutStrip : public FolderLayout<TParent>
{
public:
	typedef FolderLayout<TParent> BaseClass;

	std::vector<int> _slotOfItem;
	std::vector<int> _itemOfSlot;

	FolderLayoutStrip(TParent &parent) : BaseClass(parent)
	{
	}

	void BuildSlots()
	{
		const int nCount = _parent.GetItemCount();

		_slotOfItem.assign(nCount, -1);
		_itemOfSlot.clear();

		IW::FolderPtr pFolder = _parent.GetFolder();

		for (int i = 0; i < nCount; i++)
		{
			if (pFolder->IsItemImage(i))
			{
				_slotOfItem[i] = static_cast<int>(_itemOfSlot.size());
				_itemOfSlot.push_back(i);
			}
		}
	}

	int SlotCount() const { return static_cast<int>(_itemOfSlot.size()); }

	void Init() override
	{
		BaseClass::Init();
		BuildSlots();
	}

	void SetScrollSizeList(bool bChangeOffset) override
	{
		BuildSlots();

		const CSize sizeThumb = _sizeThumb;
		const CPoint pointOrigin = _parent.GetScreenOrigin();
		const int nSlots = SlotCount();

		_parent._nThumbsX = IW::Max(1, nSlots);
		_parent._nThumbsY = 1;

		const int cx = (IW::Max(1, nSlots) * sizeThumb.cx) + pointOrigin.x;

		// One row means there is nothing to scroll vertically; asking for the
		// real height would leave a scroll bar that can never move.
		_parent.SetScrollLine(sizeThumb.cx, 1);
		_parent.SetScrollSize(IW::LowerLimit<1>(cx), 1, true, bChangeOffset);
		_parent.SetScrollPage(IW::LowerLimit<1>(GetClientSize().cx - sizeThumb.cx), 1);
	}

	CPoint GetThumbPoint(const int nThumb) const override
	{
		const CPoint pointOrigin = _parent.GetScreenOrigin();
		const CSize sizeThumb = GetThumbSize();

		const int nSlot = (nThumb >= 0 && nThumb < static_cast<int>(_slotOfItem.size()))
			                  ? _slotOfItem[nThumb]
			                  : -1;

		if (nSlot < 0)
			return CPoint(pointOrigin.x, pointOrigin.y + (sizeThumb.cy * 4));

		return CPoint(pointOrigin.x + (nSlot * sizeThumb.cx), pointOrigin.y);
	}

	int ItemFromSlot(int nSlot) const
	{
		if (_itemOfSlot.empty())
			return -1;

		nSlot = IW::Clamp(nSlot, 0, static_cast<int>(_itemOfSlot.size()) - 1);
		return _itemOfSlot[nSlot];
	}

	bool IsSingleRow() const override { return true; }

	// The strip only holds the images, so a step is a step along the slot map;
	// stepping the index would land on a folder parked off the strip.
	int StepItem(int nItem, int nStep) const override
	{
		const int nSlot = (nItem >= 0 && nItem < static_cast<int>(_slotOfItem.size()))
			                  ? _slotOfItem[nItem]
			                  : -1;

		if (nSlot < 0)
			return NearestItem(nItem);

		const int nNext = nSlot + nStep;

		if (nNext < 0 || nNext >= SlotCount())
			return nItem;

		return _itemOfSlot[nNext];
	}

	int NearestItem(int nItem) const override
	{
		if (_itemOfSlot.empty())
			return nItem;

		if (nItem >= 0 && nItem < static_cast<int>(_slotOfItem.size()) && _slotOfItem[nItem] >= 0)
			return nItem;

		for (int i = 0; i < static_cast<int>(_itemOfSlot.size()); i++)
		{
			if (_itemOfSlot[i] >= nItem)
				return _itemOfSlot[i];
		}

		return _itemOfSlot.back();
	}

	int ClosestThumbFromPoint(int x, int y) const override
	{
		const CPoint pointOrigin = _parent.GetScreenOrigin();
		const int nItem = ItemFromSlot((x - pointOrigin.x) / GetThumbSize().cx);
		return nItem < 0 ? 0 : nItem;
	}

	int ThumbFromPoint(int x, int y) const override
	{
		const CPoint pointOrigin = _parent.GetScreenOrigin();
		const CSize sizeThumb = GetThumbSize();

		x -= pointOrigin.x;
		y -= pointOrigin.y;

		if (x < 0 || y < 0 || y >= sizeThumb.cy)
			return -1;

		const int nSlot = x / sizeThumb.cx;

		if (nSlot < 0 || nSlot >= SlotCount())
			return -1;

		return _itemOfSlot[nSlot];
	}
};



template<class TParent>
class FolderLayoutDetail : public FolderLayout<TParent>
{
public:
	typedef FolderLayout<TParent> BaseClass;
	typedef FolderLayoutDetail<TParent> ThisClass;

	// Inset of a value from its header column's edges.
	enum { titleGap = 4 };

	// Detail rows are not all one height. An item with a description gets the
	// room the description needs, and the thumbnail then centres against the
	// text instead of the text centring against the thumbnail. Prefix sums, so
	// every hit test and scroll size is still one lookup.
	std::vector<int> _rowTop;			// nCount + 1 entries
	std::vector<int> _rowHeight;
	bool _showDescriptions = false;

	FolderLayoutDetail(TParent &parent) : BaseClass(parent)
	{
	}

	void Init()
	{		
		const CSize sizeThumbDefault = _parent.GetThumbnailSize();

		_sizeThumb.cx = (sizeThumbDefault.cx + (THUMB_PADDING * 4));
		_sizeThumb.cy = (sizeThumbDefault.cy + (THUMB_PADDING * 4));

		_sizeThumbImage = sizeThumbDefault;
		
		_nTextHeight = 0;
		_sizeThumbCenter.cx = _sizeThumb.cx / 2;
		_sizeThumbCenter.cy = _sizeThumb.cy / 2;

		BuildRows();
	}

	int RowCount() const
	{
		return static_cast<int>(_rowHeight.size());
	}

	int RowTop(int nItem) const
	{
		if (_rowTop.empty())
			return nItem * _sizeThumb.cy;

		return _rowTop[IW::Clamp(nItem, 0, static_cast<int>(_rowTop.size()) - 1)];
	}

	int RowHeightAt(int nItem) const
	{
		if (nItem < 0 || nItem >= RowCount())
			return _sizeThumb.cy;

		return _rowHeight[nItem];
	}

	int TotalHeight() const
	{
		return _rowTop.empty() ? 0 : _rowTop.back();
	}

	// Where the values start: the first header column after the thumbnail.
	int TextLeft() const
	{
		CRect rectColumn;

		if (_parent.GetColumnCount() > 1 && _parent.GetColumnRect(1, rectColumn))
			return rectColumn.left + titleGap;

		return _sizeThumb.cx + THUMB_PADDING;
	}

	int TextRight() const
	{
		const CSize sizeClient = GetClientSize();
		return IW::Max(_parent.GetSizeAll().cx, sizeClient.cx) - THUMB_PADDING;
	}

	CString GetDescription(int nItem) const
	{
		return _showDescriptions ? _parent.GetItemDescription(nItem) : CString();
	}

	// The text block is one line of values with the description wrapped under
	// it. Measuring is the expensive part, so it happens once per rebuild.
	int MeasureTextHeight(int nItem, int cxDescription) const
	{
		int cy = App.m_nTextExtent;

		const CString str = GetDescription(nItem);

		if (!str.IsEmpty() && cxDescription > 0)
		{
			const CRect rectIn(0, 0, cxDescription, 0);
			cy += IW::Style::MeasureString(str, rectIn, IW::Style::Font::Standard,
			                               IW::Style::Text::Normal).Height() + THUMB_PADDING;
		}

		return cy;
	}

	void BuildRows()
	{
		const int nCount = _parent.GetItemCount();

		_showDescriptions = App.Settings.ShowDescriptions && _parent.HasDescriptions();

		_rowHeight.assign(nCount, _sizeThumb.cy);
		_rowTop.assign(nCount + 1, 0);

		// Without descriptions every row is the thumbnail's height, and this is
		// on the path of every resize -- measuring text per item there would be
		// paid by every folder whether or not one item has a description.
		if (!_showDescriptions)
		{
			for (int i = 0; i <= nCount; i++)
				_rowTop[i] = i * _sizeThumb.cy;

			return;
		}

		const int cxDescription = IW::Max(0, TextRight() - TextLeft());

		int y = 0;

		for (int i = 0; i < nCount; i++)
		{
			const int cyText = MeasureTextHeight(i, cxDescription) + (THUMB_PADDING * 2);

			_rowHeight[i] = IW::Max(_sizeThumb.cy, cyText);
			_rowTop[i] = y;
			y += _rowHeight[i];
		}

		_rowTop[nCount] = y;
	}

	CPoint GetThumbPoint(const int nThumb) const override
	{
		const CPoint pointOrigin = _parent.GetScreenOrigin();
		return CPoint(pointOrigin.x, pointOrigin.y + RowTop(nThumb));
	}

	CSize GetThumbSizeForItem(int nItem) const override
	{
		return CSize(_sizeThumb.cx, RowHeightAt(nItem));
	}

	CSize GetThumbCenterForItem(int nItem) const override
	{
		return CSize(_sizeThumbCenter.cx, RowHeightAt(nItem) / 2);
	}

	void AdjustThumbRect(CRect &rectThumb, CPoint &pointThumb, const CRect &rectClip, bool bFocus, int nItem) override
	{
		const CPoint pointOffset = _parent.GetScrollOffset();
		const CSize sizeClient = GetClientSize();

		pointThumb.x -= pointOffset.x;
		pointThumb.y -= pointOffset.y;

		int cx = IW::Max(_parent.GetSizeAll().cx, sizeClient.cx);

		rectThumb.left = pointThumb.x;
		rectThumb.top = pointThumb.y;
		rectThumb.right = pointThumb.x + cx;
		rectThumb.bottom = pointThumb.y + RowHeightAt(nItem);
	}

	void SetScrollSizeList(bool bChangeOffset)
	{
		const CPoint pointOffset = _parent.GetScrollOffset();
		const CSize sizeClient = GetClientSize();
		const CPoint pointOrigin = _parent.GetScreenOrigin();

		int cx = sizeClient.cx;
		int cy = sizeClient.cy;
		int x = pointOffset.x;
		int y = pointOffset.y;
		
		const CSize sizeThumb = _sizeThumb;
		const int nThumbCount = _parent.GetItemCount();

		if (nThumbCount > 0)
		{
			_parent._nThumbsX = 1;

			cx = IW::Max(sizeThumb.cx + 200 + pointOrigin.x, sizeClient.cx);

			// The row table is measured against the width, so it is rebuilt here
			// rather than only in Init -- this is the one call every resize,
			// folder change and sort goes through.
			BuildRows();
			cy = TotalHeight() + pointOrigin.y;
		}
		else
		{
			BuildRows();
		}

		_parent.SetScrollSize(IW::LowerLimit<1>(cx), IW::LowerLimit<1>(cy), true, bChangeOffset);

		// SetScrollSize resets both to a fraction of the total height.
		_parent.SetScrollLine(0, sizeThumb.cy);
		_parent.SetScrollPage(0, IW::LowerLimit<1>(sizeClient.cy - sizeThumb.cy));

		if (bChangeOffset)
		{
			_parent.SetScrollOffset(x, y);
		}
	}

	// Rows have different heights, so the item under a y is a search of the
	// prefix sums rather than a division.
	int RowFromY(int y) const
	{
		const int nCount = RowCount();

		if (nCount <= 0)
			return -1;

		if (y < 0)
			return -1;

		if (y >= TotalHeight())
			return -1;

		const auto it = std::upper_bound(_rowTop.begin(), _rowTop.begin() + nCount, y);
		const int i = static_cast<int>(it - _rowTop.begin()) - 1;

		return IW::Clamp(i, 0, nCount - 1);
	}

	int ClosestThumbFromPoint(const CPoint &pt) const { return ClosestThumbFromPoint(pt.x, pt.y); };	

	int ClosestThumbFromPoint(int x, int y) const
	{
		const CPoint pointOrigin = _parent.GetScreenOrigin();
		const int nMaxThumb = RowCount() - 1;

		if (nMaxThumb < 0)
			return 0;

		y -= pointOrigin.y;

		if (y < 0)
			return 0;

		const int i = RowFromY(y);
		return i == -1 ? nMaxThumb : i;
	}

	int ThumbFromPoint(int x, int y) const
	{
		const CSize sizeClient = GetClientSize();
		const CPoint pointOrigin = _parent.GetScreenOrigin();

		x -= pointOrigin.x;
		y -= pointOrigin.y;

		if (x < 0 || x >= IW::Max(_parent.GetSizeAll().cx, sizeClient.cx))
			return -1;

		return RowFromY(y);
	}

	CRect GetThumbRect(int nThumb) const
	{
		const CSize sizeClient = GetClientSize();
		const CPoint pointThumb = GetThumbPoint(nThumb);

		const CSize sizeDetail(IW::Max(_parent.GetSizeAll().cx, sizeClient.cx), RowHeightAt(nThumb));
		return CRect(pointThumb, sizeDetail);
	}

	CRect GetTextRect(int i, LPCTSTR sz) const
	{		
		const CSize sizeClient = GetClientSize();
		const CSize sizeThumb = GetThumbSize();
		const CPoint pointThumb = GetThumbPoint(i);

		int x = sizeThumb.cx + THUMB_PADDING;
		CRect rectIn(x, pointThumb.y, sizeClient.cx, 0);
		
		return IW::Style::MeasureString(sz, rectIn, IW::Style::Font::Standard, IW::Style::Text::SelectedThumbnail);
	}


	void DrawItemText(IW::CRender &render, int nItem, const IW::FolderItemAttributes &attributes, const CRect &rectThumb, bool bFocus, LPCTSTR szName, const CRect &rectAlpha, const COLORREF clrText, const COLORREF clrBG)
	{
		CSimpleArray<CString> arrayStr;
		_parent.GetThumbText(arrayStr, nItem, false);

		const CString strDescription = GetDescription(nItem);

		const int cxDescription = IW::Max(0, TextRight() - TextLeft());
		const int cyText = MeasureTextHeight(nItem, cxDescription);

		// The values and the description are one block, centred against the row
		// -- which is the thumbnail's height until a description makes it taller,
		// and then it is the thumbnail that ends up centred against the text.
		int y = rectThumb.top + IW::Half(rectThumb.Height() - cyText);
		const int cy = IW::Min(App.m_nTextExtent, rectThumb.Height());

		// One value per header column, aligned to it. Column 0 is the thumbnail,
		// so the values start at column 1.
		const int nHeaderColumns = _parent.GetColumnCount();
		const int nColumns = IW::Min(arrayStr.GetSize(), IW::Max(0, nHeaderColumns - 1));

		for (int i = 0; i < nColumns; i++)
		{
			CRect rectColumn;

			if (!_parent.GetColumnRect(i + 1, rectColumn))
				continue;

			CRect rect(rectColumn.left + titleGap, y,
			           rectColumn.right - titleGap, y + cy);

			if (rect.left >= rect.right)
				continue;

			render.DrawString(arrayStr[i], rect, IW::Style::Font::Standard, IW::Style::Text::Ellipsis, clrText);
		}

		if (!strDescription.IsEmpty() && cxDescription > 0)
		{
			const CRect rectDescription(TextLeft(), y + App.m_nTextExtent + THUMB_PADDING,
			                            TextLeft() + cxDescription, rectThumb.bottom);

			render.DrawString(strDescription, rectDescription, IW::Style::Font::Standard,
			                  IW::Style::Text::Normal, clrText);
		}
	}

	void DrawItemMarkers(IW::CRender &render, const IW::FolderItemAttributes &attributes, const CRect &rectThumb, const COLORREF clrText, const COLORREF clrBG)
	{
		CRect rectMarkerText(rectThumb);
		rectMarkerText.DeflateRect(THUMB_PADDING, THUMB_PADDING);		

		if (attributes.HasAttribute())
		{
			const IW::Style::Text::Style markersTextStyle = IW::Style::Text::Normal;
			const CString strMarkers = attributes.ToString();
			CRect rectMarkerTextOut = render.MeasureString(strMarkers, rectMarkerText, IW::Style::Font::Small, markersTextStyle);

			CRect rectMarkerTextBG(rectMarkerTextOut);
			rectMarkerTextBG.InflateRect(THUMB_PADDING, THUMB_PADDING);

			render.Blend(clrBG, rectMarkerTextBG);
			render.DrawString(strMarkers, rectMarkerTextOut, IW::Style::Font::Small, markersTextStyle, IW::Emphasize(clrText));
		}
	}

	void MakeItemVisible(int nThumb)
	{
		const CSize sizeClient = GetClientSize();
		const CRect rc = GetThumbRect(nThumb);
		CPoint pointOffset = _parent.GetScrollOffset();

		if (rc.top <  pointOffset.y)
		{
			pointOffset.y = rc.top;
		}
		else if (rc.bottom > (pointOffset.y + sizeClient.cy))
		{
			pointOffset.y = rc.bottom - sizeClient.cy;
		}		

		_parent.SetScrollOffset(pointOffset);
		_parent.UpdateBars();
	}

	void DoSize()
	{
		int nOldThumbsX = _parent._nThumbsX;

		SetScrollSizeList(true);

		CPoint pointOffset = _parent.GetScrollOffset();
		const int nThumbCount = _parent.GetItemCount();

		if (nThumbCount > 0)
		{
			const int nFirstThumb = ClosestThumbFromPoint(pointOffset);			
			const CPoint pointThumb = GetThumbPoint(nFirstThumb);			

			if (_parent._nThumbsX != nOldThumbsX)
			{
				pointOffset.x = pointOffset.x;
				pointOffset.y = pointThumb.y;			
			}

			_parent.SetScrollOffset(pointOffset);
		}
	}
};

template<class TParent>
class FolderLayoutMatrix : public FolderLayout<TParent>
{
public:
	typedef FolderLayout<TParent> BaseClass;
	typedef FolderLayoutMatrix<TParent> ThisClass;

	FolderLayoutMatrix(TParent &parent) : BaseClass(parent), _grow(0, 100, 20), _nHover(-1)
	{
	}

	CSize FindThumbSize(const CSize &sizeCanvas, const int nThumbCount, int nCurrentSize)
	{
		const int nMinSize = 16 + THUMB_PADDING;
		const int nMaxHeight = MulDiv(nCurrentSize, 3, 2);

		while(nCurrentSize > nMinSize)
		{
			int nThumbCountX = IW::LowerLimit<1>(sizeCanvas.cx / nCurrentSize);
			int nThumbCountY = IW::LowerLimit<1>((nThumbCount + nThumbCountX - 1) / nThumbCountX);

			int nPixelsY = nThumbCountY * nCurrentSize;

			if (nPixelsY <= sizeCanvas.cy)
			{
				CSize sizeThumb(sizeCanvas.cx / nThumbCountX, sizeCanvas.cy / nThumbCountY);
				if (sizeThumb.cy > nMaxHeight) sizeThumb.cy = nMaxHeight;
				return sizeThumb;
			}
			
			nCurrentSize -= 1;
		}
		
		return CSize(nMinSize, nMinSize);
	}

	void CalcThumbSize()
	{
		const CSize sizeThumbDefault = _parent.GetThumbnailSize();
		const int nThumbCount = _parent.GetItemCount();
		const CSize sizeClient = GetClientSize();
		const CPoint pointOrigin = _parent.GetScreenOrigin();
		const CSize sizeAvailable(IW::LowerLimit<1>(sizeClient.cx), IW::LowerLimit<1>(sizeClient.cy));
		const int nMaxSize = IW::Max(sizeThumbDefault.cx, sizeThumbDefault.cy) + THUMB_PADDING;

		CSize sizeThumb = FindThumbSize(sizeAvailable, nThumbCount, nMaxSize);

		_sizeThumb = sizeThumb;

		_sizeThumbImage.cx = sizeThumb.cx - THUMB_PADDING;
		_sizeThumbImage.cy = sizeThumb.cy - THUMB_PADDING;

		_nTextHeight = 0;
		_sizeThumbCenter.cx = sizeThumb.cx / 2;
		_sizeThumbCenter.cy = sizeThumb.cy / 2;
	}

	virtual void Init()
	{
		CalcThumbSize();
	}

	CRect GetThumbRect(int nItem) const
	{
		CRect rect(GetThumbPoint(nItem), GetThumbSize());

		if (nItem == _parent.GetHoverItem() && App.Settings.ZoomThumbnails)
		{			
			IW::FolderPtr pFolder = _parent.GetFolder();

			if (pFolder->IsItemImage(nItem))
				rect |= GetFocusThumbRect(pFolder->GetItemThumbRect(nItem), rect.CenterPoint());
		}
			
		return rect;
	}

	CRect GetFocusThumbRect(const CRect rectBounding, const CPoint &pointCenterImage) const
	{
		const CPoint pointImage = pointCenterImage - rectBounding.CenterPoint();

		CRect rectZoom(pointImage, rectBounding.Size());
		CRect rectScreen(_parent.GetScreenOrigin(), GetClientSize());
		rectScreen.OffsetRect(_parent.GetScrollOffset());
		rectScreen.DeflateRect(1,1);
		rectZoom = IW::ClampRect(rectZoom, rectScreen);

		rectZoom.InflateRect(2, 2);
		return rectZoom;
	}
	
	virtual void DrawItemText(IW::CRender &render, int nItem, const IW::FolderItemAttributes &attributes, const CRect &rectThumbIn, bool bFocus, LPCTSTR szName, const CRect &rectAlpha, const COLORREF clrText, const COLORREF clrBG)
	{
		CRect rectThumb(rectThumbIn);
		bool bSmallMode = IW::Min(rectThumb.Width(), rectThumb.Height()) < 40;

		if (szName && szName[0] != 0 && !bSmallMode)
		{
			if (!attributes.bIsImage)
			{
				const IW::FolderItemPtr pItem = _parent.GetItem(nItem);
				if (!pItem) return;

				CString strItemName = pItem->GetDisplayPath();
				CRect rectText = render.MeasureString(strItemName, rectThumb, IW::Style::Font::Standard, IW::Style::Text::Thumbnail);

				int cx = (rectThumb.Width() - rectText.Width()) / 2;
				int cy = rectThumb.Height() - rectText.Height();

				rectText.OffsetRect(cx, cy);
				
				render.Blend(clrBG, rectText);
				render.DrawString(strItemName, rectText, IW::Style::Font::Standard, IW::Style::Text::Thumbnail, clrText);
			}
		}
	}

	void SetScrollSizeList(bool bChangeOffset)
	{
		CalcThumbSize();
		BaseClass::SetScrollSizeList(bChangeOffset);
	}	

	void DrawImage(IW::CRender &render, IW::FolderItemAttributes& attributes, const CPoint &pointCenterImage, const CSize &sizeThumbImageCurrent, CRect& rectAlpha, bool bHover)
	{
		const IW::Page &page = attributes.page;

		if (attributes.bCanAnimate || attributes.bIsFolder)
		{
			BaseClass::DrawImage(render, 
				attributes, 
				pointCenterImage, 
				sizeThumbImageCurrent, 
				rectAlpha, bHover);
		}
		else
		{
			const CRect rectBounding = attributes.rectBounding;
			const CSize sizeImage = rectBounding.Size();
			const CSize sizeAvail = (bHover && App.Settings.ZoomThumbnails) ? AnimatedImageSize(sizeImage, sizeThumbImageCurrent) : sizeThumbImageCurrent;
			CRect rectDraw, rectSrc;

			if (sizeImage.cx <= sizeAvail.cx && sizeImage.cy <= sizeAvail.cy)
			{
				const CPoint pointOffset = page.GetOffset();

				int x = pointCenterImage.x + pointOffset.x - (rectBounding.Width() / 2);
				int y = pointCenterImage.y + pointOffset.y - (rectBounding.Height() / 2);

				rectDraw = CRect(x, y, x + page.GetWidth(), y + page.GetHeight());
				rectSrc = CRect(0, 0, page.GetWidth(), page.GetHeight());
			}
			else if ((sizeImage.cx <= sizeAvail.cx && sizeImage.cy > sizeAvail.cy) ||
				(sizeImage.cx > sizeAvail.cx && sizeImage.cy <= sizeAvail.cy))
			{
				const CPoint pointOffset = page.GetOffset();

				int x = pointCenterImage.x + pointOffset.x - (rectBounding.Width() / 2);
				int y = pointCenterImage.y + pointOffset.y - (rectBounding.Height() / 2);

				CRect rectImage(x, y, x + page.GetWidth(), y + page.GetHeight());
				CRect rectClip(CPoint(pointCenterImage.x - (sizeAvail.cx / 2), pointCenterImage.y - (sizeAvail.cy / 2)), sizeAvail);

				rectDraw = IW::ClipRect(rectImage, rectClip);
				rectSrc = CRect(CPoint(rectDraw.TopLeft() - rectImage.TopLeft()), rectDraw.Size());
			}
			else if (sizeImage.cx > sizeAvail.cx || sizeImage.cy > sizeAvail.cy)
			{
				const CPoint pointSourceCenter = rectBounding.CenterPoint();
				int nRadiusIn = IW::Min(sizeImage.cx, sizeImage.cy) / 2;
				int nRadiusOut = IW::Min(sizeAvail.cx, sizeAvail.cy) / 2;

				rectSrc = CRect(pointSourceCenter.x - nRadiusIn, 
					pointSourceCenter.y - nRadiusIn,
					pointSourceCenter.x + nRadiusIn, 
					pointSourceCenter.y + nRadiusIn);

				rectDraw = CRect(pointCenterImage.x - nRadiusOut, 
					pointCenterImage.y - nRadiusOut,
					pointCenterImage.x + nRadiusOut, 
					pointCenterImage.y + nRadiusOut);
			}

			// Only the hover zoom may be nudged back on-screen; an ordinary cell
			// must be clipped by the renderer, not moved out of its own cell.
			if (bHover && App.Settings.ZoomThumbnails)
			{
				CRect rectScreen(_parent.GetScreenOrigin(), GetClientSize());
				rectScreen.DeflateRect(1,1);
				rectDraw = IW::ClampRect(rectDraw, rectScreen);
			}

			render.DrawImage(page, rectDraw, rectSrc);
		}
	}

	CSize AnimatedImageSize(const CSize &sizeImage, const CSize &sizeThumbImageCurrent) const
	{
		const CSize sizeRange = sizeImage - sizeThumbImageCurrent;
		int pos = _grow.Pos();
		return CSize(sizeThumbImageCurrent.cx + ((sizeRange.cx * pos) / 100),
			sizeThumbImageCurrent.cy + ((sizeRange.cy * pos) / 100));
	}

	ScalarAnimation _grow;
	int _nHover;

	virtual void HoverChanged(int nHover)
	{
		_nHover = nHover;
		_grow.Reset();
		_grow.SetDirection(true);
	}

	void OnTimer()
	{
		if (_grow.Animate() && _nHover >= 0 && _nHover < _parent.GetItemCount())
			_parent.InvalidateThumb(_nHover);
	}
};

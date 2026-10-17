// ImageWalker by Zac Walker
//
// Purpose: The mouse tools on the image pane - select, move the selection,
//          move the image.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once


inline CRect GetCentreThirdRect(const CRect &rect)
{
	int cx = rect.Width() / 3;
	int cy = rect.Height() / 3;

	return CRect(rect.left + cx, rect.top + cy, 
		rect.right - cx, rect.bottom - cy);
}

class ImageTool
{
public:
	CPoint _pointLast;		
	CPoint _pointStart;

	virtual void OnLButtonDown(const CPoint &point) 
	{
		_pointLast = _pointStart = point;		
	};

	virtual HCURSOR GetCursor() const
	{
		return IW::Style::Cursor::Move;
	}

	virtual void OnMouseMove(const CPoint &point) {}
	virtual void OnLButtonUp(const CPoint &point) {}

	void DrawSelectionRect(CClientDC &dc, const CRect &r)
	{
		dc.DrawFocusRect(r);
		dc.DrawFocusRect(GetCentreThirdRect(r));
	}
};			

template<class TParent>
class ImageToolSelect : public ImageTool
{
public:

	TParent &_parent;

	ImageToolSelect(TParent &parent) : _parent(parent)
	{
	}

	HCURSOR GetCursor() const
	{
		return IW::Style::Cursor::Select;
	}

	void OnLButtonDown(const CPoint &point) 
	{
		_pointLast = _pointStart = point;
		_pointLast.x++;
		_pointLast.y++;		

		CClientDC dc(_parent);

		DrawSelectionRect(dc, _parent.GetDeviceRect());
		DrawSelectionRect(dc, CRect(_pointStart, point));
	};

	void OnMouseMove(const CPoint &point) 
	{
		CRect rOld(_pointStart, _pointLast);
		CRect rNew(_pointStart, point);

		CClientDC dc(_parent);
		DrawSelectionRect(dc, rOld);
		DrawSelectionRect(dc, rNew);

		_pointLast = point;
	}

	void OnLButtonUp(const CPoint &point) 
	{
		CRect rOld(_pointStart, _pointLast);
		CRect rNew(_pointStart, point);

		_parent.SetDeviceRect(rNew);

		CClientDC dc(_parent);
		DrawSelectionRect(dc, rOld);
	}
};

template<class TParent>
class ImageToolMoveSelection : public ImageTool
{
public:

	TParent &_parent;
	CRect _rectLast;

	ImageToolMoveSelection(TParent &parent) : _parent(parent)
	{		
	}	

	HCURSOR GetCursor() const
	{
		return IW::Style::Cursor::Move;
	}

	void OnLButtonDown(const CPoint &point) 
	{
		_pointLast = _pointStart = point;
		_rectLast = _parent.GetDeviceRect();
	};

	void OnMouseMove(const CPoint &point) 
	{
		CRect rectDevice = _parent.GetDeviceRect();
		CRect rectNew = rectDevice - (_pointStart - point);

		CClientDC dc(_parent);
		DrawSelectionRect(dc, _rectLast);
		DrawSelectionRect(dc, rectNew);

		_pointLast = point;
		_rectLast = rectNew;
	}

	void OnLButtonUp(const CPoint &point) 
	{
		CRect rectDevice = _parent.GetDeviceRect();
		CRect rectNew = rectDevice - (_pointStart - point);

		_parent.SetDeviceRect(rectNew);

		CClientDC dc(_parent);
		DrawSelectionRect(dc, _rectLast);
	}
};

template<class TParent>
class ImageToolMoveImage : public ImageTool
{
public:

	TParent &_parent;
	CPoint _pointOriginalOffset;

	ImageToolMoveImage(TParent &parent) : _parent(parent)
	{
	}

	HCURSOR GetCursor() const
	{
		return IW::Style::Cursor::HandDown;
	}

	void OnLButtonDown(const CPoint &point) 
	{
		_pointLast = _pointStart = point;		
		_pointOriginalOffset = _parent.GetScrollOffset(); 
	};

	void OnMouseMove(const CPoint &point) 
	{		
		_pointLast = point;

		int x = _pointOriginalOffset.x + _pointStart.x - _pointLast.x;
		int y = _pointOriginalOffset.y + _pointStart.y - _pointLast.y;

		_parent.ScrollTo(CPoint(x, y));
	}

	void OnLButtonUp(const CPoint &point) 
	{
		OnMouseMove(point);
	}
};

// The divider between two images being compared. Taking hold of it while they
// are side by side is what lays one over the other, so the press has to enter
// split mode before it can move anything.
template<class TParent>
class ImageToolCompare : public ImageTool
{
public:

	TParent &_parent;

	ImageToolCompare(TParent &parent) : _parent(parent)
	{
	}

	HCURSOR GetCursor() const
	{
		return IW::Style::Cursor::LeftRight;
	}

	void OnLButtonDown(const CPoint &point)
	{
		_pointLast = _pointStart = point;
		_parent.BeginCompareSplit();
		_parent.SetCompareDivider(point.x);
	}

	void OnMouseMove(const CPoint &point)
	{
		_pointLast = point;
		_parent.SetCompareDivider(point.x);
	}

	void OnLButtonUp(const CPoint &point)
	{
		OnMouseMove(point);
	}
};

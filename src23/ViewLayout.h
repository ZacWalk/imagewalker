// ImageWalker by Zac Walker
//
// Purpose: The Frame layout system the image pane overlays are positioned
//          and drawn with.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

// TestLayoutView.h : interface of the CTestLayoutView class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once

#include "iw/ColorButton.h"
#include "ViewDialogs.h"

class Frame;
class ImageState;

class IFrameParent 
{
public:
	
	virtual HWND GetHWnd() = 0;
	virtual CRect GetClientRect() = 0;
	virtual DWORD TrackPopupMenu(CMenuHandle menuPopup, int x, int y) = 0;	
	virtual void SignalCommand(int nCommand) = 0;
	virtual void InvalidateRect(const CRect &rect, BOOL bErase = TRUE) throw() = 0;
	virtual void ActivateTooltip(bool b) = 0;
	virtual void ResetLayout() = 0;
	virtual void SetChildWindowPos(IW::WindowPos &positions, HWND hWnd, const CRect &rect) = 0;
	virtual void SetChildWindowPos(IW::WindowPos &positions, HWND hWnd, int x, int y, int cx, int cy) = 0;

};

template<class TParent>
class FrameParent : public IFrameParent
{
	
private:

	TParent &_parent;

public:
	
	FrameParent(TParent &parent) : _parent(parent)
	{
	}

	HWND GetHWnd() { return _parent.m_hWnd; };

	CRect GetClientRect()
	{
		CRect r;
		::GetClientRect(GetHWnd(), r);
		return r;
	}

	void InvalidateRect(const CRect &rectLP, BOOL bErase = TRUE) throw()
	{
		if (_parent.m_hWnd)
		{
			CRect rectDP(rectLP);
			_parent.LP2DP(rectDP);
			_parent.InvalidateRect(rectDP, bErase);
		}
	}

	void ActivateTooltip(bool b)
	{
		_parent.ActivateTooltip(b);
	}

	DWORD TrackPopupMenu(CMenuHandle menuPopup, int x, int y)
	{
		return menuPopup.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,  x, y, GetHWnd());	
	}

	void SignalCommand(int nCommand) 
	{
		_parent.GetTopLevelWindow().PostMessage(WM_COMMAND, nCommand);
	};

	void ResetLayout() { return _parent.ResetLayout(); }

	void SetChildWindowPos(IW::WindowPos &positions, HWND hWnd, const CRect &rect) 
	{
		SetChildWindowPos(positions, hWnd, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
	}

	void SetChildWindowPos(IW::WindowPos &positions, HWND hWnd, int x, int y, int cx, int cy)
	{
		_parent.SetChildWindowPos(positions, hWnd, x, y, cx, cy);
	}	
};

class FrameVisitor
{
public:

	virtual void Visit(Frame *pFrame) = 0;
	virtual void AfterVisit(Frame *pFrame) {};
};

class Frame
{
private:
	Frame(const Frame&);
	void operator=(const Frame&);

public:

	enum { cxImage = 16, cyImage = 16, padding = 2, paddingLarge = 4, borderGap = 4 };

	CRect _rect;
	IFrameParent &_parent;
	bool _bVisible;	
	bool _bLayered;

	Frame(IFrameParent &parent) : 
		_parent(parent), 
		_rect(0, 0, 0, 0), 
		_bVisible(true), 
		_bLayered(false)
	{
	}	

	virtual ~Frame()
	{
	}

	virtual void Accept(FrameVisitor &visitor) 
	{ 
		visitor.Visit(this); 
		visitor.AfterVisit(this);
	};

	virtual void SetPosition(IW::WindowPos &positions, IW::CRender &render, const CRect &rectIn) 
	{
		_rect = rectIn;
		_rect.bottom = _rect.top;
	};

	virtual void Render(IW::CRender &render) {};
	virtual void Erase(IW::CRender &render) {};
	virtual void Timer() {};
	virtual void Activate(HWND hWndParent, bool bShow) {  };
	virtual void GetToolTipId(int &id, bool &bHandled) { }

	virtual void MouseMove(const CPoint &point) {};
	virtual void MouseLeave() {};	

	virtual void MouseLeftButtonDown(const CPoint &point, bool &bHandled) 
	{
		if (_rect.PtInRect(point) != 0)
		{
			bHandled = true;
		}
	};

	virtual void MouseLeftButtonUp(const CPoint &point) {};
	
	DWORD GetUniqueId()
	{
		static int nId = 1024;
		return nId++;
	}

	bool IsLayered() const
	{
		return _bLayered;
	}

	bool IsVisible() const
	{
		return _bVisible;
	}

	void SetVisible(bool b)
	{
		if (_bVisible != b)
		{
			_bVisible = b;
			if (b) _parent.ResetLayout();
			Invalidate();
		}
	}

	void Invalidate()
	{
		CRect r(_rect);
		r.InflateRect(1, 1);
		_parent.InvalidateRect(r);
	}
};



class FrameVisibleStack
{
private:

	enum { maxFlags = 64 };
	bool _visibleFlags[maxFlags];
	bool _layerFlags[maxFlags];
	int _index;

public:

	FrameVisibleStack() : _index(0)
	{
	}

	bool IsVisible() const
	{
		for(int i = 0; i < _index; i++)
		{
			if (!_visibleFlags[i])
				return false;
		}

		return true;
	}

	bool InLayer() const
	{
		for(int i = 0; i < _index - 1; i++)
		{
			if (_layerFlags[i])
				return true;
		}

		return false;
	}

	void Push(Frame *pFrame) 
	{
		assert(_index < maxFlags);
		_visibleFlags[_index] = pFrame->IsVisible();
		_layerFlags[_index] = pFrame->IsLayered();
		++_index;
	}

	bool PushAndIsVisible(Frame *pFrame)
	{
		Push(pFrame);
		return IsVisible();
	}

	void Pop()
	{
		assert(_index > 0);
		--_index;
	}
};

class FrameVisitorActivate : public FrameVisitor
{
public:

	HWND _hWndParent; 
	FrameVisibleStack _frameStack;

	FrameVisitorActivate(HWND hWndParent) : _hWndParent(hWndParent)
	{ 
	}

	void Visit(Frame *pFrame) 
	{		
		_frameStack.Push(pFrame);
		pFrame->Activate(_hWndParent, _frameStack.IsVisible());	
	}

	void AfterVisit(Frame *pFrame)
	{
		_frameStack.Pop();
	}
};

class FrameVisitorTimer : public FrameVisitor
{
public:

	void Visit(Frame *pFrame) 
	{		
		pFrame->Timer();	
	}
};

class FrameVisitorRender : public FrameVisitor
{
public:
	IW::CRender &_render;
	FrameVisibleStack _frameStack;

	FrameVisitorRender(IW::CRender &render) : _render(render) { }

	void Visit(Frame *pFrame) 
	{
		_frameStack.Push(pFrame);
		if (_frameStack.IsVisible() && !_frameStack.InLayer()) pFrame->Render(_render); 		
	}

	void AfterVisit(Frame *pFrame)
	{
		_frameStack.Pop();
	}
};



class FrameVisitorErase : public FrameVisitor
{
public:
	IW::CRender &_render;
	FrameVisibleStack _frameStack;

	FrameVisitorErase(IW::CRender &render) : _render(render) { }

	void Visit(Frame *pFrame) 
	{
		_frameStack.Push(pFrame);
		if (_frameStack.IsVisible() && !_frameStack.InLayer()) pFrame->Erase(_render); 		
	}

	void AfterVisit(Frame *pFrame) 	{ _frameStack.Pop(); }
};


class FrameVisitorMouseMove : public FrameVisitor
{
public:
	const CPoint &_point;
	FrameVisibleStack _frameStack;
	FrameVisitorMouseMove(const CPoint &point) : _point(point) { }
	void Visit(Frame *pFrame) { if (_frameStack.PushAndIsVisible(pFrame)) pFrame->MouseMove(_point); }
	void AfterVisit(Frame *pFrame) 	{ _frameStack.Pop(); }
};

class FrameVisitorMouseLeave : public FrameVisitor
{
public:
	FrameVisibleStack _frameStack;
	void Visit(Frame *pFrame) { if (_frameStack.PushAndIsVisible(pFrame)) pFrame->MouseLeave(); }
	void AfterVisit(Frame *pFrame) 	{ _frameStack.Pop(); }
};

class FrameVisitorMouseLeftButtonUp : public FrameVisitor
{
public:
	const CPoint &_point;
	FrameVisibleStack _frameStack;
	FrameVisitorMouseLeftButtonUp(const CPoint &point) : _point(point) { }
	void Visit(Frame *pFrame) { if (_frameStack.PushAndIsVisible(pFrame)) pFrame->MouseLeftButtonUp(_point); }
	void AfterVisit(Frame *pFrame) 	{ _frameStack.Pop(); }
};

class FrameVisitorMouseLeftButtonDown : public FrameVisitor
{
public:
	const CPoint &_point;
	bool &_bHandled;
	FrameVisibleStack _frameStack;
	FrameVisitorMouseLeftButtonDown(const CPoint &point, bool &bHandled) : _point(point), _bHandled(bHandled) { }
	void Visit(Frame *pFrame) { if (_frameStack.PushAndIsVisible(pFrame)) pFrame->MouseLeftButtonDown(_point, _bHandled); }
	void AfterVisit(Frame *pFrame) 	{ _frameStack.Pop(); }
};

class FrameVisitorTooltip : public FrameVisitor
{
public:
	int Id;
	bool Handled;
	FrameVisibleStack _frameStack;
	FrameVisitorTooltip() : Id(0), Handled(false) { }
	void Visit(Frame *pFrame) { if (_frameStack.PushAndIsVisible(pFrame)) { pFrame->GetToolTipId(Id, Handled); } }
	void AfterVisit(Frame *pFrame) 	{ _frameStack.Pop(); }
};


template<class T>
class FrameTextT : public Frame
{

public:
	FrameTextT(IFrameParent &parent) : Frame(parent)
	{
	}

	void SetPosition(IW::WindowPos &positions, IW::CRender &render, const CRect &rectIn)
	{
		T *pThis = static_cast<T*>(this);
		CSize size = pThis->GetSize(render, rectIn.Width());

		if (size.cx == 0 || size.cy == 0)
		{
			_rect = rectIn;
			_rect.right = _rect.left;
			_rect.bottom = _rect.top;
		}
		else
		{
			CRect rect(rectIn.left, rectIn.top, rectIn.left + size.cx, rectIn.top + size.cy);
			_rect = rect;
		}
	}

	CSize GetSize(IW::CRender &render, int cx)
	{
		T *pThis = static_cast<T*>(this);
		const CRect rectIn(0, 0, cx, 1000);
		const CRect rectOut = render.MeasureString(pThis->GetText(), rectIn, pThis->GetFont(), pThis->_stringformat);
		return rectOut.Size();
	}

	void Render(IW::CRender &render)
	{
		T *pThis = static_cast<T*>(this);
		render.DrawString(pThis->GetText(), _rect, pThis->GetFont(), pThis->_stringformat, IW::Style::Color::TaskText);
	}
};

class FrameText : public FrameTextT<FrameText>
{
public:
	IW::Style::Text::Style _stringformat;
	COLORREF _clr;	
	CString _str;

	FrameText(IFrameParent &parent, COLORREF clr = IW::Style::Color::TaskText) : 
		FrameTextT<FrameText>(parent),
		_clr(clr), 
		_stringformat(IW::Style::Text::Normal)
	{
	}

	FrameText(IFrameParent &parent, const CString &str, COLORREF clr = IW::Style::Color::TaskText) : 
		FrameTextT<FrameText>(parent),
		_str(str),
		_clr(clr), 
		_stringformat(IW::Style::Text::Normal)
	{
	}

	IW::Style::Font::Type GetFont()
	{
		return IW::Style::Font::Standard;
	}

	void SetText(const CString &str)
	{
		_str = str;
	}

	const CString &GetText() const
	{
		return _str;
	}
};

template<class TBase = Frame, bool bSetCursor = true>
class FrameHover : public TBase
{
public:
	bool _bHover;
	HCURSOR _hCursorOriginal;

	FrameHover(IFrameParent &parent) : 
		TBase(parent), 
		_bHover(false), 
		_hCursorOriginal(IW::Style::Cursor::Normal)
	{
	}

	void Activate(HWND hWndParent, bool bShow) 
	{  
		SetHover(false);
		TBase::Activate(hWndParent, bShow);
	}

 	void SetHover(bool bHover)
	{
		if (_bHover != bHover)
		{
			Invalidate();
			_parent.ActivateTooltip(bHover);
			_bHover = bHover;
			_hCursorOriginal = ::GetCursor();
			HoverChanged(bHover);
		}
	}

	virtual void HoverChanged(bool bHover)
	{
	}

	void MouseMove(const CPoint &point)
	{
		SetHover(_rect.PtInRect(point) != 0);

		if (_bHover && bSetCursor) 
		{
			::SetCursor(IW::Style::Cursor::Link);
		}
	}

	void MouseLeave()
	{
		SetHover(false);

		if (bSetCursor) 
		{
			::SetCursor(_hCursorOriginal);
		}
	}
};

class FrameLink : public FrameHover<FrameTextT<FrameLink> >
{
public:

	typedef FrameHover<FrameTextT<FrameLink> > BaseClass;

	IW::Style::Text::Style _stringformat;
	COLORREF _clr;	
	CString _str;


	FrameLink(IFrameParent &parent, const CString &str = g_szEmptyString, COLORREF clr = IW::Style::Color::TaskText) : 
		BaseClass(parent),
		_str(str), 
		_clr(clr), 
		_stringformat(IW::Style::Text::Normal)
	{
		_bHover = false;
		 
	}

	IW::Style::Font::Type GetFont()
	{
		return _bHover ? IW::Style::Font::LinkHover : IW::Style::Font::Link;
	}

	const CString &GetText() const
	{
		return _str;
	}

	void SetText(const CString &str)
	{
		_str = str;
	}
};

template<class TSubFrame>
class FrameBullet : public Frame
{
protected:
	TSubFrame _subframe;
	HIMAGELIST _hImageList;
	int _nImage;

public:


	void SetText(const CString &str)
	{
		_subframe.SetText(str);
	}

	FrameBullet(IFrameParent &parent) : Frame(parent), _subframe(parent), _nImage(0), _hImageList(App.GetGlobalBitmap())
	{
	}

	~FrameBullet()
	{
	}
	
	void SetPosition(IW::WindowPos &positions, IW::CRender &render, const CRect &rectIn)
	{
		CRect r = _rect = rectIn;
		r.left += cxImage + padding;		
		_subframe.SetPosition(positions, render, r);
		_rect.bottom = _rect.top + IW::Max(_subframe._rect.Height(), cyImage);

		if (_subframe._rect.Height() < cyImage)
		{
			int cy = (cyImage - _subframe._rect.Height()) / 2;
			_subframe._rect.OffsetRect(0, cy);
		}
	}

	void Accept(FrameVisitor &visitor) 
	{		
		visitor.Visit(this);
		_subframe.Accept(visitor);
		visitor.AfterVisit(this);
	};


	void Render(IW::CRender &render)
	{
		render.DrawImageList(_hImageList, _nImage, _rect.left, _rect.top, 16, 16);
	}
};

class FrameTitle : public FrameTextT<FrameTitle>
{
public:

	IW::Style::Text::Style _stringformat;
	CString _str;

	FrameTitle(IFrameParent &parent, const CString &str = g_szEmptyString) : 
		FrameTextT<FrameTitle>(parent),
		_str(str), 
		_stringformat(IW::Style::Text::Title)
	{		
	}

	~FrameTitle()
	{
	}

	IW::Style::Font::Type GetFont()
	{
		return IW::Style::Font::Heading;
	}

	const CString &GetText() const
	{
		return _str;
	}

	void SetText(const CString &str)
	{
		_str = str;
	}
};


class FrameArray : public Frame
{
public:	

	typedef std::vector<Frame*> FRAMES;
	FRAMES _frames;

	FrameArray(IFrameParent &parent) : Frame(parent)
	{
	}

	~FrameArray()
	{
		RemoveAll();		
	}

	void Clear()
	{
		_frames.clear();
	}

	void RemoveAll()
	{

		for(FRAMES::iterator i = _frames.begin(); i != _frames.end(); i++)
		{
			delete *i;
		}

		_frames.clear();
	}

	int GetSize() const
	{
		return static_cast<int>(_frames.size());
	}

	void AddFrame(Frame* pFrame)
	{
		_frames.push_back(pFrame);
	}

	Frame* operator[] (int nIndex)
	{
		return _frames[nIndex];
	}

	Frame* GetFrame(int nIndex)
	{
		return _frames[nIndex];
	}

	void SetPosition(IW::WindowPos &positions, IW::CRender &render, const CRect &rectIn)
	{
		CRect r = _rect = rectIn;

		for(FRAMES::iterator i = _frames.begin(); i != _frames.end(); i++)
		{
			Frame *pFrame = *i;

			if (pFrame->IsVisible())
			{
				pFrame->SetPosition(positions, render, r);
				r.top = pFrame->_rect.bottom + padding;
			}
		} 

		_rect.bottom = r.top;
	}

	void Accept(FrameVisitor &visitor) 
	{
		visitor.Visit(this);

		for(FRAMES::iterator i = _frames.begin(); i != _frames.end(); i++)
			(*i)->Accept(visitor);

		visitor.AfterVisit(this);
	};
};

class FrameTextCommand : public FrameLink
{
public:

	int _id;

	FrameTextCommand(IFrameParent &parent, const CString &str, int id) : 
		FrameLink(parent, str), 
		_id(id)
	{
		_stringformat = IW::Style::Text::SingleLine;
	}

	void MouseLeftButtonUp(const CPoint &point)
	{
		if (_rect.PtInRect(point))
		{
			_parent.SignalCommand(_id);
		}
	}
};


class  FrameLinkBar : public FrameArray
{
public:	

	enum { padding = 4 };

	
	

	FrameLinkBar(IFrameParent &parent) : FrameArray(parent)
	{
	}

	FrameTextCommand *AddLink(const CString &str, int id)
	{
		FrameTextCommand *pCommand = new FrameTextCommand(_parent, str, id);
		AddFrame(pCommand);
		return pCommand;
	}

	void SetPosition(IW::WindowPos &positions, IW::CRender &render, const CRect &rectIn)
	{
		CRect r = _rect = rectIn;
		int bottom = r.top;
		int left = r.right;

		for(FRAMES::iterator i = _frames.begin(); i != _frames.end(); i++)
		{
			Frame *pFrame = *i;
			pFrame->SetPosition(positions, render, r);

			int cx = r.right - pFrame->_rect.right;
			pFrame->_rect.OffsetRect(cx, 0);

			left = pFrame->_rect.left - (padding * 2);
			r.right = left;
			bottom = IW::Max(bottom, pFrame->_rect.bottom);
		} 

		_rect.left = left;
		_rect.bottom = bottom;
	}

	void Erase(IW::CRender &render)
	{
		if (_frames.size() > 0)
		{
			for(FRAMES::iterator i = _frames.begin() + 1; i != _frames.end(); i++)
			{
				int x = (*i)->_rect.right + padding;
				render.DrawLine(x, _rect.top, x, _rect.bottom, IW::Style::Color::TaskText);
			}
		}

		FrameArray::Erase(render);
	}
};

class ScalarAnimation
{
	int _nMin;
	int _nMax; 
	int _speed;
	int _step;
	int _pos;

public:
	ScalarAnimation(int nMin, int nMax, int nSpeed) : _nMin(nMin), _nMax(nMax), _speed(nSpeed), _step(0), _pos(nMin)
	{
	}

	bool Animate()
	{
		const int oldPos = _pos;

		if (_step > 0)
		{
			_pos = IW::Min(_nMax, _pos + _step); 
			if (_pos == _nMax) _step = 0;
		}
		else if (_step < 0)
		{
			_pos = IW::Max(_nMin, _pos + _step); 
			if (_pos == _nMin) _step = 0;
		}

		return oldPos != _pos;
	}

	void Reset()
	{
		_pos = _nMin;
	}

	void SetDirection(bool bDirection)
	{
		_step = _speed * (bDirection ? 2 : -1);
		Animate();
	}

	int Pos() const
	{
		return _pos;
	}
};

class FrameGroup : public FrameHover<Frame, false>
{
protected:
    
public:

	typedef FrameHover<Frame, false> BaseClass;

	FrameTitle _title;
	FrameArray _frames;
	FrameLinkBar _linkBar;

	ScalarAnimation _fade;
	ScalarAnimation _grow;

	FrameGroup(IFrameParent &parent, const CString &str) : FrameHover<Frame, false>(parent), 
		_frames(parent), 
		_title(parent, str),
		_linkBar(parent),
		_fade(0x80, 0xF0, 0x10),
		_grow(50, 100, 20)
	{
		_bLayered = true;
	}

	void Clear()
	{
		_frames.Clear();
	}

	void SetText(const CString &str)
	{
		_title.SetText(str);
	}

	void RemoveAll()
	{
		_frames.RemoveAll();
	}

	void AddFrame(Frame *pFrame)
	{
		_frames.AddFrame(pFrame);
	}

	Frame* operator[] (int nIndex)
	{
		return _frames[nIndex];
	}

	void SetPosition(IW::WindowPos &positions, IW::CRender &render, const CRect &rectIn)
	{
		CRect r = rectIn;		
		//r.DeflateRect(padding, padding, padding, 0);

		CRect rectTitle(r); 
		rectTitle.DeflateRect(paddingLarge, paddingLarge);
		_title.SetPosition(positions, render, rectTitle);

		CRect rectLinkBar(r); 
		rectLinkBar.DeflateRect(paddingLarge, paddingLarge);
		_linkBar.SetPosition(positions, render, rectLinkBar);

		if (_linkBar._rect.left < _title._rect.right)
		{
			rectLinkBar = r; 
			rectLinkBar.DeflateRect(paddingLarge, paddingLarge);
			rectLinkBar.top = _title._rect.bottom + padding;
			_linkBar.SetPosition(positions, render, rectLinkBar);
		}

		CRect rectFrames(r); rectFrames.DeflateRect(paddingLarge, padding);
		rectFrames.top = IW::Max(_title._rect.bottom, _linkBar._rect.bottom) + padding;
		_frames.SetPosition(positions, render, rectFrames);

		_rect = r;
		_rect.bottom = _frames._rect.bottom + padding;
	}

	void Render(IW::CRender &renderIn)
	{
		IW::CRender render;
		IW::CDCRender dc(renderIn);

		if (render.Create(dc, _rect))
		{
			render.DrawRect(_rect, IW::Style::Color::TaskFrame, IW::Style::Color::TaskBackground, 1, true);

			AcceptChildren(FrameVisitorErase(render));
			AcceptChildren(FrameVisitorRender(render));

			renderIn.DrawRender(render, _fade.Pos(), GetRenderRect());
		}
	}

	virtual CRect GetRenderRect() const
	{
		CRect r = _rect;
		int pos = _grow.Pos();
		r.bottom = r.top + ((r.Height() * pos) / 100);
		r.right = r.left + ((r.Width() * pos) / 100);
		return r;
	}

	void HoverChanged(bool bHover)
	{
		_fade.SetDirection(bHover);
		_grow.SetDirection(bHover);
	}

	void Timer()
	{
		bool bFade = _fade.Animate();
		bool bGrow = _grow.Animate();
		if (bFade || bGrow)
			Invalidate();
	}

	void Accept(FrameVisitor &visitor) 
	{		
		visitor.Visit(this);
		AcceptChildren(visitor);		
		visitor.AfterVisit(this);
	}

	void AcceptChildren(FrameVisitor &visitor) 
	{
		_title.Accept(visitor);
		_linkBar.Accept(visitor);
		_frames.Accept(visitor);
	}
};

class FrameCommand : public FrameBullet<FrameLink>
{
public:

	int _nCommand;

	FrameCommand(IFrameParent &parent, int nImage, const CString &str, int nCommand) : FrameBullet<FrameLink>(parent), _nCommand(nCommand)
	{
		_nImage = nImage;
		_subframe.SetText(str);
	}

	void MouseLeftButtonUp(const CPoint &point)
	{
		if (_rect.PtInRect(point))
		{
			_parent.SignalCommand(_nCommand);
		}
	}
};


class FrameWindow : public Frame
{
public:

	CWindow _wnd;

	FrameWindow(IFrameParent &parent) : Frame(parent) { }
	FrameWindow(IFrameParent &parent, HWND hWnd) : Frame(parent), _wnd(hWnd) { }
	virtual ~FrameWindow() { }


	void SetPosition(IW::WindowPos &positions, IW::CRender &render, const CRect &rectIn) 
	{
		_rect = rectIn;
		
		if (_wnd)
		{
			_parent.SetChildWindowPos(positions, _wnd, _rect);
		}
	};

	void Activate(HWND hWndParent, bool bShow)
	{
		if (_wnd)
		{
			_wnd.ShowWindow(bShow ? SW_SHOW : SW_HIDE);
		}
	};

};

typedef FrameGroup FrameHeader;


class FrameButton : public FrameWindow
{
public:

	CButton _ctrl;
	CString _caption;
	unsigned _id;

	FrameButton(IFrameParent &parent, const CString &strCaption) : FrameWindow(parent, NULL), _caption(strCaption), _id(GetUniqueId())
	{
	}

	FrameButton(IFrameParent &parent, const CString &strCaption, int id) : FrameWindow(parent, NULL), _caption(strCaption), _id(id)
	{
	}

	void Activate(HWND hWndParent, bool bShow)
	{
		if (_wnd == NULL)
		{			
			_wnd = _ctrl.Create(hWndParent, 0, _caption, WS_CHILD | BS_DEFPUSHBUTTON | BS_TOP | BS_MULTILINE | WS_TABSTOP, 0, _id);
			_ctrl.SetFont(IW::Style::GetFont(IW::Style::Font::Standard));			
		}

		_wnd.ShowWindow(bShow ? SW_SHOW : SW_HIDE);
	};
	
	int GetHeightOfControl(IW::CRender &render, const CRect &rectIn)
	{
		CRect rectText(rectIn);
		rectText.left += 20;
		const CRect rectTextOut = render.MeasureString(_caption, rectText, IW::Style::Font::Standard, IW::Style::Text::Normal);
		return IW::LowerLimit<24>(rectTextOut.Height() + 4);
	}

	void SetPosition(IW::WindowPos &positions, IW::CRender &render, const CRect &rectIn) 
	{
		_rect = rectIn;
		_rect.bottom = _rect.top + GetHeightOfControl(render, rectIn);
		_parent.SetChildWindowPos(positions, _ctrl, _rect);
	};	
};

class FrameCheckBox : public Frame
{
public:

	bool &_bData;
	CString _caption;
	unsigned _id;

	FrameCheckBox(IFrameParent &parent, const CString &strCaption, bool &bData) : 
		Frame(parent), 
		_caption(strCaption), 
		_bData(bData), 
		_id(GetUniqueId())
	{
	}




	void Render(IW::CRender &render)
	{
		IW::CDCRender dc(render);

		DWORD state = DFCS_BUTTONCHECK | DFCS_FLAT;
		if (_bData) state |= DFCS_CHECKED;
		DrawFrameControl(dc, CRect(_rect.TopLeft(), CSize(14, 14)), DFC_BUTTON, state);	

		CRect rectText(_rect);
		rectText.left += 16;
		render.DrawString(_caption, rectText, IW::Style::Font::Standard, IW::Style::Text::Normal, IW::Style::Color::TaskText);
	}

	int GetHeightOfControl(IW::CRender &render, const CRect &rectIn)
	{
		CRect rectText(rectIn);
		rectText.left += 16;
		const CRect rectTextOut = render.MeasureString(_caption, rectText, IW::Style::Font::Standard, IW::Style::Text::Normal);
		return rectTextOut.Height() + 4;
	}

	void SetPosition(IW::WindowPos &positions, IW::CRender &render, const CRect &rectIn) 
	{
		_rect = rectIn;
		_rect.bottom = _rect.top + GetHeightOfControl(render, rectIn);
		//_parent.SetChildWindowPos(positions, _ctrl, _rect);
	};	

	void MouseLeftButtonUp(const CPoint &point)
	{
		if (_rect.PtInRect(point))
		{
			_bData = !_bData;
			Invalidate();
		}
	}
};








template<class TBase = Frame, bool bSetCursor = true>
class FrameTracking : public TBase
{
public:	
	bool _bTracking;

	FrameTracking(IFrameParent &parent) : TBase(parent), _bTracking(false)
	{
	}

	void MouseLeftButtonDown(const CPoint &point, bool &bHandled)
	{
		if (_rect.PtInRect(point) != 0)
		{
			bHandled = true;
			SetTracking(true);
		}
	}

	void MouseLeftButtonUp(const CPoint &point)
	{
		SetTracking(false);
	}

	void MouseLeave()
	{
		SetTracking(false);
	}

	void SetTracking(bool bTracking)
	{
		if (_bTracking != bTracking)
		{
			if (bTracking)
			{
				CWindow wnd(_parent.GetHWnd());
				wnd.SetCapture();
			}
			else if (_bTracking)
			{
				::ReleaseCapture();
			}

			_bTracking = bTracking;
			Invalidate();
		}
	}
};

class FrameSlider : public FrameTracking<FrameHover<> > 
{
public:
	typedef FrameTracking<FrameHover<> >  BaseClass;

	class IChange
	{
	public:
		virtual void PosChanged(FrameSlider *pSender, int pos) = 0;
	};

protected:

	int _nPos;
	int _nMin;
	int _nMax;
	CString _strText;
	CRect _rectHandle;
	CRect _rectSlider;
	IChange *_pChange;

public:	

	FrameSlider(IFrameParent &parent, IChange *pChange, int pos = 100) : 
		BaseClass(parent),
		_nMin(10),
		_nMax(500),
		_nPos(pos),
		_pChange(pChange)
	{
	}

	void SetRange(int min, int max, bool bRedraw = true)
	{
		_nMin = min;
		_nMax = max;

		SetPos(_nPos, bRedraw);
	}

	void MouseMove(const CPoint &point)
	{
		SetHover(_rectHandle.PtInRect(point) != 0);

		if (_bHover) 
		{
			SetCursor(IW::Style::Cursor::LeftRight); 			
		}

		if (_bTracking) 
		{
			SetDevicePosition(point);
		}
	}

	void MouseLeftButtonDown(const CPoint &point, bool &bHandled)
	{
		if (_rect.PtInRect(point) != 0)
		{
			bHandled = true;
			SetTracking(true);
			SetDevicePosition(point);
		}
	}

	void MouseLeave()
	{
		SetTracking(false);
		SetHover(false);
	}

	void SetDevicePosition(const CPoint &point)
	{
		int nVal = MulDiv(point.x - _rectSlider.left, _nMax - _nMin, _rectSlider.Width());
		SetPos(_nMin + nVal);
		SetHover(_rectHandle.PtInRect(point) != 0);
		_pChange->PosChanged(this, _nPos);
	}

	void SetPos(int nVal, bool bRedraw = true)
	{
		const int nExisting = _nPos;
		_nPos = IW::Clamp(nVal, _nMin, _nMax);

		if (nExisting != _nPos && bRedraw)
		{
			Invalidate();
			GetHoverRect(_rectHandle);
		}
	}

	void SetPosition(IW::WindowPos &positions, IW::CRender &render, const CRect &rectIn)
	{
		CRect r = rectIn;
		r.bottom = r.top + 18;
		_rectSlider = _rect = r;
		_rectSlider.DeflateRect(HandleSize * 2, 1);
		GetHoverRect(_rectHandle);
	}

	void Render(IW::CRender &render)
	{
		const int height = 2;
		const int y = (_rect.top + _rect.bottom) / 2;

		CRect r(_rectSlider.left, y - height, _rectSlider.right, y + height);
		r.DeflateRect(1,1);

		render.DrawRect(r, IW::Emphasize(IW::Style::Color::TaskBackground, 64), IW::Emphasize(IW::Style::Color::TaskBackground, 32), 1);

		DWORD clrHandle = _bHover ? IW::Style::Color::Highlight : IW::Style::Color::Face;
		render.DrawRect(_rectHandle, IW::Emphasize(clrHandle), clrHandle, 1);
	};

	enum { HandleSize = 4 };

	void GetHoverRect(CRect &rect) const
	{
		const int nValue = IW::Clamp(_nPos, _nMin, _nMax);
		const int x = _rectSlider.left + MulDiv(_rectSlider.Width(), nValue - _nMin, _nMax - _nMin);
		const int y = (_rectSlider.top + _rectSlider.bottom) / 2;

		const int width = HandleSize;
		const int height = _rectSlider.Height() / 2;

		rect.left = x - width;
		rect.right = x + width;
		rect.top = y - height;
		rect.bottom = y + height;
	}
	
};


template <class T>
class FrameWindowImpl : public CCustomDraw<T>
{
public:

	typedef FrameWindowImpl<T> ThisClass;
	bool _bHover;

	typedef std::vector<Frame*> FRAMES;
	FRAMES _frames;

	FrameParent<T> _frameParent;

	BEGIN_MSG_MAP(ThisClass)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)	
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)	

		MESSAGE_HANDLER(WM_CTLCOLORDLG,       OnColorDlg) // For dialog background.
		MESSAGE_HANDLER(WM_CTLCOLORSTATIC,    OnColorDlg) // For static and read only edit.
		//MESSAGE_HANDLER(WM_CTLCOLOREDIT,      OnColorDlg) // For edit boxes
		MESSAGE_HANDLER(WM_CTLCOLORBTN,       OnColorDlg) // Owner-drawn only will respond.
		//MESSAGE_HANDLER(WM_CTLCOLORLISTBOX,   OnColorDlg) // List and combo.
		//MESSAGE_HANDLER(WM_CTLCOLORSCROLLBAR, OnColorDlg) // Scroll bars. No good for edit-box children.

		CHAIN_MSG_MAP(CCustomDraw<T>)
		
	END_MSG_MAP()

	FrameWindowImpl() :  
		_bHover(false),
		_frameParent(*static_cast<T*>(this))
	{		
	}

	bool PointOnFrame(const CPoint &point)
	{
		for(FRAMES::iterator it = _frames.begin(); it != _frames.end(); ++it)
		{
			Frame *pFrame = *it;
			if (pFrame->IsVisible() && pFrame->_rect.PtInRect(point))
				return true;
		}
		return false;
	}

	void Accept(FrameVisitor &visitor) 
	{
		for(FRAMES::iterator it = _frames.begin(); it != _frames.end(); ++it)
			(*it)->Accept(visitor);
	}

	void SizeClients()
	{
		T* pT = static_cast<T*>(this);

		CRect rect;
		pT->GetClientRect(rect);
		IW::WindowPos positions;

		CClientDC dc(pT->m_hWnd);
		IW::CRender render;

		if (render.Create(dc))
		{
			rect.DeflateRect(Frame::borderGap, Frame::borderGap);

			for(FRAMES::iterator it = _frames.begin(); it != _frames.end(); ++it)
			{
				Frame *pFrame = *it;

				if (pFrame->IsVisible())
				{
					pFrame->SetPosition(positions, render, rect);
				}
			}
		}

		positions.Apply();

		CPoint point;
		GetCursorPos(&point);
		pT->ScreenToClient(&point);
		pT->Accept(FrameVisitorMouseMove(point));
	}

	void SetHover(bool bHover)
	{
		_bHover = bHover;
	}

	DWORD OnPrePaint(int idCtrl, LPNMCUSTOMDRAW lpNMCustomDraw)
	{
		CDCHandle dc = lpNMCustomDraw->hdc;
		CRect rc = lpNMCustomDraw->rc;
		dc.FillSolidRect(rc, IW::Style::Color::TaskBackground);

		return CDRF_DODEFAULT;
	}

	LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		T* pT = static_cast<T*>(this);
		pT->ModifyStyleEx(0,WS_EX_CONTROLPARENT,0);
		bHandled = false;		
		return 0;
	}

	LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		bHandled = false;		
		return 0;
	}

	LRESULT OnEraseBackground(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		return 1;
	}

	LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		T* pT = static_cast<T*>(this);
		pT->LayoutFrames(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		bHandled = TRUE;
		return 1;
	}

	void LayoutFrames(int cx, int cy)
	{
	}


	LRESULT OnMouseLeave(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		T* pT = static_cast<T*>(this);
		pT->SetHover(false);
		pT->Accept(FrameVisitorMouseLeave());

		bHandled = FALSE;
		return 0;
	}

	LRESULT OnMouseMove(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
	{
		T* pT = static_cast<T*>(this);

		if (!_bHover)
		{
			pT->SetHover(true);

			TRACKMOUSEEVENT tme = { 0 };
			tme.cbSize = sizeof(tme);
			tme.dwFlags = TME_LEAVE;
			tme.hwndTrack = pT->m_hWnd;
			_TrackMouseEvent(&tme);
		}

		CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));	
		pT->DP2LP(point);
		pT->Accept(FrameVisitorMouseMove(point));

		bHandled = FALSE;
		return 0;
	}

	LRESULT OnLButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
	{		
		T* pT = static_cast<T*>(this);

		CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));	
		pT->DP2LP(point);
		pT->Accept(FrameVisitorMouseLeftButtonUp(point));

		bHandled = FALSE;
		return 0;
	}

	LRESULT OnLButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
	{		
		T* pT = static_cast<T*>(this);

		bool bHandledClick = false;
		CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));	
		pT->DP2LP(point);
		pT->Accept(FrameVisitorMouseLeftButtonDown(point, bHandledClick));

		bHandled = bHandledClick;
		return 0;
	}

	void DP2LP(CPoint &point)
	{
	}	

	void LP2DP(CPoint &point)
	{
	}

	void LP2DP(CRect &rect)
	{
	}

	LRESULT OnColorDlg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		if (wParam != NULL)
		{
			CDCHandle dc((HDC)wParam);
			dc.SetBkMode(TRANSPARENT);
			dc.SetTextColor(IW::Style::Color::TaskText);
		}

		return (LRESULT)(HBRUSH)IW::Style::Brush::TaskBackground;
	}

	
	void ResetLayout()
	{
		T* pT = static_cast<T*>(this);
		HWND hWnd = pT->m_hWnd;

		if (hWnd)
		{		
			pT->Accept(FrameVisitorActivate(hWnd));

			CRect rectClient;
			pT->GetClientRect(rectClient);
			pT->LayoutFrames(rectClient.Width(), rectClient.Height());
		}
	}
	
	void SetChildWindowPos(IW::WindowPos &positions, HWND hWnd, int x, int y, int cx, int cy)
	{
		positions.SetWindowPos(hWnd, NULL, x, y, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE);
	}
};

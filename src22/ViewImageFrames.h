// ImageWalker by Zac Walker
//
// Purpose: The navigator drawn over the image pane. 2.3's other overlays -- the
//          zoom slider, the histogram and the metadata panel -- are not here:
//          2.2 puts those in the gutter and in a docked panel.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "ViewLayout.h"
#include "Metadata.h"

template<class THost>
class FrameImageNavigate : 
	public FrameTracking<FrameHover<Frame, true> >
{
private:
	State &_state;
	THost *_pHost;
	ScalarAnimation _fade;
	ScalarAnimation _grow;
    
public:	

	typedef FrameTracking<FrameHover<Frame, true> > BaseClass;

	FrameImageNavigate(IFrameParent &parent, THost *pHost, State &state) : 
		BaseClass(parent), _state(state), _pHost(pHost),		
		_fade(0xA0, 0xF0, 0x10), _grow(50, 100, 20)
	{
		_bLayered = true;
	}

	void SetPosition(IW::WindowPos &positions, IW::CRender &render, const CRect &rectIn)
	{
		const IW::Image &image = _state.Image.GetThumbnailImage();

		CRect r(rectIn); 
		r.DeflateRect(1, 1);

		if (!image.IsEmpty())
		{
			CRect rectImage = _state.Image.GetThumbnailImage().GetBoundingRect();
			r.left = r.right - rectImage.Width();
			r.bottom = r.top + rectImage.Height();
		}

		r.InflateRect(1, 1);
		_rect = r;

	}

	CPoint GetPointDrawImage()
	{
		return CPoint( _rect.left + 1, _rect.top + 1);
	}
	
	CRect GetRectDrawImage()
	{
		const IW::Image &image = _state.Image.GetThumbnailImage();
		return CRect(GetPointDrawImage(), image.GetBoundingRect().Size());
	}

	CRect GetNavigationRect()
	{
		const IW::Image &image = _state.Image.GetThumbnailImage();

		CSize sizeCanvas = _pHost->GetCanvasSize();
		CRect rectClient(_pHost->GetScrollOffset(), _pHost->GetClientSize());
		CSize sizeWindow = image.GetBoundingRect().Size();

		return IW::MulDivRect(rectClient, sizeWindow, sizeCanvas) + GetPointDrawImage();
	}

	void Render(IW::CRender &renderIn)
	{
		IW::CRender render;
		IW::CDCRender dc(renderIn);

		if (render.Create(dc, _rect))
		{
			render.DrawRect(_rect, IW::Style::Color::TaskFrame, IW::Style::Color::TaskBackground, 1, true);
			RenderImage(render);

			CRect r = _rect;
			int pos = _grow.Pos();
			r.bottom = r.top + ((r.Height() * pos) / 100);
			r.left = r.right - ((r.Width() * pos) / 100);
			renderIn.DrawRender(render, _fade.Pos(), r);
		}
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

	void RenderImage(IW::CRender &render)
	{
		const IW::Image &image = _state.Image.GetThumbnailImage();

		if (!image.IsEmpty())
		{
			render.DrawImage(image.GetFirstPage(), GetPointDrawImage());

			const CRect rectImage = GetRectDrawImage();
			const CRect rectNavigation = IW::ClipRect(GetNavigationRect(), rectImage);

			render.Blend(RGB(128, 128, 128), CRect(rectImage.left, rectImage.top, rectImage.right, rectNavigation.top));
			render.Blend(RGB(128, 128, 128), CRect(rectImage.left, rectNavigation.bottom, rectImage.right, rectImage.bottom));
			render.Blend(RGB(128, 128, 128), CRect(rectImage.left, rectNavigation.top, rectNavigation.left, rectNavigation.bottom));
			render.Blend(RGB(128, 128, 128), CRect(rectNavigation.right, rectNavigation.top, rectImage.right, rectNavigation.bottom));
			render.DrawRect(rectNavigation, IW::Style::Color::Highlight);
		}
	}

	void MouseMove(const CPoint &point)
	{
		if (_bTracking) ScrollTo(point); 
		BaseClass::MouseMove(point);
	}

	void ScrollTo(const CPoint &point)
	{
		const CRect rectDrawnImage = GetRectDrawImage();
		const CRect rectOld = GetNavigationRect();
		const CRect rectNew = IW::ClampRect(CRect(point - IW::Half(rectOld.Size()), rectOld.Size()), rectDrawnImage);
		const CPoint pointLocalScroll = rectNew.TopLeft() - GetPointDrawImage(); 
		const CSize sizeAll = _pHost->GetCanvasSize();

		const CPoint pointToScrollTo(
			MulDiv(pointLocalScroll.x, sizeAll.cx, rectDrawnImage.Width()),
			MulDiv(pointLocalScroll.y, sizeAll.cy, rectDrawnImage.Height()));

		_pHost->ScrollTo(pointToScrollTo);
	}
};

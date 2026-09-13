// ImageWalker by Zac Walker
//
// Purpose: The overlays drawn on the image pane - captions, histogram,
//          buttons, sliders and the description panel.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "ViewLayout.h"
#include "Metadata.h"


class FrameEditableText : 
	public FrameHover<FrameTextT<FrameEditableText> >
{
public:

	typedef FrameHover<FrameTextT<FrameEditableText> > BaseClass;

	class IChange
	{
	public:
		virtual void OnEditText(FrameEditableText *pChange) = 0;
	};

	CString _str;
	IW::Style::Text::Style _stringformat;
	IChange *_pChangeEvents;	

	FrameEditableText(IFrameParent &parent, IChange *pChangeEvents = 0) : 
		BaseClass(parent),
		_pChangeEvents(pChangeEvents),
		_stringformat(IW::Style::Text::Normal)
	{
	}

	int GetMaxTextLengt() const
	{
		return INT_MAX;
	}

	void SetText(const CString &str)
	{	
		_str = str;
		_parent.ResetLayout();
	}

	CString GetText() const
	{
		return _str.IsEmpty() ? _T("No description") : _str;
	}


	IW::Style::Font::Type GetFont()
	{
		return _bHover ? IW::Style::Font::LinkHover : IW::Style::Font::Link;
	}

	void SetPosition(IW::WindowPos &positions, IW::CRender &render, const CRect &rectIn)
	{
		_rect = render.MeasureString(GetText(), rectIn, GetFont(), _stringformat);
		//_rect.OffsetRect(0, rectIn.Height() - _rect.Height()); 
	}

	void MouseLeftButtonUp(const CPoint &point)
	{
		if (_rect.PtInRect(point))
		{
			if (_pChangeEvents) _pChangeEvents->OnEditText(this);
		}
	}
};

template<class THost>
class FrameScale : public Frame, public FrameSlider::IChange
{
public:

	FrameText _title;
	FrameSlider _slider;
	FrameTextCommand _text;	

	THost *_pHost;

	FrameScale(IFrameParent &parent, THost *pHost) : 
		_pHost(pHost), 
		Frame(parent),
		_slider(parent, this, 0), 
		_title(parent, _T("Scale:")), 
		_text(parent, _T(""), ID_SCALE_TOGGLE)
	{
		OnScaleChange();
	}

	void SetPosition(IW::WindowPos &positions, IW::CRender &render, const CRect &rectIn)
	{
		const int cxTitle = _title.GetSize(render, 100).cx;
		const int cxText = cxTitle;

		CRect rectTitle(rectIn.left, rectIn.top + padding, rectIn.left + cxTitle, rectIn.bottom);
		CRect rectSlider(rectIn.left + cxTitle,  rectIn.top, rectIn.right - cxText, rectIn.bottom);
		CRect rectDelay(rectIn.right - cxText, rectIn.top + padding, rectIn.right, rectIn.bottom);

		_title.SetPosition(positions, render, rectTitle);	
		_slider.SetPosition(positions, render, rectSlider);	
		_text.SetPosition(positions, render, rectDelay);
		_text._rect.right = _text._rect.left + cxText;

		_rect = rectIn;
		_rect.bottom = _rect.top + 16; 
	}

	void Accept(FrameVisitor &visitor) 
	{
		visitor.Visit(this); 
		
		_title.Accept(visitor);
		_slider.Accept(visitor);
		_text.Accept(visitor);
		
		visitor.AfterVisit(this);
	}

	void PosChanged(FrameSlider *pSender, int pos)
	{
		_pHost->SetScale(pos);
	}

	void OnScaleChange()
	{
		_text.SetText(_pHost->GetScaleText());
		Invalidate();
	}	
};


class  FrameButtonBar : public FrameArray
{
public:	

	enum { padding = 4 };
	
	class FrameButton : public FrameTracking<FrameHover<Frame, true> >
	{
	public:

		typedef FrameTracking<FrameHover<Frame, true> > BaseClass;

		int _id;
		int _nImage;

		FrameButton(IFrameParent &parent, const CString &str, int id, int nImage) : 
			BaseClass(parent), 
			_id(id),
			_nImage(nImage)
		{
		}

		void SetPosition(IW::WindowPos &positions, IW::CRender &render, const CRect &rectIn)
		{
			_rect = rectIn;
			_rect.left = _rect.right - (16 + (padding * 2));
			_rect.top = _rect.bottom - (16 + (padding * 2));
		}

		void Render(IW::CRender &render)
		{
			int x = _rect.left + padding;
			int y = _rect.top + padding;

			if (_bTracking)
			{
				x += 1;
				y += 1;
			}

			render.DrawImageList(App.GetGlobalBitmap(), _nImage, x, y, 16, 16);
		}

		void Erase(IW::CRender &render)
		{
			if (_bHover || _bTracking)
			{
				render.DrawRect(_rect, IW::Emphasize(IW::Style::Color::Highlight), IW::Style::Color::Highlight, 1, true, _bTracking);
			}
		}

		void GetToolTipId(int &id, bool &bHandled)
		{
			if (_bHover)
			{
				id = _id;
				bHandled = true;
			}
		}

		void MouseLeftButtonUp(const CPoint &point)
		{
			if (_rect.PtInRect(point) && _bTracking)
			{
				_parent.SignalCommand(_id);
			}
			SetTracking(false);
		}
	};

	FrameButtonBar(IFrameParent &parent) : FrameArray(parent)
	{
	}

	void AddLink(const CString &str, int id, int nImage)
	{
		FrameButton *pCommand = new FrameButton(_parent, str, id, nImage);
		AddFrame(pCommand);
	}

	void SetPosition(IW::WindowPos &positions, IW::CRender &render, const CRect &rectIn)
	{
		CRect r = _rect = rectIn;
		int top = r.bottom;
		int left = r.right;

		for(FRAMES::iterator i = _frames.begin(); i != _frames.end(); i++)
		{
			Frame *pFrame = *i;
			pFrame->SetPosition(positions, render, r);
			left = pFrame->_rect.left;
			r.right = left;
			top = IW::Min(top, pFrame->_rect.top);
		} 

		_rect.left = left;
		_rect.top = top;
	}
};




class FrameHistogram : public Frame
{
public:

	const IW::Histogram *_pHistogram;
	IW::Image _image;
	enum { ImageHeight = 100 };

	FrameHistogram(IFrameParent &parent) : Frame(parent), _pHistogram(0)
	{
		_image.CreatePage(IW::Histogram::MaxValue, ImageHeight, IW::PixelFormat::PF32);
	}

	virtual ~FrameHistogram() 
	{
	}

	void SetPosition(IW::WindowPos &positions, IW::CRender &render, const CRect &rectIn) 
	{
		_rect = rectIn;
		_rect.bottom = _rect.top + 80;
	};

	void SetHistogram(const IW::Histogram &histogram)
	{
		_pHistogram = &histogram;
		RenderBitmap();
	}

	void RenderBitmap()
	{
		float max = fastSqrt((float)_pHistogram->_max);	
		float yratio = static_cast<float>(ImageHeight) / max;
		for(int y = 0; y < ImageHeight; y++)
		{
			LPCOLORREF pLine = (LPCOLORREF)_image.GetFirstPage().GetBitmapLine((ImageHeight - 1) - y);

			for(int x = 0; x < IW::Histogram::MaxValue; x++)
			{
				int r = (yratio * fastSqrt((float)_pHistogram->_r[x])) > y ? 0xFF : 0x00;
				int g = (yratio * fastSqrt((float)_pHistogram->_g[x])) > y ? 0xFF : 0x00;
				int b = (yratio * fastSqrt((float)_pHistogram->_b[x])) > y ? 0xFF : 0x00;

				pLine[x] = IW::RGBA(r,g,b);
			}
		}
	}

	void Render(IW::CRender &render)
	{
		render.DrawImage(_image.GetFirstPage(), _rect);
	}

private:

	//sqrt(fX)
	inline float fastSqrt(float fX)
	{
		float tmp = fX;
		float fHalf = 0.5f*fX;
		int i = *reinterpret_cast<int*>(&fX);
		i = 0x5f3759df - (i >> 1); // This line hides a LOT of math!
		fX = *reinterpret_cast<float*>(&i);

		// repeat this statement for a better approximation
		fX = fX*(1.5f - fHalf*fX*fX); 
		fX = fX*(1.5f - fHalf*fX*fX);
		return tmp * fX;
	}

	
};

class FrameImageInfo : 
	public FrameGroup,
	public FrameEditableText::IChange
{
public:	

	FrameText _text;
	FrameHistogram _histogram;	
	FrameEditableText _description;
	FrameText _metaData;
	CString _str;
	CString _strMeta;
	State &_state;

	FrameImageInfo(IFrameParent &parent, State &state) : 
		FrameGroup(parent, _T("Image")),
		_text(parent),
		_histogram(parent),
		_description(parent, this),
		_metaData(parent),
		_state(state)
	{
		AddFrame(&_histogram);
		AddFrame(&_text);
		AddFrame(&_description);
		AddFrame(&_metaData);
	}

	~FrameImageInfo()
	{
		Clear();
	}

	void Refresh()
	{
		const long nLevel = App.Settings.ImageDetailLevel;

		_linkBar.RemoveAll();
		_linkBar.AddLink(_T("Close"), ID_VIEW_DESCRIPTION);
		if (nLevel > 0) _linkBar.AddLink(_T("Less"), ID_VIEW_LESSIMAGE);
		if (nLevel < AppSettings::ImageDetailMax) _linkBar.AddLink(_T("More"), ID_VIEW_ADVANCEDIMAGE);

		_str.Empty();

		const IW::Image &image = _state.Image.GetImage(); 
		IW::CameraSettings settings = image.GetCameraSettings();

		SetText(_state.Image.GetTitle());
		_histogram.SetHistogram(_state.Image.GetHistogram());			

		AddProperty(_str, _T("Aperture: "), settings.FormatAperture());
		AddProperty(_str, _T("ISO: "), settings.FormatIsoSpeed());
		AddProperty(_str, _T("Exposure: "), settings.FormatExposureTime());
		AddProperty(_str, _T("Focal Length: "), settings.FormatFocalLength());

		_histogram.SetVisible(nLevel > 0);
		_text.SetVisible(nLevel > 0);
		_text.SetText(_str);

		_metaData.SetVisible(nLevel >= AppSettings::ImageDetailMax);
		if (nLevel >= AppSettings::ImageDetailMax) BuildMetaData(image);
		_metaData.SetText(_strMeta);

		_description.SetText(_state.Image.GetDescription());
	}

	void SetPosition(IW::WindowPos &positions, IW::CRender &render, const CRect &rectIn)
	{
		CRect r = rectIn;
		r.right = r.left + 220;

		if (_metaData.IsVisible())
		{
			// The pane does not scroll, so lay out without the list first to find
			// what room is left for it, then fit as many lines as will go.
			_metaData.SetText(CString());
			FrameGroup::SetPosition(positions, render, r);
			FitToHeight(render, r.Width() - (paddingLarge * 2), rectIn.bottom - _rect.bottom);
		}

		FrameGroup::SetPosition(positions, render, r);
	}

	void FitToHeight(IW::CRender &render, int cx, int cyAvailable)
	{
		CString str = _strMeta;
		_metaData.SetText(str);

		while (!str.IsEmpty() && _metaData.GetSize(render, cx).cy > cyAvailable)
		{
			const int nCut = str.ReverseFind(_T('\n'));
			str = (nCut < 0) ? CString() : str.Left(nCut);
			_metaData.SetText(str);
		}
	}

	static void AddProperty(CString &str, const CString &strTitle, const CString &strProperty)
	{
		if (!strProperty.IsEmpty())
		{
			if (!str.IsEmpty()) str += _T("\n");
			str += strTitle;
			str += strProperty;			
		}
	}	

	// The same fields the description dialog lists, read straight from the file's
	// IPTC and XMP. Nothing here writes them back.
	void BuildMetaData(const IW::Image &image)
	{
		_strMeta.Empty();

		ImageMetaData metaData(image);

		AddProperty(_strMeta, _T("Title: "), metaData.GetTitle());
		AddProperty(_strMeta, _T("Object Name: "), metaData.GetObjectName());
		AddProperty(_strMeta, _T("Tags: "), metaData.GetTags());
		AddProperty(_strMeta, _T("Category: "), metaData.GetCategory());
		AddProperty(_strMeta, _T("Sub Category: "), metaData.GetSubCategory());
		AddProperty(_strMeta, _T("Date Created: "), metaData.GetDateCreated());
		AddProperty(_strMeta, _T("Byline: "), metaData.GetByLine());
		AddProperty(_strMeta, _T("Byline Title: "), metaData.GetByLineTitle());
		AddProperty(_strMeta, _T("Caption Writer: "), metaData.GetCaptionWriter());
		AddProperty(_strMeta, _T("Credit: "), metaData.GetCredit());
		AddProperty(_strMeta, _T("Source: "), metaData.GetSource());
		AddProperty(_strMeta, _T("Copyright: "), metaData.GetCopyright());
		AddProperty(_strMeta, _T("City: "), metaData.GetCity());
		AddProperty(_strMeta, _T("Province/State: "), metaData.GetProvenceState());
		AddProperty(_strMeta, _T("Country: "), metaData.GetCountryName());
		AddProperty(_strMeta, _T("Instructions: "), metaData.GetSpecialInstructions());
		AddProperty(_strMeta, _T("Transmission Ref: "), metaData.GetOriginalTR());
	}

	void OnEditText(FrameEditableText *pChange)
	{
		_parent.SignalCommand(ID_IMAGE_EDITDESCRIPTION);
	}
};



class FrameMood : public FrameGroup, public FrameSlider::IChange
{
public:	

	class FrameBlackCheckBox : public FrameCheckBox
	{
	public:


		FrameBlackCheckBox(IFrameParent &parent, const CString &strCaption, bool &bData) : FrameCheckBox(parent, strCaption, bData)
		{
		}

		void MouseLeftButtonUp(const CPoint &point)
		{
			if (_rect.PtInRect(point))
			{
				_bData = !_bData;
				Invalidate();

				IW::Style::SetMood();
				InvalidateApplication();
			}
		}

		void InvalidateApplication()
		{
			CWindow wnd(_parent.GetHWnd());
			wnd = wnd.GetTopLevelWindow();

			wnd.SetWindowRgn(NULL);
			wnd.SetWindowPos(NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
			wnd.RedrawWindow(NULL, NULL, RDW_ALLCHILDREN | RDW_INVALIDATE | RDW_FRAME);
		}
	};



	FrameSlider _slider;
	FrameBlackCheckBox _black;
	FrameBlackCheckBox _skin;

	FrameMood(IFrameParent &parent) : FrameGroup(parent, _T("Mood")),
		_slider(parent, this),
		_black(parent, _T("Black background"), App.Settings.BlackBackground),
		_skin(parent, _T("Black skin"), App.Settings.BlackSkin)
	{
		_slider.SetRange(0, 255, false);
		_slider.SetPos(App.Settings.Mood, false);

		AddFrame(&_slider);
		AddFrame(&_black);
		AddFrame(&_skin);
	}

	~FrameMood()
	{
		Clear();
	}

	void Accept(FrameVisitor &visitor) 
	{		
		visitor.Visit(this);

		_title.Accept(visitor);
		_slider.Accept(visitor);
		_black.Accept(visitor);
		_skin.Accept(visitor);

		visitor.AfterVisit(this);
	};

	void PosChanged(FrameSlider *pSender, int pos)
	{
		App.Settings.Mood = pos;
		IW::Style::SetMood();
		InvalidateApplication();
	}

	void InvalidateApplication()
	{
		CWindow wnd(_parent.GetHWnd());
		wnd.GetTopLevelWindow().RedrawWindow(NULL, NULL, RDW_ALLCHILDREN | RDW_INVALIDATE);
	}

	CRect GetRenderRect() const
	{
		CRect r = _rect;
		int pos = _grow.Pos();
		int scale = 10;
		int cx = (r.Width() / scale) - ((r.Width() * pos) / (100 * scale));
		int cy = (r.Height() / scale) - ((r.Height() * pos) / (100 * scale));
		r.DeflateRect(cx, cy);
		return r;
	}
};



class FrameHelp : public FrameGroup
{
public:

	FrameHelp(IFrameParent &parent) : FrameGroup(parent, _T("ImageWalker"))
	{
		
	}

	void Refresh()
	{
		RemoveAll();

		AddFrame(new FrameText(_parent, App.LoadString(IDS_START_FREE)));
		AddFrame(new FrameCommand(_parent, ImageIndex::Help, _T("Click here for help"), ID_HELP_FINDER));
	}

	CRect GetRenderRect() const
	{
		CRect r = _rect;
		int pos = _grow.Pos();
		int scale = 10;
		int cx = (r.Width() / scale) - ((r.Width() * pos) / (100 * scale));
		int cy = (r.Height() / scale) - ((r.Height() * pos) / (100 * scale));
		r.DeflateRect(cx, cy);
		return r;
	}
};

class FrameTip : public FrameGroup
{
public:

	FrameTip(IFrameParent &parent) : FrameGroup(parent, _T("Tip"))
	{
		static int tips[] = {IDS_TIP1, IDS_TIP2, IDS_TIP3, IDS_TIP4, IDS_TIP5, IDS_TIP6 };

		srand(GetTickCount());
		int tip = (rand() * countof(tips)) / RAND_MAX;

		AddFrame(new FrameText(parent, App.LoadString(tips[tip])));		
	}

	CRect GetRenderRect() const
	{
		CRect r = _rect;
		int pos = _grow.Pos();
		int scale = 10;
		int cx = (r.Width() / scale) - ((r.Width() * pos) / (100 * scale));
		int cy = (r.Height() / scale) - ((r.Height() * pos) / (100 * scale));
		r.DeflateRect(cx, cy);
		return r;
	}
};


class FrameImageWalker : public FrameArray
{
public:
	FrameHelp _help;
	FrameMood _mood;
	FrameTip _tip;

	FrameImageWalker(IFrameParent &parent) : FrameArray(parent),
		_help(parent),
		_mood(parent),
		_tip(parent)
	{
		
	}

	~FrameImageWalker()
	{
		Clear();
	}

	void Refresh()
	{
		Clear();

		AddFrame(&_help);
		AddFrame(&_mood);
		AddFrame(&_tip);

		_help.Refresh();
	}

	void SetPosition(IW::WindowPos &positions, IW::CRender &render, const CRect &rectIn)
	{
		CRect r = rectIn;
		r.DeflateRect(20, 10);
		_rect = r;
		_help.SetPosition(positions, render, r);

		r.top = _help._rect.bottom;
		r.DeflateRect(20, 10);

		CRect rMood = r;
		CRect rTip = r;
		rMood.right = (rMood.left + rMood.right) / 2;
		rTip.left = (rTip.left + rTip.right) / 2;
		
		_mood.SetPosition(positions, render, rMood);
		_tip.SetPosition(positions, render, rTip);

		_rect.bottom = IW::Max(_mood._rect.bottom, _tip._rect.bottom);
	}
};



// The way back to the image panel once it has been closed. Without it the only
// way to bring the description and the histogram back is a menu item, and a
// panel with a Close link and no visible opener is a one-way door.
class FrameShowImageInfo : public FrameTracking<FrameHover<Frame, true> >
{
public:

	typedef FrameTracking<FrameHover<Frame, true> > BaseClass;

	enum { extent = 20 };

	FrameShowImageInfo(IFrameParent &parent) : BaseClass(parent)
	{
	}

	void SetPosition(IW::WindowPos &positions, IW::CRender &render, const CRect &rectIn)
	{
		_rect = CRect(rectIn.left, rectIn.top, rectIn.left + extent, rectIn.top + extent);
	}

	void Erase(IW::CRender &render)
	{
		render.DrawRect(_rect, IW::Style::Color::TaskFrame,
		                _bHover ? IW::Emphasize(IW::Style::Color::TaskBackground)
		                        : IW::Style::Color::TaskBackground,
		                1, true, !_bTracking);
	}

	void Render(IW::CRender &render)
	{
		// Measured rather than DT_VCENTER'd: the render helper only centres
		// horizontally, and an off-centre plus in a 20 pixel chip is obvious.
		const CRect rectMeasured = render.MeasureString(_T("+"), _rect,
		                                               IW::Style::Font::Heading,
		                                               IW::Style::Text::NormalCentre);

		CRect r(_rect);
		r.top += IW::Half(_rect.Height() - rectMeasured.Height());
		r.bottom = r.top + rectMeasured.Height();

		render.DrawString(_T("+"), r, IW::Style::Font::Heading,
		                  IW::Style::Text::NormalCentre, IW::Style::Color::TaskText);
	}

	void GetToolTipId(int &id, bool &bHandled)
	{
		if (_bHover)
		{
			id = ID_VIEW_DESCRIPTION;
			bHandled = true;
		}
	}

	void MouseLeftButtonUp(const CPoint &point)
	{
		if (_rect.PtInRect(point) && _bTracking)
		{
			_parent.SignalCommand(ID_VIEW_DESCRIPTION);
		}

		SetTracking(false);
	}
};

template<class THost>
class FrameImageCtrl : 
	public FrameHover<Frame, false>
{
protected:

	FrameScale<THost> _scale;
	FrameButtonBar _links;
	ScalarAnimation _fade;
	ScalarAnimation _grow;
    
public:	

	FrameImageCtrl(IFrameParent &parent, THost *pHost, State &state) : 
		FrameHover<Frame, false>(parent), 
		_scale(parent, pHost),
		_links(parent),		
		_fade(0xA0, 0xF0, 0x10),
		_grow(50, 100, 20)
	{
		_bLayered = true;

		_links.AddLink(_T("Show"), ID_VIEW_IMAGEFULLSCREEN, ImageIndex::ShowFullScreen);
		_links.AddLink(_T("Rotate Left"), ID_EDIT_ROTATELEFT, ImageIndex::FilterRotateLeft);
		_links.AddLink(_T("Rotate Right"), ID_EDIT_ROTATERIGHT, ImageIndex::FilterRotateRight);
		_links.AddLink(_T("Edit"), ID_VIEW_EDIT, ImageIndex::FilterCrop);
	}

	void SetPosition(IW::WindowPos &positions, IW::CRender &render, const CRect &rectIn)
	{
		CRect r(rectIn); 
		r.DeflateRect(paddingLarge, paddingLarge);		
		_rect = r;		

		CRect rectLinks = r;
		_links.SetPosition(positions, render, rectLinks);
		
		r.right = _links._rect.left - padding;
		r.top = _links._rect.top;
		int cx = IW::Min(r.Width(), 250);
		r.left = r.right - cx;
		_scale.SetPosition(positions, render, r);

		_rect.left = r.left;
		_rect.top = r.top;
		_rect.InflateRect(paddingLarge, paddingLarge);
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

			CRect r = _rect;
			int pos = _grow.Pos();
			r.top = r.bottom - ((r.Height() * pos) / 100);
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

	void OnScaleChange()
	{
		_scale.OnScaleChange();
	}

	void Accept(FrameVisitor &visitor) 
	{		
		visitor.Visit(this);
		AcceptChildren(visitor);		
		visitor.AfterVisit(this);
	}

	void AcceptChildren(FrameVisitor &visitor) 
	{
		_scale.Accept(visitor);
		_links.Accept(visitor);
	}
};

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

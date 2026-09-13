// ImageWalker by Zac Walker
//
// Purpose: The docked description panel - the General, Details, IPTC, XMP and
//          EXIF pages for one image.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "ViewModelImage.h"
#include "ViewMetadataList.h"
#include "Metadata.h"
#include "MetadataExif.h"
#include "MetadataIPTC.h"
#include "MetadataXMP.h"

#ifndef ETDT_ENABLETAB
#define ETDT_ENABLETAB 6
#endif

// Designer coordinates are the layout; each control then follows one edge of
// the page as the panel is resized. A page that cannot get any shorter than
// cyMin scrolls instead, which is the only way the fifteen-row Details page
// fits under a picture.
class CPageLayout
{
public:

	enum
	{
		StretchX = 0x01,
		StretchY = 0x02,
		MoveX = 0x04,
		MoveY = 0x08
	};

	struct Anchor
	{
		int id;
		DWORD flags;
	};

	struct Item
	{
		HWND hWnd;
		DWORD flags;
		CRect rect;
	};

	HWND _hWndPage = nullptr;
	std::vector<Item> _items;
	CSize _sizeInit = CSize(0, 0);
	int _cyMin = 0;
	int _yScroll = 0;

	// Every anchored control needs a real id - IDC_STATIC resolves to whichever
	// label the dialog manager finds first, so a shared id would move one label
	// and leave the rest of them behind.
	void Init(HWND hWndPage, const Anchor *anchors, int nCount, int cyMin)
	{
		_hWndPage = hWndPage;
		_cyMin = cyMin;
		_yScroll = 0;
		_items.clear();

		CRect rectClient;
		::GetClientRect(hWndPage, rectClient);
		_sizeInit = rectClient.Size();

		for (int i = 0; i < nCount; i++)
		{
			const HWND hWnd = ::GetDlgItem(hWndPage, anchors[i].id);

			if (hWnd == nullptr)
				continue;

			CRect rect;
			::GetWindowRect(hWnd, rect);
			::MapWindowPoints(nullptr, hWndPage, reinterpret_cast<LPPOINT>(&rect), 2);

			const Item item = {hWnd, anchors[i].flags, rect};
			_items.push_back(item);
		}
	}

	bool CanScroll() const
	{
		return _cyMin > 0;
	}

	int ContentHeight(int cy) const
	{
		return IW::Max(cy, _cyMin);
	}

	// Raising the scroll bar takes its width off the client, so the caller has
	// to measure again between this and Apply.
	void UpdateScrollBar(int cy)
	{
		if (!CanScroll() || _hWndPage == nullptr)
			return;

		const int cyContent = ContentHeight(cy);
		_yScroll = IW::Min(_yScroll, IW::Max(0, cyContent - cy));

		SCROLLINFO si = {sizeof(SCROLLINFO)};
		si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
		si.nMin = 0;
		si.nMax = IW::Max(0, cyContent - 1);
		si.nPage = IW::Max(0, cy);
		si.nPos = _yScroll;

		::SetScrollInfo(_hWndPage, SB_VERT, &si, TRUE);
	}

	void Apply(int cx, int cy)
	{
		if (_items.empty() || _sizeInit.cx <= 0)
			return;

		const int dx = cx - _sizeInit.cx;
		const int dy = ContentHeight(cy) - _sizeInit.cy;

		HDWP hdwp = ::BeginDeferWindowPos(static_cast<int>(_items.size()));

		for (const Item &item : _items)
		{
			CRect rect(item.rect);

			if (item.flags & StretchX) rect.right += dx;
			if (item.flags & MoveX) rect.OffsetRect(dx, 0);
			if (item.flags & StretchY) rect.bottom += dy;
			if (item.flags & MoveY) rect.OffsetRect(0, dy);

			rect.right = IW::Max(rect.right, rect.left + 4);
			rect.bottom = IW::Max(rect.bottom, rect.top + 4);
			rect.OffsetRect(0, -_yScroll);

			if (hdwp != nullptr)
			{
				hdwp = ::DeferWindowPos(hdwp, item.hWnd, nullptr, rect.left, rect.top,
				                        rect.Width(), rect.Height(),
				                        SWP_NOZORDER | SWP_NOACTIVATE);
			}
			else
			{
				::SetWindowPos(item.hWnd, nullptr, rect.left, rect.top,
				               rect.Width(), rect.Height(),
				               SWP_NOZORDER | SWP_NOACTIVATE);
			}
		}

		if (hdwp != nullptr)
			::EndDeferWindowPos(hdwp);
	}

	void ScrollTo(int y, int cy)
	{
		if (!CanScroll() || _hWndPage == nullptr)
			return;

		const int yNew = IW::Min(IW::Max(y, 0), IW::Max(0, ContentHeight(cy) - cy));

		if (yNew == _yScroll)
			return;

		const int dy = _yScroll - yNew;
		_yScroll = yNew;

		::SetScrollPos(_hWndPage, SB_VERT, _yScroll, TRUE);
		::ScrollWindow(_hWndPage, 0, dy, nullptr, nullptr);
	}

	int TrackPos() const
	{
		SCROLLINFO si = {sizeof(SCROLLINFO)};
		si.fMask = SIF_TRACKPOS;
		return ::GetScrollInfo(_hWndPage, SB_VERT, &si) ? si.nTrackPos : _yScroll;
	}
};

// The behaviour every page shares: stretch to the panel, and scroll when the
// panel is shorter than the form. The pages themselves are the ones the modal
// dialog used - the panel replaced the dialog, not the pages.
template<class T>
class CDescriptionPageImpl : public CDialogImpl<T>
{
public:

	typedef CDialogImpl<T> BaseClass;

	CPageLayout _layout;

	BEGIN_MSG_MAP(CDescriptionPageImpl)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_VSCROLL, OnVScroll)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
	END_MSG_MAP()

	void InitLayout(const CPageLayout::Anchor *anchors, int nCount, int cyMin)
	{
		T *pT = static_cast<T*>(this);

		if (cyMin > 0)
			pT->ModifyStyle(0, WS_VSCROLL);

		_layout.Init(pT->m_hWnd, anchors, nCount, cyMin);
	}

	void ResetScroll()
	{
		T *pT = static_cast<T*>(this);

		if (pT->m_hWnd == nullptr || !_layout.CanScroll())
			return;

		_layout._yScroll = 0;
		LayoutPage();
		pT->Invalidate();
	}

	void LayoutPage()
	{
		T *pT = static_cast<T*>(this);

		CRect rect;
		pT->GetClientRect(rect);
		_layout.UpdateScrollBar(rect.Height());

		// Between those two calls the scroll bar may have appeared or gone.
		pT->GetClientRect(rect);
		_layout.Apply(rect.Width(), rect.Height());
	}

	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL &bHandled)
	{
		LayoutPage();
		bHandled = FALSE;
		return 0;
	}

	// A multiline edit has a scroll bar of its own and sends this too; only the
	// page's own bar has a null lParam.
	LRESULT OnVScroll(UINT, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
	{
		if (lParam != 0 || !_layout.CanScroll())
		{
			bHandled = FALSE;
			return 0;
		}

		T *pT = static_cast<T*>(this);

		CRect rect;
		pT->GetClientRect(rect);

		const int cy = rect.Height();
		int y = _layout._yScroll;

		switch (LOWORD(wParam))
		{
		case SB_LINEUP: y -= App.m_nTextExtent; break;
		case SB_LINEDOWN: y += App.m_nTextExtent; break;
		case SB_PAGEUP: y -= cy; break;
		case SB_PAGEDOWN: y += cy; break;
		case SB_TOP: y = 0; break;
		case SB_BOTTOM: y = _layout.ContentHeight(cy); break;
		case SB_THUMBTRACK:
		case SB_THUMBPOSITION: y = _layout.TrackPos(); break;
		default: return 0;
		}

		_layout.ScrollTo(y, cy);
		return 0;
	}

	LRESULT OnMouseWheel(UINT, WPARAM wParam, LPARAM, BOOL &bHandled)
	{
		if (!_layout.CanScroll())
		{
			bHandled = FALSE;
			return 0;
		}

		T *pT = static_cast<T*>(this);

		CRect rect;
		pT->GetClientRect(rect);

		const int nDelta = GET_WHEEL_DELTA_WPARAM(wParam);

		_layout.ScrollTo(_layout._yScroll - MulDiv(nDelta, App.m_nTextExtent * 3, WHEEL_DELTA),
		                 rect.Height());

		return 0;
	}
};


template<class TParent>
class CDescriptionMainPage : public CDescriptionPageImpl<CDescriptionMainPage<TParent> >
{
public:
	typedef CDescriptionMainPage<TParent> ThisClass;
	typedef CDescriptionPageImpl<ThisClass> BaseClass;

	TParent &_parent;

	enum { IDD = IDD_DESCRIPTION_MAIN };

	CDescriptionMainPage(TParent &parent) : _parent(parent)
	{
	}

	BEGIN_MSG_MAP(ThisClass)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)

		COMMAND_HANDLER(IDC_RESOLUTION, CBN_SELCHANGE, OnChangeSelect)

		CHAIN_MSG_MAP(BaseClass)
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		CComboBox comboRes = GetDlgItem(IDC_RESOLUTION);
		comboRes.AddString(App.LoadString(IDS_PIXELS_PER_INCH));
		comboRes.AddString(App.LoadString(IDS_PIXELS_PER_CM));

		// The caption takes every pixel the panel has over; everything above it
		// keeps the height the designer gave it. Nothing is bottom-anchored, so
		// no label on this page needs an id of its own.
		static const CPageLayout::Anchor anchors[] =
		{
			{IDC_HEADLINE, CPageLayout::StretchX},
			{IDC_TAGS, CPageLayout::StretchX},
			{IDC_RESOLUTION, CPageLayout::StretchX},
			{IDC_CAPTION_LABEL, CPageLayout::StretchX},
			{IDC_CAPTION, CPageLayout::StretchX | CPageLayout::StretchY}
		};

		InitLayout(anchors, _countof(anchors), 0);

		OnPopulate();

		bHandled = FALSE;
		return 0;
	}

	void OnPopulate()
	{
		CComboBox comboRes = GetDlgItem(IDC_RESOLUTION);
		comboRes.SetCurSel(App.Settings.m_nResolutionSelection);

		const IW::Image &image = _parent._state.GetImage();

		ImageMetaData metaData(image);

		SetDlgItemText(IDC_HEADLINE, metaData.GetTitle());
		SetDlgItemText(IDC_TAGS, metaData.GetTags());
		SetDlgItemText(IDC_CAPTION, metaData.GetDescription());

		SetResolution(CSize(image.GetXPelsPerMeter(), image.GetYPelsPerMeter()));
	}

	LRESULT OnChangeSelect(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		CComboBox comboRes = GetDlgItem(IDC_RESOLUTION);

		CSize sizePelsPerMeter = GetResolution();
		App.Settings.m_nResolutionSelection = comboRes.GetCurSel();
		SetResolution(sizePelsPerMeter);

		return 0;
	}

	void SetResolution(CSize sizePelsPerMeter)
	{
		int cx, cy;

		if (App.Settings.m_nResolutionSelection == 0)
		{
			cx = IW::MeterToInch(sizePelsPerMeter.cx);
			cy = IW::MeterToInch(sizePelsPerMeter.cy);
		}
		else
		{
			cx = IW::MeterToCM(sizePelsPerMeter.cx);
			cy = IW::MeterToCM(sizePelsPerMeter.cy);
		}

		SetDlgItemInt(IDC_CX, cx);
		SetDlgItemInt(IDC_CY, cy);
	}

	CSize GetResolution()
	{
		CSize sizePelsPerMeter;
		CString str;
		GetDlgItemText(IDC_CX, str);
		int cx = _ttol(str);

		GetDlgItemText(IDC_CY, str);
		int cy = _ttol(str);

		if (App.Settings.m_nResolutionSelection == 0)
		{
			sizePelsPerMeter.cx = IW::InchToMeter(cx);
			sizePelsPerMeter.cy = IW::InchToMeter(cy);
		}
		else
		{
			sizePelsPerMeter.cx = IW::CMToMeter(cx);
			sizePelsPerMeter.cy = IW::CMToMeter(cy);
		}

		return sizePelsPerMeter;
	}
};


template<class TParent>
class CDescriptionDetailsPage : public CDescriptionPageImpl<CDescriptionDetailsPage<TParent> >
{
public:
	typedef CDescriptionDetailsPage<TParent> ThisClass;
	typedef CDescriptionPageImpl<ThisClass> BaseClass;

	TParent &_parent;

	enum { IDD = IDD_DESCRIPTION_DETAILS };

	CDescriptionDetailsPage(TParent &parent) : _parent(parent)
	{		
	}

	BEGIN_MSG_MAP(ThisClass)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		CHAIN_MSG_MAP(BaseClass)
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		// Fifteen fixed rows: the form only ever grows sideways, and a panel
		// shorter than the form scrolls rather than clipping the last fields.
		static const CPageLayout::Anchor anchors[] =
		{
			{IDC_CAPTION_WRITER, CPageLayout::StretchX},
			{IDC_SPECIAL_INSTRUCTIONS, CPageLayout::StretchX},
			{IDC_CATEGORY, CPageLayout::StretchX},
			{IDC_SUB_CATEGORY, CPageLayout::StretchX},
			{IDC_BYLINE, CPageLayout::StretchX},
			{IDC_BYLINETITLE, CPageLayout::StretchX},
			{IDC_CREDIT, CPageLayout::StretchX},
			{IDC_SOURCE, CPageLayout::StretchX},
			{IDC_COPYRIGHT, CPageLayout::StretchX},
			{IDC_OBJECT_NAME, CPageLayout::StretchX},
			{IDC_DATE_CREATED, CPageLayout::StretchX},
			{IDC_CITY, CPageLayout::StretchX},
			{IDC_PROVENCE_STATE, CPageLayout::StretchX},
			{IDC_COUNTRY_NAME, CPageLayout::StretchX},
			{IDC_ORIGINAL_TR, CPageLayout::StretchX}
		};

		CRect rect;
		GetClientRect(rect);

		InitLayout(anchors, _countof(anchors), rect.Height());

		OnPopulate();

		bHandled = FALSE;
		return 0;
	}

	void OnPopulate()
	{		
		const IW::Image &image = _parent._state.GetImage();

		ImageMetaData metaData(image);
		SetDlgItemText(IDC_CAPTION_WRITER, metaData.GetCaptionWriter());
		SetDlgItemText(IDC_SPECIAL_INSTRUCTIONS, metaData.GetSpecialInstructions());
		SetDlgItemText(IDC_BYLINE, metaData.GetByLine());
		SetDlgItemText(IDC_BYLINETITLE, metaData.GetByLineTitle());
		SetDlgItemText(IDC_CREDIT, metaData.GetCredit());
		SetDlgItemText(IDC_SOURCE, metaData.GetSource());
		SetDlgItemText(IDC_COPYRIGHT, metaData.GetCopyright());
		SetDlgItemText(IDC_CATEGORY, metaData.GetCategory());
		SetDlgItemText(IDC_SUB_CATEGORY, metaData.GetSubCategory());
		SetDlgItemText(IDC_OBJECT_NAME, metaData.GetObjectName());
		SetDlgItemText(IDC_DATE_CREATED, metaData.GetDateCreated());
		SetDlgItemText(IDC_CITY, metaData.GetCity());
		SetDlgItemText(IDC_PROVENCE_STATE, metaData.GetProvenceState());
		SetDlgItemText(IDC_COUNTRY_NAME, metaData.GetCountryName());
		SetDlgItemText(IDC_ORIGINAL_TR, metaData.GetOriginalTR());

		ResetScroll();
	}
};

template<class TParent>
class CDescriptionIptcPage : public CDescriptionPageImpl<CDescriptionIptcPage<TParent> >
{
public:
	typedef CDescriptionIptcPage<TParent> ThisClass;
	typedef CDescriptionPageImpl<ThisClass> BaseClass;

	TParent &_parent;
	CMetadataListCtrl _list;	

	enum { IDD = IDD_DESCRIPTION_IPTC };

	CDescriptionIptcPage(TParent &parent) : _parent(parent)
	{
	}

	BEGIN_MSG_MAP(ThisClass)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		CHAIN_MSG_MAP(BaseClass)
		REFLECT_NOTIFICATIONS()
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		CStatic readonly;
		readonly.Attach(GetDlgItem(IDC_READONLY));
		readonly.SetFont(IW::Style::GetFont(IW::Style::Font::Heading));

		_list.Attach(GetDlgItem(IDC_LIST1));
		_list.Init();

		static const CPageLayout::Anchor anchors[] =
		{
			{IDC_READONLY, CPageLayout::StretchX},
			{IDC_LIST1, CPageLayout::StretchX | CPageLayout::StretchY}
		};

		InitLayout(anchors, _countof(anchors), 0);

		OnPopulate();

		bHandled = FALSE;
		return 0;
	}

	void OnPopulate()
	{		
		_list.Reset();

		IW::MetadataProperties properties;
		MetadataIPTC(_parent._state.GetImage().GetMetaData(IW::MetaDataTypes::PROFILE_IPTC)).Load(properties);
		properties.Apply(_list);
	}
};

template<class TParent>
class CDescriptionXmpPage : public CDescriptionPageImpl<CDescriptionXmpPage<TParent> >
{
public:
	typedef CDescriptionXmpPage<TParent> ThisClass;
	typedef CDescriptionPageImpl<ThisClass> BaseClass;

	TParent &_parent;
	CMetadataListCtrl _list;	

	enum { IDD = IDD_DESCRIPTION_XMP };

	CDescriptionXmpPage(TParent &parent) : _parent(parent)
	{
	}

	BEGIN_MSG_MAP(ThisClass)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		CHAIN_MSG_MAP(BaseClass)
		REFLECT_NOTIFICATIONS()
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		CStatic readonly;
		readonly.Attach(GetDlgItem(IDC_READONLY));
		readonly.SetFont(IW::Style::GetFont(IW::Style::Font::Heading));

		_list.Attach(GetDlgItem(IDC_LIST1));
		_list.Init();

		static const CPageLayout::Anchor anchors[] =
		{
			{IDC_READONLY, CPageLayout::StretchX},
			{IDC_LIST1, CPageLayout::StretchX | CPageLayout::StretchY}
		};

		InitLayout(anchors, _countof(anchors), 0);

		OnPopulate();

		bHandled = FALSE;
		return 0;
	}

	void OnPopulate()
	{		
		_list.Reset();

		IW::MetadataProperties properties;
		MetadataXMP(_parent._state.GetImage().GetMetaData(IW::MetaDataTypes::PROFILE_XMP)).Load(properties);
		properties.Apply(_list);
	}
};


template<class TParent>
class CDescriptionExifPage : public CDescriptionPageImpl<CDescriptionExifPage<TParent> >
{
public:
	typedef CDescriptionExifPage<TParent> ThisClass;
	typedef CDescriptionPageImpl<ThisClass> BaseClass;

	CStatic _prop;
	CMetadataListCtrl _list;
	IW::MetadataProperties _properties;

	TParent &_parent;
	enum { IDD = IDD_DESCRIPTION_EXIF };

	CDescriptionExifPage(TParent &parent) : _parent(parent)
	{		
	}

	BEGIN_MSG_MAP(ThisClass)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)

		NOTIFY_HANDLER(IDC_LIST1, LVN_ITEMCHANGED, OnSelect)
		CHAIN_MSG_MAP(BaseClass)
		REFLECT_NOTIFICATIONS()

	END_MSG_MAP()


	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		CStatic readonly;
		readonly.Attach(GetDlgItem(IDC_READONLY));
		readonly.SetFont(IW::Style::GetFont(IW::Style::Font::Heading));

		_prop.Attach(GetDlgItem(IDC_PROPTITLE));
		_prop.SetFont(IW::Style::GetFont(IW::Style::Font::Heading));

		_list.Attach(GetDlgItem(IDC_LIST1));
		_list.Init();

		// The description of the selected tag stays on the bottom edge; the
		// list takes whatever is between it and the heading.
		static const CPageLayout::Anchor anchors[] =
		{
			{IDC_READONLY, CPageLayout::StretchX},
			{IDC_LIST1, CPageLayout::StretchX | CPageLayout::StretchY},
			{IDC_PROPTITLE, CPageLayout::StretchX | CPageLayout::MoveY},
			{IDC_PROPS, CPageLayout::StretchX | CPageLayout::MoveY}
		};

		InitLayout(anchors, _countof(anchors), 0);

		OnPopulate();

		bHandled = FALSE;
		return 0;
	}

	LRESULT OnSelect(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/)
	{
		LPNMLISTVIEW nmlv = (LPNMLISTVIEW)pnmh;

		if ((nmlv->uChanged & LVIF_STATE) && (nmlv->uNewState & LVIS_SELECTED))
		{
			CString strItem;
			_list.GetItemText(nmlv->iItem, 0, strItem);
			SetDlgItemText(IDC_PROPTITLE, strItem);

			const IW::MetadataProperty *pProperty = _properties.Find(strItem);
			SetDlgItemText(IDC_PROPS, pProperty ? pProperty->description : g_szEmptyString);
		}
		
        return 0;
    }

	void OnPopulate()
	{		
		_list.Reset();

		// The old selection described a tag of the previous picture.
		SetDlgItemText(IDC_PROPTITLE, g_szEmptyString);
		SetDlgItemText(IDC_PROPS, g_szEmptyString);

		_properties = IW::MetadataProperties();
		MetadataExif(_parent._state.GetImage().GetMetaData(IW::MetaDataTypes::PROFILE_EXIF)).Load(_properties);
		_properties.Apply(_list);
	}
};


// 2.3 floats this information over the picture as a Frame; 2.2 docks it as a
// real child in a splitter, which is what everything else in this frame is.
// It replaced a modal "Image description" dialog and took that dialog's five
// pages with it - the pages were always the view, the dialog only a frame
// around them.
//
// It reads and never writes: the engine's IPTC and XMP write sides are gone,
// so every field is ES_READONLY and there is nothing to apply or revert.
class CDescriptionWindow : public CWindowImpl<CDescriptionWindow>
{
public:

	typedef CDescriptionWindow ThisClass;

	DECLARE_WND_CLASS_EX(_T("IWDescriptionWindow"), CS_HREDRAW | CS_VREDRAW, COLOR_BTNFACE)

	ImageState &_state;

	enum
	{
		kTabsId = 0x7f50,
		kPageCount = 5
	};

	// A plain tab control with the pages as siblings of it, rather than the
	// dialog-container tab this panel started with: that one placed its items
	// correctly and never drew the tab row at all outside a dialog.
	CTabCtrl _tabs;
	HWND _hWndPages[kPageCount] = {nullptr, nullptr, nullptr, nullptr, nullptr};

	CDescriptionMainPage<ThisClass> _pageMain;
	CDescriptionDetailsPage<ThisClass> _pageDetails;
	CDescriptionIptcPage<ThisClass> _pageIptc;
	CDescriptionXmpPage<ThisClass> _pageXmp;
	CDescriptionExifPage<ThisClass> _pageExif;

	CDescriptionWindow(State &state) :
		_state(state.Image),
		_pageMain(*this),
		_pageDetails(*this),
		_pageIptc(*this),
		_pageXmp(*this),
		_pageExif(*this)
	{
	}

	BEGIN_MSG_MAP(CDescriptionWindow)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		NOTIFY_HANDLER(kTabsId, TCN_SELCHANGE, OnTabChanged)
	END_MSG_MAP()

	// Called whenever the picture changes. Reading metadata is a map walk over
	// blobs already in memory, so it does not go near the loader thread.
	void Refresh()
	{
		if (m_hWnd == nullptr)
			return;

		if (_pageMain.m_hWnd) _pageMain.OnPopulate();
		if (_pageDetails.m_hWnd) _pageDetails.OnPopulate();
		if (_pageIptc.m_hWnd) _pageIptc.OnPopulate();
		if (_pageXmp.m_hWnd) _pageXmp.OnPopulate();
		if (_pageExif.m_hWnd) _pageExif.OnPopulate();
	}

private:

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL& bHandled)
	{
		_tabs.Create(m_hWnd, rcDefault, nullptr,
		             WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, kTabsId);

		if (_tabs.m_hWnd == nullptr)
			return -1;

		_tabs.SetFont(AtlGetStockFont(DEFAULT_GUI_FONT));

		_pageMain.Create(m_hWnd);
		_pageDetails.Create(m_hWnd);
		_pageIptc.Create(m_hWnd);
		_pageXmp.Create(m_hWnd);
		_pageExif.Create(m_hWnd);

		_hWndPages[0] = _pageMain;
		_hWndPages[1] = _pageDetails;
		_hWndPages[2] = _pageIptc;
		_hWndPages[3] = _pageXmp;
		_hWndPages[4] = _pageExif;

		static LPCTSTR const szTabs[kPageCount] =
		{
			_T("General"), _T("Details"), _T("IPTC"), _T("XMP"), _T("EXIF")
		};

		for (int i = 0; i < kPageCount; i++)
		{
			TCITEM tci = {0};
			tci.mask = TCIF_TEXT;
			tci.pszText = const_cast<LPTSTR>(szTabs[i]);
			_tabs.InsertItem(i, &tci);

			if (_hWndPages[i] != nullptr)
			{
				// Q149501: without this the tab key stops at the page.
				::SetWindowLong(_hWndPages[i], GWL_EXSTYLE,
				                ::GetWindowLong(_hWndPages[i], GWL_EXSTYLE) | WS_EX_CONTROLPARENT);
				::ShowWindow(_hWndPages[i], SW_HIDE);
				EnableTabTexture(_hWndPages[i]);
			}
		}

		ShowPage(IW::Min(IW::Max(static_cast<int>(App.Settings.m_nDescriptionPage), 0),
		                 kPageCount - 1));

		bHandled = FALSE;
		return 0;
	}

	// The pages are dialogs, so they paint COLOR_3DFACE; on a themed tab body
	// that reads as a grey patch unless the page is told to use the tab's own
	// background.
	static void EnableTabTexture(HWND hWnd)
	{
		typedef HRESULT (WINAPI *PFNENABLETHEMEDIALOGTEXTURE)(HWND, DWORD);

		const HMODULE hDll = ::LoadLibrary(_T("uxtheme.dll"));

		if (hDll == nullptr)
			return;

		const auto pfn = reinterpret_cast<PFNENABLETHEMEDIALOGTEXTURE>(
			::GetProcAddress(hDll, "EnableThemeDialogTexture"));

		if (pfn != nullptr)
			pfn(hWnd, ETDT_ENABLETAB);

		::FreeLibrary(hDll);
	}

	void ShowPage(int nPage)
	{
		if (nPage < 0 || nPage >= kPageCount)
			return;

		if (_tabs.GetCurSel() != nPage)
			_tabs.SetCurSel(nPage);

		for (int i = 0; i < kPageCount; i++)
		{
			if (_hWndPages[i] != nullptr && i != nPage)
				::ShowWindow(_hWndPages[i], SW_HIDE);
		}

		LayoutPage(nPage);

		if (_hWndPages[nPage] != nullptr)
			::ShowWindow(_hWndPages[nPage], SW_SHOW);
	}

	// The pages are siblings of the tab control, not children of it, so the
	// tab's display area is already in this window's coordinates.
	void LayoutPage(int nPage)
	{
		if (nPage < 0 || nPage >= kPageCount || _hWndPages[nPage] == nullptr)
			return;

		CRect rect;
		_tabs.GetClientRect(rect);
		_tabs.AdjustRect(FALSE, rect);

		::SetWindowPos(_hWndPages[nPage], HWND_TOP, rect.left, rect.top,
		               IW::Max(0, rect.Width()), IW::Max(0, rect.Height()),
		               SWP_NOACTIVATE);
	}

	LRESULT OnTabChanged(int, LPNMHDR, BOOL&)
	{
		ShowPage(_tabs.GetCurSel());
		return 0;
	}

	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled)
	{
		if (_tabs.m_hWnd != nullptr)
			App.Settings.m_nDescriptionPage = IW::Max(0, _tabs.GetCurSel());

		bHandled = FALSE;
		return 0;
	}

	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL& bHandled)
	{
		if (_tabs.m_hWnd != nullptr)
		{
			CRect rect;
			GetClientRect(rect);
			_tabs.SetWindowPos(nullptr, rect, SWP_NOZORDER | SWP_NOACTIVATE);
			LayoutPage(_tabs.GetCurSel());
		}

		bHandled = FALSE;
		return 0;
	}
};

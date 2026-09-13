// ImageWalker by Zac Walker
//
// Purpose: The description window - the summary, details, IPTC, EXIF and XMP
//          pages for one image.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "ViewModelImage.h"
#include "ViewMetadataList.h"
#include "Metadata.h"
#include "MetadataExif.h"
#include "ViewTabCtrl.h"


template<class TParent>
class CDescriptionMainPage : public CDialogImpl<CDescriptionMainPage<TParent> >
{
public:
	typedef CDescriptionMainPage<TParent> ThisClass;
	typedef CDialogImpl<ThisClass> BaseClass;
	
	TParent &_parent;

	enum { IDD = IDD_DESCRIPTION_MAIN };

	CDescriptionMainPage(TParent &parent) : _parent(parent)
	{
	}

	BEGIN_MSG_MAP(ThisClass)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)

		COMMAND_HANDLER(IDC_RESOLUTION, CBN_SELCHANGE, OnChangeSelect)

	END_MSG_MAP()

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		CComboBox comboRes = GetDlgItem(IDC_RESOLUTION);
		comboRes.AddString(App.LoadString(IDS_PIXELS_PER_INCH));
		comboRes.AddString(App.LoadString(IDS_PIXELS_PER_CM));

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
class CDescriptionDetailsPage : public CDialogImpl<CDescriptionDetailsPage<TParent> >
{
public:
	typedef CDescriptionDetailsPage<TParent> ThisClass;
	typedef CDialogImpl<ThisClass> BaseClass;
	
	TParent &_parent;

	enum { IDD = IDD_DESCRIPTION_DETAILS };

	CDescriptionDetailsPage(TParent &parent) : _parent(parent)
	{		
	}

	BEGIN_MSG_MAP(ThisClass)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
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
	}
};

template<class TParent>
class CDescriptionIptcPage : public CDialogImpl<CDescriptionIptcPage<TParent> >
{
public:
	typedef CDescriptionIptcPage<TParent> ThisClass;
	typedef CDialogImpl<ThisClass> BaseClass;
	
	TParent &_parent;
	CMetadataListCtrl _list;	

	enum { IDD = IDD_DESCRIPTION_IPTC };

	CDescriptionIptcPage(TParent &parent) : _parent(parent)
	{
	}

	BEGIN_MSG_MAP(ThisClass)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		REFLECT_NOTIFICATIONS()
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		CStatic readonly;
		readonly.Attach(GetDlgItem(IDC_READONLY));
		readonly.SetFont(IW::Style::GetFont(IW::Style::Font::Heading));

		_list.Attach(GetDlgItem(IDC_LIST1));
		_list.Init();

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
class CDescriptionXmpPage : public CDialogImpl<CDescriptionXmpPage<TParent> >
{
public:
	typedef CDescriptionXmpPage<TParent> ThisClass;
	typedef CDialogImpl<ThisClass> BaseClass;
	
	TParent &_parent;
	CMetadataListCtrl _list;	

	enum { IDD = IDD_DESCRIPTION_XMP };

	CDescriptionXmpPage(TParent &parent) : _parent(parent)
	{
	}

	BEGIN_MSG_MAP(ThisClass)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		REFLECT_NOTIFICATIONS()
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		CStatic readonly;
		readonly.Attach(GetDlgItem(IDC_READONLY));
		readonly.SetFont(IW::Style::GetFont(IW::Style::Font::Heading));

		_list.Attach(GetDlgItem(IDC_LIST1));
		_list.Init();

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
class CDescriptionExifPage : public CDialogImpl<CDescriptionExifPage<TParent> >
{
public:
	typedef CDescriptionExifPage<TParent> ThisClass;
	typedef CDialogImpl<ThisClass> BaseClass;

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

class CDescriptionDlg : public CDialogImpl<CDescriptionDlg>
{
public:

	typedef CDescriptionDlg ThisClass;
	typedef CDialogImpl<CDescriptionDlg> BaseClass;

	ImageState &_state;	
	IW::Image  _image;

	CDialogTabCtrl _tabs;
	CDescriptionMainPage<ThisClass> _pageMain;
	CDescriptionDetailsPage<ThisClass> _pageDetails;
	CDescriptionIptcPage<ThisClass> _pageIptc;
	CDescriptionExifPage<ThisClass> _pageExif;
	CDescriptionXmpPage<ThisClass> _pageXmp;

	enum { IDD = IDD_DESCRIPTION };

	CDescriptionDlg(ImageState &state) : 
		_state(state),
		_pageMain(*this),
		_pageDetails(*this),
		_pageExif(*this),
		_pageXmp(*this),
		_pageIptc(*this)
	{
	}
	

	BEGIN_MSG_MAP(ThisClass)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)

		MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
		REFLECT_NOTIFICATIONS()
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		CenterWindow();
		OnPopulate();

		_tabs.SubclassWindow(GetDlgItem(IDC_TAB1));		

		_pageMain.Create(m_hWnd);
		_pageDetails.Create(m_hWnd);
		_pageIptc.Create(m_hWnd);
		_pageXmp.Create(m_hWnd);
		_pageExif.Create(m_hWnd);

		TCITEM tci = { 0 };
		tci.mask = TCIF_TEXT;

		tci.pszText = _T("General");
		_tabs.InsertItem(0, &tci, _pageMain);

		tci.pszText = _T("Details");
		_tabs.InsertItem(1, &tci, _pageDetails);
		
		tci.pszText = _T("IPTC");
		_tabs.InsertItem(2, &tci, _pageIptc);
		
		tci.pszText = _T("XMP");
		_tabs.InsertItem(3, &tci, _pageXmp);

		tci.pszText = _T("EXIF");
		_tabs.InsertItem(4, &tci, _pageExif);

		_tabs.SetCurSel(App.Settings.m_nDescriptionPage);		

		return 0;
	}

	void OnPopulate()
	{
		CRect rectCtrl;
		CWindow previewCtrl = GetDlgItem(IDC_PREVIEW);
		previewCtrl.GetClientRect(rectCtrl);

		_image = CreatePreview(_state.GetImage(), rectCtrl.Size());
		
		previewCtrl.Invalidate();

		if (_pageMain.m_hWnd) _pageMain.OnPopulate();
		if (_pageDetails.m_hWnd) _pageDetails.OnPopulate();
		if (_pageIptc.m_hWnd) _pageIptc.OnPopulate();
		if (_pageXmp.m_hWnd) _pageXmp.OnPopulate();
		if (_pageExif.m_hWnd) _pageExif.OnPopulate();
	}

	LRESULT OnDrawItem(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		LPDRAWITEMSTRUCT lpDrawItemStruct = (LPDRAWITEMSTRUCT)lParam;
		int nIDCtl = static_cast<int>(wParam);

		if (IDC_PREVIEW == nIDCtl)
		{
			CRect r(lpDrawItemStruct->rcItem);
			CSize size(_image.GetBoundingRect().Size());

			::FillRect(lpDrawItemStruct->hDC, &r, (HBRUSH)LongToPtr(COLOR_3DFACE + 1));

			CRect r2(r.CenterPoint() - CSize(size.cx / 2, size.cy / 2), size);	
			IW::Page page = _image.GetFirstPage();
			IW::CRender::DrawToDC(lpDrawItemStruct->hDC, page, r2);
		}

		return 0;
	}

	LRESULT OnCloseCmd(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		App.Settings.m_nDescriptionPage = _tabs.GetCurSel();
		EndDialog(wID);		
		return 0;
	}
};

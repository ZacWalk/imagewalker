// ImageWalker by Zac Walker
//
// Purpose: CFilterResize: the one surviving interactive filter, and the
//          shape a filter has.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once


template<class T>
class ImageFilter
{
public:
	ImageFilter()
	{
	}

	~ImageFilter()
	{
	}

	bool DisplaySettingsDialog(const IW::Image &image)
	{
		return true;
	}

	bool CreatePreview(const IW::Image &imageIn, IW::Image &imageOut, IW::IStatus *pStatus)
	{
		T *pT = static_cast<T*>(this);
		return pT->ApplyFilter(imageIn, imageOut, pStatus);
	}

	bool IsPreviewScaleIndependent() const
	{
		return false;
	}


	void OnReset() 
	{
	}

	void OnHelp() const
	{
		App.InvokeHelp(IW::GetMainWindow(), T::GetHelpID());
	}

};

class  CFilterResize : public ImageFilter<CFilterResize>
{
public:

	bool m_bHasImage;

	int m_nOriginalWidth;
	int m_nOriginalHeight;

	int m_nWidth;
	int m_nHeight;
	bool m_bKeepAspect;
	bool m_bScaleDown;

	int m_nFilter;
	int m_nType;
	DWORD m_dwXPelsPerMeter;
	DWORD m_dwYPelsPerMeter;

public:
	CFilterResize()  
	{
		m_nOriginalWidth = m_nWidth = 640;
		m_nOriginalHeight = m_nHeight = 480;
		m_nFilter = 0;
		m_nType = 0;
		m_dwXPelsPerMeter = App.Settings.m_dwXPelsPerMeter;
		m_dwYPelsPerMeter = App.Settings.m_dwYPelsPerMeter;
		m_bKeepAspect = true;
		m_bScaleDown = false;
		m_bHasImage = false;
	};

	~CFilterResize()
	{
	}

	CString GetKey() { return _T("Resize"); };
	CString GetTitle() { return App.LoadString(IDS_RESIZE); };
	CString GetDescription() { return App.LoadString(IDS_RESIZE_DESC); };
	//DWORD GetFlags() { return IW::ImageFilterFlags::MULTIPAGE | IW::ImageFilterFlags::MULTIPLE | IW::ImageFilterFlags::SINGLE | IW::ImageFilterFlags::OPTIONS; };
	DWORD GetIcon() { return MAXDWORD; };
	DWORD GetHelpID() { return HELP_FILTER_RESIZE; };

	bool ApplyFilter(const IW::Image &imageIn, IW::Image &imageOut, IW::IStatus *pStatus);
	bool DisplaySettingsDialog(const IW::Image &image);
};

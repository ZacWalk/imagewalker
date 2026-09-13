// ImageWalker by Zac Walker
//
// Purpose: AppSettings - the defaults, and the one read and one write that
//          move every preference between the struct and the ini. This is the
//          only file in the app that touches IW::Ini.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"
#include "Settings.h"
#include "iw/inifile.h"

static auto g_szShortDates = _T("ShortDates");
static auto g_szWalkFolders = _T("WalkFolders");
static auto g_szShowDescriptions = _T("ShowDescriptions");
static auto g_szZoomThumbnails = _T("ZoomThumbnails");
static auto g_szMood = _T("Mood");
static auto g_szDescriptionPage = _T("DescriptionPage");
static auto g_szShowDescription = _T("ShowDescription");
static auto g_szShowAdvancedImageDetails = _T("ImageDetailLevel");
static auto g_szBlackBackground = _T("BlackBackground");
static auto g_szBlackSkin = _T("BlackSkin");
static auto g_szExifAutoRotate = _T("ExifAutoRotate");
static auto g_szCenter = _T("Center");
static auto g_szWordWrap = _T("WordWrap");
static auto g_szCount = _T("Count");

// Splitter positions live on CSplitter2Impl's 0..10000 proportional scale.
static const int g_nSplitterMax = 10000;
static const int g_nSplitterSidePane = g_nSplitterMax / 4;
static const int g_nSplitterNormal = g_nSplitterMax / 2;
static const int g_nSplitterPanel = (g_nSplitterMax * 3) / 4;

namespace
{
	// Typed wrappers so each field below is one line. A failed read leaves the
	// caller's value alone, which is what makes the constructors the single
	// source of the defaults.
	bool Get(LPCTSTR szSection, LPCTSTR szKey, int &value) { return IW::Ini::ReadInt(szSection, szKey, value); }
	bool Get(LPCTSTR szSection, LPCTSTR szKey, bool &value) { return IW::Ini::ReadBool(szSection, szKey, value); }
	bool Get(LPCTSTR szSection, LPCTSTR szKey, DWORD &value) { return IW::Ini::ReadDword(szSection, szKey, value); }

	bool Get(LPCTSTR szSection, LPCTSTR szKey, long &value)
	{
		int number = 0;

		if (!IW::Ini::ReadInt(szSection, szKey, number))
			return false;

		value = number;
		return true;
	}

	bool Get(LPCTSTR szSection, LPCTSTR szKey, CString &value)
	{
		IW::Paths::String text;

		if (!IW::Ini::ReadString(szSection, szKey, text) || text.empty())
			return false;

		value = text.c_str();
		return true;
	}

	void Set(LPCTSTR szSection, LPCTSTR szKey, int value) { IW::Ini::WriteInt(szSection, szKey, value); }
	void Set(LPCTSTR szSection, LPCTSTR szKey, bool value) { IW::Ini::WriteBool(szSection, szKey, value); }
	void Set(LPCTSTR szSection, LPCTSTR szKey, DWORD value) { IW::Ini::WriteDword(szSection, szKey, value); }
	void Set(LPCTSTR szSection, LPCTSTR szKey, long value) { IW::Ini::WriteInt(szSection, szKey, static_cast<int>(value)); }
	void Set(LPCTSTR szSection, LPCTSTR szKey, LPCTSTR value) { IW::Ini::WriteString(szSection, szKey, value); }

	// A pane dragged shut still persists as half - GetProportionalPos never
	// returns zero - so a zero here is an older build's default, and applying it
	// opens the pane collapsed with no handle left to drag.
	void GetSplitter(LPCTSTR szSection, int &value, int nDefault)
	{
		Get(szSection, g_szSplitterPos, value);

		if (value <= 0 || value >= g_nSplitterMax)
			value = nDefault;
	}
}

CString AppSettings::Section(LPCTSTR szChild)
{
	return IW::Ini::SectionPath(g_szSettingsKey, szChild).c_str();
}

//////////////////////////////////////////////////////////////////////////

SearchSettings::SearchSettings()
{
	OnlyShowImages = false;
	Size = false;
	DateModified = false;
	DateTaken = false;
	SizeOption = 0;
	SizeKB = 100;
	NumberOfDays = 30;
	Month = 0;
	Year = 0;
}

void SearchSettings::Read(LPCTSTR szSection)
{
	Get(szSection, g_szText, Text);
	Get(szSection, g_szOnlyShowImages, OnlyShowImages);
	Get(szSection, g_szSize, Size);
	Get(szSection, g_szDateModified, DateModified);
	Get(szSection, g_szDateTaken, DateTaken);
	Get(szSection, g_szSizeOption, SizeOption);
	Get(szSection, g_szSizeKB, SizeKB);
	Get(szSection, g_szNumberOfDays, NumberOfDays);
	Get(szSection, g_szMonth, Month);
	Get(szSection, g_szYear, Year);
}

void SearchSettings::Write(LPCTSTR szSection) const
{
	Set(szSection, g_szText, static_cast<LPCTSTR>(Text));
	Set(szSection, g_szOnlyShowImages, OnlyShowImages);
	Set(szSection, g_szSize, Size);
	Set(szSection, g_szDateModified, DateModified);
	Set(szSection, g_szDateTaken, DateTaken);
	Set(szSection, g_szSizeOption, SizeOption);
	Set(szSection, g_szSizeKB, SizeKB);
	Set(szSection, g_szNumberOfDays, NumberOfDays);
	Set(szSection, g_szMonth, Month);
	Set(szSection, g_szYear, Year);
}

//////////////////////////////////////////////////////////////////////////

RenameSettings::RenameSettings()
{
	Template = _T("File####");
	Position = 0;
}

void RenameSettings::Read(LPCTSTR szSection)
{
	Get(szSection, g_szPosition, Position);
	Get(szSection, g_szTemplate, Template);
}

void RenameSettings::Write(LPCTSTR szSection) const
{
	Set(szSection, g_szPosition, Position);
	Set(szSection, g_szTemplate, static_cast<LPCTSTR>(Template));
}

//////////////////////////////////////////////////////////////////////////

void IW::CodecSettings::Read(LPCTSTR szSection)
{
	Get(szSection, g_szQuality, JpegQuality);
	Get(szSection, g_szProgressive, JpegProgressive);
	Get(szSection, g_szOptimize, JpegOptimize);

	JpegQuality = IW::Clamp(JpegQuality, 1L, 100L);
}

void IW::CodecSettings::Write(LPCTSTR szSection) const
{
	Set(szSection, g_szQuality, JpegQuality);
	Set(szSection, g_szProgressive, JpegProgressive);
	Set(szSection, g_szOptimize, JpegOptimize);
}

//////////////////////////////////////////////////////////////////////////

PrintSettings::PrintSettings()
{
	m_annotations.Add(IW::ePropertyTitle);
	m_annotations.Add(IW::ePropertyType);
	m_annotations.Add(IW::ePropertyDescription);

	_sizeRowsColumns.cx = 3;
	_sizeRowsColumns.cy = 3;
	_strHeader.LoadString(IDS_PRINT_HEADER);
	_strFooter.LoadString(IDS_PRINT_FOOTER);
	m_rcMargin.left = 1000;
	m_rcMargin.top = 1000;
	m_rcMargin.right = 1000;
	m_rcMargin.bottom = 1000;
	m_bPrintSelected = false;
	m_bPrintOnePerPage = false;
	m_nPrintRotateBest = 0;
	m_bPrintLandscape = false;
	m_bCenter = true;
	m_bWrap = true;
	m_bShowPageNumbers = true;
	m_bShowFooter = true;
	m_bShowHeader = true;
	m_clrBackGround = RGB(255, 255, 255);
	m_bShadow = true;
	m_bFrame = true;
}

void PrintSettings::Read(LPCTSTR szSection)
{
	Get(szSection, g_szShadow, m_bShadow);
	Get(szSection, g_szFrame, m_bFrame);
	Get(szSection, g_szPrintSelected, m_bPrintSelected);
	Get(szSection, g_szPrintOnePerPage, m_bPrintOnePerPage);
	Get(szSection, g_szPrintLandscape, m_bPrintLandscape);
	Get(szSection, g_szPrintRotateBest, m_nPrintRotateBest);
	Get(szSection, g_szColumns, _sizeRowsColumns.cx);
	Get(szSection, g_szRows, _sizeRowsColumns.cy);
	Get(szSection, g_szShowFooter, m_bShowFooter);
	Get(szSection, g_szShowHeader, m_bShowHeader);
	Get(szSection, g_szShowPageNumbers, m_bShowPageNumbers);
	Get(szSection, g_szHeader, _strHeader);
	Get(szSection, g_szFooter, _strFooter);
	Get(szSection, g_szColorBackGround, m_clrBackGround);
	Get(szSection, g_szCenter, m_bCenter);
	Get(szSection, g_szWordWrap, m_bWrap);

	DWORD dw = sizeof(m_rcMargin);
	CRect rcMargin(0, 0, 0, 0);

	if (IW::Ini::ReadBinary(szSection, g_szPrintMargin, &rcMargin, dw) && dw == sizeof(m_rcMargin))
	{
		m_rcMargin = rcMargin;
	}

	CString str;

	// Only on a hit: an absent key would otherwise parse "" over the default.
	if (Get(szSection, g_szAnnotations, str))
		m_annotations.ParseFromString(str);
}

void PrintSettings::Write(LPCTSTR szSection) const
{
	Set(szSection, g_szShadow, m_bShadow);
	Set(szSection, g_szFrame, m_bFrame);
	Set(szSection, g_szPrintSelected, m_bPrintSelected);
	Set(szSection, g_szPrintOnePerPage, m_bPrintOnePerPage);
	Set(szSection, g_szPrintLandscape, m_bPrintLandscape);
	Set(szSection, g_szPrintRotateBest, m_nPrintRotateBest);

	IW::Ini::WriteBinary(szSection, g_szPrintMargin, &m_rcMargin, sizeof(m_rcMargin));

	Set(szSection, g_szColumns, _sizeRowsColumns.cx);
	Set(szSection, g_szRows, _sizeRowsColumns.cy);

	Set(szSection, g_szShowFooter, m_bShowFooter);
	Set(szSection, g_szShowHeader, m_bShowHeader);
	Set(szSection, g_szShowPageNumbers, m_bShowPageNumbers);

	Set(szSection, g_szHeader, static_cast<LPCTSTR>(_strHeader));
	Set(szSection, g_szFooter, static_cast<LPCTSTR>(_strFooter));

	Set(szSection, g_szColorBackGround, m_clrBackGround);

	Set(szSection, g_szAnnotations, static_cast<LPCTSTR>(m_annotations.GetAsString()));
	Set(szSection, g_szCenter, m_bCenter);
	Set(szSection, g_szWordWrap, m_bWrap);
}

//////////////////////////////////////////////////////////////////////////

ToolTargetSettings::ToolTargetSettings()
{
	_bRecurse = false;
	m_bOverwrite = false;
	m_bFolder = true;
}

void ToolTargetSettings::Read(LPCTSTR szSection)
{
	Get(szSection, g_szRecurse, _bRecurse);
	Get(szSection, g_szOverwrite, m_bOverwrite);
	Get(szSection, g_szFolder, m_bFolder);

	CString str;
	if (Get(szSection, g_szFolderOut, str)) _pathFolderOut = str;
}

void ToolTargetSettings::Write(LPCTSTR szSection) const
{
	Set(szSection, g_szRecurse, _bRecurse);
	Set(szSection, g_szOverwrite, m_bOverwrite);
	Set(szSection, g_szFolder, m_bFolder);
	Set(szSection, g_szFolderOut, static_cast<LPCTSTR>(_pathFolderOut));
}

JpegToolSettings::JpegToolSettings()
{
	m_bMirrorLR = false;
	m_bMirrorTB = false;
	m_bRotate90 = false;
	m_bRotate180 = false;
	m_bRotate270 = false;
	m_bGreyScale = false;
	m_bDropEdge = false;
	m_bOptimize = false;
	m_bProgressive = false;
}

void JpegToolSettings::Read(LPCTSTR szSection)
{
	ToolTargetSettings::Read(szSection);

	Get(szSection, g_szMirrorLR, m_bMirrorLR);
	Get(szSection, g_szMirrorTB, m_bMirrorTB);
	Get(szSection, g_szRotate90, m_bRotate90);
	Get(szSection, g_szRotate180, m_bRotate180);
	Get(szSection, g_szRotate270, m_bRotate270);
	Get(szSection, g_szGreyScale, m_bGreyScale);
	Get(szSection, g_szDropEdge, m_bDropEdge);
	Get(szSection, g_szOptimize, m_bOptimize);
	Get(szSection, g_szProgressive, m_bProgressive);
}

void JpegToolSettings::Write(LPCTSTR szSection) const
{
	ToolTargetSettings::Write(szSection);

	Set(szSection, g_szMirrorLR, m_bMirrorLR);
	Set(szSection, g_szMirrorTB, m_bMirrorTB);
	Set(szSection, g_szRotate90, m_bRotate90);
	Set(szSection, g_szRotate180, m_bRotate180);
	Set(szSection, g_szRotate270, m_bRotate270);
	Set(szSection, g_szGreyScale, m_bGreyScale);
	Set(szSection, g_szDropEdge, m_bDropEdge);
	Set(szSection, g_szOptimize, m_bOptimize);
	Set(szSection, g_szProgressive, m_bProgressive);
}

ResizeToolSettings::ResizeToolSettings()
{
	Width = 640;
	Height = 480;
	KeepAspect = true;
	ScaleDown = false;
	Filter = 0;
	Type = 0;
	XPelsPerMeter = 2834;
	YPelsPerMeter = 2834;
}

void ResizeToolSettings::Read(LPCTSTR szSection)
{
	ToolTargetSettings::Read(szSection);

	Get(szSection, g_szWidth, Width);
	Get(szSection, g_szHeight, Height);
	Get(szSection, g_szKeepAspect, KeepAspect);
	Get(szSection, g_szScaleDown, ScaleDown);
	Get(szSection, g_szFilter, Filter);
	Get(szSection, g_szType, Type);
	Get(szSection, g_szXPelsPerMeter, XPelsPerMeter);
	Get(szSection, g_szYPelsPerMeter, YPelsPerMeter);
}

void ResizeToolSettings::Write(LPCTSTR szSection) const
{
	ToolTargetSettings::Write(szSection);

	Set(szSection, g_szWidth, Width);
	Set(szSection, g_szHeight, Height);
	Set(szSection, g_szKeepAspect, KeepAspect);
	Set(szSection, g_szScaleDown, ScaleDown);
	Set(szSection, g_szFilter, Filter);
	Set(szSection, g_szType, Type);
	Set(szSection, g_szXPelsPerMeter, XPelsPerMeter);
	Set(szSection, g_szYPelsPerMeter, YPelsPerMeter);
}

ConvertToolSettings::ConvertToolSettings() : LoaderKey(g_szJPG)
{
}

void ConvertToolSettings::Read(LPCTSTR szSection)
{
	ToolTargetSettings::Read(szSection);

	Get(szSection, g_szLoader, LoaderKey);
	Codec.Read(szSection);
}

void ConvertToolSettings::Write(LPCTSTR szSection) const
{
	ToolTargetSettings::Write(szSection);

	Set(szSection, g_szLoader, static_cast<LPCTSTR>(LoaderKey));
	Codec.Write(szSection);
}

ContactSheetToolSettings::ContactSheetToolSettings() : LoaderKey(g_szJPG), Position(0)
{
}

void ContactSheetToolSettings::Read(LPCTSTR szSection)
{
	ToolTargetSettings::Read(szSection);

	Get(szSection, g_szPosition, Position);
	Get(szSection, g_szTemplate, Template);
	Get(szSection, g_szLoader, LoaderKey);

	CString str;
	if (Get(szSection, g_szOutputFolder, str)) _pathFolderOut = str;

	Codec.Read(szSection);
	Print.Read(IW::Ini::SectionPath(szSection, g_szOptions).c_str());
}

void ContactSheetToolSettings::Write(LPCTSTR szSection) const
{
	ToolTargetSettings::Write(szSection);

	Set(szSection, g_szPosition, Position);
	Set(szSection, g_szTemplate, static_cast<LPCTSTR>(Template));
	Set(szSection, g_szLoader, static_cast<LPCTSTR>(LoaderKey));
	Set(szSection, g_szOutputFolder, static_cast<LPCTSTR>(_pathFolderOut));

	Codec.Write(szSection);
	Print.Write(IW::Ini::SectionPath(szSection, g_szOptions).c_str());
}

void ToolSettings::Read()
{
	Convert.Read(_T("ConvertTool"));
	Resize.Read(_T("ResizeTool"));
	Jpeg.Read(_T("JpegTool"));
	ContactSheet.Read(_T("ContactSheetTool"));
}

void ToolSettings::Write() const
{
	Convert.Write(_T("ConvertTool"));
	Resize.Write(_T("ResizeTool"));
	Jpeg.Write(_T("JpegTool"));
	ContactSheet.Write(_T("ContactSheetTool"));
}

//////////////////////////////////////////////////////////////////////////

AppSettings::AppSettings()
{
	m_nDescriptionPage = 0;
	_sizeRowsColumns.cx = 3;
	_sizeRowsColumns.cy = 3;
	m_annotations.Add(IW::ePropertyName);
	m_columns.Add(IW::ePropertyName);
	m_columns.Add(IW::ePropertyType);
	m_columns.Add(IW::ePropertySize);
	m_columns.Add(IW::ePropertyModifiedDate);
	_sizeThumbImage.cx = 200;
	_sizeThumbImage.cy = 200;
	m_bUseEffects = true;
	_bExifAutoRotate = true;
	m_bDoubleClickShowsFullScreen = true;
	m_bShowToolTips = true;
	m_bShowMarkers = true;
	m_bShortDates = true;
	m_bWalkFolders = true;
	m_bShowHidden = false;
	_bDontHideCursor = false;
	ShowDescriptions = true;
	ZoomThumbnails = true;
	Mood = 0;
	m_nResolutionSelection = 0;
	m_dwXPelsPerMeter = 2834;
	m_dwYPelsPerMeter = 2834;
	ShowDescription = true;
	ImageDetailLevel = 1;
	BlackBackground = true;

	// The skin draws the frame itself. This used to read !IsWindowsVista(),
	// which tests dwMajorVersion == 6 - false on 10 and 11, so the gate it
	// looked like it was applying had not been applied since Windows 8.
	BlackSkin = true;
	CaptureFormat = g_szJPG;

	IW::MemZero(&Placement, sizeof(Placement));
	SplitterPos = g_nSplitterSidePane;
	ViewMode = 0;
	NormalSplitterPos = g_nSplitterNormal;
	NormalFolderView = 0;
	ThumbnailSize = 0;
	EditSplitterPos = g_nSplitterPanel;
	PrintSplitterPos = g_nSplitterPanel;

	// Scale::Parse reads an empty string as 1%, not as "no opinion".
	NormalScale.LoadString(IDS_FIT);
}

void AppSettings::Load()
{
	const CString strRoot = g_szSettingsKey;
	const CString strDefaults = Section(g_szDefaults);

	Get(strDefaults, g_szDescriptionPage, m_nDescriptionPage);
	Get(strDefaults, g_szColumns, _sizeRowsColumns.cx);
	Get(strDefaults, g_szRows, _sizeRowsColumns.cy);
	Get(strDefaults, g_szThumbCX, _sizeThumbImage.cx);
	Get(strDefaults, g_szThumbCY, _sizeThumbImage.cy);
	Get(strDefaults, g_szUseEffects, m_bUseEffects);
	Get(strDefaults, g_szShowDescriptions, ShowDescriptions);
	Get(strDefaults, g_szZoomThumbnails, ZoomThumbnails);
	Get(strDefaults, g_szExifAutoRotate, _bExifAutoRotate);
	Get(strDefaults, g_szDoubleClickShowsFullScreen, m_bDoubleClickShowsFullScreen);
	Get(strDefaults, g_szToolTips, m_bShowToolTips);
	Get(strDefaults, g_szHidden, m_bShowHidden);
	Get(strDefaults, g_szMarkers, m_bShowMarkers);
	Get(strDefaults, g_szShortDates, m_bShortDates);
	Get(strDefaults, g_szWalkFolders, m_bWalkFolders);

	CString str;

	// Only on a hit: an absent key would otherwise parse "" over the default.
	if (Get(strDefaults, g_szViewAnnotations, str)) m_annotations.ParseFromString(str);
	if (Get(strDefaults, g_szViewColumns, str)) m_columns.ParseFromString(str);

	Get(strDefaults, g_szMood, Mood);
	Get(strDefaults, g_szResolution, m_nResolutionSelection);
	Get(strDefaults, g_szXPelsPerMeter, m_dwXPelsPerMeter);
	Get(strDefaults, g_szYPelsPerMeter, m_dwYPelsPerMeter);
	Get(strDefaults, g_szShowDescription, ShowDescription);
	Get(strDefaults, g_szShowAdvancedImageDetails, ImageDetailLevel);
	ImageDetailLevel = IW::Clamp(ImageDetailLevel, 0L, static_cast<long>(ImageDetailMax));
	Get(strDefaults, g_szBlackBackground, BlackBackground);
	Get(strDefaults, g_szBlackSkin, BlackSkin);
	Get(strDefaults, g_szCapture, CaptureFormat);

	Codec.Read(Section(g_szCodec));
	Search.Read(Section(g_szSearch));
	Rename.Read(Section(g_szRename));

	Get(Section(g_szTags), g_szTags, Tags);

	GetSplitter(strRoot, SplitterPos, g_nSplitterSidePane);
	Get(strRoot, g_szViewMode, ViewMode);
	Get(strRoot, g_szDefaultFolder, DefaultFolder);

	DWORD dw = sizeof(Placement);

	if (!IW::Ini::ReadBinary(strRoot, g_szWindowRect, &Placement, dw) || dw != sizeof(Placement))
		IW::MemZero(&Placement, sizeof(Placement));

	const CString strNormal = Section(g_szNormal);
	GetSplitter(strNormal, NormalSplitterPos, g_nSplitterNormal);
	Get(strNormal, g_szScale, NormalScale);
	Get(strNormal, g_szFolderView, NormalFolderView);
	Get(strNormal, g_szThumbSize, ThumbnailSize);

	GetSplitter(Section(g_szEdit), EditSplitterPos, g_nSplitterPanel);

	const CString strPrint = Section(g_szPrintOptions);
	GetSplitter(strPrint, PrintSplitterPos, g_nSplitterPanel);
	Print.Read(strPrint);

	Favourites.clear();

	const CString strFavourites = Section(g_szCopyToList);
	DWORD nMax = 0;
	Get(strFavourites, g_szCount, nMax);

	for (DWORD i = 0; i < nMax; i++)
	{
		CString strKey, strPath;
		strKey.Format(_T("Item %d"), i);

		if (!Get(strFavourites, strKey, strPath))
			break;

		Favourites.push_back(strPath);
	}

	Tools.Read();
}

void AppSettings::Save() const
{
	const CString strRoot = g_szSettingsKey;
	const CString strDefaults = Section(g_szDefaults);

	Set(strDefaults, g_szDescriptionPage, m_nDescriptionPage);
	Set(strDefaults, g_szColumns, _sizeRowsColumns.cx);
	Set(strDefaults, g_szRows, _sizeRowsColumns.cy);
	Set(strDefaults, g_szThumbCX, _sizeThumbImage.cx);
	Set(strDefaults, g_szThumbCY, _sizeThumbImage.cy);
	Set(strDefaults, g_szUseEffects, m_bUseEffects);
	Set(strDefaults, g_szShowDescriptions, ShowDescriptions);
	Set(strDefaults, g_szZoomThumbnails, ZoomThumbnails);
	Set(strDefaults, g_szExifAutoRotate, _bExifAutoRotate);
	Set(strDefaults, g_szDoubleClickShowsFullScreen, m_bDoubleClickShowsFullScreen);
	Set(strDefaults, g_szToolTips, m_bShowToolTips);
	Set(strDefaults, g_szHidden, m_bShowHidden);
	Set(strDefaults, g_szMarkers, m_bShowMarkers);
	Set(strDefaults, g_szShortDates, m_bShortDates);
	Set(strDefaults, g_szWalkFolders, m_bWalkFolders);
	Set(strDefaults, g_szViewAnnotations, static_cast<LPCTSTR>(m_annotations.GetAsString()));
	Set(strDefaults, g_szViewColumns, static_cast<LPCTSTR>(m_columns.GetAsString()));
	Set(strDefaults, g_szMood, Mood);
	Set(strDefaults, g_szResolution, m_nResolutionSelection);
	Set(strDefaults, g_szXPelsPerMeter, m_dwXPelsPerMeter);
	Set(strDefaults, g_szYPelsPerMeter, m_dwYPelsPerMeter);
	Set(strDefaults, g_szShowDescription, ShowDescription);
	Set(strDefaults, g_szShowAdvancedImageDetails, ImageDetailLevel);
	Set(strDefaults, g_szBlackBackground, BlackBackground);
	Set(strDefaults, g_szBlackSkin, BlackSkin);
	Set(strDefaults, g_szCapture, static_cast<LPCTSTR>(CaptureFormat));

	Codec.Write(Section(g_szCodec));
	Search.Write(Section(g_szSearch));
	Rename.Write(Section(g_szRename));

	Set(Section(g_szTags), g_szTags, static_cast<LPCTSTR>(Tags));

	Set(strRoot, g_szSplitterPos, SplitterPos);
	Set(strRoot, g_szViewMode, ViewMode);
	Set(strRoot, g_szDefaultFolder, static_cast<LPCTSTR>(DefaultFolder));

	if (Placement.length == sizeof(Placement))
		IW::Ini::WriteBinary(strRoot, g_szWindowRect, &Placement, sizeof(Placement));

	const CString strNormal = Section(g_szNormal);
	Set(strNormal, g_szSplitterPos, NormalSplitterPos);
	Set(strNormal, g_szScale, static_cast<LPCTSTR>(NormalScale));
	Set(strNormal, g_szFolderView, NormalFolderView);
	Set(strNormal, g_szThumbSize, ThumbnailSize);

	Set(Section(g_szEdit), g_szSplitterPos, EditSplitterPos);

	const CString strPrint = Section(g_szPrintOptions);
	Set(strPrint, g_szSplitterPos, PrintSplitterPos);
	Print.Write(strPrint);

	const CString strFavourites = Section(g_szCopyToList);
	Set(strFavourites, g_szCount, static_cast<DWORD>(Favourites.size()));

	for (size_t i = 0; i < Favourites.size(); i++)
	{
		CString strKey;
		strKey.Format(_T("Item %d"), static_cast<int>(i));
		Set(strFavourites, strKey, static_cast<LPCTSTR>(Favourites[i]));
	}

	Tools.Write();
}

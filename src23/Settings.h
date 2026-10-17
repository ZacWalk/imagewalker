// ImageWalker by Zac Walker
//
// Purpose: AppSettings: every persisted preference in one struct, read once
//          at startup and written once at exit.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

struct SearchSettings
{
	CString Text;
	bool OnlyShowImages;
	bool Size;
	bool DateModified;
	bool DateTaken;
	int SizeOption;
	int SizeKB;
	int NumberOfDays;
	int Month;
	int Year;

	SearchSettings();

	void Read(LPCTSTR szSection);
	void Write(LPCTSTR szSection) const;
};

struct RenameSettings
{
	CString Template;
	int Position;

	RenameSettings();

	void Read(LPCTSTR szSection);
	void Write(LPCTSTR szSection) const;
};

// The persisted half of CPrintFolder, which inherits it. App.Settings.Print is
// the default; the contact sheet tool keeps its own.
struct PrintSettings
{
	bool m_bCenter;
	bool m_bPrintLandscape;
	bool m_bPrintOnePerPage;
	bool m_bPrintSelected;
	bool m_bShadow;
	bool m_bFrame;
	bool m_bShowFooter;
	bool m_bShowHeader;
	bool m_bShowPageNumbers;
	bool m_bWrap;
	COLORREF m_clrBackGround;
	int m_nPrintRotateBest;
	IW::CArrayDWORD m_annotations;
	CString _strFooter;
	CString _strHeader;
	CRect m_rcMargin;
	CSize _sizeRowsColumns;

	PrintSettings();

	void Read(LPCTSTR szSection);
	void Write(LPCTSTR szSection) const;
};

// Where a tool's output goes and where its input comes from. CToolDlg inherits
// it, so the fields keep the names its dialog code already uses.
struct ToolTargetSettings
{
	bool _bRecurse;
	bool m_bOverwrite;
	bool m_bFolder;
	IW::CFilePath _pathFolderOut;

	ToolTargetSettings();

	void Read(LPCTSTR szSection);
	void Write(LPCTSTR szSection) const;
};

struct JpegToolSettings : ToolTargetSettings
{
	bool m_bMirrorLR;
	bool m_bMirrorTB;
	bool m_bRotate90;
	bool m_bRotate180;
	bool m_bRotate270;
	bool m_bGreyScale;
	bool m_bDropEdge;
	bool m_bOptimize;
	bool m_bProgressive;

	JpegToolSettings();

	void Read(LPCTSTR szSection);
	void Write(LPCTSTR szSection) const;
};

struct ResizeToolSettings : ToolTargetSettings
{
	int Width;
	int Height;
	bool KeepAspect;
	bool ScaleDown;
	int Filter;
	int Type;
	DWORD XPelsPerMeter;
	DWORD YPelsPerMeter;

	ResizeToolSettings();

	void Read(LPCTSTR szSection);
	void Write(LPCTSTR szSection) const;
};

struct ConvertToolSettings : ToolTargetSettings
{
	CString LoaderKey;
	IW::CodecSettings Codec;

	ConvertToolSettings();

	void Read(LPCTSTR szSection);
	void Write(LPCTSTR szSection) const;
};

struct ContactSheetToolSettings : ToolTargetSettings
{
	int Position;
	CString Template;
	CString LoaderKey;
	IW::CodecSettings Codec;
	PrintSettings Print;

	ContactSheetToolSettings();

	void Read(LPCTSTR szSection);
	void Write(LPCTSTR szSection) const;
};

struct ToolSettings
{
	ConvertToolSettings Convert;
	ResizeToolSettings Resize;
	JpegToolSettings Jpeg;
	ContactSheetToolSettings ContactSheet;

	void Read();
	void Write() const;
};

struct AppSettings
{
	AppSettings();

	// Constants
	enum { g_ePropMax = 10000 };

	// 0 = description only, 1 = adds the histogram and camera settings,
	// 2 = adds every metadata field the description dialog shows.
	enum { ImageDetailMax = 2 };

	// The only two calls that touch the ini for these.
	void Load();
	void Save() const;

	// A child of the app's root ini section.
	static CString Section(LPCTSTR szChild);

	// View Options
	DWORD m_nDescriptionPage;
	CSize _sizeThumbImage;

	CSize _sizeRowsColumns;

	IW::CArrayDWORD m_annotations;
	IW::CArrayDWORD m_columns;

	bool ShowDescriptions;
	bool ZoomThumbnails;
	bool m_bDoubleClickShowsFullScreen;
	bool m_bShowToolTips;
	bool m_bShowMarkers;
	bool m_bShowHidden;
	bool m_bWalkFolders;
	bool m_bShortDates;
	bool m_bUseEffects;
	bool _bExifAutoRotate;

	// Not a preference: a scope flag the context menu holds while it is up.
	bool _bDontHideCursor;

	bool ShowDescription;

	long ImageDetailLevel;

	bool BlackBackground;
	bool BlackSkin;

	long Mood;

	// Resolution
	int m_nResolutionSelection;
	DWORD m_dwXPelsPerMeter;
	DWORD m_dwYPelsPerMeter;

	IW::CodecSettings Codec;

	SearchSettings Search;
	RenameSettings Rename;
	PrintSettings Print;
	ToolSettings Tools;

	// Frame layout. Placement.length is 0 until a run has stored one.
	WINDOWPLACEMENT Placement;
	int SplitterPos;
	int ViewMode;

	int NormalSplitterPos;
	CString NormalScale;
	int NormalFolderView;

	// The status bar slider's thumbnail edge, at or below _sizeThumbImage.
	// 0 means the full size.
	int ThumbnailSize;

	int EditSplitterPos;
	int PrintSplitterPos;

	// Parsing paths, so they survive a folder being renamed under a different pidl.
	CString DefaultFolder;
	std::vector<CString> Favourites;

	// What the tag dialog last applied.
	CString Tags;

	// Loader key the New Image command writes with.
	CString CaptureFormat;
};

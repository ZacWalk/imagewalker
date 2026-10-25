// ImageWalker by Zac Walker
//
// Purpose: Page layout for printing - how many images fit a page and where
//          each one goes.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "ViewModelItems.h"
#include "FileFormatAny.h"

class CPrintFolder : public CPrintJobInfo, public PrintSettings
{
private:

	
	State &_state; 

public:

	CPrintFolder(State &state);	
	CPrintFolder(const CPrintFolder &f);

	virtual ~CPrintFolder();	
	
	void Copy(const CPrintFolder &f);
	void operator=(const CPrintFolder &f) { Copy(f); };


	bool PrintImage(CDCHandle dc, IW::Folder *pFolder, IW::FolderItem *pItem, CPoint point);
	bool PrintPage(CDCHandle dc, UINT nPage);
	bool PrintHeaders(CDCHandle dc, UINT nPage);

	int GetPageCount();
	bool CalcLayout();
	bool CalcLayout(CDCHandle dc);
	bool CalcLayout(CDCHandle dc, const CRect &rcPage);

	// The grid _sizeSection was measured against -- "one per page" makes it 1x1.
	// Anything that positions a thumbnail must ask this, not _sizeRowsColumns.
	CSize GridSize() const
	{
		return m_bPrintOnePerPage ? CSize(1, 1) : _sizeRowsColumns;
	}

	bool DrawImage(CDCHandle dc, CPoint point, IW::Image &image, IW::Folder *pFolder, IW::FolderItem *pItem);

	

	//print job info callback
	virtual bool IsValidPage(UINT nPage);
	virtual bool PrintPage(UINT nPage, HDC hDC);

	// Serialisation
	void LoadSettings(const PrintSettings &settings) { PrintSettings::operator=(settings); }
	PrintSettings SaveSettings() const { return *this; }

protected:

	// Preview
	CRect m_rcOutput = CRect(0, 0, 0, 0);
	CSize _sizeLogPixels = CSize(0, 0);
	CLoadAny _loader;

public:

	CFont m_font;
	CFont m_fontTitle;

	CDevModeT<true> m_devmode;
	CPrinterT<true> m_printer;

	IW::IStatus *_pStatus;

public:

	void GetPageRect(CRect& rc, LPRECT prc);
	void DoPaint(CDCHandle dc, CRect& rc, int nCurPage);

	

	// Content
	bool m_bPrinting;
	int m_nFooterHeight;
	int m_nHeaderHeight;
	int m_nPadding;
	int _nTextHeight = 0;
	CRect _rectExtents;
	CSize _sizeSection;
	CSize _sizeThumbNail;
	bool m_bHasPrinter = false;

	int m_nMaxPage = 0;
	int m_nMinPage = 0;
	int m_nCurPage = 0;
	int _nImageCount = 0;

	
};

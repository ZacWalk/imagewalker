// ImageWalker by Zac Walker
//
// Purpose: Print layout implementation, shared by print mode and the contact
//          sheet tool.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"
#include "ViewModelState.h"
#include "ViewPrintFolder.h"
#include "ViewFolderCtrl.h"


constexpr auto g_szCenter = _T("Center");
constexpr auto g_szWordWrap = _T("WordWrap");

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CPrintFolder::CPrintFolder(State& state) :
	_state(state),
	_loader(state.Loaders),
	m_bHasPrinter(false),
	m_nMaxPage(0),
	m_nMinPage(1),
	m_nCurPage(1),
	_nImageCount(-1)
{
	m_nPadding = 20;
	m_nFooterHeight = 80;
	m_nHeaderHeight = 160;
	m_bPrinting = false;
	_pStatus = IW::CNullStatus::Instance;
	m_rcOutput.left = 0;
	m_rcOutput.top = 0;
	m_rcOutput.right = 2878;
	m_rcOutput.bottom = 4123;
	_sizeLogPixels.cx = 360;
	_sizeLogPixels.cy = 360;

	m_printer.OpenDefaultPrinter();
	m_devmode.CopyFromPrinter(m_printer);

	//CDC dcPrinter = m_printer.CreatePrinterDC(m_devmode);
	//CalcLayout(dcPrinter.m_hDC);	
}

CPrintFolder::CPrintFolder(const CPrintFolder& f) : PrintSettings(f), _state(f._state), _loader(f._state.Loaders)
{
	Copy(f);
};

void CPrintFolder::Copy(const CPrintFolder& f)
{
	PrintSettings::operator=(f);

	m_bPrinting = f.m_bPrinting;
	m_nFooterHeight = f.m_nFooterHeight;
	m_nHeaderHeight = f.m_nHeaderHeight;
	m_nPadding = f.m_nPadding;
	_nTextHeight = f._nTextHeight;
	_rectExtents = f._rectExtents;
	_sizeSection = f._sizeSection;
	_sizeThumbNail = f._sizeThumbNail;

	m_rcOutput = f.m_rcOutput;
	_sizeLogPixels = f._sizeLogPixels;
	m_bHasPrinter = f.m_bHasPrinter;
	m_nMaxPage = f.m_nMaxPage;
	m_nMinPage = f.m_nMinPage;
	m_nCurPage = f.m_nCurPage;
	_nImageCount = f._nImageCount;

	_pStatus = f._pStatus;
}


CPrintFolder::~CPrintFolder()
{
}

int CPrintFolder::GetPageCount()
{
	int nCount = 0;

	try
	{
		IW::FolderPtr pFolder = _state.Folder.GetFolder();
		long nNormalCount = pFolder->GetItemCount();

		bool bImage = false;
		bool bSelected = false;

		for (int i = 0; i < nNormalCount; i++)
		{
			bImage = pFolder->IsItemImage(i);
			bSelected = pFolder->IsItemSelected(i);

			// Only process if image
			if (bImage &&
				(!m_bPrintSelected || bSelected))
			{
				nCount++;
			}
		}

		int nImageRows = m_bPrintOnePerPage ? 1 : _sizeRowsColumns.cy;
		int nImageColumns = m_bPrintOnePerPage ? 1 : _sizeRowsColumns.cx;

		nCount = IW::iceil(nCount, nImageRows * nImageColumns);
	}
	catch (_com_error& e)
	{
		IW::CMessageBoxIndirect mb;
		mb.ShowException(IDS_LOW_LEVEL_ERROR_FMT, e);
	}

	return IW::LowerLimit<1>(nCount);
}

bool CPrintFolder::CalcLayout()
{
	if (m_devmode.m_pDevMode)
	{
		m_devmode.m_pDevMode->dmOrientation = m_bPrintLandscape ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
	}

	CDC dcPrinter = m_printer.CreatePrinterDC(m_devmode);
	return CalcLayout(dcPrinter.m_hDC);
}


bool CPrintFolder::CalcLayout(CDCHandle dcPrinter)
{
	if (!dcPrinter.IsNull())
	{
		CRect rcPage(0, 0,
		             dcPrinter.GetDeviceCaps(PHYSICALWIDTH) - 2 * dcPrinter.GetDeviceCaps(PHYSICALOFFSETX),
		             dcPrinter.GetDeviceCaps(PHYSICALHEIGHT) - 2 * dcPrinter.GetDeviceCaps(PHYSICALOFFSETY));

		// Fix for 98...PHYSICALWIDTH seems to fail on 98?
		if (rcPage.right == 0)
		{
			rcPage.right = dcPrinter.GetDeviceCaps(HORZRES);
			rcPage.bottom = dcPrinter.GetDeviceCaps(VERTRES);
		}

		return CalcLayout(dcPrinter, rcPage);
	}

	return CalcLayout(dcPrinter, m_rcOutput);
}

bool CPrintFolder::CalcLayout(CDCHandle dcPrinter, const CRect& rcPage)
{
	m_bHasPrinter = !dcPrinter.IsNull();

	if (m_bHasPrinter)
	{
		if (rcPage.right > 0)
		{
			m_rcOutput = rcPage;
		}

		_sizeLogPixels.cx = dcPrinter.GetDeviceCaps(LOGPIXELSX);
		_sizeLogPixels.cy = dcPrinter.GetDeviceCaps(LOGPIXELSY);
	}


	if (m_fontTitle.m_hFont != nullptr) m_fontTitle.DeleteObject();
	if (m_font.m_hFont != nullptr) m_font.DeleteObject();

	int nTitleFontSize = 16;
	int nFontSize = 8;
	int nPaddingSize = 4;

	//Set up the font for the titles on the intro and ending pages
	NONCLIENTMETRICS ncm = {0};
	ncm.cbSize = sizeof(ncm);

	LOGFONT lf;

	if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
	{
		//Create the intro/end title font
		lf = ncm.lfMessageFont;
	}
	else
	{
		::GetObject(::GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf);
	}

	lf.lfHeight = 0 - MulDiv(_sizeLogPixels.cy, nTitleFontSize, 72);
	m_fontTitle.CreateFontIndirect(&lf);
	lf.lfHeight = 0 - MulDiv(_sizeLogPixels.cy, nFontSize, 72);
	m_font.CreateFontIndirect(&lf);

	///////////////////////
	//convert from 1/1000" to twips
	_rectExtents.left = m_rcOutput.left + MulDiv(m_rcMargin.left, _sizeLogPixels.cx, 1000);
	_rectExtents.right = m_rcOutput.right - MulDiv(m_rcMargin.right, _sizeLogPixels.cx, 1000);
	_rectExtents.top = m_rcOutput.top + MulDiv(m_rcMargin.top, _sizeLogPixels.cy, 1000);
	_rectExtents.bottom = m_rcOutput.bottom - MulDiv(m_rcMargin.bottom, _sizeLogPixels.cy, 1000);

	m_nPadding = MulDiv(_sizeLogPixels.cy, nPaddingSize, 72);
	m_nFooterHeight = MulDiv(_sizeLogPixels.cy, nFontSize, 72) * 2;
	m_nHeaderHeight = MulDiv(_sizeLogPixels.cy, nTitleFontSize, 72) * 2;

	// Do we want footers and headers?
	int nFooterHeaderHeight = 0;

	if (m_bShowFooter || m_bShowPageNumbers)
	{
		nFooterHeaderHeight += m_nFooterHeight;
	}

	if (m_bShowHeader)
	{
		nFooterHeaderHeight += m_nHeaderHeight;
	}

	const CSize sizeGrid = GridSize();
	int nImageRows = sizeGrid.cy;
	int nImageColumns = sizeGrid.cx;

	// Calculate the thumbnail size
	_sizeSection.cx = ((_rectExtents.right - _rectExtents.left) / nImageColumns);
	_sizeSection.cy = (((_rectExtents.bottom - _rectExtents.top) - nFooterHeaderHeight) / nImageRows);

	if (_sizeSection.cx < 10 || _sizeSection.cy < 10)
	{
		return false;
	}

	_sizeThumbNail = _sizeSection;
	_sizeThumbNail.cy -= (m_nPadding * 2);
	_sizeThumbNail.cx -= (m_nPadding * 2);

	if (_sizeThumbNail.cy < 10)
	{
		return false;
	}

	m_nMaxPage = GetPageCount();

	return true;
}

bool CPrintFolder::PrintPage(CDCHandle dc, UINT nPage)
{
	try
	{
		IW::FolderPtr pFolder = _state.Folder.GetFolder();

		int nBkMode = dc.SetBkMode(TRANSPARENT);
		COLORREF clrTextColor = dc.SetTextColor(RGB(0, 0, 0));
		int nImageRows = m_bPrintOnePerPage ? 1 : _sizeRowsColumns.cy;
		int nImageColumns = m_bPrintOnePerPage ? 1 : _sizeRowsColumns.cx;
		int nImagePerPage = nImageColumns * nImageRows;
		int nMin = nImagePerPage * nPage;
		int nMax = nMin + nImagePerPage;
		int nImage = 0;
		long nNormalCount = pFolder->GetItemCount();
		bool bImage = false;
		bool bSelected = false;
		//bool bIsPreview = OBJ_ENHMETADC == GetObjectType(dc);

		for (int i = 0; i < nNormalCount; i++)
		{
			_pStatus->SetHighLevelProgress(i, nNormalCount);

			bImage = pFolder->IsItemImage(i);
			bSelected = pFolder->IsItemSelected(i);

			// Only process if image
			if (bImage &&
				(!m_bPrintSelected || bSelected))
			{
				if (nImage >= nMin && nImage < nMax)
				{
					int nImageThisPage = nImage - nMin;

					CPoint point;
					point.x = _rectExtents.left + (_sizeSection.cx * (nImageThisPage % nImageColumns));
					point.y = _rectExtents.top + (_sizeSection.cy * (nImageThisPage / nImageColumns));

					if (m_bShowHeader)
					{
						point.y += m_nHeaderHeight;
					}

					IW::FolderItemPtr pItem = pFolder->GetItem(i);
					if (pItem && !PrintImage(dc, pFolder, pItem, point))
						return false;
				}

				nImage++;
			}
		}

		dc.SetBkMode(nBkMode);
		dc.SetTextColor(clrTextColor);
	}
	catch (_com_error& e)
	{
		IW::CMessageBoxIndirect mb;
		mb.ShowException(IDS_LOW_LEVEL_ERROR_FMT, e);
	}

	return true;
}

bool CPrintFolder::PrintHeaders(CDCHandle dc, UINT nPage)
{
	try
	{
		int nBkMode = dc.SetBkMode(TRANSPARENT);
		COLORREF clrTextColor = dc.SetTextColor(RGB(0, 0, 0));

		// Draw header and footer!!
		if (m_bShowFooter || m_bShowPageNumbers)
		{
			HFONT hOldFont = dc.SelectFont(m_font);

			int cy = _rectExtents.bottom - (m_nFooterHeight - m_nPadding);

			dc.MoveTo(_rectExtents.left + m_nPadding, cy);
			dc.LineTo(_rectExtents.right - m_nPadding, cy);

			CRect r(_rectExtents.left, cy, _rectExtents.right, _rectExtents.bottom);

			if (m_bShowFooter)
			{
				DWORD dwStyle = DT_NOCLIP | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX;
				dwStyle |= (!m_bShowPageNumbers) ? DT_CENTER : DT_LEFT;
				dc.DrawText(_strFooter, -1, r, dwStyle);
			}

			if (m_bShowPageNumbers)
			{
				DWORD dwStyle = DT_NOCLIP | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX;
				dwStyle |= (!m_bShowFooter) ? DT_CENTER : DT_RIGHT;

				CString str;
				str.Format(IDS_PRINT_PAGE_FMT, nPage + 1, GetPageCount());
				dc.DrawText(str, -1, &r, dwStyle);
			}

			dc.SelectFont(hOldFont);
		}

		if (m_bShowHeader)
		{
			int cy = (_rectExtents.top + m_nHeaderHeight) - m_nPadding;

			dc.MoveTo(_rectExtents.left + m_nPadding, cy);
			dc.LineTo(_rectExtents.right - m_nPadding, cy);

			CRect r(_rectExtents.left, _rectExtents.top, _rectExtents.right, cy);

			HFONT hOldFont = dc.SelectFont(m_fontTitle);
			dc.DrawText(_strHeader, -1, &r,
			            DT_NOCLIP | DT_VCENTER | DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
			dc.SelectFont(hOldFont);
		}

		dc.SetBkMode(nBkMode);
		dc.SetTextColor(clrTextColor);
	}
	catch (_com_error& e)
	{
		IW::CMessageBoxIndirect mb;
		mb.ShowException(IDS_LOW_LEVEL_ERROR_FMT, e);
	}

	return true;
}

bool CPrintFolder::DrawImage(CDCHandle dc, CPoint point, IW::Image& image, IW::Folder* pFolder, IW::FolderItem* pItem)
{
	HFONT hOldFont = dc.SelectFont(m_font);
	UINT uTextStyle = DT_NOPREFIX | DT_EDITCONTROL;
	if (m_bCenter) uTextStyle |= DT_CENTER;
	if (!m_bWrap) uTextStyle |= DT_WORD_ELLIPSIS;
	else uTextStyle |= DT_WORDBREAK;

	// Calc Text Size
	CSimpleArray<CString> arrayStrText;
	CSimpleValArray<int> arrayHeights;
	pFolder->GetItemFormatText(pItem, arrayStrText, m_annotations, true);

	int i, nHeightText = 0;

	for (i = 0; i < arrayStrText.GetSize(); i++)
	{
		CRect r(point.x,
		        point.y + _sizeSection.cy,
		        point.x + _sizeSection.cx,
		        point.y + _sizeSection.cy);

		dc.DrawText(arrayStrText[i], -1,
		            &r, uTextStyle | DT_CALCRECT);

		int nLimit = _sizeThumbNail.cy / 2;
		if (nHeightText + r.Height() > nLimit)
		{
			arrayHeights.Add(nLimit - nHeightText);
			nHeightText = nLimit;
			break;
		}
		nHeightText += r.Height();
		arrayHeights.Add(r.Height());
	}

	CSize sizeThumbNailLocal = _sizeThumbNail;
	sizeThumbNailLocal.cy -= nHeightText;

	if (!image.IsEmpty())
	{
		IW::Page page = image.GetFirstPage();

		// Best fit rotate
		if (m_nPrintRotateBest > 0)
		{
			bool bRotate = ((sizeThumbNailLocal.cx > sizeThumbNailLocal.cy) &&
					(page.GetWidth() < page.GetHeight())) ||
				((sizeThumbNailLocal.cx < sizeThumbNailLocal.cy) &&
					(page.GetWidth() > page.GetHeight()));

			if (bRotate)
			{
				IW::Image imageRotate;

				if (m_nPrintRotateBest == 1)
				{
					IW::Rotate270(image, imageRotate, _pStatus);
				}
				else
				{
					IW::Rotate90(image, imageRotate, _pStatus);
				}

				image = imageRotate;
			}
		}

		page = image.GetFirstPage();
		page.SetBackGround(m_clrBackGround);

		const CRect rectBounding = image.GetBoundingRect();

		const long icx = rectBounding.Width();
		const long icy = rectBounding.Height();
		constexpr long nDiv = 0x1000;

		// Scale the image
		long sh = MulDiv(sizeThumbNailLocal.cx, nDiv, icx);
		long sw = MulDiv(sizeThumbNailLocal.cy, nDiv, icy);
		long s = IW::Min(sh, sw);

		const CSize sizeImage(MulDiv(page.GetWidth(), s, nDiv), MulDiv(page.GetHeight(), s, nDiv));
		const CPoint pt(point.x + ((sizeThumbNailLocal.cx - sizeImage.cx) / 2) + m_nPadding,
		                point.y + ((sizeThumbNailLocal.cy - sizeImage.cy) / 2) + m_nPadding);

		const CRect rectPrint(pt, sizeImage);

		if ((rectPrint.Width() < page.GetWidth()) && (rectPrint.Height() < page.GetHeight()))
		{
			IW::Image imageScaled;
			IW::Scale(image, imageScaled, rectPrint.Size(), _pStatus);
			image = imageScaled;
			page = image.GetFirstPage();
		}

		IW::CRender::DrawToDC(dc, page, rectPrint);
	}

	// Draw the Text!
	CRect rectText(point.x,
	               point.y + _sizeSection.cy - nHeightText,
	               point.x + _sizeSection.cx,
	               point.y + _sizeSection.cy);

	for (i = 0; i < arrayHeights.GetSize(); i++)
	{
		dc.DrawText(arrayStrText[i], -1, &rectText, uTextStyle);
		rectText.top += arrayHeights[i];
	}

	dc.SelectFont(hOldFont);


	return true;
}

bool CPrintFolder::PrintImage(CDCHandle dc, IW::Folder* pFolder, IW::FolderItem* pItem, CPoint point)
{
	try
	{
		// Scale the image
		IW::Image image;

		// If its a preview get
		// preloaded thumbnail
		if (!m_bPrinting)
		{
			// Through the folder, not the item: the decode worker assigns _image
			// under _cs and Image::operator= frees the buffer being copied here.
			pFolder->GetItemImage(pItem, image);
		}
		else
		{
			CString strFilePath = pItem->GetFilePath();

			if (_loader.LoadImage(strFilePath, image, _pStatus) && !image.IsEmpty())
			{
			}
			else
			{
				IW::CMessageBoxIndirect mb;
				mb.Show(IDS_FAILEDTOLOAD);
				return false;
			}
		}

		DrawImage(dc, point, image, pFolder, pItem);
	}
	catch (_com_error& e)
	{
		IW::CMessageBoxIndirect mb;
		mb.ShowException(IDS_LOW_LEVEL_ERROR_FMT, e);
	}

	return true;
}

//print job info callback
bool CPrintFolder::IsValidPage(UINT nPage)
{
	long nMaxCount = GetPageCount();
	return (nPage >= 1 && nPage <= static_cast<unsigned long>(nMaxCount)); // we have only one page
}

bool CPrintFolder::PrintPage(UINT nPage, HDC hDC)
{
	CDCHandle dc(hDC);

	if (m_clrBackGround != RGB(255, 255, 255))
	{
		dc.FillSolidRect(&_rectExtents, m_clrBackGround);
	}

	return PrintPage(dc, nPage - 1) &&
		PrintHeaders(dc, nPage - 1);
}

void CPrintFolder::GetPageRect(CRect& rc, LPRECT prc)
{
	int x1 = rc.right - rc.left;
	int y1 = rc.bottom - rc.top;

	if ((x1 < 0) || (y1 < 0))
		return;

	//Compute whether we are OK vertically or horizontally
	int x2 = m_rcOutput.right - m_rcOutput.left;
	int y2 = m_rcOutput.bottom - m_rcOutput.top;
	int y1p = MulDiv(x1, y2, x2);
	int x1p = MulDiv(y1, x2, y2);

	ATLASSERT((x1p <= x1) || (y1p <= y1));

	if (x1p <= x1)
	{
		prc->left = rc.left + (x1 - x1p) / 2;
		prc->right = prc->left + x1p;
		prc->top = rc.top;
		prc->bottom = rc.bottom;
	}
	else
	{
		prc->left = rc.left;
		prc->right = rc.right;
		prc->top = rc.top + (y1 - y1p) / 2;
		prc->bottom = prc->top + y1p;
	}
}

// Painting helper
void CPrintFolder::DoPaint(CDCHandle dc, CRect& rc, int nCurPage)
{
	dc.SetMapMode(MM_ANISOTROPIC);
	dc.SetWindowExt(m_rcOutput.right - m_rcOutput.left, m_rcOutput.bottom - m_rcOutput.top);
	dc.SetWindowOrg(0, 0);

	dc.SetViewportExt(rc.right - rc.left, rc.bottom - rc.top);
	dc.SetViewportOrg(rc.left, rc.top);

	PrePrintPage(nCurPage, dc);
	PrintPage(nCurPage, dc);
	PostPrintPage(nCurPage, dc);
}

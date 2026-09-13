// ImageWalker by Zac Walker
//
// Purpose: Contact sheet implementation.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"
#include "ToolContactSheet.h"

#include "ViewModelState.h"
#include "ViewDialogs.h"
#include "ViewPrintFolder.h"


///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CToolContactSheet::CToolContactSheet(CPrintFolder& options, State& state) :
	BaseClass(state),
	_optionsPage(*this, state.Loaders),
	_options(options),
	_state(state),
	_loader(state.Loaders)
{
	// Defaults
	_strOutputFile.LoadString(IDS_TOOL_CONTACTSHEET_DEFAULTFILE);

	_sizeOutputImage.cx = GetSystemMetrics(SM_CXFULLSCREEN);
	_sizeOutputImage.cy = GetSystemMetrics(SM_CYFULLSCREEN);

	// The sheet is what the print preview is showing, so it takes its file set
	// from the same place the preview does.
	_bSelected = options.m_bPrintSelected;
	_bRecurse = false;

	_nImageNumber = 0;
	_nImageOutCount = 1;

	_options.m_bPrinting = true;
	_options.m_bShowPageNumbers = false;

	_options.m_rcMargin.left = 0;
	_options.m_rcMargin.top = 0;
	_options.m_rcMargin.right = 0;
	_options.m_rcMargin.bottom = 0;
}


CToolContactSheet::~CToolContactSheet()
{
}

void CToolContactSheet::LoadSettings()
{
	BaseClass::LoadSettings();

	_options.LoadSettings(Settings().Print);
	_optionsPage._strDefaultSelection = Settings().LoaderKey;
	_optionsPage.Codec = Settings().Codec;
}

void CToolContactSheet::SaveSettings()
{
	Settings().Print = _options.SaveSettings();
	Settings().Codec = _optionsPage.Codec;

	if (m_pLoaderFactory != nullptr)
		Settings().LoaderKey = m_pLoaderFactory->GetKey();

	BaseClass::SaveSettings();
}

CString CToolContactSheet::GetKey() const
{
	return _T("ContactSheetTool");
}

CString CToolContactSheet::GetTitle() const
{
	return App.LoadString(IDS_TOOL_CONTACTSHEET_TITLE);
}

CString CToolContactSheet::GetCompletedText() const
{
	return App.LoadString(IDS_TOOL_CONTACTSHEET_COMPLETE);
}

HWND CToolContactSheet::OnCreateOptions(HWND hWndParent)
{
	return _optionsPage.Create(hWndParent);
}

bool CToolContactSheet::OnApplyOptions()
{
	return _optionsPage.Apply();
}

void CToolContactSheet::OnProcess(IStatus* pStatus)
{
	IW::CFilePath path(_pathFolderOut);

	if (!path.CreateAllDirectories())
	{
		IW::CMessageBoxIndirect mb;
		mb.ShowOsErrorWithFile(_pathFolderOut, IDS_FAILEDTO_CREATE_FOLDER);
	}
	else
	{
		IterateItems(this);

		const CSize sizeGrid = _options.GridSize();
		int nImagePerPage = sizeGrid.cx * sizeGrid.cy;

		// May need to save the last image
		if (_nImageNumber < nImagePerPage && _nImageNumber > 0)
		{
			CDC dc;
			dc.CreateCompatibleDC(nullptr);

			IW::Image image;
			if (image.Copy(dc, m_bmCoverSheet))
			{
				SaveContactSheet(image, pStatus);
			}
		}
	}
}

void CToolContactSheet::OnComplete()
{
}

bool CToolContactSheet::StartFolder(IW::Folder* pFolder, IStatus* pStatus)
{
	if (_bRecurse)
	{
		pFolder->IterateItems(this, pStatus);
	}

	return true;
}

bool CToolContactSheet::StartItem(IW::FolderItem* pItem, IStatus* pStatus)
{
	// Create the thumbnail
	IW::Image imageIn = pItem->OpenAsImage(_loader, pStatus);

	if (imageIn.IsEmpty())
	{
		// Set error
		pStatus->SetError(App.LoadString(IDS_FAILEDTOLOAD));
		return false;
	}
	// Add it to out image.
	if (_nImageNumber == 0)
	{
		// Set image to white
		CDC dc;
		if (dc.CreateCompatibleDC(nullptr))
		{
			if (m_bmCoverSheet.m_hBitmap == nullptr &&
				m_bmCoverSheet.CreateBitmap(_sizeOutputImage.cx, _sizeOutputImage.cy, 1, dc.GetDeviceCaps(BITSPIXEL),
				                            nullptr) == nullptr)
			{
				CString str;
				str.LoadString(IDS_FAILEDTO_CREATE_IMAGE);
				pStatus->SetError(str);
				return false;
			}

			HBITMAP hbmOld = dc.SelectBitmap(m_bmCoverSheet);

			if (hbmOld)
			{
				RECT rcPage = {0, 0, _sizeOutputImage.cx, _sizeOutputImage.cy};

				// Setup the print options
				_options.CalcLayout(dc.m_hDC, rcPage);

				dc.FillSolidRect(&_options._rectExtents, _options.m_clrBackGround);

				_options.PrintHeaders(dc.m_hDC, 0);
				dc.SelectBitmap(hbmOld);
			}
		}
	}

	const CSize sizeGrid = _options.GridSize();
	const int nImagePerPage = sizeGrid.cx * sizeGrid.cy;
	const int x = _options._rectExtents.left + (_options._sizeSection.cx * (_nImageNumber % sizeGrid.cx));
	int y = _options._rectExtents.top + (_options._sizeSection.cy * (_nImageNumber / sizeGrid.cx));

	if (_options.m_bShowHeader)
	{
		y += _options.m_nHeaderHeight;
	}


	// Draw the image
	CDC dc;
	if (dc.CreateCompatibleDC(nullptr))
	{
		HBITMAP hbmOld = dc.SelectBitmap(m_bmCoverSheet);

		if (hbmOld)
		{
			HFONT hOldFont = dc.SelectFont(_options.m_font);
			dc.SetBkMode(TRANSPARENT);

			// Draw the image
			IW::Image image;
			image.Copy(imageIn);

			//CPoint point(x + (_options._sizeThumbNail.cx / 2), y + (_options._sizeThumbNail.cy / 2));
			CPoint point(x, y);
			_options.DrawImage(static_cast<HDC>(dc), point, image, _state.Folder.GetFolder(), pItem);

			dc.SelectFont(hOldFont);
			dc.SelectBitmap(hbmOld);
		}
	}

	_nImageNumber++;

	if (_nImageNumber >= nImagePerPage)
	{
		_nImageNumber = 0;

		IW::Image image;
		if (!image.Copy(dc, m_bmCoverSheet))
			return false;

		return SaveContactSheet(image, pStatus);
	}

	return true;
}

bool CToolContactSheet::EndItem()
{
	return true;
}

bool CToolContactSheet::EndFolder()
{
	return true;
}

bool CToolContactSheet::SaveContactSheet(const IW::Image& image, IStatus* pStatus)
{
	// The counter starts again every run, so without this a second sheet of the
	// same folder would be written over the first.
	IW::CFilePath pathOut;

	do
	{
		CString strInOutName;
		strInOutName.Format(_T("%s %d"), static_cast<LPCTSTR>(_strOutputFile), _nImageOutCount++);

		pathOut = _pathFolderOut;
		pathOut += strInOutName;
		pathOut.SetExtension(m_pLoaderFactory->GetExtensionDefault());
	}
	while (IW::Path::FileExists(pathOut));

	IW::CFileTemp f;

	if (!f.OpenForWrite(pathOut))
	{
		CString str;
		str.LoadString(IDS_FAILEDTOSAVEIMAGE);
		pStatus->SetError(str);
		f.Abort();

		return false;
	}
	if (m_pLoader == nullptr || !m_pLoader->Write(g_szEmptyString, &f, image, _optionsPage.Codec, pStatus))
	{
		CString str;
		str.LoadString(IDS_FAILEDTOSAVEIMAGE);
		pStatus->SetError(str);
		f.Abort();

		return false;
	}

	return f.Close(pStatus);
}


//////////////////////////////////////////////////////////////////////
// CToolContactSheetOptions
//////////////////////////////////////////////////////////////////////

LRESULT CToolContactSheetOptions::OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	CComboBox comboWidth = GetDlgItem(IDC_WIDTH);
	CComboBox comboHeight = GetDlgItem(IDC_HEIGHT);

	int widths[] = { 320, 640, 800, 1024, 1280, 1600, 2048, GetSystemMetrics(SM_CXFULLSCREEN), -1 };
	int heights[] = { 320, 640, 800, 1024, 1280, 1600, 2048, GetSystemMetrics(SM_CYFULLSCREEN), -1 };

	IW::SetItems(comboWidth, widths, _parent._sizeOutputImage.cx);
	IW::SetItems(comboHeight, heights, _parent._sizeOutputImage.cy);

	SetDlgItemText(IDC_FILE, _parent._strOutputFile);

	// The loader combo fills itself; let its own handler run too.
	bHandled = FALSE;
	return 0;
}

bool CToolContactSheetOptions::Apply()
{
	BOOL b;

	_parent._sizeOutputImage.cx = GetDlgItemInt(IDC_WIDTH, &b, TRUE);
	_parent._sizeOutputImage.cy = GetDlgItemInt(IDC_HEIGHT, &b, TRUE);

	GetDlgItemText(IDC_FILE, _parent._strOutputFile);

	if (!IW::CFilePath::CheckFileName(_parent._strOutputFile))
	{
		CString str;
		str.Format(IDS_INVALID_FILE, static_cast<LPCTSTR>(_parent._strOutputFile));

		IW::CMessageBoxIndirect mb;
		mb.Show(str);

		return false;
	}

	OnApplyLoader();

	_parent.m_pLoader = m_pLoader;
	_parent.m_pLoaderFactory = m_pLoaderFactory;

	return m_pLoader != nullptr && m_pLoaderFactory != nullptr;
}

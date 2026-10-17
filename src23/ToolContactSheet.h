// ImageWalker by Zac Walker
//
// Purpose: Contact sheet tool: lays a folder out as a grid of thumbnails on
//          printed pages.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "ViewPrintFolder.h"
#include "ToolDlg.h"

class CToolContactSheet;

// Sheet size, output format and base file name -- three wizard pages when the
// three of them fit on four lines. The folder they go in is the shared
// destination on the tool dialog.
class CToolContactSheetOptions :
	public CDialogImpl<CToolContactSheetOptions>,
	public IW::CImageLoaderDlgImpl<CToolContactSheetOptions>
{
public:

	typedef CToolContactSheetOptions ThisClass;
	typedef IW::CImageLoaderDlgImpl<ThisClass> LoaderBase;

	enum { IDD = IDD_TOOL_OPT_CONTACTSHEET };

	CToolContactSheet &_parent;

	CToolContactSheetOptions(CToolContactSheet &parent, ImageLoaders &loaders) :
		LoaderBase(loaders), _parent(parent)
	{
	}

	BEGIN_MSG_MAP(ThisClass)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		CHAIN_MSG_MAP(LoaderBase)
	END_MSG_MAP()

	void OnChange()
	{
	}

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	bool Apply();
};

class CToolContactSheet : public CToolDlg<CToolContactSheet, ContactSheetToolSettings>
{
public:
	typedef CToolContactSheet ThisClass;
	typedef CToolDlg<ThisClass, ContactSheetToolSettings> BaseClass;

protected:

	CToolContactSheetOptions _optionsPage;

public:

	// Output stats
	DWORD _nStart;
	CPrintFolder _options;
	State &_state;

	CToolContactSheet(CPrintFolder &options, State &state);
	~CToolContactSheet();

	bool SaveContactSheet(const IW::Image &image, IW::IStatus *pStatus);
	
	// CToolDlg host contract
	HWND OnCreateOptions(HWND hWndParent);
	bool OnApplyOptions();
	void OnProcess(IW::IStatus *pStatus);
	void OnComplete();

	// A contact sheet is not one file per input, so there is nothing to overwrite,
	// and which files it holds was settled by the print view that opened it.
	bool AllowOverwrite() const { return false; }
	bool AllowSource() const { return false; }

	// Item Iteration
	bool StartFolder(IW::Folder *pFolder, IW::IStatus *pStatus);
	bool StartItem(IW::FolderItem *pItem, IW::IStatus *pStatus);
	bool EndItem();
	bool EndFolder(); 

	void OnHelp() const
	{
		App.InvokeHelp(m_hWnd, HELP_PROCESSCATALOGUE);
	}

	// Methods to get description info
	CString GetKey() const;
	CString GetTitle() const;
	CString GetCompletedText() const;

	// Properties
	ContactSheetToolSettings &Settings() { return App.Settings.Tools.ContactSheet; }

	void LoadSettings();
	void SaveSettings();

	// Attributes
	CString _strOutputFile;
	CSize _sizeOutputImage;
	
	IW::ImageLoaderInfoPtr m_pLoaderFactory;
	IW::RefPtr<IW::IImageLoader> m_pLoader;

protected:	

	CBitmap m_bmCoverSheet;
	CLoadAny _loader;

	long _nImageNumber;
	long _nImageOutCount;
};

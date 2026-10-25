// ImageWalker by Zac Walker
//
// Purpose: Convert tool: batch format conversion with the destination format
//          settings.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "ToolDlg.h"

// Just the format to write and its settings. Choosing a loader and remembering
// its options is what IW::CImageLoaderDlgImpl already does for the save dialog
// and the contact sheet, so this page no longer keeps its own copy of it.
class CToolConvertOptions :
	public CDialogImpl<CToolConvertOptions>,
	public IW::CImageLoaderDlgImpl<CToolConvertOptions>
{
public:

	typedef CToolConvertOptions ThisClass;
	typedef IW::CImageLoaderDlgImpl<ThisClass> LoaderBase;

	enum { IDD = IDD_TOOL_OPT_CONVERT };

	CToolConvertOptions(ImageLoaders &loaders) : LoaderBase(loaders)
	{
	}

	BEGIN_MSG_MAP(ThisClass)
		CHAIN_MSG_MAP(LoaderBase)
	END_MSG_MAP()

	void OnChange()
	{
	}
};

class CToolConvert : public CToolDlg<CToolConvert, ConvertToolSettings>
{
public:
	typedef CToolConvert ThisClass;
	typedef CToolDlg<ThisClass, ConvertToolSettings> BaseClass;

protected:

	CToolConvertOptions _options;
	CLoadAny _loader;

public:

	IW::ImageLoaderInfoPtr m_pLoaderFactory;
	IW::RefPtr<IW::IImageLoader> m_pLoader;

	CToolConvert(State &state);
	~CToolConvert();

	// CToolDlg host contract
	HWND OnCreateOptions(HWND hWndParent);
	bool OnApplyOptions();
	void OnProcess(IW::IStatus *pStatus);
	void OnComplete();

	// Item iteration
	bool StartFolder(IW::Folder *pFolder, IW::IStatus *pStatus);
	bool StartItem(IW::FolderItem *pItem, IW::IStatus *pStatus);
	bool EndItem();
	bool EndFolder();

	// Converting in place used to mean "write the new file over the old one and
	// then delete the old one" -- which, whenever the two names matched, deleted
	// the file it had just written. Convert produces new files now.
	bool AllowOverwrite() const { return false; }
	CString DefaultOutputSubFolder() const { return _T("Converted"); }

	CString GetKey() const;
	CString GetTitle() const;
	CString GetCompletedText() const;

	ConvertToolSettings &Settings() { return App.Settings.Tools.Convert; }

	void LoadSettings();
	void SaveSettings();

	void OnHelp() const
	{
		App.InvokeHelp(m_hWnd, HELP_TOOL_CONVERT);
	}
};

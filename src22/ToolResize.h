// ImageWalker by Zac Walker
//
// Purpose: Resize tool: batch resize to a size, a percentage or a bounding
//          box.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "ToolDlg.h"
#include "ViewResizeDlg.h"

class CToolResize;

class CToolResizeOptions :
	public CDialogImpl<CToolResizeOptions>,
	public CResizeFields<CToolResizeOptions>
{
public:

	typedef CToolResizeOptions ThisClass;
	typedef CResizeFields<ThisClass> ResizeBase;

	enum { IDD = IDD_TOOL_RESIZE };

	CToolResizeOptions(CFilterResize *pFilter) : ResizeBase(pFilter)
	{
	}

	BEGIN_MSG_MAP(ThisClass)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_HANDLER(IDC_WIDTH, CBN_EDITCHANGE, OnWidthChange)
		COMMAND_HANDLER(IDC_HEIGHT, CBN_EDITCHANGE, OnHeightChange)
		COMMAND_HANDLER(IDC_WIDTH, CBN_SELCHANGE, OnWidthChangeSel)
		COMMAND_HANDLER(IDC_HEIGHT, CBN_SELCHANGE, OnHeightChangeSel)
		COMMAND_ID_HANDLER(IDC_KEEP_ASPECT, OnButtonChange)
		COMMAND_ID_HANDLER(IDC_SCALE_DOWN, OnButtonChange)
		COMMAND_HANDLER(IDC_TYPE, CBN_SELCHANGE, OnChangeType)
		COMMAND_HANDLER(IDC_FILTER, CBN_SELCHANGE, OnChangeFilter)
		COMMAND_HANDLER(IDC_CX, EN_CHANGE, OnChangeRes)
		COMMAND_HANDLER(IDC_CY, EN_CHANGE, OnChangeRes)
	END_MSG_MAP()

	void OnChange()
	{
	}
};

class CToolResize : public CToolDlg<CToolResize, ResizeToolSettings>
{
public:
	typedef CToolResize ThisClass;
	typedef CToolDlg<ThisClass, ResizeToolSettings> BaseClass;

	CToolResizeOptions _options;
	CLoadAny _loader;
	CFilterResize _filter;

	CToolResize(State &state);
	~CToolResize();

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

	// The overwrite branch wrote to a temp file nothing ever committed, so it had
	// never once resized anything. Resize produces new files.
	bool AllowOverwrite() const { return false; }
	CString DefaultOutputSubFolder() const { return _T("Resized"); }

	CString GetKey() const;
	CString GetTitle() const;
	CString GetCompletedText() const;

	ResizeToolSettings &Settings() { return App.Settings.Tools.Resize; }

	void LoadSettings();
	void SaveSettings();

	void OnHelp() const
	{
		App.InvokeHelp(m_hWnd, HELP_TOOL_RESIZE);
	}
};

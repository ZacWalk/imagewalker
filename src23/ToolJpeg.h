// ImageWalker by Zac Walker
//
// Purpose: Lossless JPEG tool: batch rotate, flip and crop without
//          re-encoding.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

// Jpeg.h: interface for the CToolJpeg class.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "ToolDlg.h"
#include "iw/legacy/JpegTransform.h"

class CToolJpeg;

// The transformation and the encoder switches, which were two wizard pages for
// no reason other than that a wizard page was the unit available.
class CToolJpegOptions : public CDialogImpl<CToolJpegOptions>
{
public:

	typedef CToolJpegOptions ThisClass;

	CToolJpeg &_parent;
	enum { IDD = IDD_TOOL_OPT_JPEG };

	CToolJpegOptions(CToolJpeg &parent) : _parent(parent)
	{
	}

	BEGIN_MSG_MAP(ThisClass)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	void Apply();
};

/////////////////////////////////////////////////////////////////

class CToolJpeg : public CToolDlg<CToolJpeg, JpegToolSettings>
{
public:
	typedef CToolJpeg ThisClass;
	typedef CToolDlg<ThisClass, JpegToolSettings> BaseClass;

protected:

	CToolJpegOptions _options;

	void SetTransformOptions();
	JXFORM_CODE m_transform{JXFORM_NONE};
 
public:
	
	CToolJpeg(State &state);
	~CToolJpeg();

	// CToolDlg host contract
	HWND OnCreateOptions(HWND hWndParent);
	bool OnApplyOptions();
	void OnProcess(IW::IStatus *pStatus);
	void OnComplete();

	CString GetKey() const;
	CString GetTitle() const;
	CString GetCompletedText() const;

	// Item Iteration
	bool StartFolder(IW::Folder *pFolder, IW::IStatus *pStatus);
	bool StartItem(IW::FolderItem *pItem, IW::IStatus *pStatus);
	bool EndItem();
	bool EndFolder();

	// This tool used to show the overwrite choice and then ignore it, writing over
	// the originals however the destination was set. It writes new files now.
	bool AllowOverwrite() const { return false; }
	CString DefaultOutputSubFolder() const { return _T("Transformed"); }

	JpegToolSettings &Settings() { return App.Settings.Tools.Jpeg; }

	void OnHelp() const
	{
		App.InvokeHelp(m_hWnd, HELP_TOOL_JPEG);
	}
};

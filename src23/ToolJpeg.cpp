// ImageWalker by Zac Walker
//
// Purpose: Lossless JPEG tool implementation.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

// Jpeg.cpp: implementation of the CToolJpeg class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "ToolJpeg.h"


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CToolJpeg::CToolJpeg(State& state) : BaseClass(state), _options(*this)
{
	// Jpeg transformation options
	m_bMirrorLR = false;
	m_bMirrorTB = false;
	m_bRotate90 = false;
	m_bRotate180 = false;
	m_bRotate270 = false;

	// Advanced
	m_bGreyScale = false;
	m_bDropEdge = false;
	m_bOptimize = false;
	m_bProgressive = false;
}

CToolJpeg::~CToolJpeg()
{
}


CString CToolJpeg::GetKey() const
{
	return _T("JpegTool");
}

CString CToolJpeg::GetTitle() const
{
	return App.LoadString(IDS_TOOL_JPEG_TITLE);
}

CString CToolJpeg::GetCompletedText() const
{
	return App.LoadString(IDS_TOOL_JPEG_COMPLETED);
}

HWND CToolJpeg::OnCreateOptions(HWND hWndParent)
{
	return _options.Create(hWndParent);
}

bool CToolJpeg::OnApplyOptions()
{
	_options.Apply();
	return true;
}

void CToolJpeg::OnProcess(IStatus* pStatus)
{
	SetTransformOptions();
	IterateItems(this);
}

void CToolJpeg::OnComplete()
{
}


bool CToolJpeg::StartFolder(IW::Folder* pFolder, IStatus* pStatus)
{
	if (_bRecurse)
	{
		ScopeLockFolderStack folderStack(m_arrayFolderNames, pFolder);
		IW::CFilePath path = OutputFolder();

		if (!path.CreateAllDirectories())
		{
			// OK here carries on to the next folder, so the report is the only
			// record that this one was skipped entirely.
			pStatus->SetError(App.LoadString(IDS_FAILEDTO_CREATE_FOLDER));

			IW::CMessageBoxIndirect mb;
			if (IDCANCEL == mb.ShowOsErrorWithFile(path, IDS_FAILEDTO_CREATE_FOLDER, GetLastError(),
			                                       MB_ICONHAND | MB_OKCANCEL | MB_HELP))
			{
				return false;
			}
		}
		else
		{
			pFolder->IterateItems(this, pStatus);
		}
	}

	return true;
}

void CToolJpeg::SetTransformOptions()
{
	m_transform = JXFORM_NONE;

	if (m_bMirrorLR)
	{
		m_transform = JXFORM_FLIP_H;
	}
	else if (m_bMirrorTB)
	{
		m_transform = JXFORM_FLIP_V;
	}
	else if (m_bRotate90)
	{
		m_transform = JXFORM_ROT_90;
	}
	else if (m_bRotate180)
	{
		m_transform = JXFORM_ROT_180;
	}
	else if (m_bRotate270)
	{
		m_transform = JXFORM_ROT_270;
	}
}


bool CToolJpeg::StartItem(IW::FolderItem* pItem, IStatus* pStatus)
{
	IW::CFilePath path = OutputFolder();
	path += pItem->GetFileName();

	if (RefuseToOverwriteSource(path, pItem, pStatus))
		return true;

	if (!MakeUniqueOutputPath(path))
	{
		CString str;
		str.Format(IDS_FAILEDTO_CREATE_FILE, static_cast<LPCTSTR>(path));
		pStatus->SetError(str);
		return true;
	}

	try
	{
		IW::FolderStreamIn streamIn(pItem);
		IW::CFileTemp streamOut;

		if (!streamOut.OpenForWrite(path))
		{
			CString str;
			str.Format(IDS_FAILEDTO_CREATE_FILE, static_cast<LPCTSTR>(path));
			pStatus->SetError(str);
			return true;
		}

		pStatus->SetStatusMessage(App.LoadString(IDS_JPEGTRANSFORM));
		IW::JpegTransformRequest request;
		request.transform = m_transform;
		request.trim = m_bDropEdge;
		request.forceGrayscale = m_bGreyScale;
		request.optimizeCoding = m_bOptimize;
		request.progressive = m_bProgressive;
		const auto result = IW::TransformJpegStream(streamIn, streamOut, request, pStatus);
		if (result != JpegTransformResult::Done || pStatus->QueryCancel())
		{
			streamOut.Abort();
			if (result == JpegTransformResult::NotJpeg)
				pStatus->SetError(App.LoadString(IDS_FAILEDTOLOAD));
			return result != JpegTransformResult::Failed;
		}

		return streamIn.Close(pStatus) && streamOut.Close(pStatus);
	}
	catch (const std::exception& error)
	{
		pStatus->SetError(CString(error.what()));
		return false;
	}
}

bool CToolJpeg::EndItem()
{
	return true;
}

bool CToolJpeg::EndFolder()
{
	return true;
}


//////////////////////////////////////////////////////////////////////
// CToolJpegOptions
//////////////////////////////////////////////////////////////////////

LRESULT CToolJpegOptions::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	CheckDlgButton(IDC_MIRROR_LR, _parent.m_bMirrorLR ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_MIRROR_TB, _parent.m_bMirrorTB ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_ROTATE_90, _parent.m_bRotate90 ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_ROTATE_180, _parent.m_bRotate180 ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_ROTATE_270, _parent.m_bRotate270 ? BST_CHECKED : BST_UNCHECKED);

	if (!(_parent.m_bMirrorLR ||
		_parent.m_bMirrorTB ||
		_parent.m_bRotate90 ||
		_parent.m_bRotate180 ||
		_parent.m_bRotate270))
	{
		CheckDlgButton(IDC_NONE, BST_CHECKED);
	}

	CheckDlgButton(IDC_DROP_EDGE, _parent.m_bDropEdge ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_OPTIMIZE, _parent.m_bOptimize ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_PROGRESSIVE, _parent.m_bProgressive ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_GREYSCALE, _parent.m_bGreyScale ? BST_CHECKED : BST_UNCHECKED);

	return 0;
}

void CToolJpegOptions::Apply()
{
	_parent.m_bMirrorLR = BST_CHECKED == IsDlgButtonChecked(IDC_MIRROR_LR);
	_parent.m_bMirrorTB = BST_CHECKED == IsDlgButtonChecked(IDC_MIRROR_TB);
	_parent.m_bRotate90 = BST_CHECKED == IsDlgButtonChecked(IDC_ROTATE_90);
	_parent.m_bRotate180 = BST_CHECKED == IsDlgButtonChecked(IDC_ROTATE_180);
	_parent.m_bRotate270 = BST_CHECKED == IsDlgButtonChecked(IDC_ROTATE_270);

	_parent.m_bDropEdge = BST_CHECKED == IsDlgButtonChecked(IDC_DROP_EDGE);
	_parent.m_bOptimize = BST_CHECKED == IsDlgButtonChecked(IDC_OPTIMIZE);
	_parent.m_bProgressive = BST_CHECKED == IsDlgButtonChecked(IDC_PROGRESSIVE);
	_parent.m_bGreyScale = BST_CHECKED == IsDlgButtonChecked(IDC_GREYSCALE);
}


////////////////////////////////////////////////////////////////

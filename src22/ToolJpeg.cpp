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
//
// Here's the routine that will replace the standard error_exit method:


METHODDEF(void) ima_jpeg_error_exit(j_common_ptr cinfo)
{
	char sz[JMSG_LENGTH_MAX];
	(*cinfo->err->format_message)(cinfo, sz);
	throw IW::invalid_file(sz);
}

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
	// Initialize the JPEG decompression object with default error handling.
	m_srcinfo.err = jpeg_std_error(&m_jsrcerr);
	m_jsrcerr.error_exit = ima_jpeg_error_exit;
	jpeg_create_decompress(&m_srcinfo);

	// Initialize the JPEG compression object with default error handling.
	m_dstinfo.err = jpeg_std_error(&m_jdsterr);
	m_jdsterr.error_exit = ima_jpeg_error_exit;
	jpeg_create_compress(&m_dstinfo);

	// Set Options
	SetTransformOptions();
	ApplyCompressorSwitches(&m_dstinfo);
	m_jsrcerr.trace_level = m_jdsterr.trace_level;
	m_srcinfo.mem->max_memory_to_use = m_dstinfo.mem->max_memory_to_use;

	IterateItems(this);

	jpeg_destroy_compress(&m_dstinfo);
	jpeg_destroy_decompress(&m_srcinfo);
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
	m_copyoption = JCOPYOPT_ALL;

	m_transformoption.transform = JXFORM_NONE;
	m_transformoption.trim = m_bDropEdge;
	m_transformoption.force_grayscale = m_bGreyScale;
	m_transformoption.crop = FALSE;
	m_transformoption.crop_width_set = JCROP_UNSET;
	m_transformoption.crop_height_set = JCROP_UNSET;
	m_transformoption.crop_xoffset_set = JCROP_UNSET;
	m_transformoption.crop_yoffset_set = JCROP_UNSET;

	if (m_bMirrorLR)
	{
		m_transformoption.transform = JXFORM_FLIP_H;
	}
	else if (m_bMirrorTB)
	{
		m_transformoption.transform = JXFORM_FLIP_V;
	}
	else if (m_bRotate90)
	{
		m_transformoption.transform = JXFORM_ROT_90;
	}
	else if (m_bRotate180)
	{
		m_transformoption.transform = JXFORM_ROT_180;
	}
	else if (m_bRotate270)
	{
		m_transformoption.transform = JXFORM_ROT_270;
	}
}

// jpeg_copy_critical_parameters takes these from the source, so they have to be
// put back afterwards. Doing it by re-running the whole switch parse also reset
// the transform, which by then had the file's own orientation folded into it.
void CToolJpeg::ApplyCompressorSwitches(j_compress_ptr cinfo)
{
	cinfo->err->trace_level = 0;
	cinfo->optimize_coding = m_bOptimize ? TRUE : FALSE;
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

	bool bRet = false;

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

		// Specify data source for decompression
		jpeg_iw_src(&m_srcinfo, &streamIn, nullptr);

		// Enable saving of extra markers that we want to copy
		jcopy_markers_setup(&m_srcinfo, m_copyoption);

		// Read file header
		if (JPEG_HEADER_OK != jpeg_read_header(&m_srcinfo, TRUE))
		{
			pStatus->SetError(App.LoadString(IDS_FAILEDTOLOAD));
			jpeg_abort((j_common_ptr)&m_srcinfo);
			return true;
		}

		// One turn, however the camera stored the picture: the orientation tag is
		// folded into the transform and then cleared.
		SetTransformOptions();
		jtransform_normalise_exif_orientation(&m_srcinfo, &m_transformoption);

		// Any space needed by a transform option must be requested before
		// jpeg_read_coefficients so that memory allocation will be done right.
		jtransform_request_workspace(&m_srcinfo, &m_transformoption);

		// Read source file as DCT coefficients
		src_coef_arrays = jpeg_read_coefficients(&m_srcinfo);

		// Initialize destination compression parameters from source values
		jpeg_copy_critical_parameters(&m_srcinfo, &m_dstinfo);

		// Adjust destination parameters if required by transform options;
		// also find out which set of coefficient arrays will hold the output.
		dst_coef_arrays = jtransform_adjust_parameters(&m_srcinfo, &m_dstinfo,
		                                               src_coef_arrays,
		                                               &m_transformoption);

		ApplyCompressorSwitches(&m_dstinfo);

		// Select simple progressive mode.
		if (m_bProgressive)
		{
			jpeg_simple_progression(&m_dstinfo);
		}

		// Specify data destination for compression
		jpeg_iw_dest(&m_dstinfo, &streamOut);

		// Start compressor (note no image data is actually written here)
		jpeg_write_coefficients(&m_dstinfo, dst_coef_arrays);

		// Copy to the output file any extra markers that we want to preserve
		jcopy_markers_execute(&m_srcinfo, &m_dstinfo, m_copyoption);

		pStatus->SetStatusMessage(App.LoadString(IDS_JPEGTRANSFORM));

		// Execute image transformation, if any 
		jtransform_execute_transform(
			&m_srcinfo, &m_dstinfo,
			src_coef_arrays,
			&m_transformoption,
			pStatus);

		streamIn.Close(pStatus);

		// A cancel leaves the coefficient arrays half filled, so the output must be
		// thrown away rather than kept.
		if (pStatus->QueryCancel())
		{
			streamOut.Abort();
			jpeg_abort((j_common_ptr)&m_dstinfo);
			jpeg_abort((j_common_ptr)&m_srcinfo);
			return false;
		}

		jpeg_finish_compress(&m_dstinfo);
		jpeg_finish_decompress(&m_srcinfo);

		bRet = streamOut.Close(pStatus);
	}
	catch (std::exception& e)
	{
		CString strError(e.what());
		pStatus->SetError(strError);
		ATLTRACE(_T("Exception in CToolJpeg::StartItem %s"), static_cast<LPCTSTR>(strError));

		// Finish compression and release memory 
		jpeg_abort((j_common_ptr)&m_dstinfo);
		jpeg_abort((j_common_ptr)&m_srcinfo);
	}

	return bRet;
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

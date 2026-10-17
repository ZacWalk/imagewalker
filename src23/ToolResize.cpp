// ImageWalker by Zac Walker
//
// Purpose: Resize tool implementation.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"
#include "ToolResize.h"

CToolResize::CToolResize(State& state) : BaseClass(state),
                                         _options(&_filter),
                                         _loader(state.Loaders)
{
}

CToolResize::~CToolResize()
{
}

HWND CToolResize::OnCreateOptions(HWND hWndParent)
{
	return _options.Create(hWndParent);
}

bool CToolResize::OnApplyOptions()
{
	return true;
}

void CToolResize::LoadSettings()
{
	BaseClass::LoadSettings();

	_filter.m_nWidth = Settings().Width;
	_filter.m_nHeight = Settings().Height;
	_filter.m_bKeepAspect = Settings().KeepAspect;
	_filter.m_bScaleDown = Settings().ScaleDown;
	_filter.m_nFilter = Settings().Filter;
	_filter.m_nType = Settings().Type;
	_filter.m_dwXPelsPerMeter = Settings().XPelsPerMeter;
	_filter.m_dwYPelsPerMeter = Settings().YPelsPerMeter;
}

void CToolResize::SaveSettings()
{
	Settings().Width = _filter.m_nWidth;
	Settings().Height = _filter.m_nHeight;
	Settings().KeepAspect = _filter.m_bKeepAspect;
	Settings().ScaleDown = _filter.m_bScaleDown;
	Settings().Filter = _filter.m_nFilter;
	Settings().Type = _filter.m_nType;
	Settings().XPelsPerMeter = _filter.m_dwXPelsPerMeter;
	Settings().YPelsPerMeter = _filter.m_dwYPelsPerMeter;

	BaseClass::SaveSettings();
}

CString CToolResize::GetKey() const
{
	return _T("ResizeTool");
}

CString CToolResize::GetTitle() const
{
	return App.LoadString(IDS_TOOL_RESIZE_TITLE);
}

CString CToolResize::GetCompletedText() const
{
	return App.LoadString(IDS_TOOL_RESIZE_COMPLETED);
}

void CToolResize::OnProcess(IStatus* pStatus)
{
	IterateItems(this);
}

void CToolResize::OnComplete()
{
}

bool CToolResize::StartFolder(IW::Folder* pFolder, IStatus* pStatus)
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

bool CToolResize::StartItem(IW::FolderItem* pItem, IStatus* pStatus)
{
	IW::Image imageIn = pItem->OpenAsImage(_loader, pStatus);

	if (imageIn.IsEmpty())
	{
		// Set error
		pStatus->SetError(App.LoadString(IDS_FAILEDTOLOAD));
	}
	else
	{
		IW::Image imageOut;

		const CString strKey = imageIn.GetLoaderName();
		IW::RefPtr<IW::IImageLoader> pLoader = _loader.GetLoader(strKey);

		if ((IW::ImageLoaderFlags::SAVE & _loader.GetFlags(strKey)) == 0)
		{
			pStatus->SetError(App.LoadString(IDS_FAILEDTO_SAVE_IMAGE_TYPE));
			return false;
		}

		// If this image is animated it is best to render it
		if (imageIn.NeedRenderForDisplay())
		{
			imageIn.Render(imageOut);
			imageIn = imageOut;
			imageOut.Free();
		}

		if (_filter.ApplyFilter(imageIn, imageOut, pStatus))
		{
			CString strFileName = pItem->GetFileName();
			IW::CFilePath path = OutputFolder();
			path += strFileName;

			if (RefuseToOverwriteSource(path, pItem, pStatus))
				return true;

			if (!MakeUniqueOutputPath(path))
			{
				CString str;
				str.Format(IDS_FAILEDTO_CREATE_FILE, static_cast<LPCTSTR>(path));
				pStatus->SetError(str);
				return true;
			}

			IW::CFileTemp f;
			if (!f.OpenForWrite(path))
			{
				CString str;
				str.Format(IDS_FAILEDTO_CREATE_FILE, static_cast<LPCTSTR>(path));
				pStatus->SetError(str);
				return false;
			}

			LPCTSTR szType = IW::Path::FindExtension(strFileName);
			bool bSaved = pLoader->Write(szType, &f, imageOut, App.Settings.Codec, pStatus) && f.Close(pStatus);

			if (!bSaved)
			{
				CString str;
				str.Format(IDS_FAILEDTO_CREATE_FILE, static_cast<LPCTSTR>(path));
				pStatus->SetError(str);
				return false;
			}
		}
	}

	return true;
}

bool CToolResize::EndItem()
{
	return true;
}

bool CToolResize::EndFolder()
{
	return true;
}

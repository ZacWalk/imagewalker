// ImageWalker by Zac Walker
//
// Purpose: Convert tool implementation.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"
#include "ViewModelState.h"
#include "ToolConvert.h"


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////


CToolConvert::CToolConvert(State& state) :
	BaseClass(state),
	_options(state.Loaders),
	_loader(state.Loaders)
{
	m_pLoaderFactory = nullptr;
	m_pLoader = nullptr;
}

CToolConvert::~CToolConvert()
{
}

HWND CToolConvert::OnCreateOptions(HWND hWndParent)
{
	return _options.Create(hWndParent);
}

bool CToolConvert::OnApplyOptions()
{
	_options.OnApplyLoader();

	m_pLoader = _options.m_pLoader;
	m_pLoaderFactory = _options.m_pLoaderFactory;

	return m_pLoader != nullptr && m_pLoaderFactory != nullptr;
}

void CToolConvert::LoadSettings()
{
	BaseClass::LoadSettings();

	_options._strDefaultSelection = Settings().LoaderKey;
	_options.Codec = Settings().Codec;
}

void CToolConvert::SaveSettings()
{
	Settings().Codec = _options.Codec;

	if (m_pLoaderFactory != nullptr)
		Settings().LoaderKey = m_pLoaderFactory->GetKey();

	BaseClass::SaveSettings();
}

CString CToolConvert::GetKey() const
{
	return _T("ConvertTool");
}

CString CToolConvert::GetTitle() const
{
	return App.LoadString(IDS_TOOL_CONV_TITLE);
}

CString CToolConvert::GetCompletedText() const
{
	return App.LoadString(IDS_TOOL_CONV_COMPLETED);
}

void CToolConvert::OnProcess(IStatus* pStatus)
{
	IterateItems(this);
}

void CToolConvert::OnComplete()
{
}


bool CToolConvert::StartFolder(IW::Folder* pFolder, IStatus* pStatus)
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

bool CToolConvert::StartItem(IW::FolderItem* pItem, IStatus* pStatus)
{
	IW::Image imageIn = pItem->OpenAsImage(_loader, pStatus);

	if (imageIn.IsEmpty())
	{
		pStatus->SetError(App.LoadString(IDS_FAILEDTOLOAD));
		return true;
	}

	IW::CFilePath path = OutputFolder();
	path += pItem->GetFileName();
	path.SetExtension(m_pLoaderFactory->GetExtensionDefault());

	if (RefuseToOverwriteSource(path, pItem, pStatus))
		return true;

	if (!MakeUniqueOutputPath(path))
	{
		pStatus->SetError(App.LoadString(IDS_FAILEDTOWRITEFILE));
		return true;
	}

	// A CFileTemp, so a decode that fails half way through cannot leave a
	// truncated file where a good one used to be.
	IW::CFileTemp f;

	if (!f.OpenForWrite(path))
	{
		pStatus->SetError(App.LoadString(IDS_FAILEDTOWRITEFILE));
		return true;
	}

	if (!m_pLoader->Write(g_szEmptyString, &f, imageIn, _options.Codec, pStatus) || !f.Close(pStatus))
	{
		pStatus->SetError(App.LoadString(IDS_FAILEDTOWRITEFILE));
		return true;
	}

	return true;
}

bool CToolConvert::EndItem()
{
	return true;
}

bool CToolConvert::EndFolder()
{
	return true;
}


////////////////////////////////////////////////////////////////

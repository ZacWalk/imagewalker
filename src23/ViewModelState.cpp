// ImageWalker by Zac Walker
//
// Purpose: State implementation, including the unsaved-changes question
//          asked before an image is replaced.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"
#include "ViewModelState.h"
#include "ViewYesNoDlg.h"

bool ImageState::CanDisplayNewImage(const CString& strNewImageName)
{
	IW::Focus preserveFocus;

	// The synchronous walks (LoadNextImage, Reload) land here without passing
	// through the frame's ShowImage, so the view is asked here as well.
	if (strNewImageName.CompareNoCase(GetImageFileName()) != 0 && !_pCoupling->CanChangeImage())
		return false;

	if (IsDirty())
	{
		return QuerySave();
	}

	return true;
}

CString ImageState::GetChangesList() const
{
	CString str;
	for (int i = 0; i < _arUndo.GetSize(); i++)
	{
		IW::AddToList(str, _arUndo[i]->GetAction());
	}
	return str;
}

bool ImageState::QuerySave()
{
	if (IsDirty())
	{
		CString str = IW::Format(IDS_SAVE_CHANGES, IW::Path::FindFileName(GetImageFileName()), static_cast<LPCTSTR>(GetChangesList()));
		CYesNoDlg dlg(GetImage(), str);
		int nRet = static_cast<int>(dlg.DoModal());
		if (nRet == IDCANCEL)
		{
			return false;
		}
		if (nRet == IDYES)
		{
			return Save();
		}
	}

	return true;
}

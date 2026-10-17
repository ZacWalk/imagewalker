#include "stdafx.h"
#include "FileFormatAny.h"

bool CLoadAny::LoadImage(const CString& strFileName, IW::IImageStream* pImageOut, IW::IStatus* pStatus)
{
	bool bRet = false;
	const CString strExt = IW::Path::FindExtension(strFileName);

	IW::CFile file;
	if (file.OpenForRead(strFileName))
	{
		bRet = Read(strExt, &file, pImageOut, pStatus);
		pImageOut->Flush();
	}

	return bRet;
}

bool CLoadAny::SaveImage(const CString& strFileName, IW::Image& image, const IW::CodecSettings& settings,
                         IW::IStatus* pStatus)
{
	IW::CFileTemp f;
	if (!f.OpenForWrite(strFileName))
	{
		return false;
	}

	if (!Write(IW::Path::FindExtension(strFileName), &f, image, settings, pStatus))
	{
		return false;
	}

	return f.Close(pStatus);
}

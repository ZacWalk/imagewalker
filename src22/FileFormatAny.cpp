// ImageWalker by Zac Walker
//
// Purpose: CLoadAny implementation - format selection, and the retry that
//          lets a mis-named file still open.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"

#include "FileJpeg.h"
#include "FileBmp.h"
#include "FileGif.h"
#include "FilePcx.h"
#include "FilePng.h"
#include "FileTiff.h"
#include "FilePsd.h"
#include "FileWpg.h"
#include "FileMetaFile.h"
#include "FileCompoundDoc.h"
#include "FileFormatAny.h"

//////////////////////////////////////////////////////////////
// The loader table

template <class T>
static IW::IImageLoader* CreateLoader()
{
	return new IW::RefObj<T>;
}

// Each loader still describes itself through its own static members, so adding
// one is a single row here.
template <class T>
static IW::ImageLoaderInfo LoaderInfo()
{
	IW::ImageLoaderInfo info;
	info._strKey = T::_GetKey();
	info._strTitle = T::_GetTitle();
	info._strDescription = T::_GetDescription();
	info._strExtensionList = T::_GetExtensionList();
	info._strExtensionDefault = T::_GetExtensionDefault();
	info._dwFlags = T::_GetFlags();
	info._pfnCreate = &CreateLoader<T>;
	return info;
}

ImageLoaders::ImageLoaders()
{
	// Reserved so the rows never move: every lookup hands out a pointer into
	// this vector and those have to stay valid.
	_loaders.reserve(16);

	Add(LoaderInfo<CLoadJpg>());
	AddHeaderWord(0xD8FF, CLoadJpg::_GetKey());

	Add(LoaderInfo<CLoadBmp>());
	AddHeaderWord(0x4D42, CLoadBmp::_GetKey());

#ifdef COMPILE_GIF
	Add(LoaderInfo<CLoadGif>());
	AddHeaderWord(0x4947, CLoadGif::_GetKey());
#endif

#ifdef COMPILE_PNG
	Add(LoaderInfo<CLoadPng>());
	AddHeaderWord(0x5089, CLoadPng::_GetKey());
#endif

#ifdef COMPILE_TIFF
	Add(LoaderInfo<CLoadTiff>());
	AddHeaderWord(0x4949, CLoadTiff::_GetKey());
	AddHeaderWord(0x4d4d, CLoadTiff::_GetKey());
#endif

#ifdef COMPILE_PSD
	Add(LoaderInfo<CLoadPsd>());
#endif

	Add(LoaderInfo<CLoadWpg>());

	Add(LoaderInfo<CLoadPcx>());
	AddHeaderWord(0x050A, CLoadPcx::_GetKey());

	Add(LoaderInfo<CLoadMetaFile>());
	Add(LoaderInfo<CLoadCompoundDoc>());

	assert(_loaders.size() <= 16);

	// Built last: Add() grows _loaders and would invalidate these.
	_sorted.reserve(_byKey.size());

	for (auto it = _byKey.begin(); it != _byKey.end(); ++it)
		_sorted.push_back(&_loaders[it->second]);
}

void ImageLoaders::Add(const IW::ImageLoaderInfo& info)
{
	CString strKey = info.GetKey();
	strKey.MakeLower();

	assert(!_byKey.contains(strKey));

	const size_t n = _loaders.size();
	_loaders.push_back(info);
	_byKey[strKey] = n;

	CString str(info.GetExtensionList());
	int curPos = 0;

	CString token = str.Tokenize(_T(","), curPos);

	while (!token.IsEmpty())
	{
		token.MakeLower();
		_byExtension[token] = n;
		token = str.Tokenize(_T(","), curPos);
	}
}

void ImageLoaders::AddHeaderWord(WORD w, const CString& strKey)
{
	CString str(strKey);
	str.MakeLower();

	auto it = _byKey.find(str);

	if (it != _byKey.end())
	{
		_byHeaderWord[w] = it->second;
	}
}

IW::ImageLoaderInfoPtr ImageLoaders::Find(const CString &strKeyIn) const
{
	CString strKey(strKeyIn);
	strKey.MakeLower();

	auto itKey = _byKey.find(strKey);

	if (itKey != _byKey.end())
		return &_loaders[itKey->second];

	// If it has a dot try without
	if (!strKey.IsEmpty() && strKey[0] == '.')
		strKey.Delete(0);

	auto itExt = _byExtension.find(strKey);

	if (itExt != _byExtension.end())
		return &_loaders[itExt->second];

	return nullptr;
}

IW::ImageLoaderInfoPtr ImageLoaders::Find(WORD w) const
{
	auto it = _byHeaderWord.find(w);

	if (it != _byHeaderWord.end())
		return &_loaders[it->second];

	return nullptr;
}

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

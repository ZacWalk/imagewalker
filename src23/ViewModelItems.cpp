// ImageWalker by Zac Walker
//
// Purpose: Folder and item implementation - enumeration, sorting, selection,
//          and the thumbnail load handshake with the worker.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

//
// Thumb.cpp: implementation of the IW::CFile class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "ViewModelItems.h"
#include "ImagingStreams.h"
#include "FileFormatAny.h"
#include "ViewProgressDlg.h"
#include "Metadata.h"


/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////


void IW::FolderItem::selectItem(FolderItem* pThumb)
{
	pThumb->_uFlags |= THUMB_SELECTED;
}

void IW::FolderItem::selectInverse(FolderItem* pThumb)
{
	pThumb->_uFlags ^= THUMB_SELECTED;
}

void IW::FolderItem::selectIfImage(FolderItem* pThumb)
{
	if (pThumb->IsImage())
	{
		pThumb->_uFlags |= THUMB_SELECTED;
	}
	else
	{
		pThumb->_uFlags &= ~THUMB_SELECTED;
	}
}

void IW::FolderItem::clearLoadedFlag(FolderItem* pThumb)
{
	pThumb->_uFlags &= ~THUMB_LOADED;
}

bool IW::FolderItem::isSelected(const FolderItem* pThumb)
{
	return (pThumb->_uFlags & THUMB_SELECTED) != 0;
}

bool IW::FolderItem::isNotSelected(const FolderItem* pThumb)
{
	return (pThumb->_uFlags & THUMB_SELECTED) == 0;
}

bool IW::FolderItem::isDeleted(const FolderItem* pThumb)
{
	return (pThumb->_uFlags & THUMB_DELETE) != 0;
}

bool IW::FolderItem::isNotDeleted(const FolderItem* pThumb)
{
	return (pThumb->_uFlags & THUMB_DELETE) == 0;
}

bool IW::FolderItem::isLoaded(const FolderItem* pThumb)
{
	return (pThumb->_uFlags & THUMB_LOADED) != 0;
}

bool IW::FolderItem::isImage(const FolderItem* pThumb)
{
	return (pThumb->_uFlags & THUMB_IMAGE) != 0 &&
		(pThumb->_ulAttribs & SFGAO_FOLDER) == 0;
}

bool IW::FolderItem::isImageIcon(const FolderItem* pThumb)
{
	return (pThumb->_uFlags & THUMB_IMAGE_ICON) != 0;
}

bool IW::FolderItem::isFolder(const FolderItem* pThumb)
{
	return (pThumb->_ulAttribs & SFGAO_FOLDER) != 0;
}

bool IW::FolderItem::isLink(const FolderItem* pThumb)
{
	return (pThumb->_ulAttribs & SFGAO_LINK) != 0;
}

bool IW::FolderItem::isFileSystem(const FolderItem* pThumb)
{
	return (pThumb->_ulAttribs & SFGAO_FILESYSTEM) != 0;
}

int IW::FolderItem::CompareName(CShellFolder& pShellFolder, const FolderItem* a, const FolderItem* b)
{
	HRESULT hr = pShellFolder->CompareIDs(0, a->_item, b->_item);
	ATLASSERT(SUCCEEDED(hr));
	return static_cast<short>(SCODE_CODE(hr));
}

int IW::FolderItem::CompareType(CShellFolder& pShellFolder, const FolderItem* a, const FolderItem* b)
{
	int i = a->_uExtension - b->_uExtension;
	if ((i == 0) || (a->_ulAttribs & SFGAO_FOLDER) || (b->_ulAttribs & SFGAO_FOLDER)) i = CompareName(
		pShellFolder, a, b);
	return i;
}

int IW::FolderItem::CompareSize(CShellFolder& pShellFolder, const FolderItem* a, const FolderItem* b)
{
	int i = a->_sizeFile - b->_sizeFile;
	if ((i == 0) || (a->_ulAttribs & SFGAO_FOLDER) || (b->_ulAttribs & SFGAO_FOLDER)) i = CompareName(
		pShellFolder, a, b);
	return i;
}

int IW::FolderItem::CompareCreationTime(CShellFolder& pShellFolder, const FolderItem* a, const FolderItem* b)
{
	int i = a->_ftCreationTime.Compare(b->_ftCreationTime);
	if ((i == 0) || (a->_ulAttribs & SFGAO_FOLDER) || (b->_ulAttribs & SFGAO_FOLDER)) i = CompareName(
		pShellFolder, a, b);
	return i;
}

int IW::FolderItem::CompareDateTaken(CShellFolder& pShellFolder, const FolderItem* a, const FolderItem* b)
{
	int i = a->GetTakenTime().Compare(b->GetTakenTime());
	if ((i == 0) || (a->_ulAttribs & SFGAO_FOLDER) || (b->_ulAttribs & SFGAO_FOLDER)) i = CompareName(
		pShellFolder, a, b);
	return i;
}

int IW::FolderItem::CompareLastWriteTime(CShellFolder& pShellFolder, const FolderItem* a, const FolderItem* b)
{
	int i = a->_ftLastWriteTime.Compare(b->_ftLastWriteTime);
	if ((i == 0) || (a->_ulAttribs & SFGAO_FOLDER) || (b->_ulAttribs & SFGAO_FOLDER)) i = CompareName(
		pShellFolder, a, b);
	return i;
}

////////////////////////////////////////////////////////////
/// Thumbs

IW::FolderItem::FolderItem() : // Variable defaults.
	_dwFileAttributes(0),
	_ulAttribs(0),
	_uExtension(0),
	_nImage(-1),
	_nTimer(0),
	_nFrame(0),
	_uFlags(0),
	_nSubItemCount(0)
{
	static DWORD dwHashCodeFolderItem = 1;
	_dwHashCode = dwHashCodeFolderItem++;
}

bool IW::FolderItem::Init(const CString& strFilePath)
{
	CShellItem itemFull;
	itemFull.Open(strFilePath);

	if (itemFull.Depth() > 1)
	{
		CShellItem itemFolder(itemFull);
		itemFolder.StripToParent();

		CShellItem itemTail(itemFull);
		itemTail.StripToTail();

		CShellFolder pShellFolder;
		pShellFolder.Open(itemFolder, true);

		return Init(pShellFolder, itemTail.Detach());
	}
	return Init(CShellDesktop(), itemFull.Detach());
}

bool IW::FolderItem::Init(IShellFolder* pFolder, LPITEMIDLIST pItem, LPCITEMIDLIST pItemGap)
{
	_item.Attach(pItem);
	if (pItemGap) _itemGap = pItemGap;
	_pFolder = pFolder;

	ATLASSERT(_item.Depth() == 1);

	return SUCCEEDED(RefreshItem());
}

void IW::FolderItem::ModifyFlags(DWORD dwRemove, DWORD dwAdd)
{
	_uFlags = (_uFlags & ~dwRemove) | dwAdd;
}

void IW::FolderItem::RefreshNames()
{
	_strFileName = GetDisplayNameOf(SHGDN_FORPARSING | SHGDN_INFOLDER);
	_strFilePath = GetDisplayNameOf(SHGDN_FORPARSING);
	_strDisplayName = GetDisplayNameOf(SHGDN_NORMAL | SHGDN_INFOLDER);
	_strDisplayPath = GetDisplayNameOf(SHGDN_NORMAL);
}

HRESULT IW::FolderItem::RefreshItem()
{
	RefreshNames();

	DWORD dwRemovable = SFGAO_REMOVABLE;
	HRESULT hr = _pFolder->GetAttributesOf(1, _item, &dwRemovable);

	if (SUCCEEDED(hr) && dwRemovable & SFGAO_REMOVABLE)
	{
		_ulAttribs = SFGAO_REMOVABLE | SFGAO_FOLDER;
	}
	else
	{
		_ulAttribs = SFGAO_LINK | SFGAO_FOLDER | SFGAO_FILESYSTEM | SFGAO_GHOSTED;
		hr = _pFolder->GetAttributesOf(1, _item, &_ulAttribs);

		if (FAILED(hr))
		{
			_ulAttribs = 0;
		}
	}

	if (IsFileSystem())
	{
		WIN32_FIND_DATA findFileData;


		const HRESULT dataResult = SHGetDataFromIDList(_pFolder, _item, SHGDFIL_FINDDATA, &findFileData, sizeof(findFileData));

		if (SUCCEEDED(dataResult))
		{
			_ftLastWriteTime = findFileData.ftLastWriteTime;
			_ftCreationTime = findFileData.ftCreationTime;
			_sizeFile = FileSize(findFileData.nFileSizeHigh, findFileData.nFileSizeLow);
			_dwFileAttributes = findFileData.dwFileAttributes;
		}

		if (_ftCreationTime.IsEmpty() || _ftLastWriteTime.IsEmpty())
		{
			WIN32_FIND_DATA ffd;
			HANDLE sh = FindFirstFile(GetFilePath(), &ffd);

			if (INVALID_HANDLE_VALUE != sh)
			{
				_ftCreationTime = ffd.ftCreationTime;
				_ftLastWriteTime = ffd.ftLastWriteTime;
				FindClose(sh);
			}
		}
	}

	_uExtension = App.GetExtensionKey(GetFileName());

	return hr;
}

IW::FolderItem::FolderItem(const FolderItem& other) :
	_ftCreationTime(other._ftCreationTime),
	_ftLastWriteTime(other._ftLastWriteTime),
	_image(other._image),
	_item(other._item),
	_pFolder(other._pFolder),
	_dwFileAttributes(other._dwFileAttributes),
	_dwHashCode(other._dwHashCode),
	_sizeFile(other._sizeFile),
	_ulAttribs(other._ulAttribs),
	_uExtension(other._uExtension),
	_nImage(other._nImage),
	_nTimer(other._nTimer),
	_nFrame(other._nFrame),
	_uFlags(other._uFlags),
	_nSubItemCount(other._nSubItemCount),
	_strFileName(other._strFileName),
	_strFilePath(other._strFilePath),
	_strDisplayName(other._strDisplayName),
	_strDisplayPath(other._strDisplayPath)
{
}

IW::FolderItem::~FolderItem()
{
	_pFolder.Release();
}

void IW::FolderItem::operator =(const FolderItem& other)
{
	_dwHashCode = other._dwHashCode;
	_ftCreationTime = other._ftCreationTime;
	_ftLastWriteTime = other._ftLastWriteTime;
	_sizeFile = other._sizeFile;
	_dwFileAttributes = other._dwFileAttributes;
	_ulAttribs = other._ulAttribs;
	_image = other._image;
	_uExtension = other._uExtension;
	_uFlags = other._uFlags;
	_nImage = other._nImage;
	_nTimer = other._nTimer;
	_nFrame = other._nFrame;
	_pFolder = other._pFolder;
	_item = other._item;
	_nSubItemCount = other._nSubItemCount;
	_strFileName = other._strFileName;
	_strFilePath = other._strFilePath;
	_strDisplayName = other._strDisplayName;
	_strDisplayPath = other._strDisplayPath;
}

bool IW::FolderItem::IsFolder() const { return isFolder(this); };
bool IW::FolderItem::IsImage() const { return isImage(this); };
bool IW::FolderItem::IsLink() const { return isLink(this); };
bool IW::FolderItem::IsFileSystem() const { return isFileSystem(this); };
bool IW::FolderItem::IsImageIcon() const { return isImageIcon(this); };

IW::FolderPtr IW::FolderItem::GetFolder()
{
	CShellItem item = _item;

	if (!_itemGap.IsNull())
	{
		item.Cat(_itemGap, _item);
	}

	FolderPtr pFolder = new RefObj<Folder>;

	if (!IsZipFile())
	{
		if (IsLink())
		{
			CShellItem itemLink;

			if (SUCCEEDED(_pFolder.ResolveLink(item, itemLink)))
			{
				if (pFolder->Init(itemLink))
				{
					return pFolder;
				}
			}
		}
		else
		{
			CShellFolder pShellFolder;

			if (SUCCEEDED(_pFolder->BindToObject(item, NULL, IID_IShellFolder, (LPVOID*)pShellFolder.GetPtr())))
			{
				if (pFolder->Init(pShellFolder))
				{
					return pFolder;
				}
			}
		}
	}

	return nullptr;
}

CString IW::FolderItem::GetToolTip() const
{
	RefPtr<IQueryInfo> spInfo;
	HRESULT hr = E_FAIL;
	CString strToolTip;

	if (!isImage(this))
	{
		hr = _pFolder->GetUIObjectOf(GetMainWindow(), 1, _item, IID_IQueryInfo, nullptr, (LPVOID*)&spInfo);

		if (SUCCEEDED(hr))
		{
			WCHAR* wsz = nullptr;
			if (SUCCEEDED(spInfo->GetInfoTip(0, &wsz)))
			{
				strToolTip = wsz;

				CComPtr<IMalloc> spMalloc;
				SHGetMalloc(&spMalloc);

				if (spMalloc)
					spMalloc->Free(wsz);
			}
		}
	}
	else
	{
		CSimpleArray<CString> arrayStrOut;
		CArrayDWORD array;

		array.Add(ePropertyTitle);
		array.Add(ePropertyObjectName);
		array.Add(ePropertyType);
		array.Add(ePropertyDateTaken);
		array.Add(ePropertyModifiedDate);
		array.Add(ePropertySize);
		array.Add(ePropertyAperture);
		array.Add(ePropertyIsoSpeed);
		array.Add(ePropertyWhiteBalance);
		array.Add(ePropertyExposureTime);
		array.Add(ePropertyFocalLength);
		array.Add(ePropertyDescription);

		// Get the strings
		GetFormatText(arrayStrOut, array, true);

		for (int i = 0; i < arrayStrOut.GetSize(); i++)
		{
			CString& str = arrayStrOut[i];

			if (!str.IsEmpty())
			{
				if (!strToolTip.IsEmpty()) strToolTip += g_szCRLF;

				// Dont title the description
				if (ePropertyDescription != array[i])
				{
					strToolTip += App.GetMetaDataTitle(array[i]);
					strToolTip += _T(": ");
				}

				strToolTip += str;
			}
		}
	}

	return strToolTip;
}

IW::Image IW::FolderItem::OpenAsImage(CLoadAny& loader, IStatus* pStatus)
{
	Image image;
	ImageStream<IImageStream> imageOut(image);

	if (!loader.LoadImage(GetFilePath(), &imageOut, pStatus))
	{
		image.Free();
	}

	return image;
}

IW::FolderItemPtr IW::FolderItem::CreateTestItem()
{
	FolderItemPtr pItem = new FolderItem();

	pItem->_item.Open(GetMainWindow(), CSIDL_PERSONAL);
	pItem->_pFolder = CShellDesktop();
	pItem->_image.CreatePage(10, 10, PixelFormat::PF24);
	pItem->_uFlags |= THUMB_IMAGE;

	return pItem;
}


bool IW::FolderItem::SaveAsImage(CLoadAny& loader, Image& image, const CString& strFilterName, IStatus* pStatus) const
{
	RefPtr<IImageLoader> pLoader = loader.GetLoader(strFilterName);
	FolderStreamOut streamOut(this);

	// FolderStreamOut is a CFileTemp. Without the Close its destructor deletes the
	// temp, so this used to report success and leave the file untouched.
	return pLoader->Write(loader.GetExtensionDefault(strFilterName), &streamOut, image, App.Settings.Codec, pStatus) &&
		streamOut.Close(pStatus);
}

IW::TAGSET IW::FolderItem::GetTags() const
{
	if (IsImage())
	{
		return Split(_image.GetTags());
	}

	return TAGSET();
}

bool IW::FolderItem::LoadJobBegin(FolderItemLoader& loader)
{
	if (_uFlags & THUMB_LOADED)
	{
		return false;
	}

	loader._item = *this;
	loader._strFilePath = GetFilePath();
	_uFlags |= THUMB_LOADING;

	return true;
}

bool IW::FolderItem::IsHTMLDisplayable(ImageLoaders& loaders) const
{
	if (!IsFolder())
	{
		CString strKey;

		bool bHasImage = IsImage() && !_image.IsEmpty();
		if (bHasImage) strKey = _image.GetLoaderName();

		if (strKey.IsEmpty())
		{
			strKey = Path::FindExtension(GetFileName());
		}

		ImageLoaderInfoPtr pFactory = loaders.Find(strKey);
		return pFactory && (ImageLoaderFlags::HTML & pFactory->GetFlags());
	}

	return false;
}

bool IW::FolderItem::SetItemName(const CString& strNewName, DWORD uFlags)
{
	bool bRet = false;

	USES_CONVERSION;
	LPITEMIDLIST pidlOut;
	HRESULT hr = _pFolder->SetNameOf(GetMainWindow(), _item, T2COLE(strNewName), uFlags, &pidlOut);

	if (SUCCEEDED(hr))
	{
		_item.Attach(pidlOut);
	}

	if (SUCCEEDED(hr))
	{
		RefreshNames();
		_uExtension = App.GetExtensionKey(GetFileName());
		bRet = true;
	}

	return bRet;
}

bool IW::FolderItem::IsReadOnly() const
{
	return 0 != (_dwFileAttributes & FILE_ATTRIBUTE_READONLY);
}

bool IW::FolderItem::IsOffline() const
{
	// A hydrated cloud file keeps its reparse point but loses these, so this is
	// true only for the ones a read would have to download.
	return 0 != (_dwFileAttributes & (FILE_ATTRIBUTE_OFFLINE |
		FILE_ATTRIBUTE_RECALL_ON_OPEN |
		FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS));
}

bool IW::FolderItem::CanRename() const
{
	return 0 != _pFolder.GetAttributes(_item, SFGAO_CANRENAME);
}

bool IW::FolderItem::IsSelected() const
{
	return isSelected(this);
}


void IW::FolderItem::LoadJobEnd(FolderItemLoader& loader)
{
	if (loader._bDidStartLoad)
	{
		if (loader._item._dwHashCode == _dwHashCode)
		{
			loader.SyncThumb(*this);
			_uFlags |= THUMB_INVALIDATE;
		}
	}
}

void IW::Folder::LoadJobEnd(FolderItemLoader& loader, FolderItemPtr pItem)
{
	// The worker publishes _image and _uFlags here, and the UI reads them
	// under this same lock
	IW::CAutoLockCS lock(_cs);

	if (loader._bDidStartLoad)
	{
		if (loader._item._dwHashCode == pItem->_dwHashCode)
		{
			bool bAlreadyImage = pItem->IsImage();
			pItem->LoadJobEnd(loader);

			if (loader.IsImageLoaded())
			{
				if (!bAlreadyImage)
				{
					_nImageCount++;
				}
			}

			_nLoadedCount++;
			_timeLast = GetTickCount();
		}
	}

	// The claim belongs to the job, not to the result: a decode that failed, was
	// abandoned or landed on a re-enumerated item still has to release it, or
	// CanLoad skips this thumbnail for the life of the folder.
	pItem->_uFlags &= ~THUMB_LOADING;
}

void IW::FolderItem::GetAttributes(FolderItemAttributes& attributes, bool bGetImage) const
{
	DWORD dwImageFlags = _image.GetFlags();

	attributes.bIsImage = IsImage();
	attributes.bIsImageIcon = IsImageIcon();
	attributes.bIsFolder = IsFolder();
	attributes.nImage = _nImage;
	attributes.nFrame = _nFrame;
	attributes.nSubItemCount = _nSubItemCount;
	attributes.bHasIPTC = 0 != (dwImageFlags & ImageFlags::HasIPTC);
	attributes.bHasXmp = 0 != (dwImageFlags & ImageFlags::HasXmp);
	attributes.bHasExif = 0 != (dwImageFlags & ImageFlags::HasExif);
	attributes.bHasICC = 0 != (dwImageFlags & ImageFlags::HasICC);
	attributes.bHasError = 0 != (dwImageFlags & ImageFlags::HasError);
	attributes.nPageCount = _image.GetPageCount();
	attributes.bIsLink = (_ulAttribs & SFGAO_LINK) != 0;
	attributes.bIsHidden = (_ulAttribs & SFGAO_GHOSTED) != 0;

	if (bGetImage && !_image.IsEmpty())
	{
		attributes.bCanAnimate = _image.CanAnimate();
		const int nFrame = attributes.bCanAnimate
			                   ? IW::Clamp(_nFrame, 0, static_cast<int>(_image.GetPageCount()) - 1)
			                   : 0;

		attributes.page = _image.GetPage(nFrame);
		attributes.rectBounding = _image.GetBoundingRect();
	}
}


/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

inline bool IsAllowedType(UINT uExtension)
{
	IW::CAutoLockCS lock(App._cs);

	// Ignore some common known
	// non image extensions of image formats
	// that are to slow
	static std::set<UINT> set;

	if (set.size() == 0)
	{
		set.insert(App.GetExtensionKey(_T(".EXE")));
		set.insert(App.GetExtensionKey(_T(".SCR")));
		set.insert(App.GetExtensionKey(_T(".DLL")));
	}

	return !set.contains(uExtension);
};

inline bool IsFolderWalkImageType(UINT uExtension)
{
	IW::CAutoLockCS lock(App._cs);
	static std::set<UINT> set;

	if (set.size() == 0)
	{
		set.insert(App.GetExtensionKey(_T(".JPG")));
		set.insert(App.GetExtensionKey(_T(".TIF")));
		set.insert(App.GetExtensionKey(_T(".GIF")));
		set.insert(App.GetExtensionKey(_T(".BMP")));
		set.insert(App.GetExtensionKey(_T(".PNG")));
	}

	return set.contains(uExtension);
};


CString IW::FolderItem::GetStatistics() const
{
	if (IsFolder())
	{
		CString str;
		str.Format(_T("%d Items"), _nSubItemCount);
		return str;
	}

	return _image.GetStatistics();
}


CString IW::FolderItem::GetType() const
{
	CString str = GetStatistics();

	if (!str.IsEmpty())
		return str;

	// The shell type name depends only on the extension, and this is on the
	// details view's per-item paint path, so memoise it by extension. Folders,
	// drives and extensionless files have no shared key and skip the cache.
	CString strKey = IW::Path::FindExtension(_strFileName);
	const bool bCacheable = !IsFolder() && !strKey.IsEmpty();

	static CCriticalSection csTypeNames;
	static std::map<CString, CString> mapTypeNames;

	if (bCacheable)
	{
		strKey.MakeUpper();

		IW::CAutoLockCS lock(csTypeNames);
		auto it = mapTypeNames.find(strKey);

		if (it != mapTypeNames.end())
			return it->second;
	}

	// Outside csTypeNames, so one slow path does not block every other lookup.
	// Callers reaching here through GetItemFormatText still hold Folder::_cs.
	SHFILEINFO sfi;
	MemZero(&sfi, sizeof(sfi));

	const bool bGot = SHGetFileInfo(_strFilePath,
	              0,
	              &sfi,
	              sizeof(SHFILEINFO),
	              SHGFI_TYPENAME |
	              SHGFI_DISPLAYNAME) != 0 && sfi.szTypeName[0] != 0;

	if (!bGot)
		return CString();

	str = sfi.szTypeName;

	// Only a real answer is cached: the map lives for the process, so caching a
	// failure would blank the Type column for that extension for the session.
	if (bCacheable)
	{
		IW::CAutoLockCS lock(csTypeNames);
		mapTypeNames[strKey] = str;
	}

	return str;
}


bool IW::FolderItem::GetFormatText(CString& strOut, CArrayDWORD& array, bool bFormat) const
{
	strOut = g_szEmptyString;
	CSimpleArray<CString> arrayStr;

	if (!GetFormatText(arrayStr, array, bFormat))
	{
		return false;
	}

	for (int i = 0; i < arrayStr.GetSize(); ++i)
	{
		CString& str = arrayStr[i];

		if (!str.IsEmpty())
		{
			if (!strOut.IsEmpty())
			{
				strOut += g_szCRLF;
			}

			strOut += str;
		}
	}

	return true;
}


bool IW::FolderItem::GetFormatText(CSimpleArray<CString>& arrayStrOut, CArrayDWORD& array, bool bFormat) const
{
	CameraSettings cameraSettings = _image.GetCameraSettings();
	CString strPath;
	int nCount = array.GetSize();

	for (int i = 0; i < nCount; ++i)
	{
		int nItem = array[i];
		int nIdent = LOWORD(nItem);
		CString str;

		switch (nIdent)
		{
		case ePropertyTitle:
			str = _image.GetTitle();
			if (str.IsEmpty()) str = GetDisplayName();
			break;

		case ePropertyObjectName:
			str = _image.GetObjectName();
			break;

		case ePropertyName:
			str = GetDisplayPath();
			if (str.IsEmpty()) str = App.LoadString(IDS_UNKNOWNFILENAME);
			break;

		case ePropertyType:
			str = GetType();
			if (str.IsEmpty()) str = App.LoadString(IDS_UNKNOWNTYPE);
			break;

		case ePropertySize:
			str = GetFileSize().ToString();
			break;

		case ePropertyModifiedDate:
			str = GetLastWriteTime().ToLocalTime().GetDateFormat(App.GetLangId(), App.Settings.m_bShortDates);
			break;

		case ePropertyModifiedTime:
			str = GetLastWriteTime().ToLocalTime().GetTimeFormat(App.GetLangId());
			break;

		case ePropertyCreatedDate:
			str = GetCreatedTime().ToLocalTime().GetDateFormat(App.GetLangId(), App.Settings.m_bShortDates);
			break;

		case ePropertyCreatedTime:
			str = GetCreatedTime().ToLocalTime().GetTimeFormat(App.GetLangId());
			break;

		case ePropertyPath:
			str = GetFilePath();
			if (str.IsEmpty()) str = App.LoadString(IDS_UNKNOWNFILEPATH);
			break;

		case ePropertyWidth:
			str = IToStr(cameraSettings.OriginalImageSize.cx);
			break;

		case ePropertyHeight:
			str = IToStr(cameraSettings.OriginalImageSize.cy);
			break;

		case ePropertyDepth:
			str = cameraSettings.OriginalBpp.ToString();
			break;

		case ePropertyAperture:
			str = cameraSettings.FormatAperture();
			break;

		case ePropertyIsoSpeed:
			str = cameraSettings.FormatIsoSpeed();
			break;

		case ePropertyWhiteBalance:
			str = cameraSettings.FormatWhiteBalance();
			break;

		case ePropertyExposureTime:
			str = cameraSettings.FormatExposureTime();
			break;

		case ePropertyFocalLength:
			str = cameraSettings.FormatFocalLength();
			break;

		case ePropertyDescription:
			str = _image.GetDescription();
			break;

		case ePropertyDateTaken:
			{
				FileTime taken = GetTakenTime().ToLocalTime();
				str = taken.GetDateFormat(App.GetLangId(), App.Settings.m_bShortDates);
				str += _T(" ");
				str += taken.GetTimeFormat(App.GetLangId());
			}
			break;

		default:
			ATLASSERT(0); //Unknown entry
		}

		if (arrayStrOut.GetSize() <= i)
		{
			arrayStrOut.Add(str);
		}
		else
		{
			arrayStrOut[i] = str;
		}
	}

	return true;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

IW::Folder::Folder() :
	_bHasSelection(false),
	_nFocusItem(-1),
	_nImageCount(0),
	_nLoadedCount(0),
	_timeLast(0),
	_timeRemaining(INT_MAX),
	_timeFirst(0),
	_nLoadItem(0)
{
	_item = CShellDesktopItem();
}


IW::Folder::~Folder()
{
	DeleteAllThumbs();
	// Sanity check FinalRelease has been called?
	assert(_thumbs.size() == 0);
}

bool IW::Folder::Init(const CString& strFilePath)
{
	CFilePath path(strFilePath);
	path.Normalize(true);
	path.MakeDosPath();

	CShellItem item;
	if (!item.Open(path))
	{
		return false;
	}

	return Init(item);
}

bool IW::Folder::Init(CShellFolder& folder)
{
	return Init(folder.GetShellItem(), folder);
}

bool IW::Folder::Init(const CShellItem& itemFull)
{
	if (itemFull.Depth() > 1)
	{
		CShellItem itemFolder(itemFull);
		itemFolder.StripToParent();

		CShellItem itemTail(itemFull);
		itemTail.StripToTail();

		CShellFolder pShellFolder;
		HRESULT hr = pShellFolder.Open(itemFolder, true);
		if (FAILED(hr)) return false;

		return Init(itemFull, pShellFolder, itemTail);
	}

	return Init(itemFull, CShellDesktop(), itemFull);
}

bool IW::Folder::Init(const CShellItem& itemFull, CShellFolder& pFolder, const CShellItem& item)
{
	HRESULT hr;

	// Connect to folder
	// Get the IEnumIDList object for the given folder.
	CShellFolder pNewFolder;

	if (itemFull.IsDesktop())
	{
		pNewFolder = CShellDesktop();
	}
	else
	{
		hr = pNewFolder.Open(pFolder, item, true);

		if (FAILED(hr))
		{
			return false;
		}
	}

	return Init(itemFull, pNewFolder);
}

bool IW::Folder::Init(const CShellItem& itemFull, CShellFolder& pNewFolder)
{
	{
		CAutoLockCS lock(_cs);

		// Get the IEnumIDList object for the given folder.
		CShellItemEnum enumitem;

		UINT dwFlags = SHCONTF_FOLDERS | SHCONTF_NONFOLDERS;
		if (App.Settings.m_bShowHidden) dwFlags |= SHCONTF_INCLUDEHIDDEN;

		HRESULT hr = enumitem.Create(GetMainWindow(), pNewFolder, dwFlags);

		if (FAILED(hr) || enumitem == nullptr)
		{
			return false;
		}

		// Build up item map
		// Enumerate through the list of items.	
		ITEMLIST thumbsNew;
		LPITEMIDLIST pItem = nullptr;
		ULONG ulFetched = 0;

		while (enumitem->Next(1, &pItem, &ulFetched) == S_OK)
		{
			FolderItemPtr pThumb = new RefObj<FolderItem>;

			if (!pThumb->Init(pNewFolder, pItem, nullptr))
				return false;

			int nIcon = App.GetIcon(pThumb->_uExtension);
			if (nIcon != -1) pThumb->_nImage = nIcon;
			thumbsNew.push_back(pThumb);
		}

		// Swap over
		_bHasSelection = false;
		_item = itemFull;

		SetShellFolder(pNewFolder);

		// Delete old thumbs and insert the new
		_thumbs = thumbsNew;
	}

	// update selected items
	UpdateSelectedItems();

	return true;
}

class TagMapper
{
public:
	IW::TAGMAP& _tags;

	TagMapper(IW::TAGMAP& tags) : _tags(tags)
	{
	}

	TagMapper(const TagMapper& other) : _tags(other._tags)
	{
	}

	void operator()(const IW::FolderItem* pItem)
	{
		IW::TAGSET tagset = pItem->GetTags();

		for (auto it = tagset.begin(); it != tagset.end(); ++it)
		{
			_tags[*it]++;
		}
	}
};

IW::TAGMAP IW::Folder::GetTags() const
{
	CAutoLockCS lock(_cs);

	TAGMAP tags;
	TagMapper mapper(tags);

	std::for_each(_thumbs.begin(), _thumbs.end(), mapper);

	return tags;
}

//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////


int IW::Folder::WriteFocus()
{
	CAutoLockCS lock(_cs);

	int nFocusItem = GetFocusItem();

	if (nFocusItem != -1)
	{
		int nSize = GetSize();

		for (int i = 0; i < nSize; i++)
		{
			if (GetItemFlags(i) & THUMB_FOCUS)
			{
				ModifyItemFlags(i, THUMB_FOCUS, 0);
			}
		}

		if (nFocusItem < nSize && nFocusItem >= 0)
		{
			ModifyItemFlags(nFocusItem, 0, THUMB_FOCUS);
		}
	}

	return nFocusItem;
}

int IW::Folder::ReadFocus()
{
	CAutoLockCS lock(_cs);

	int nSize = GetSize();
	int nFocusItem = -1;

	for (int i = 0; i < nSize; i++)
	{
		if (GetItemFlags(i) & THUMB_FOCUS)
		{
			ModifyItemFlags(i, THUMB_FOCUS, 0);
			nFocusItem = i;
			break;
		}
	}

	return nFocusItem;
}


HRESULT IW::Folder::GetSelectedUIObjectOf(HWND hwnd, REFIID riid, UINT* prgfInOut, void** ppv)
{
	CAutoLockCS lock(_cs);

	int nCount = static_cast<int>(std::count_if(_thumbs.begin(), _thumbs.end(), FolderItem::isSelected));
	if (nCount == 0) return E_FAIL;
	IW::CBuffer<LPCITEMIDLIST> items(nCount + 1);
	copy_ref_if(_thumbs.begin(), _thumbs.end(), items.data(), FolderItem::isSelected);

	return GetShellFolder()->GetUIObjectOf(hwnd, nCount, items, riid, prgfInOut, ppv);
}


CString IW::Folder::GetToolTip(int nItem) const
{
	CAutoLockCS lock(_cs);
	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size())) return CString();
	FolderItem* pThumb = _thumbs[nItem];
	return pThumb->GetToolTip();
}

HRESULT IW::Folder::BindToStorage(int nItem, REFIID riid, void** ppv) const
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return E_INVALIDARG;

	return _thumbs[nItem]->BindToStorage(nItem, riid, ppv);
}

HRESULT IW::FolderItem::BindToStorage(int nItem, REFIID riid, void** ppv) const
{
	if (_itemGap.IsNull())
	{
		return _pFolder->BindToObject(GetShellItem(), nullptr, riid, ppv);
	}

	CShellItem item;
	item.Cat(_itemGap, _item);
	return _pFolder->BindToStorage(item, nullptr, riid, ppv);
}

HRESULT IW::Folder::BindToObject(int nItem, REFIID riid, void** ppv)
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return E_INVALIDARG;

	return _thumbs[nItem]->BindToObject(nItem, riid, ppv);
}


HRESULT IW::FolderItem::BindToObject(int nItem, REFIID riid, void** ppv)
{
	if (_itemGap.IsNull())
	{
		return _pFolder->BindToObject(_item, nullptr, riid, ppv);
	}

	CShellItem item;
	item.Cat(_itemGap, _item);
	return _pFolder->BindToObject(item, nullptr, riid, ppv);
}

HRESULT IW::Folder::GetUIObjectOf(int nItem, REFIID riid, void** ppv)
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return E_INVALIDARG;

	return _thumbs[nItem]->GetUIObjectOf(nItem, riid, ppv);
}

HRESULT IW::FolderItem::GetUIObjectOf(int nItem, REFIID riid, void** ppv)
{
	if (_itemGap.IsNull())
	{
		return _pFolder->GetUIObjectOf(GetMainWindow(), 1, _item, riid, nullptr, ppv);
	}

	CShellItem item;
	item.Cat(_itemGap, _item);
	return _pFolder->GetUIObjectOf(GetMainWindow(), 1, item, riid, nullptr, ppv);
}


HRESULT IW::Folder::CreateViewObject(HWND hwnd, REFIID riid, void** ppv)
{
	CAutoLockCS lock(_cs);
	return GetShellFolder()->CreateViewObject(hwnd, riid, ppv);
}

class CShellItemCompare
{
protected:
	IShellFolder* _pFolder;
	const IW::CShellItem& _item;

public:
	CShellItemCompare(IShellFolder* pFolder, IW::CShellItem& item) : _pFolder(pFolder), _item(item)
	{
	}

	CShellItemCompare(IW::FolderItem* pThumb) : _pFolder(pThumb->GetShellFolder()), _item(pThumb->GetShellItem())
	{
	}

	CShellItemCompare(IW::FolderItemPtr pThumb) : _pFolder(pThumb->GetShellFolder()), _item(pThumb->GetShellItem())
	{
	}

	CShellItemCompare(const CShellItemCompare& sc) : _pFolder(sc._pFolder), _item(sc._item)
	{
	};

	bool operator<(const CShellItemCompare& sc) const
	{
		return _item.Compare(_pFolder, sc._item) < 0;
	}

private:
	void operator=(IW::FolderItem* pThumb)
	{
	};

	void operator=(const CShellItemCompare& sc)
	{
	};
};

HRESULT IW::Folder::Refresh(HWND hwnd)
{
	SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_IDLIST, static_cast<LPCITEMIDLIST>(_item), nullptr);

	// Get the IEnumIDList object for the given folder.
	CShellItemEnum enumitem;

	UINT dwFlags = SHCONTF_FOLDERS | SHCONTF_NONFOLDERS;
	if (App.Settings.m_bShowHidden) dwFlags |= SHCONTF_INCLUDEHIDDEN;

	HRESULT hr = enumitem.Create(hwnd, GetShellFolder(), dwFlags);

	if (FAILED(hr))
	{
		// In this case we will probably
		// Want to rever to the parent item
		return hr;
	}

	// Enumerated without the lock: on a network share or a namespace extension
	// this is seconds of shell calls, and _cs is what the thumbnail worker and
	// every accessor on the paint path go through.
	ITEMLIST enumerated;

	LPITEMIDLIST pItem = nullptr;
	ULONG ulFetched = 0;

	while (enumitem->Next(1, &pItem, &ulFetched) == S_OK)
	{
		FolderItemPtr pThumb = new RefObj<FolderItem>;

		// Was "return false", i.e. S_OK to the caller, and it left every
		// surviving item still flagged THUMB_DELETE for the next refresh
		// to act on. Skip the one item instead.
		if (pThumb->Init(GetShellFolder(), pItem, nullptr))
		{
			enumerated.push_back(pThumb);
		}
	}

	{
		CAutoLockCS lock(_cs);

		using MAPTHUMBS = std::map<CShellItemCompare, FolderItemPtr>;
		MAPTHUMBS mapThumbs;

		for (auto i = _thumbs.begin(); i != _thumbs.end(); ++i)
		{
			FolderItem* pThumb = *i;
			mapThumbs[pThumb] = pThumb;

			// A search result comes from another folder, so it is never in this
			// enumeration and the sweep below would always drop it.
			if ((pThumb->_uFlags & THUMB_IS_SEARCH_RESULT) == 0)
				pThumb->_uFlags |= THUMB_DELETE;
		}

		for (auto i = enumerated.begin(); i != enumerated.end(); ++i)
		{
			FolderItemPtr pThumb = *i;
			auto thumbOld = mapThumbs.find(pThumb);

			if (thumbOld != mapThumbs.end())
			{
				FolderItem* pFoundThumb = thumbOld->second;

				// If its not an image strip the
				// load status so we can reload it
				// OR if the image is been updated
				if ((pFoundThumb->_ftLastWriteTime != pThumb->_ftLastWriteTime) ||
					(pFoundThumb->_sizeFile != pThumb->_sizeFile))
				{
					pFoundThumb->_uFlags &= ~THUMB_LOADED;
				}

				// Copy over possibly refreshed values
				pFoundThumb->_ulAttribs = pThumb->_ulAttribs;
				pFoundThumb->_ftCreationTime = pThumb->_ftCreationTime;
				pFoundThumb->_ftLastWriteTime = pThumb->_ftLastWriteTime;
				pFoundThumb->_sizeFile = pThumb->_sizeFile;
				pFoundThumb->_dwFileAttributes = pThumb->_dwFileAttributes;

				// The names are cached now, so a retargeted shortcut or a renamed
				// namespace item would keep its stale name without this.
				pFoundThumb->_strFileName = pThumb->_strFileName;
				pFoundThumb->_strFilePath = pThumb->_strFilePath;
				pFoundThumb->_strDisplayName = pThumb->_strDisplayName;
				pFoundThumb->_strDisplayPath = pThumb->_strDisplayPath;

				// Mark for do not delete
				pFoundThumb->_uFlags &= ~THUMB_DELETE;
			}
			else
			{
				// New file!
				_thumbs.push_back(pThumb);
			}
		}

		ITEMLIST notDeleted;
		IW::copy_if(_thumbs.begin(), _thumbs.end(), std::inserter(notDeleted, notDeleted.end()),
		            FolderItem::isNotDeleted);
		_thumbs = notDeleted;

		// Reset timer
		_timeRemaining = INT_MAX;
	}

	UpdateSelectedItems();

	return S_OK;
}


UINT IW::Folder::GetSelectionAttributes() const
{
	CAutoLockCS lock(_cs);

	int nCount = static_cast<int>(std::count_if(_thumbs.begin(), _thumbs.end(), FolderItem::isSelected));
	if (nCount == 0) return 0;

	IW::CBuffer<LPCITEMIDLIST> items(nCount + 1);
	copy_ref_if(_thumbs.begin(), _thumbs.end(), items.data(), FolderItem::isSelected);

	ULONG ulInOut = SFGAO_CANRENAME | SFGAO_CANMOVE | SFGAO_CANDELETE | SFGAO_CANCOPY | SFGAO_HASPROPSHEET |
		SFGAO_READONLY;
	ULONG nAttribs = 0;

	HRESULT hr = GetShellFolder()->GetAttributesOf(nCount, items, &ulInOut);

	if (SUCCEEDED(hr))
		nAttribs = ulInOut;

	return nAttribs;
};

void IW::Folder::UpdateSelectedItems()
{
	CAutoLockCS lock(_cs);
	_bHasSelection = _thumbs.end() != std::find_if(_thumbs.begin(), _thumbs.end(), FolderItem::isSelected);
}


CString IW::Folder::GetSelectedFileList() const
{
	CAutoLockCS lock(_cs);
	CString str;
	TCHAR szDelim = _T('\n');

	for (auto i = _thumbs.begin(); i != _thumbs.end(); ++i)
	{
		const FolderItemPtr pThumb = *i;

		if (pThumb->IsSelected())
		{
			str += pThumb->GetFilePath();
			str += szDelim;
		}
	}

	str += szDelim;
	return str;
}


void IW::Folder::DeleteAllThumbs()
{
	CAutoLockCS lock(_cs);

	_thumbs.clear();

	// Reset from delete
	_nFocusItem = -1;
	_timeRemaining = INT_MAX;
}

void IW::Folder::InsertThumb(FolderItem* pThumbIn)
{
	CAutoLockCS lock(_cs);

	_thumbs.push_back(pThumbIn);

	if (FolderItem::isImage(pThumbIn))
	{
		_nImageCount++;
	}

	if (pThumbIn->_uFlags & THUMB_LOADED)
	{
		_nLoadedCount++;
	}
}


bool IW::Folder::GetParentItem(CShellItem& item) const
{
	CAutoLockCS lock(_cs);

	// item
	item = _item;
	return item.StripToParent();
}

int IW::Folder::Find(const CShellItem& item) const
{
	CAutoLockCS lock(_cs);
	int n = 0;

	for (auto i = _thumbs.begin(); i != _thumbs.end(); ++i)
	{
		const FolderItem* pThumb = *i;

		HRESULT hr = GetShellFolder()->CompareIDs(0, pThumb->_item, item);

		ATLASSERT(SUCCEEDED(hr));

		if (SCODE_CODE(hr) == 0)
			return n;

		n++;
	}

	return -1;
}


int IW::Folder::Find(const CString& strFileName) const
{
	CAutoLockCS lock(_cs);

	for (int i = 0; i < GetSize(); ++i)
	{
		if (GetItemName(i).CompareNoCase(strFileName) == 0)
		{
			return i;
		}
	}

	return -1;
}

CString IW::Folder::GetFolderName() const
{
	CAutoLockCS lock(_cs);
	CShellFolder pParentFolder;

	if (_item.Depth() > 1)
	{
		CShellItem itemParent(_item);
		itemParent.StripToParent();
		pParentFolder.Open(itemParent, true);
	}
	else
	{
		pParentFolder = CShellDesktop();
	}

	return pParentFolder.GetDisplayNameOf(_item.GetTailItem(), SHGDN_FORPARSING | SHGDN_INFOLDER);
}

CString IW::Folder::GetFolderPath() const
{
	// _item is written only by Init, before the folder is published, so this
	// needs no lock -- and holding one across a shell call on an unreachable
	// path would block the UI thread's paint for as long as it takes.
	CShellDesktop desktop;
	return desktop.GetDisplayNameOf(_item, SHGDN_FORPARSING);
}

bool IW::Folder::GetParentFolder(Folder** ppFolderOut)
{
	CAutoLockCS lock(_cs);

	if (!_item.IsDesktop())
	{
		CShellItem item(_item);
		item.StripToParent();

		FolderPtr pFolder = new RefObj<Folder>;
		if (!pFolder->Init(item))
			return false;

		return (ReferencePtrAssign(ppFolderOut, static_cast<Folder*>(pFolder)) != nullptr);
	}

	ReferencePtrAssign(ppFolderOut, static_cast<Folder*>(nullptr));
	return false;
}

// Methods to handel selectability
long IW::Folder::GetSelectedItemCount() const
{
	CAutoLockCS lock(_cs);
	int nCount = static_cast<int>(std::count_if(_thumbs.begin(), _thumbs.end(), FolderItem::isSelected));
	return nCount;
}

long IW::Folder::GetItemCount() const
{
	CAutoLockCS lock(_cs);
	return static_cast<long>(_thumbs.size());
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////
//// FolderItemLoader
////

IW::FolderItemLoader::FolderItemLoader() :
	_pStatus(CNullStatus::Instance),
	_pLoadingClaim(nullptr),
	_bLoadedImage(false),
	_bLoadedIcon(false),
	_bDidStartLoad(false),
	_nSubItemCount(0),
	_sizeThumbnail(App.Settings._sizeThumbImage)
{
}

IW::FolderItemLoader::FolderItemLoader(const CSize& sizeThumbnail) :
	_pStatus(CNullStatus::Instance),
	_pLoadingClaim(nullptr),
	_bLoadedImage(false),
	_bLoadedIcon(false),
	_bDidStartLoad(false),
	_nSubItemCount(0),
	_sizeThumbnail(sizeThumbnail)
{
}

IW::FolderItemLoader::FolderItemLoader(FolderItem& item) :
	_item(item),
	_pStatus(CNullStatus::Instance),
	_pLoadingClaim(&item),
	_bLoadedImage(false),
	_bLoadedIcon(false),
	_bDidStartLoad(false),
	_nSubItemCount(0),
	_sizeThumbnail(App.Settings._sizeThumbImage)
{
	item._uFlags |= THUMB_LOADING;
	_strFilePath = item.GetFilePath();
}

IW::FolderItemLoader::~FolderItemLoader()
{
	// LoadImage or RenderAndScale throwing would otherwise leave the item
	// marked THUMB_LOADING for good, and CanLoad never offers it again.
	if (_pLoadingClaim != nullptr)
	{
		_pLoadingClaim->_uFlags &= ~THUMB_LOADING;
		_pLoadingClaim = nullptr;
	}
}

bool IW::FolderItemLoader::SyncThumb(FolderItem& item)
{
	if (_bLoadedImage)
	{
		// Set the thumbnail dib
		item._uFlags |= THUMB_IMAGE;
		item._image = _item._image;
	}

	if (_bLoadedIcon)
	{
		item._uFlags |= THUMB_IMAGE_ICON;
		item._image = _item._image;
	}

	item._nSubItemCount = _nSubItemCount;
	item._nImage = _item._nImage;

	// A decode the folder-watch refresh cancelled produced nothing, and CanLoad
	// never offers an item marked LOADED again -- it would be stuck on its
	// generic icon for the rest of the session.
	if (_bLoadedImage || _bLoadedIcon || _pStatus == nullptr || !_pStatus->QueryCancel())
	{
		item._uFlags |= THUMB_LOADED;
	}

	item._uFlags &= ~THUMB_LOADING;

	if (_pLoadingClaim == &item)
		_pLoadingClaim = nullptr;

	return true;
}


bool IW::FolderItemLoader::LoadImage(CLoadAny* pLoader, const Search::Spec& spec, bool bForceLoad)
{
	if (bForceLoad || (_item._uFlags & THUMB_LOADED) == 0)
	{
		_bDidStartLoad = true;

		// Figure out extension
		DWORD uExtension = App.GetExtensionKey(GetFilePath());

		// Load an icon
		if (_item._nImage == -1)
		{
			bool bMayBeCachedIcon = App.CanBeCached(uExtension);

			if (bMayBeCachedIcon)
			{
				_item._nImage = App.GetIcon(uExtension);
			}

			if (_item._nImage == -1)
			{
				/* IW::RefPtr<IShellIcon> pShellIcon;
				hr = _item._pFolder->QueryInterface(IID_IShellIcon, (void**)&pShellIcon);

				if (FAILED(hr))
				{
					hr = _item._pFolder->GetUIObjectOf(NULL, 1, _item._item, IID_IShellIcon, NULL, (void**)&pShellIcon);
				}

				if (SUCCEEDED(hr) && pShellIcon) // early shell version, thumbs not supported
				{
					hr = pShellIcon->GetIconOf(_item._item, GIL_FORSHELL, &_item._nImage);
				}

				if (hr != S_OK)
				{*/
				SHFILEINFO sfi;
				MemZero(&sfi, sizeof(sfi));

				if (SHGetFileInfo(GetFilePath(), 0, &sfi, sizeof(SHFILEINFO), SHGFI_SYSICONINDEX) != 0)
				{
					_item._nImage = sfi.iIcon;
				}
				//}

				if (bMayBeCachedIcon)
				{
					App.SetIcon(uExtension, _item._nImage);
				}
			}
		}

		if (pLoader && _item.IsFolder())
		{
			if (App.Settings.m_bWalkFolders)
			{
				LoadFolderImage(pLoader, bForceLoad);
			}
		}
		// A cloud placeholder stays on its file-type icon. CreateFile alone hydrates
		// one, so the test has to come before anything opens it; the item is still
		// marked loaded, and an explicit open by the user downloads it as before.
		else if (bForceLoad || !_item.IsOffline())
		{
			if (pLoader)
			{
				ATLTRACE(_T("Thumbnailing Image %s\n"), static_cast<LPCTSTR>(GetFilePath()));

				if (IsAllowedType(uExtension))
				{
					Image& image = _item._image;
					image.Free();
					ImageStreamThumbnail<IImageStream> imageOut(image, spec, _sizeThumbnail);
					_bLoadedImage = pLoader->LoadImage(GetFilePath(), &imageOut, _pStatus);
					_bLoadedImage = _bLoadedImage && !image.IsEmpty();
				}
			}




			/* if (!_bLoadedImage && App.Settings._bSystemThumbs)
			{
				ULONG u = _item._pFolder.GetAttributes(_item._item, SFGAO_ISSLOW);

				if ((SFGAO_ISSLOW & u) == 0)
				{
					HRESULT hr;
					IW::RefPtr<IExtractImage> pIExtract;
					hr = _item._pFolder->GetUIObjectOf(NULL, 1, _item._item, IID_IExtractImage, NULL, (void**)&pIExtract);

					if (SUCCEEDED(hr) && pIExtract) // early shell version, thumbs not supported
					{
						OLECHAR wszPathBuffer[MAX_PATH];
						DWORD dwPriority = IEIT_PRIORITY_NORMAL | IEIFLAG_OFFLINE | IEIFLAG_QUALITY;
						DWORD dwFlags = IEIFLAG_QUALITY;
						DWORD dwRecClrDepth = 24;
						HBITMAP hBmpImage = NULL;
						CSize sizeScale = App.Settings._sizeThumbImage;

						hr = pIExtract->GetLocation(wszPathBuffer, MAX_PATH, &dwPriority, &sizeScale, dwRecClrDepth, &dwFlags);

						// even if we've got shell v4.70+, not all files support thumbnails 
						if(NOERROR == hr) 
						{
							hr = pIExtract->Extract(&hBmpImage);

							if (SUCCEEDED(hr))
							{
								CWindowDC dc(GetDesktopWindow());
								if (_item._image.Copy(dc, hBmpImage))
								{
									_bLoadedIcon = true;
								}

								DeleteObject(hBmpImage);
							}
						}						
					}
				}
			} */
		}

		// Did we take longer than 5 seconds?
		//assert(timeStart + 5000 > GetTickCount());
	}

	return true;
}

void IW::FolderItemLoader::LoadFolderImage(CLoadAny* pLoader, bool bForceLoad)
{
	WIN32_FIND_DATA findData;
	CString strSearch = GetFilePath() + _T("\\*.*");
	HANDLE hSearch = FindFirstFile(strSearch, &findData);

	if (hSearch != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (IsFolderWalkImageType(App.GetExtensionKey(findData.cFileName)))
			{
				const bool bOffline = 0 != (findData.dwFileAttributes & (FILE_ATTRIBUTE_OFFLINE |
					FILE_ATTRIBUTE_RECALL_ON_OPEN |
					FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS));

				if (!_bLoadedIcon && !bOffline)
				{
					CString strFilePath = Path::Combine(GetFilePath(), findData.cFileName);

					FolderItem subItem;
					subItem.Init(strFilePath);

					FolderItemLoader job(_sizeThumbnail);
					job.SetStatus(_pStatus);

					if (subItem.LoadJobBegin(job))
					{
						job.LoadImage(pLoader, Search::Any, bForceLoad);
						job.RenderAndScale();
					}

					subItem.LoadJobEnd(job);

					if (subItem.IsImage())
					{
						CSize size(_sizeThumbnail.cx / 2, _sizeThumbnail.cy / 2);

						ImageStreamThumbnail<IImageStream> imageOut(_item._image, Search::Any, size);
						IterateImage(subItem.GetImage(), imageOut, _pStatus);

						_bLoadedIcon = !_item._image.IsEmpty();
					}
				}

				_nSubItemCount++;
			}
		}
		while (FindNextFile(hSearch, &findData));

		FindClose(hSearch);
	}
}

void IW::FolderItemLoader::RenderAndScale()
{
	if (_bLoadedImage || _bLoadedIcon)
	{
		bool bIsFolder = (_item._ulAttribs & SFGAO_FOLDER) != 0;

		CSize sizeScale = _sizeThumbnail;
		const CRect rectSize = _item._image.GetBoundingRect();

		if (bIsFolder)
		{
			sizeScale.cx /= 2;
			sizeScale.cy /= 2;
		}

		if (_item._image.NeedRenderForDisplay())
		{
			Image imageTemp;
			_item._image.Render(imageTemp);
			_item._image = imageTemp;
		}
	}
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

CString IW::Folder::GetItemName(int nItem) const
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return CString();

	return _thumbs[nItem]->GetFileName();
}

CString IW::Folder::GetItemPath(int nItem) const
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return CString();

	return _thumbs[nItem]->GetFilePath();
}

///////////////////////////////////////////////////////////////////////////////////////
// Selection Helpers

void IW::Folder::Select(int nFocusNew, UINT nFlags)
{
	CAutoLockCS lock(_cs);

	int nSize = GetSize();

	if (nFocusNew < 0 || nFocusNew > (nSize - 1))
		return;

	int nFocusOld = _nFocusItem;

	if (nFocusNew != nFocusOld)
	{
		if (-1 != nFocusOld)
		{
			FolderItem* pThumb = _thumbs[nFocusOld];
			pThumb->_uFlags |= THUMB_INVALIDATE;
		}

		_nFocusItem = nFocusNew;
		FolderItem* pThumb = _thumbs[nFocusNew];
		pThumb->_uFlags |= THUMB_INVALIDATE;
	}

	if (!(nFlags & MK_CONTROL))
	{
		for (int j = 0; j < nSize; j++)
		{
			FolderItem* pThumb = _thumbs[j];

			if (pThumb->IsSelected())
			{
				pThumb->_uFlags &= ~THUMB_SELECTED;
				pThumb->_uFlags |= THUMB_INVALIDATE;
			}
		}
	}

	if (nFlags & MK_SHIFT && nFocusOld != -1)
	{
		if (nFocusNew < nFocusOld)
		{
			for (int j = nFocusNew; j <= nFocusOld; j++)
			{
				FolderItem* pThumb = _thumbs[j];

				if (nFlags & MK_CONTROL)
					pThumb->_uFlags ^= THUMB_SELECTED;
				else
					pThumb->_uFlags |= THUMB_SELECTED;

				pThumb->_uFlags |= THUMB_INVALIDATE;
			}
		}
		else
		{
			for (int j = nFocusOld; j <= nFocusNew; j++)
			{
				FolderItem* pThumb = _thumbs[j];

				if (nFlags & MK_CONTROL)
					pThumb->_uFlags ^= THUMB_SELECTED;
				else
					pThumb->_uFlags |= THUMB_SELECTED;

				pThumb->_uFlags |= THUMB_INVALIDATE;
			}
		}
	}
	else
	{
		FolderItem* pThumb = _thumbs[nFocusNew];

		if (nFlags & MK_CONTROL)
			pThumb->_uFlags ^= THUMB_SELECTED;
		else
			pThumb->_uFlags |= THUMB_SELECTED;

		pThumb->_uFlags |= THUMB_INVALIDATE;
	}

	UpdateSelectedItems();
}

void IW::Folder::SelectAll()
{
	CAutoLockCS lock(_cs);
	std::for_each(_thumbs.begin(), _thumbs.end(), FolderItem::selectItem);
	UpdateSelectedItems();
}

void IW::Folder::SelectInverse()
{
	CAutoLockCS lock(_cs);
	std::for_each(_thumbs.begin(), _thumbs.end(), FolderItem::selectInverse);
	UpdateSelectedItems();
}

class SelectTagAdapter
{
public:
	CString _tag;
	int *_pnMatched;

	SelectTagAdapter(const CString &tag, int *pnMatched) : _tag(tag), _pnMatched(pnMatched)
	{
	}

	SelectTagAdapter(const SelectTagAdapter& other) : _tag(other._tag), _pnMatched(other._pnMatched)
	{
	}

	void operator()(IW::FolderItem* pThumb)
	{
		IW::TAGSET tags = pThumb->GetTags();
		bool bSelect = false;

		for (auto it = tags.begin(); it != tags.end(); ++it)
		{
			if (_tag.CompareNoCase(*it) == 0)
			{
				bSelect = true;
				break;
			}
		}

		if (bSelect)
		{
			pThumb->ModifyFlags(0, THUMB_SELECTED);
			*_pnMatched += 1;
		}
		else
		{
			pThumb->ModifyFlags(THUMB_SELECTED, 0);
		}
	}
};


void IW::Folder::SelectImages()
{
	CAutoLockCS lock(_cs);
	std::for_each(_thumbs.begin(), _thumbs.end(), FolderItem::selectIfImage);
	UpdateSelectedItems();
}


int IW::Folder::SelectTag(const CString& strTag)
{
	CAutoLockCS lock(_cs);
	int nMatched = 0;
	SelectTagAdapter selector(strTag, &nMatched);

	std::for_each(_thumbs.begin(), _thumbs.end(), selector);
	UpdateSelectedItems();

	return nMatched;
}

int IW::Folder::GetSelectCount() const
{
	CAutoLockCS lock(_cs);
	return static_cast<int>(std::count_if(_thumbs.begin(), _thumbs.end(), FolderItem::isSelected));
}

void IW::Folder::GetSelectStatus(int& nCount, int& nImages, FileSize& size) const
{
	CAutoLockCS lock(_cs);

	for (auto i = _thumbs.begin(); i != _thumbs.end(); ++i)
	{
		const FolderItem* pThumb = *i;

		if (pThumb->IsSelected())
		{
			nCount++;
			size += pThumb->_sizeFile;

			if (pThumb->IsImage())
			{
				nImages++;
			}
		}
	}
}


void IW::Folder::DragSelection(int nItem)
{
	CAutoLockCS lock(_cs);

	int nItemCount = static_cast<int>(_thumbs.size());
	int nInsertLocation = Clamp(nItem, 0, nItemCount);

	ITEMLIST newOrderThumbs;
	newOrderThumbs.reserve(nItemCount);

	int nIndex = 0;

	for (auto i = _thumbs.begin(); i != _thumbs.end(); ++i)
	{
		if (nIndex == nInsertLocation)
		{
			IW::copy_if(_thumbs.begin(), _thumbs.end(),
			            std::inserter(newOrderThumbs, newOrderThumbs.end()),
			            FolderItem::isSelected);
		}

		if (!FolderItem::isSelected(*i))
		{
			newOrderThumbs.push_back(*i);
		}

		nIndex++;
	}

	//IW::copy_if(_thumbs.begin(), _thumbs.end(), newOrderThumbs.end(), isNotSelected);
	//IW::copy_if(_thumbs.begin(), _thumbs.end(), newOrderThumbs.end(), isSelected);
	//IW::copy_if(_thumbs.begin() + nInsertLocation, _thumbs.end(), newOrderThumbs.end(), isNotSelected);

	_thumbs = newOrderThumbs;
}

bool IW::Folder::IsItemDropTarget(int nItem) const
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return false;

	const FolderItem* pThumb = _thumbs[nItem];

	CShellItem item = pThumb->_item;
	CShellFolder pShellFolder = pThumb->GetShellFolder();

	ULONG u = pShellFolder.GetAttributes(item, SFGAO_LINK | SFGAO_DROPTARGET);

	if (u & SFGAO_LINK)
	{
		if (FAILED(pShellFolder.ResolveLink(item, item)))
		{
			u = 0;
		}
		else
		{
			CShellDesktop desktop;
			u = desktop.GetAttributes(item, SFGAO_DROPTARGET);
		}
	}

	return (u & SFGAO_DROPTARGET) == 0;
}

bool IW::Folder::IsItemBrowsable(int nItem) const
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return false;

	FolderItem* pThumb = _thumbs[nItem];
	return pThumb->GetShellFolder().IsBrowsable(pThumb->_item);
}


bool IW::Folder::IsItemImage(int nItem) const
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return false;

	return FolderItem::isImage(_thumbs[nItem]);
}


// Takes a counted reference under the lock. The search worker push_backs into
// _thumbs while the UI paints, so an unlocked index can dereference a buffer
// that has just been reallocated.
IW::FolderItemPtr IW::Folder::GetItem(int nItem) const
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return 0;

	return _thumbs[nItem];
}

void IW::Folder::GetItemAttributes(int nItem, FolderItemAttributes& attributes, bool bGetImage) const
{
	CAutoLockCS lock(_cs);

	if (nItem >= 0 && nItem < static_cast<int>(_thumbs.size()))
		_thumbs[nItem]->GetAttributes(attributes, bGetImage);
}

CString IW::Folder::GetItemDescription(int nItem) const
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return g_szEmptyString;

	return _thumbs[nItem]->GetImage().GetDescription();
}

void IW::Folder::GetItemFormatText(int nItem, CSimpleArray<CString>& arrayStrOut, CArrayDWORD& array, bool bFormat) const
{
	CAutoLockCS lock(_cs);

	if (nItem >= 0 && nItem < static_cast<int>(_thumbs.size()))
		_thumbs[nItem]->GetFormatText(arrayStrOut, array, bFormat);
}

void IW::Folder::GetItemFormatText(int nItem, CString& strOut, CArrayDWORD& array, bool bFormat) const
{
	CAutoLockCS lock(_cs);

	if (nItem >= 0 && nItem < static_cast<int>(_thumbs.size()))
		_thumbs[nItem]->GetFormatText(strOut, array, bFormat);
}

void IW::Folder::GetItemImage(int nItem, Image& imageOut) const
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return;

	imageOut = _thumbs[nItem]->_image;
}

void IW::Folder::GetItemFormatText(const FolderItem* pItem, CSimpleArray<CString>& arrayStrOut, CArrayDWORD& array, bool bFormat) const
{
	CAutoLockCS lock(_cs);

	if (pItem != nullptr)
		pItem->GetFormatText(arrayStrOut, array, bFormat);
}

void IW::Folder::GetItemImage(const FolderItem* pItem, Image& imageOut) const
{
	CAutoLockCS lock(_cs);

	if (pItem != nullptr)
		imageOut = pItem->_image;
}

int IW::Folder::GetItemImageNum(int nItem) const
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return -1;

	return _thumbs[nItem]->_nImage;
}

bool IW::Folder::IsItemMultiPageImage(int nItem) const
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return false;

	const FolderItem* pThumb = _thumbs[nItem];

	return pThumb->IsImage() &&
		pThumb->IsFolder() &&
		pThumb->_image.GetPageCount() > 1;
}

bool IW::Folder::IsItemAnimatedImage(int nItem) const
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return false;

	const FolderItem* pThumb = _thumbs[nItem];

	return pThumb->IsImage() &&
		pThumb->IsFolder() &&
		pThumb->_image.CanAnimate();
}

bool IW::Folder::AnimationStep(int nItem)
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return false;

	FolderItem* pThumb = _thumbs[nItem];

	bool bInvalidate = false;
	pThumb->_nTimer -= 100;

	if (pThumb->_nTimer <= 0)
	{
		// Work out page
		int nPage = (pThumb->_nFrame + 1) % pThumb->_image.GetPageCount();
		Page page = pThumb->_image.GetPage(nPage);

		pThumb->_nTimer = page.GetTimeDelay();
		pThumb->_nFrame = nPage;

		bInvalidate = true;
	}

	return bInvalidate;
}

bool IW::Folder::IsItemSelected(int nItem) const
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return false;

	return _thumbs[nItem]->IsSelected() != 0;
}

bool IW::Folder::CanLoad(int nItem) const
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return false;

	return (_thumbs[nItem]->_uFlags & (THUMB_LOADED | THUMB_LOADING)) == 0;
}

UINT IW::Folder::GetItemFlags(int nItem) const
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return 0;

	return _thumbs[nItem]->_uFlags;
}

int IW::Folder::GetItemFrame(int nItem) const
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return 0;

	return _thumbs[nItem]->_nFrame;
}

UINT IW::Folder::GetItemAttribs(int nItem) const
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return 0;

	return _thumbs[nItem]->_ulAttribs;
}

CRect IW::Folder::GetItemThumbRect(int nItem) const
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return CRect(0, 0, 0, 0);

	return _thumbs[nItem]->_image.GetBoundingRect();
}

void IW::Folder::GetCameraSettings(int nItem, CameraSettings& settings) const
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return;

	settings = _thumbs[nItem]->_image.GetCameraSettings();
}

void IW::Folder::SetItemFlags(int nItem, UINT uFlags)
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return;

	_thumbs[nItem]->_uFlags = uFlags;
}

void IW::Folder::ModifyItemFlags(int nItem, DWORD dwRemove, DWORD dwAdd)
{
	CAutoLockCS lock(_cs);

	if (nItem < 0 || nItem >= static_cast<int>(_thumbs.size()))
		return;

	FolderItem* pThumb = _thumbs[nItem];

	DWORD dwFlags = pThumb->_uFlags;
	DWORD dwNewFlags = (dwFlags & ~dwRemove) | dwAdd;

	if (dwFlags != dwNewFlags)
	{
		pThumb->_uFlags = dwNewFlags;
	}
}

void IW::Folder::ResetLoadedFlag()
{
	CAutoLockCS lock(_cs);
	std::for_each(_thumbs.begin(), _thumbs.end(), FolderItem::clearLoadedFlag);
}

int IW::Folder::CalcLoadedCount()
{
	CAutoLockCS lock(_cs);
	return static_cast<int>(std::count_if(_thumbs.begin(), _thumbs.end(), FolderItem::isLoaded));
}

int IW::Folder::CalcImageCount()
{
	CAutoLockCS lock(_cs);
	return static_cast<int>(std::count_if(_thumbs.begin(), _thumbs.end(), FolderItem::isImage));
}

int IW::Folder::GetFocusItem() const
{
	CAutoLockCS lock(_cs);
	return _nFocusItem;
}

void IW::Folder::SetFocusItem(int nFocusItem)
{
	CAutoLockCS lock(_cs);
	_nFocusItem = nFocusItem;
}

int IW::Folder::GetImageCount() const
{
	CAutoLockCS lock(_cs);
	return _nImageCount;
}

void IW::Folder::InvalidateThumb(int nItem)
{
	ModifyItemFlags(nItem, 0, THUMB_INVALIDATE);
}

int IW::Folder::GetPercentComplete() const
{
	CAutoLockCS lock(_cs);
	long nFilesOrFolders = GetSize();

	// MulDiv answers -1 for a zero denominator, which a progress bar reads as a
	// negative width.
	if (nFilesOrFolders <= 0)
		return 100;

	long nPercentComplete = MulDiv(_nLoadedCount, 100, nFilesOrFolders);

	// A refresh can drop an item the worker is still decoding, and its
	// LoadJobEnd still counts it -- the status bar paints this straight into a
	// part width with no clamp of its own.
	return nPercentComplete > 100 ? 100 : nPercentComplete;
}

int IW::Folder::GetTimeRemaining() const
{
	CAutoLockCS lock(_cs);
	int timeTaken = (_timeLast - _timeFirst) / 100;

	long nFilesOrFolders = GetSize();

	// If wParam is set then all
	// thumbs are loaded
	long nPercentComplete = MulDiv(_nLoadedCount, 100, nFilesOrFolders);
	long nSecondsTimesTen = timeTaken;

	int nPercentRemaining = 100 - nPercentComplete;
	int timeRemaining = MulDiv(nSecondsTimesTen, nPercentRemaining, nPercentComplete);

	if (timeRemaining < 1)
		timeRemaining = 1;

	return _timeRemaining = (timeRemaining + _timeRemaining) / 2;
}

int IW::Folder::GetTimeTaken() const
{
	CAutoLockCS lock(_cs);
	int timeTaken = (_timeLast - _timeFirst) / 100;
	return timeTaken;
}

void IW::Folder::ResetCounters()
{
	CAutoLockCS lock(_cs);

	_nLoadedCount = CalcLoadedCount();
	_nImageCount = CalcImageCount();

	// Rest the timer
	_timeLast = _timeFirst = GetTickCount();
	_timeRemaining = INT_MAX;
	_nLoadItem = 0;
}


/////////////////////////////////////////////////////////////////////////////

// Compare
template <bool bReverse>
class CCompareName
{
protected:
	IW::CShellFolder& _pShellFolder;

public:
	CCompareName(IW::CShellFolder& pShellFolder) : _pShellFolder(pShellFolder)
	{
	}

	bool operator()(const IW::FolderItem* a, const IW::FolderItem* b) const
	{
		int i = IW::FolderItem::CompareName(_pShellFolder, a, b);
		return (bReverse ? -i : i) < 0;
	}
};

template <bool bReverse>
class CCompareSize
{
protected:
	IW::CShellFolder& _pShellFolder;

public:
	CCompareSize(IW::CShellFolder& pShellFolder) : _pShellFolder(pShellFolder)
	{
	}

	bool operator()(const IW::FolderItem* a, const IW::FolderItem* b) const
	{
		int i = IW::FolderItem::CompareSize(_pShellFolder, a, b);
		return (bReverse ? -i : i) < 0;
	}
};

template <bool bReverse>
class CCompareType
{
protected:
	IW::CShellFolder& _pShellFolder;

public:
	CCompareType(IW::CShellFolder& pShellFolder) : _pShellFolder(pShellFolder)
	{
	}

	bool operator()(const IW::FolderItem* a, const IW::FolderItem* b) const
	{
		int i = IW::FolderItem::CompareType(_pShellFolder, a, b);
		return (bReverse ? -i : i) < 0;
	}
};

template <bool bReverse>
class CCompareCreationTime
{
protected:
	IW::CShellFolder& _pShellFolder;

public:
	CCompareCreationTime(IW::CShellFolder& pShellFolder) : _pShellFolder(pShellFolder)
	{
	}

	bool operator()(const IW::FolderItem* a, const IW::FolderItem* b) const
	{
		int i = IW::FolderItem::CompareCreationTime(_pShellFolder, a, b);
		return (bReverse ? -i : i) < 0;
	}
};

template <bool bReverse>
class CCompareDateTaken
{
protected:
	IW::CShellFolder& _pShellFolder;

public:
	CCompareDateTaken(IW::CShellFolder& pShellFolder) : _pShellFolder(pShellFolder)
	{
	}

	bool operator()(const IW::FolderItem* a, const IW::FolderItem* b) const
	{
		int i = IW::FolderItem::CompareDateTaken(_pShellFolder, a, b);
		return (bReverse ? -i : i) < 0;
	}
};

template <bool bReverse>
class CCompareLastWriteTime
{
protected:
	IW::CShellFolder& _pShellFolder;

public:
	CCompareLastWriteTime(IW::CShellFolder& pShellFolder) : _pShellFolder(pShellFolder)
	{
	}

	bool operator()(const IW::FolderItem* a, const IW::FolderItem* b) const
	{
		int i = IW::FolderItem::CompareLastWriteTime(_pShellFolder, a, b);
		return (bReverse ? -i : i) < 0;
	}
};

template <bool bReverse>
class CCompareWidth
{
protected:
	IW::CShellFolder& _pShellFolder;

public:
	CCompareWidth(IW::CShellFolder& pShellFolder) : _pShellFolder(pShellFolder)
	{
	}

	bool operator()(const IW::FolderItem* a, const IW::FolderItem* b) const
	{
		int i = a->GetImage().GetCameraSettings().OriginalImageSize.cx -
			b->GetImage().GetCameraSettings().OriginalImageSize.cx;
		return (bReverse ? -i : i) < 0;
	}
};

template <bool bReverse>
class CCompareHeight
{
protected:
	IW::CShellFolder& _pShellFolder;

public:
	CCompareHeight(IW::CShellFolder& pShellFolder) : _pShellFolder(pShellFolder)
	{
	}

	bool operator()(const IW::FolderItem* a, const IW::FolderItem* b) const
	{
		int i = a->GetImage().GetCameraSettings().OriginalImageSize.cy -
			b->GetImage().GetCameraSettings().OriginalImageSize.cy;
		return (bReverse ? -i : i) < 0;
	}
};

template <bool bReverse>
void SortThumbs(int nSortOrder, IW::CShellFolder& pShellFolder, IW::ITEMLIST& thumbs)
{
	switch (nSortOrder)
	{
	default:
	case IW::ePropertyName:
	case IW::ePropertyPath:
		{
			CCompareName<bReverse> t(pShellFolder);
			std::sort(thumbs.begin(), thumbs.end(), t);
		}
		break;

	case IW::ePropertyType:
		{
			CCompareType<bReverse> t(pShellFolder);
			std::sort(thumbs.begin(), thumbs.end(), t);
		}
		break;

	case IW::ePropertySize:
		{
			CCompareSize<bReverse> t(pShellFolder);
			std::sort(thumbs.begin(), thumbs.end(), t);
		}
		break;

	case IW::ePropertyModifiedDate:
	case IW::ePropertyModifiedTime:
		{
			CCompareCreationTime<bReverse> t(pShellFolder);
			std::sort(thumbs.begin(), thumbs.end(), t);
		}
		break;

	case IW::ePropertyDateTaken:
		{
			CCompareDateTaken<bReverse> t(pShellFolder);
			std::sort(thumbs.begin(), thumbs.end(), t);
		}
		break;

	case IW::ePropertyCreatedDate:
	case IW::ePropertyCreatedTime:
		{
			CCompareLastWriteTime<bReverse> t(pShellFolder);
			std::sort(thumbs.begin(), thumbs.end(), t);
		}
		break;
	case IW::ePropertyWidth:
		{
			CCompareWidth<bReverse> t(pShellFolder);
			std::sort(thumbs.begin(), thumbs.end(), t);
		}
		break;
	case IW::ePropertyHeight:
		{
			CCompareHeight<bReverse> t(pShellFolder);
			std::sort(thumbs.begin(), thumbs.end(), t);
		}
		break;
	}
};


void IW::Folder::Sort(int nSortOrder, bool bAscending)
{
	CWaitCursor wait;
	CAutoLockCS lock(_cs);

	if (bAscending)
	{
		SortThumbs<false>(nSortOrder, _pFolder, _thumbs);
	}
	else
	{
		SortThumbs<true>(nSortOrder, _pFolder, _thumbs);
	}
}

class FolderItemCompareName
{
public:
	bool operator()(const IW::FolderItem* a, const IW::FolderItem* b) const
	{
		CString strNameA = a->GetFileName();
		CString strNameB = b->GetFileName();
		return strNameA.CompareNoCase(strNameB) < 0;
	}
};

void IW::Folder::Sort(ITEMLIST& thumbs, int nSortOrder, bool bAscending)
{
	CWaitCursor wait;
	CAutoLockCS lock(_cs);

	if (bAscending)
	{
		SortThumbs<false>(nSortOrder, _pFolder, thumbs);
	}
	else
	{
		SortThumbs<true>(nSortOrder, _pFolder, thumbs);
	}
}

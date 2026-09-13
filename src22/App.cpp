// ImageWalker by Zac Walker
//
// Purpose: Application startup and shutdown, settings load/save, and the
//          cached string, icon and extension lookups.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

// App.cpp: the Application singleton.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "App.h"
#include "ViewModelItems.h"
#include "FileFormatAny.h"

#include <HtmlHelp.h>

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

Application App;

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

Application::Application() :
	IsTesting(false),
	ControlKeyDown(false)
{
}

Application::~Application()
{
}


void Application::Init()
{
	// Log the application startup
	{
		TCHAR szBuffer[100];
		_tzset();
		_tstrtime_s(szBuffer, countof(szBuffer));

		CString str;
		str.Format(IDS_STARTED_AT, szBuffer);
		Log(str);
	}


	srand(GetTickCount());

	Settings.Load();

	// Built once here so every later read is of a table nothing writes to.
	BuildMetaDataTypes();


	//jas_init(); // Jasper

	CWindowDC dc(nullptr);

	const CRect rectTextIn(0, 0, 100, 100);
	const CRect rectTextOut = IW::Style::MeasureString(_T("X"), rectTextIn, IW::Style::Font::Standard,
	                                                   IW::Style::Text::Thumbnail);
	m_nTextExtent = rectTextOut.Height();

	m_paletteHalftone.CreateHalftonePalette(dc);
}

void Application::Free()
{
	Settings.Save();

	m_setCanBeCached.clear();
	m_mapStrings.clear();
	m_mapIcons.clear();
	_stringPool.clear();
	//jas_cleanup(); // Jasper
}

bool Application::CanBeCached(UINT uExtension)
{
	IW::CAutoLockCS lock(_cs);

	if (m_setCanBeCached.empty())
	{
		m_setCanBeCached.insert(GetExtensionKey(_T(".EXE")));
		m_setCanBeCached.insert(GetExtensionKey(_T(".LNK")));
		m_setCanBeCached.insert(GetExtensionKey(_T(".SCR")));
		m_setCanBeCached.insert(GetExtensionKey(_T(".DLL")));
		m_setCanBeCached.insert(GetExtensionKey(_T(".COM")));
	}

	return !m_setCanBeCached.contains(uExtension);
}

int Application::GetIcon(DWORD dwExtension)
{
	IW::CAutoLockCS lock(_cs);
	auto i = m_mapIcons.find(dwExtension);

	if (i != m_mapIcons.end())
	{
		return i->second;
	}

	return -1;
}

void Application::SetIcon(DWORD dwExtension, int nIcon)
{
	IW::CAutoLockCS lock(_cs);
	m_mapIcons[dwExtension] = nIcon;
}


HIMAGELIST Application::GetShellImageList(bool fSmall)
{
	static HIMAGELIST hilSmall = nullptr;
	static HIMAGELIST hilLarge = nullptr;

	HIMAGELIST& hilOut = fSmall ? hilSmall : hilLarge;

	if (hilOut == nullptr)
	{
		HWND hWnd = IW::GetMainWindow();
		USES_CONVERSION;
		SHFILEINFO sfi;
		ZeroMemory(&sfi, sizeof(sfi));

		hilOut = (HIMAGELIST)SHGetFileInfo(_T("C:\\"), 0, &sfi,
		                                   sizeof(SHFILEINFO), SHGFI_SYSICONINDEX |
		                                   (fSmall ? SHGFI_SMALLICON : SHGFI_LARGEICON));

		// Do a version check first because you only need to use this code on
		// Windows NT version 4.0.
		if (IW::IsWindowsNT4())
		{
			return hilOut;
		}

		// You need to create a temporary, empty .lnk file that you can use to
		// pass to IShellIconOverlay::GetOverlayIndex. You could just enumerate
		// down from the Start Menu folder to find an existing .lnk file, but
		// there is a very remote chance that you will not find one. By creating
		// your own, you know this code will always work.
		IW::CShellFolder spsfTempDir;
		CComPtr<IShellIconOverlay> spsio;

		IW::CShellItem itemTempDir;
		IW::CShellItem itemTempFile;

		HRESULT hr;
		TCHAR szTempDir[MAX_PATH];
		TCHAR szTempFile[MAX_PATH] = _T("");
		TCHAR szFile[MAX_PATH];
		HANDLE hFile;
		int i;
		DWORD dwAttributes;
		DWORD dwEaten;
		int nIndex;

		// Get the desktop folder.
		IW::CShellDesktop pDesktop;

		// Get the TEMP directory.
		if (!GetTempPath(MAX_PATH, szTempDir))
		{
			//There might not be a TEMP directory. If this is the case, use the
			//Windows directory. 
			if (!GetWindowsDirectory(szTempDir, MAX_PATH))
			{
				return hilOut;
			}
		}

		// Create a temporary .lnk file.
		if (szTempDir[_tcsclen(szTempDir) - 1] != '\\')
			_tcscat_s(szTempDir, MAX_PATH, _T("\\"));

		for (i = 0, hFile = INVALID_HANDLE_VALUE;
		     INVALID_HANDLE_VALUE == hFile;
		     i++)
		{
			_tcscpy_s(szTempFile, MAX_PATH, szTempDir);
			_stprintf_s(szFile, MAX_PATH, _T("temp%d.lnk"), i);
			_tcscat_s(szTempFile, MAX_PATH, szFile);

			hFile = CreateFile(szTempFile,
			                   GENERIC_WRITE,
			                   0,
			                   nullptr,
			                   CREATE_NEW,
			                   FILE_ATTRIBUTE_NORMAL,
			                   nullptr);

			// Do not try this more than 100 times.
			if (i > 100)
			{
				return hilOut;
			}
		}

		// Close the file you just created.
		CloseHandle(hFile);
		hFile = INVALID_HANDLE_VALUE;

		// Get the PIDL for the directory.
		hr = pDesktop->ParseDisplayName(
			hWnd,
			nullptr,
			T2OLE(szTempDir),
			&dwEaten,
			itemTempDir.GetPtr(),
			&dwAttributes);

		if (SUCCEEDED(hr))
		{
			// Get the IShellFolder for the TEMP directory.
			hr = pDesktop->BindToObject(itemTempDir,
			                            nullptr,
			                            IID_IShellFolder,
			                            (LPVOID*)spsfTempDir.GetPtr());

			if (SUCCEEDED(hr))
			{
				// Get the IShellIconOverlay interface for this folder. If this fails,
				// it could indicate that you are running on a pre-Internet Explorer 4.0
				// shell, which doesn't support this interface. If this is the case, the
				// overlay icons are already in the system image list.
				hr = spsfTempDir->QueryInterface(IID_IShellIconOverlay, (LPVOID*)&spsio);

				if (SUCCEEDED(hr))
				{
					// Get the PIDL for the temporary .lnk file.
					hr = spsfTempDir->ParseDisplayName(
						hWnd,
						nullptr,
						T2OLE(szFile),
						&dwEaten,
						itemTempFile.GetPtr(),
						&dwAttributes);

					if (SUCCEEDED(hr))
					{
						// Get the overlay icon for the .lnk file. This causes the shell
						// to put all of the standard overlay icons into your copy of the system
						// image list.
						hr = spsio->GetOverlayIndex(itemTempFile, &nIndex);
					}
				}
			}
		}

		// Delete the temporary file.
		DeleteFile(szTempFile);
	}

	return hilOut;
}

HIMAGELIST Application::GetGlobalBitmap()
{
	static CImageList images;

	if (images == nullptr)
	{
		bool bhasAlpha = true;

		if (bhasAlpha)
		{
			// No ILC_MASK: on a masked list ILD_TRANSPARENT keys off the mask, and
			// an image added without one is fully opaque, so the alpha channel is
			// ignored and the glyphs get hard edges.
			images.Create(16, 16, ILC_COLOR32, 0, 1);

			CBitmap bmImage;
			bmImage.Attach(static_cast<HBITMAP>(LoadImage(GetBitmapResourceInstance(),
			                                              MAKEINTRESOURCE(IDB_COMMAND_TOOLBAR_ALPHA), IMAGE_BITMAP, 0, 0,
			                                              LR_DEFAULTSIZE | LR_CREATEDIBSECTION)));
			images.Add(bmImage);
		}
		else
		{
			images.Create(16, 16, ILC_COLOR32 | ILC_MASK, 0, 1);

			CBitmap bmImage;
			bmImage.LoadBitmap(IDB_COMMAND_TOOLBAR);
			images.Add(bmImage, RGB(0x81, 0x81, 0x81));
		}

		// Load the global image list
		// Load the image list
		//hImageList = ImageList_LoadImage(GetBitmapResourceInstance(), 
		//	MAKEINTRESOURCE(IDB_BITMAPS), 20, 1, RGB(0xff, 0x00, 0xff), 
		//	IMAGE_BITMAP, LR_CREATEDIBSECTION);
	}

	return images;
}

HINSTANCE Application::GetBitmapResourceInstance()
{
	return _AtlBaseModule.GetModuleInstance();
}

HINSTANCE Application::GetResourceInstance()
{
	return _AtlBaseModule.GetModuleInstance();
}

void Application::Log(const CString& str)
{
	IW::CAutoLockCS lock(_cs);

	IW::Logging::Write(_T("%s"), static_cast<LPCTSTR>(str));

	ATLTRACE(str);
	ATLTRACE(_T("\n"));
}

///////////////////////////////////////////////////////////////////////
/// Languages 

static LCID g_lLangId = MAKELCID(LANG_NEUTRAL, SORT_DEFAULT);

LCID Application::GetLangId()
{
	return g_lLangId;
}

void Application::SetLangId(LCID l)
{
	g_lLangId = l;
}


// Helper to invoke help
void Application::InvokeHelp(HWND hwnd, UINT nId)
{
	IW::CFilePath path;
	path.GetModuleFileName(nullptr);
	path.SetFileNameAndExtension(_T("ImageWalker"), _T("chm"));

	if (nId == 0)
	{
		HtmlHelp(hwnd, path, HH_DISPLAY_TOPIC, 0);
	}
	else
	{
		HtmlHelp(hwnd, path, HH_HELP_CONTEXT, nId);
	}
}


LPCTSTR Application::LoadString(UINT dwId)
{
	// Called from both workers, and this mutates the map and the pool
	IW::CAutoLockCS lock(_cs);

	auto it = m_mapStrings.find(dwId);

	if (it != m_mapStrings.end())
	{
		return it->second;
	}

	// Load it new
	LPCTSTR sz = _stringPool.LoadString(GetResourceInstance(), dwId);
	return m_mapStrings[dwId] = sz;
}

void Application::BuildMetaDataTypes()
{
	_metaDataTypes = {
		{IW::ePropertyName, LoadString(IDS_FILENAME)},
		{IW::ePropertyType, LoadString(IDS_FILETYPE)},
		{IW::ePropertySize, LoadString(IDS_FILESIZE)},
		{IW::ePropertyModifiedDate, LoadString(IDS_FILEMODIFIED)},
		{IW::ePropertyCreatedDate, LoadString(IDS_FILECREATED)},
		{IW::ePropertyPath, LoadString(IDS_FILEPATH)},
		{IW::ePropertyModifiedTime, LoadString(IDS_FILEMODIFIED_TIME)},
		{IW::ePropertyCreatedTime, LoadString(IDS_FILECREATED_TIME)},
		{IW::ePropertyWidth, LoadString(IDS_WIDTH)},
		{IW::ePropertyHeight, LoadString(IDS_HEIGHT)},
		{IW::ePropertyDepth, LoadString(IDS_DEPTH)},
		{IW::ePropertyTitle, LoadString(IDS_TITLE)},
		{IW::ePropertyObjectName, _T("Object Name")},
		{IW::ePropertyAperture, _T("Aperture")},
		{IW::ePropertyIsoSpeed, _T("Iso Speed")},
		{IW::ePropertyWhiteBalance, _T("White Balance")},
		{IW::ePropertyExposureTime, _T("Exposure Time")},
		{IW::ePropertyFocalLength, _T("Focal Length")},
		{IW::ePropertyDateTaken, _T("Date Taken")},
		{IW::ePropertyDescription, _T("Description")},
	};

	for (const auto &type : _metaDataTypes)
		m_MetaDataPropertyMap[type.Id] = type.Title;
}

CString Application::GetMetaDataTitle(DWORD dw)
{
	return m_MetaDataPropertyMap[dw];
}

CString Application::GetMetaDataShortTitle(DWORD dw)
{
	if (m_shortMetaDataPropertyMap.size() == 0)
	{
		m_shortMetaDataPropertyMap[IW::ePropertyName] = _T("Name");
		m_shortMetaDataPropertyMap[IW::ePropertyType] = _T("Type");
		m_shortMetaDataPropertyMap[IW::ePropertySize] = _T("Size");
		m_shortMetaDataPropertyMap[IW::ePropertyModifiedDate] = _T("Modified");
		m_shortMetaDataPropertyMap[IW::ePropertyCreatedDate] = _T("Created");
		m_shortMetaDataPropertyMap[IW::ePropertyPath] = _T("Path");
		m_shortMetaDataPropertyMap[IW::ePropertyModifiedTime] = _T("Modified");
		m_shortMetaDataPropertyMap[IW::ePropertyCreatedTime] = _T("Created");
		m_shortMetaDataPropertyMap[IW::ePropertyWidth] = _T("Width");
		m_shortMetaDataPropertyMap[IW::ePropertyHeight] = _T("Height");
		m_shortMetaDataPropertyMap[IW::ePropertyDepth] = _T("Depth");
		m_shortMetaDataPropertyMap[IW::ePropertyTitle] = _T("Title");
		m_shortMetaDataPropertyMap[IW::ePropertyObjectName] = _T("Object Name");
		m_shortMetaDataPropertyMap[IW::ePropertyAperture] = _T("Aperture");
		m_shortMetaDataPropertyMap[IW::ePropertyIsoSpeed] = _T("Iso");
		m_shortMetaDataPropertyMap[IW::ePropertyWhiteBalance] = _T("White Bal");
		m_shortMetaDataPropertyMap[IW::ePropertyExposureTime] = _T("Exposure");
		m_shortMetaDataPropertyMap[IW::ePropertyFocalLength] = _T("Focal Len");
		m_shortMetaDataPropertyMap[IW::ePropertyDateTaken] = _T("Taken");
		m_shortMetaDataPropertyMap[IW::ePropertyDescription] = _T("Description");
	}

	return m_shortMetaDataPropertyMap[dw];
}

CString Application::GetIWSFilter()
{
	static CString str;

	if (str.IsEmpty())
	{
		CString strAllFiles, strSettingFiles;

		strAllFiles.LoadString(IDS_ALL_FILES);
		strSettingFiles.LoadString(IDS_SETTING_FILES);

		str.Format(_T("%s (*.iws)|*.iws|%s (*.*)|*.*||"), static_cast<LPCTSTR>(strSettingFiles), static_cast<LPCTSTR>(strAllFiles));
		str.Replace('|', 0);
	}

	return str;
}


//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
// Memory and properties

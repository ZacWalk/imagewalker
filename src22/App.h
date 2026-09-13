// ImageWalker by Zac Walker
//
// Purpose: The Application singleton: options, the string and shell-icon
//          caches, the loader registry, logging and help.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once


class CImageMetaDataList;

struct MetaDataType
{
	DWORD Id;
	CString Title;
};

#define IDC_COMMAND_BAR		 (30000)
#define IDC_ANIMATE			 (30001)
#define IDC_VIEW_ADDRESS	 (30003)
#define IDC_VIEW_TOOLBAR	 (30004)
#define IDC_LOGO			 (30005)
#define IDC_PRINT		     (30006)
#define IDC_DELAYBAR		 (30009)
#define IDC_SCALEBAR		 (30010)
#define IDC_EDIT_TOOLBAR	 (30011)
#define ID_FOLDER_BAR		 (30023)

// PLugin host implementation
class Application
{
protected:


	typedef std::map<DWORD, LPCTSTR> MAPSTRINGS;
	MAPSTRINGS m_mapStrings;

	typedef std::map<UINT, int> MAPICONS;
	MAPICONS m_mapIcons;

	std::set<UINT> m_setCanBeCached;

	typedef std::map<int, CString> METADATAPROPERTYMAP;

	METADATAPROPERTYMAP m_MetaDataPropertyMap;
	METADATAPROPERTYMAP m_shortMetaDataPropertyMap;

	std::vector<MetaDataType> _metaDataTypes;

	void BuildMetaDataTypes();

	IW::StringPool _stringPool;

public:
	Application();
	virtual ~Application();

	void Init();

	// Cleanup Plugins
	void Free();

	// Read once in Init, written once in Free.
	AppSettings Settings;	

	bool CanBeCached(UINT uExtension);
	int GetIcon(DWORD dwExtension);
	void SetIcon(DWORD dwExtension, int nIcon);


	// Iterate possible meta data
	const std::vector<MetaDataType> &GetMetaDataTypes() const { return _metaDataTypes; }
	CString GetMetaDataTitle(DWORD dw);
	CString GetMetaDataShortTitle(DWORD dw);


	// Help and Strings
	LPCTSTR LoadString(UINT nId);
	void InvokeHelp(HWND hwnd, UINT nId);

	// Resource handeling
	HINSTANCE GetResourceInstance();
	HINSTANCE GetBitmapResourceInstance();

	// Debugging
	void Log(const CString &str);
	CString GetLog();

	// Resources
	HIMAGELIST GetGlobalBitmap();
	HIMAGELIST GetShellImageList(bool fSmall = false);

	CPalette m_paletteHalftone;	
	int m_nTextExtent;

	LCID GetLangId();
	void SetLangId(LCID l);

	CString GetIWSFilter();

	CCriticalSection _cs;

	bool IsTesting;
	bool ControlKeyDown;


	UINT GetExtensionKey(const CString &strFileName)
	{
		IW::CAutoLockCS lock(_cs);

		UINT uExtension = 0;
		CString strType = IW::Path::FindExtension(strFileName);
		strType.MakeUpper();

		if (!strType.IsEmpty())
		{	
			for(int i = 1; (i < 3) && strType[i]; i++)
			{
				TCHAR c = strType[i];
				uExtension = (uExtension << 8) + c;
			}
		}

		return uExtension;
	}
};

extern Application App;

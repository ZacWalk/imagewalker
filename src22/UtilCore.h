// ImageWalker by Zac Walker
//
// Purpose: Core helpers: paths, allocation, string conversion, message
//          boxes, critical sections and the shell wrappers.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "iw/imaging/UtilBase.h"
#include "iw/imaging/Imaging.h"
#include "iw/imaging/UtilFileTime.h"
#include "iw/imaging/CoreSupport.h"

// Pre defs
class _com_error;

struct _IMAGELIST;
typedef struct _IMAGELIST* HIMAGELIST;
typedef DWORD DROPEFFECT;

class CMainFrame;
class State;

extern State *g_pState;
extern CMainFrame *g_pMainWin;


namespace IW
{

	class Image;
	class CRender;


#define ID_PREVIEW_TOOLBAR 1025

#define ID_IMAGE_SCROLL 120
#define ID_IMAGE_SCROLLV 121
#define ID_FOLDER_VIEW 122
#define ID_SEARCH_VIEW 123
#define ID_FOLDER_VIEWBAR 124
#define ID_FOLDER_SCROLL 125
#define ID_FOLDER_HEADER 126
#define ID_ADDRESS 127
#define	IDC_RENAME 128

#define WM_CONTEXTSWITCH (WM_USER + 209)
#define WM_THUMBNAILING (WM_USER + 210)
#define WM_THUMBNAILING_COMPLETE (WM_USER + 211)
#define WM_SEARCHING (WM_USER + 212)
#define WM_SEARCHING_COMPLETE (WM_USER + 213)
#define WM_SEARCHING_FOLDER (WM_USER + 214)
#define WM_ADJUST_HEADER (WM_USER + 215)

#define WM_START (WM_USER + 301)

#define WM_GETFILENAME (WM_USER + 401)
#define WM_LOADCOMPLETE (WM_USER + 402)
#define WM_SCALECOMPLETE (WM_USER + 403)


#define IW_WS_CHILD (WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN)

	bool SimpleMatch(const CString &strFilter, const CString &strFileNameOrg);

	// Windows Version
	bool IsWindows95();
	bool IsWindows98();
	bool IsWindowsME();
	bool IsWindowsXP();
	bool IsWindowsVista();
	bool IsWindows2000();
	bool IsWindowsNT4();
	bool IsWindowsNT();
	bool IsWindowsXPOrBetter();
	bool IsWindowsAscii();

	// Render
	CWindow GetMainWindow();

	class startup_exception : public std::exception
	{	// base of all bad allocation exceptions
	public:
		startup_exception(const char *_Message = "Failed to start up") throw ()
			: exception(_Message)
		{	// construct from message string
		}

		virtual ~startup_exception() throw ()
		{	// destroy the object
		}
	};

	// IsEdit: a helper function to determine if a given CWnd pointer
	// points to a CEDit control.
	// Use the SDK ::GetClassName()

	inline bool IsEdit( HWND hWnd )
	{
		if (hWnd == NULL)
			return FALSE;

		TCHAR szClassName[6];
		return ::GetClassName(hWnd, szClassName, 6) &&
			_tcsicmp(szClassName, _T("Edit")) == 0;
	}

	static bool IsActive(HWND hWndParent, HWND hWnd)
	{
		return hWndParent == hWnd || ::IsChild(hWndParent, hWnd);
	};

	static bool HasVisibleStyle(HWND hWnd)
	{
		return hWnd != NULL &&
			((DWORD)::GetWindowLong(hWnd, GWL_STYLE) & WS_VISIBLE);
	}


	class Focus
	{
	protected:
		HWND _hFocus;
		HWND _hCapture;
	public:
		Focus()
		{
			_hFocus = ::GetFocus();
			_hCapture = ::GetCapture();
			if (_hCapture) ::ReleaseCapture();
		}

		~Focus()
		{
			if (_hCapture) ::SetCapture(_hCapture);
			::SetFocus(_hFocus);
		}
	};

	class ListViewItem : public LVITEM
	{
	public:
		ListViewItem()
		{
			IW::MemZero(static_cast<LVITEM*>(this), sizeof(LVITEM));
		}

		ListViewItem(int n)
		{
			IW::MemZero(static_cast<LVITEM*>(this), sizeof(LVITEM));
			mask = LVIF_PARAM;
			iItem = n;
		}

		ListViewItem(int n, void *lParamIn)
		{
			IW::MemZero(static_cast<LVITEM*>(this), sizeof(LVITEM));

			mask = LVIF_TEXT|LVIF_IMAGE|LVIF_PARAM;
			iItem = n;
			pszText = (LPTSTR)LPSTR_TEXTCALLBACK;
			lParam = (LPARAM)lParamIn;
		}

	};

	// CArrayDWORD Helper
	class CArrayDWORD : public CSimpleValArray<DWORD>
	{
	public:
		CArrayDWORD() {};
		virtual ~CArrayDWORD() {};

		CArrayDWORD(const CArrayDWORD &aIn)
		{
			Copy(aIn);
		}

		CArrayDWORD &operator=(const CArrayDWORD &aIn)
		{
			Copy(aIn);
			return *this;
		}

		void Copy(const CArrayDWORD &aIn)
		{
			RemoveAll();

			for(int i = 0; i < aIn.GetSize(); i++)
			{
				Add(aIn[i]);
			}
		}


		// Serialisation
		CString GetAsString() const
		{
			if (GetSize() == 0)
			{
				return g_szEmptyString;
			}

			CString str;

			for(int i = 0; i < GetSize(); i++)
			{
				str += IToStr(operator[](i));
				str += _T("-");
			}

			return str;
		}

		bool ParseFromString(const CString &str)
		{
			if (str.IsEmpty())
			{
				RemoveAll();
				return true;
			}

			bool bFirst = true;

			static const TCHAR szSeps[] = _T(" -");
			CString token;
			int curPos = 0;

			token = str.Tokenize(szSeps, curPos);
			while (!token.IsEmpty())
			{
				if (bFirst)
				{
					RemoveAll();
					bFirst = false;
				}

				DWORD dw = _ttol(token);
				Add(dw);

				token = str.Tokenize(szSeps, curPos);
			};


			return true;
		}
	};

	// CoInitialize Helper
	class CCoInit
	{
	public:

		CCoInit(bool bIsWorkerThead)
		{
			_hr = ::CoInitializeEx(NULL, bIsWorkerThead ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED);
			assert(SUCCEEDED(_hr));
		}

		~CCoInit()
		{
			if (SUCCEEDED(_hr))
				::CoUninitialize();
		};

	private:
		HRESULT _hr;
	};

	class  CMessageBoxIndirect : public MSGBOXPARAMS
	{
	public:

		CMessageBoxIndirect();
		static VOID CALLBACK ErrorBoxCallBack(LPHELPINFO lpHelpInfo);

		int Show(unsigned nId, unsigned nStyle = MB_ICONHAND | MB_OK | MB_HELP);
		int Show(const CString &str = _T("A serious error has occurred."), unsigned nStyle = MB_ICONHAND | MB_OK | MB_HELP);
		int Show(unsigned nIdFormat, const CString &strMessage, unsigned nStyle = MB_ICONHAND | MB_OK | MB_HELP);

		int ShowOsError(unsigned nId, int nError = GetLastError(), unsigned nStyle = MB_ICONHAND | MB_OK | MB_HELP);
		int ShowOsErrorWithFile(const CString &strFile, unsigned nId, int nError = GetLastError(), unsigned nStyle = MB_ICONHAND | MB_OK | MB_HELP);

		int ShowException(unsigned nId, _com_error &e, unsigned nStyle = MB_ICONHAND | MB_OK | MB_HELP);
		int ShowException(unsigned nId, std::exception &e, unsigned nStyle = MB_ICONHAND | MB_OK | MB_HELP);

	};

	///////////////////////////////////////////////////////////////
	//
	// CSearch
	//
	typedef enum tagSEARCH_EXPRESSION
	{
		seAND = 0,
		seOR = 1,
		seNOT = 2
	}
	SEARCH_EXPRESSION;

	class CSearchNode
	{
	private:

		CString _str;

		// The match is case-insensitive and runs per node per candidate field per
		// item, so the pattern is upper-cased once, when the text is set.
		CString _strUpper;
		CString _strUpperWild;

		void UpdateMatchPattern();

	public:

		DWORD _se;

		CSearchNode();
		CSearchNode(DWORD se, const CString &str, bool bNot = false);
		CSearchNode(const CSearchNode &sn);

		const CString &GetText() const { return _str; }
		const CString &GetMatchPattern(bool bSubString) const { return bSubString ? _strUpperWild : _strUpper; }

		void SetText(const CString &str);

		bool ParseFromString(const CString &strPrefix, const CString &strText);
	};

	class CSearchNodeList
	{
	protected:
		CSimpleArray<CSearchNode> _children;

	public:
		CSearchNodeList();
		virtual ~CSearchNodeList();

		CSearchNodeList(const CSearchNodeList &ss);
		void operator=(const CSearchNodeList &ss);
		void Copy(const CSearchNodeList &ss);

		void operator=(const CString &strSource) { ParseFromString(strSource); };

		bool ParseFromString(const CString &strSource);
		bool ParseFromString(DWORD se, const CString &str);
		bool ParseFromString(const CString &strNOT, const CString &strAND, const CString &strOR);

		bool Match(const CString &strSource, bool bMatchSubString = false) const;

		bool Format(CString &strOut);
		bool Format(CString &strNOT, CString &strAND, CString &strOR);

		bool IsEmpty() const { return (_children.GetSize() == 0); };
	};

	class CAutoLockCS
	{

	public:

		CAutoLockCS(CCriticalSection &lock) : _lock(lock)
		{
			_lock.Enter();
		}

		~CAutoLockCS()
		{
			_lock.Leave();
		}

		// Implementation
	private:
		CCriticalSection &_lock;

		// Private to prevent accidental use
		CAutoLockCS( const CAutoLockCS& ) throw();
		CAutoLockCS& operator=( const CAutoLockCS& ) throw();
	};


	///////////////////////////////////////////////////////////////////////
	//
	// CAutoLockRawCS
	// For a plain CRITICAL_SECTION, such as ATL's module locks
	//

	class CAutoLockRawCS
	{

	public:

		CAutoLockRawCS(CRITICAL_SECTION &lock) : _lock(lock)
		{
			::EnterCriticalSection(&_lock);
		}

		~CAutoLockRawCS()
		{
			::LeaveCriticalSection(&_lock);
		}

		// Implementation
	private:
		CRITICAL_SECTION &_lock;

		// Private to prevent accidental use
		CAutoLockRawCS( const CAutoLockRawCS& ) throw();
		CAutoLockRawCS& operator=( const CAutoLockRawCS& ) throw();
	};


	class FileOperation
	{
	private:
		SHFILEOPSTRUCT _struct;
		IW::AutoFreePtr<TCHAR> szFiles;
		IW::AutoFreePtr<TCHAR> szTo;

		// SHFILEOPSTRUCT requires a double-null-terminated list
		static LPTSTR AllocList(LPCTSTR szIn)
		{
			const size_t nLen = _tcslen(szIn);
			LPTSTR sz = static_cast<LPTSTR>(IW::Alloc((nLen + 2) * sizeof(TCHAR)));
			_tcscpy_s(sz, nLen + 1, szIn);
			sz[nLen] = 0;
			sz[nLen + 1] = 0;
			return sz;
		}

	public:
		FileOperation(const CString &strFiles)
		{
			IW::MemZero(&_struct, sizeof(_struct));
			_struct.hwnd = IW::GetMainWindow();

			TCHAR szDelim = _T('\n');
			LPTSTR sz = AllocList(strFiles);

			for(int n = 0; sz[n] != 0; n++)
			{
				if (sz[n] == szDelim)
				{
					sz[n] = 0;
				}
			}

			szFiles.reset(sz);
			_struct.pFrom = sz;
		}

		bool Delete(bool bAllowUndo)
		{
			_struct.wFunc = FO_DELETE;
			_struct.fFlags = bAllowUndo ? FOF_ALLOWUNDO : 0;

			return 0 == SHFileOperation(&_struct);
		}

		bool Copy(const CString &strDestFolder, bool bMove)
		{
			szTo.reset(AllocList(strDestFolder));

			_struct.wFunc = bMove ? FO_MOVE : FO_COPY;
			_struct.pTo = szTo.get();
			_struct.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMMKDIR;

			return 0 == SHFileOperation(&_struct);
		}

		// The shell asks its own confirmation. Answering No is not a failure,
		// and telling the user it was is worse than saying nothing.
		bool WasAborted() const
		{
			return _struct.fAnyOperationsAborted != FALSE;
		}
	};

	// Not a lock: it marks a scope, so the flag must have exactly one writer
	class ScopeLockedBool
	{
	private:
		volatile bool &_b;

		ScopeLockedBool( const ScopeLockedBool& );
		ScopeLockedBool& operator=( const ScopeLockedBool& );
	public:
		ScopeLockedBool(volatile bool &b) : _b(b)
		{
			_b = true;
		}

		~ScopeLockedBool()
		{
			_b = false;
		}

	};

	class CLockWindowUpdate
	{
	public:
		CWindow _wnd;

		CLockWindowUpdate(CWindow wnd) : _wnd(wnd)
		{
			_wnd.LockWindowUpdate(true);
		}

		~CLockWindowUpdate()
		{
			_wnd.LockWindowUpdate(false);
		}
	};


	inline bool NavigateToWebPage(const CString &url)
	{
		return ShellExecute(IW::GetMainWindow(),
			_T("open"),
			url,
			NULL,
			NULL,
			SW_SHOW) > (HINSTANCE)32;
	}

	inline int SetItem(CComboBox ctrl, int nNumber)
	{
		CString str;
		str.Format(_T("%4d"), nNumber);

		int nSel = ctrl.FindString(-1, str);

		if (CB_ERR  == nSel)
		{
			return ctrl.AddString(str);
		}

		return nSel;
	}

	inline void SetItem(CComboBox combo, const CString &str, UINT nFlags)
	{
		combo.SetItemData(combo.AddString(str), nFlags);
	}

	inline void SetItems(CComboBox ctrl, int values[], int nDefault)
	{
		for(int i = 0; values[i] != -1; i++)
		{
			IW::SetItem(ctrl, values[i]);
		}

		int nSel = IW::SetItem(ctrl, nDefault);
		ctrl.SetCurSel(nSel);
	}


	inline CString Format(UINT nFormatID, ... )
	{
		CString str;
		CString strFormat;
		ATLVERIFY( strFormat.LoadString( nFormatID ) );

		va_list argList;
		va_start( argList, nFormatID );
		str.FormatV( strFormat, argList );
		va_end( argList );

		return str;
	}

} // namespace IW

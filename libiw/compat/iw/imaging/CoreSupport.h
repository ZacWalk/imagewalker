#pragma once

namespace IW
{
	enum class SimdLevel { Scalar, Sse2, Avx2 };
	SimdLevel GetSimdLevel();


	inline int Clamp(const int v, const int l, const int h)
	{
		return v < l ? l : (v > h ? h : v);
	}

	inline CRect ClampRect(const CRect &rectIn, const CRect &rectLimit)
	{
		CSize offset(0,0);
		CRect rectOut = rectIn;

		if (rectOut.top < rectLimit.top)
			offset.cy = rectLimit.top - rectOut.top;

		if (rectOut.left < rectLimit.left)
			offset.cx = rectLimit.left - rectOut.left;

		if (rectOut.bottom > rectLimit.bottom)
			offset.cy = rectLimit.bottom - rectOut.bottom;

		if (rectOut.right > rectLimit.right)
			offset.cx = rectLimit.right - rectOut.right;

		rectOut.OffsetRect(offset);

		return rectOut;
	}

	inline CRect ClipRect(CRect &rectIn, const CRect &rectLimit)
	{
		CRect rectOut(rectIn);

		if (rectIn.top < rectLimit.top)
			rectOut.top = rectLimit.top;

		if (rectIn.left < rectLimit.left)
			rectOut.left = rectLimit.left;

		if (rectIn.bottom > rectLimit.bottom)
			rectOut.bottom = rectLimit.bottom;

		if (rectIn.right > rectLimit.right)
			rectOut.right = rectLimit.right;

		return rectOut;
	}

	inline CRect MulDivRect(const CRect &rect, const CSize &sizeNumerator, const CSize &sizeDenominator)
	{
		return CRect(
			MulDiv(rect.left, sizeNumerator.cx, sizeDenominator.cx),
			MulDiv(rect.top, sizeNumerator.cy, sizeDenominator.cy),
			MulDiv(rect.right, sizeNumerator.cx, sizeDenominator.cx),
			MulDiv(rect.bottom, sizeNumerator.cy, sizeDenominator.cy));
	}

	// Delete helpers
	struct delete_object
	{
		template <typename T>
		void operator()(T *ptr){ delete ptr;}
	};

	// Stl Helpers
	template<typename InputIterator, typename OutputIterator, typename Predicate>
	OutputIterator copy_if(InputIterator begin, InputIterator end, OutputIterator destBegin, Predicate p)
	{
		while (begin != end){
			if(p(*begin)) *destBegin++ = *begin;
			++begin;
		}
		return destBegin;
	}

	template<typename InputIterator, typename OutputIterator, typename Predicate>
	OutputIterator copy_ref_if(InputIterator begin, InputIterator end, OutputIterator destBegin, Predicate p)
	{
		while (begin != end){
			if(p(*begin)) *destBegin++ = **begin;
			++begin;
		}
		return destBegin;
	}

	inline bool IsNullOrEmpty(LPCTSTR sz)
	{
		return sz == 0 || sz[0] == 0;
	}

	/////////////////////////////////////////////////////////////////////////
	/// Strings

	CString TextToHtmlFriendly(const CString &strIn, int nLength = -1);
	CString HtmlFriendlyToText(const CString &strIn, int nLength = -1);
	CString MakeURLSafe(const CString &strIn);
	CString MakeURLUnSafe(const CString &strIn);

	/////////////////////////////////////////////////////////////////////////
	/// Exception

	class invalid_file : public std::exception
	{	// base of all bad allocation exceptions
	public:
		invalid_file(const char *_Message = "Invalid File") throw ()
			: exception(_Message)
		{	// construct from message string
		}

		virtual ~invalid_file() throw ()
		{	// destroy the object
		}
	};


	class CFilePath
	{
	protected:
		CString _strPath;

	public:
		CFilePath()
		{
		}

		CFilePath(const CString &str) : _strPath(str)
		{
		}

		CFilePath(const CFilePath &other) : _strPath(other._strPath)
		{
		}

		void operator+=(const CString &str)
		{
			if (!_strPath.IsEmpty()) TerminateFolderPath();
			_strPath += str;
		}

		void operator=(const CString &str)
		{
			_strPath = str;
		}

		void operator=(const CFilePath &other)
		{
			_strPath = other._strPath;
		}

		bool operator==(const CFilePath &other) const
		{
			CFilePath path1(*this);
			CFilePath path2(other);

			path1.NormalizeForCompare();
			path2.NormalizeForCompare();

			return path1._strPath == path2._strPath;
		}

		const CString &ToString() const
		{
			return _strPath;
		}

		void CopyTo(LPTSTR sz, int nMax)
		{
			_tcsncpy_s(sz, nMax, _strPath, _TRUNCATE);
		}

		bool Exists() const
		{
			return PathFileExists(_strPath) != 0;
		}

		void GetModuleFileName(HMODULE hModule)
		{
			::GetModuleFileName(hModule, _strPath.GetBuffer(MAX_PATH), MAX_PATH);
			_strPath.ReleaseBuffer();
		}

		void GetTempFilePath()
		{
			static UINT uUnique = (UINT)time(NULL);

			TCHAR szTempPath[MAX_PATH];
			TCHAR szTempFileName[MAX_PATH];

			::GetTempPath(_MAX_PATH, szTempPath);
			::GetTempFileName(szTempPath, _T("IW"), ++uUnique, szTempFileName);

			_strPath = szTempFileName;
		}


		bool IsTerminated() const
		{
			int nLast = _strPath.GetLength() - 1;

			return  (nLast >= 0) &&
				(_strPath[nLast] == _T('\\') ||
				_strPath[nLast] == _T('/'));
		}

		void TerminateFolderPath()
		{
			if (!IsTerminated())
			{
				_strPath += _T('\\');
			}
		}

		void RemoveTermination()
		{
			if (IsTerminated())
			{
				// Truncate, not SetAt: SetAt would leave the length unchanged
				// and an embedded NUL as the last character.
				_strPath.Truncate(_strPath.GetLength() - 1);
			}
		}

		void MakeUnixPath()
		{
			_strPath.Replace(_T('\\'), _T('/'));
		}

		void MakeDosPath()
		{
			_strPath.Replace(_T('/'), _T('\\'));
		}

		void GetCurrentDirectory()
		{
			::GetCurrentDirectory(MAX_PATH, _strPath.GetBuffer(MAX_PATH));
			_strPath.ReleaseBuffer();
		}


		void RemoveIllegalFromFileName()
		{
			TCHAR szFileName[_MAX_FNAME];
			TCHAR szExt[_MAX_EXT];
			TCHAR szDrive[_MAX_DRIVE];
			TCHAR szDir[_MAX_DIR];

			_tsplitpath_s( _strPath, szDrive, countof(szDrive), szDir, countof(szDir), szFileName, countof(szFileName), szExt, countof(szExt));

			RemoveIllegalFromFileName(szFileName);
			RemoveIllegalFromFileName(szExt);

			MakePath(szDrive, szDir, szFileName, szExt);
		}


		void MakePath(LPCTSTR szDrive, LPCTSTR szDir, LPCTSTR szFileName, LPCTSTR szExt)
		{
			_tmakepath_s(_strPath.GetBuffer(MAX_PATH), MAX_PATH, szDrive, szDir, szFileName, szExt);
			_strPath.ReleaseBuffer();
		}

		void RemoveIllegalFromFileName(LPTSTR sz)
		{
			// remove chars we dont like
			while(*sz != 0)
			{
				switch(*sz)
				{
				case '\'':
				case '\"':
				case ' ':
				case '?':
				case '&':
					*sz = '_';
					break;
				default:
					break;
				}

				sz++;
			}
		}

		CString GetFileName() const
		{
			TCHAR szFileName[_MAX_FNAME];
			_tsplitpath_s(_strPath, NULL, 0, NULL, 0, szFileName, _MAX_FNAME, NULL, 0);
			return szFileName;
		}

		void SetExtension(LPCTSTR szExt)
		{
			TCHAR szFileName[_MAX_FNAME];
			TCHAR szDrive[_MAX_DRIVE];
			TCHAR szDir[_MAX_DIR];

			_tsplitpath_s(_strPath, szDrive, countof(szDrive), szDir, countof(szDir), szFileName, countof(szFileName), NULL, 0);
			MakePath(szDrive, szDir, szFileName, szExt);
		}

		void SetFileNameAndExtension(LPCTSTR szFileName, LPCTSTR szExt)
		{
			TCHAR szDrive[_MAX_DRIVE];
			TCHAR szDir[_MAX_DIR];

			_tsplitpath_s(_strPath, szDrive, countof(szDrive), szDir, countof(szDir), NULL, 0, NULL, 0);
			MakePath(szDrive, szDir, szFileName, szExt);
		}


		void NormalizeForCompare()
		{
			RemoveTermination();
			MakeDosPath();
			MakeUpper();
		}

		void Normalize(bool bIsFolder)
		{
			if (bIsFolder)
			{
				TerminateFolderPath();
			}

			MakeUnixPath();
		}

		void RemoveFileName()
		{
			TCHAR szDrive[_MAX_DRIVE];
			TCHAR szDir[_MAX_DIR];

			_tsplitpath_s(_strPath, szDrive, countof(szDrive), szDir, countof(szDir), NULL, 0, NULL, 0);
			MakePath(szDrive, szDir, NULL, NULL);
		}

		void StripToFilename()
		{
			TCHAR szFileName[_MAX_FNAME];
			_tsplitpath_s(_strPath, NULL, 0, NULL, 0, szFileName, countof(szFileName), NULL, 0);
			_strPath = szFileName;
		}

		void StripToExtension()
		{
			CString str = PathFindExtension(_strPath);
			_strPath = str;
		}

		void StripToFilenameAndExtension()
		{
			CString str = PathFindFileName(_strPath);
			_strPath = str;
		}

		void StripToPath()
		{
			TCHAR szDrive[_MAX_DRIVE];
			TCHAR szDir[_MAX_DIR];

			_tsplitpath_s(_strPath, szDrive, countof(szDrive), szDir, countof(szDir), NULL, 0, NULL, 0);
			MakePath(szDrive, szDir, NULL, NULL );
		}


		void Prefix(const CString &strPre)
		{
			_strPath = strPre + _strPath;
		}



		void MakeLower()
		{
			_strPath.MakeLower();
		}

		void MakeUpper()
		{
			_strPath.MakeUpper();
		}

		static bool CreateAllDirectories(const CString &strFolder)
		{
			TCHAR sz[MAX_PATH], szCurrent[MAX_PATH];
			_tcscpy_s(sz, MAX_PATH, strFolder);

			int n = _tcsclen(sz) - 1;

			if (n < 3)
				return true;

			// remove ending / if exists
			while(n > 0 && (sz[n] == '\\' || sz[n] == '/'))
				sz[n--] = 0;

			_tcscpy_s(szCurrent, MAX_PATH, sz);

			// base case . . .if directory exists
			if(GetFileAttributes(sz)!=-1)
				return true;

			// remove ending folder
			// recursive call, one less directory
			while(n > 0 && sz[n] != '\\' && sz[n] != '/')
				sz[n--] = 0;

			if (!CreateAllDirectories(sz))
				return false;

			// actual work
			return CreateDirectory(szCurrent,NULL) != 0;
		}

		static bool CheckFileName(const CString &strFileNameIn)
		{
			TCHAR szName[_MAX_FNAME];
			_tsplitpath_s(strFileNameIn, NULL, 0, NULL, 0, szName, countof(szName), NULL, 0);

			if (_tcsclen(szName) > 215)
			{
				return false;
			}

			static TCHAR szIllegal[] = _T("\\/:*?\"<>|");

			for (int i = 0; szIllegal[i] != 0; i++)
			{
				if (_tcschr(szName, szIllegal[i]) != NULL)
					return false;
			}

			return true;
		}

		bool CreateAllDirectories() { return CreateAllDirectories(_strPath); };

		operator LPCTSTR() const
		{
			return _strPath;
		};

		operator const CString&() const
		{
			return _strPath;
		};

		operator CString&()
		{
			return _strPath;
		};
	};








	//////////////////////////////////////////////////////////////////////////////
	// Template Helpers

	template<class T> CComPtr<T> CreateComObject()
	{
		CComObject<T> *p;
		p = new CComObject<T>;
		if (p == NULL) throw std::bad_alloc();

		p->SetVoid(NULL);
		p->InternalFinalConstructAddRef();

		HRESULT hRes = p->FinalConstruct();

		if (SUCCEEDED(hRes))
		{
			hRes = p->_AtlFinalConstruct();
		}

		p->InternalFinalConstructRelease();

		if (FAILED(hRes))
		{
			assert("CreateComObject Failed");

			delete p;
			p = NULL;
			throw std::bad_alloc();
		}

		return CComPtr<T>(p);
	}


	///////////////////////////////////////////////////////////////////////
	//
	// CAutoLock
	// Normally used to lock a CCriticalSection
	//


	class CNullStatus : public IStatus
	{
	public:
		// Iterate Properties
		void SetMessage(const CString &strMessage) { };
		void SetWarning(const CString &strWarning) { };
		void SetError(const CString &strError) { };
		void SetContext(const CString &strContext) { };
		void Progress(int nCurrentStep, int TotalSteps) { };
		bool QueryCancel() { return false; };
		void SetStatusMessage(const CString &strMessage) { };
		void SetHighLevelProgress(int nCurrentStep, int TotalSteps) { };
		void SetHighLevelStatusMessage(const CString &strMessage) { };

		static CNullStatus *Instance;
	};

	class Path
	{
	public:
		static CString Combine(const CString &strFolder, const CString &strFile)
		{
			if (strFolder.IsEmpty())
				return strFile;

			CString strResult = strFolder;
			const TCHAR chLast = strResult[strResult.GetLength() - 1];

			if (chLast != _T('\\') && chLast != _T('/'))
			{
				strResult += _T('\\');
			}

			strResult += strFile;
			return strResult;
		}

		static bool Compare(const CFilePath &path1, const CFilePath &path2)
		{
			return path1 == path2;
		}

		static LPCTSTR FindExtension(const CString &strPath)
		{
			return PathFindExtension(strPath);
		}

		static LPCTSTR FindFileName(const CString &strPath)
		{
			return PathFindFileName(strPath);
		}

		static bool FileExists(const CString &strPath)
		{
			return PathFileExists(strPath) != 0;
		}

		static bool IsDirectory(const CString &strPath)
		{
			return PathIsDirectory(strPath) != 0;
		}
	};


	class FileSize
	{
	private:
		__int64 _size;

	public:

		FileSize(): _size(0)
		{
		}

		FileSize(DWORD nHigh, DWORD nLow) : _size((((__int64)nHigh) << 32 ) + nLow)
		{
		}

		FileSize(const FileSize &other): _size(other._size)
		{
		}

		// The store is __int64; taking only an unsigned here truncated anything
		// past 4GB, which is what the status bar's memory figure ran into.
		FileSize(unsigned __int64 nSize): _size(nSize)
		{
		}

		void operator=(const FileSize &other)
		{
			_size = other._size;
		}

		void operator=(unsigned __int64 nSize)
		{
			_size = nSize;
		}

		void operator+=(const FileSize &other)
		{
			_size += other._size;
		}

		operator int() const
		{
			return (int)_size;
		}

		static FileSize FromFile(const CString &strFileName)
		{
			WIN32_FIND_DATA   ff32; MemZero(&ff32, sizeof(ff32));

			if (!IW::IsNullOrEmpty(strFileName))
			{
				HANDLE hFind = FindFirstFile(strFileName, &ff32);

				if (INVALID_HANDLE_VALUE != hFind)
				{
					FindClose(hFind);
				}
			}

			return FileSize(ff32.nFileSizeHigh, ff32.nFileSizeLow);
		}

		static FileSize FromHandle(HANDLE hFile)
		{
			assert(hFile != INVALID_HANDLE_VALUE);

			DWORD nHigh = 0;
			DWORD nLow = ::GetFileSize(hFile, &nHigh);

			return FileSize(nHigh, nLow);
		}

		CString ToString(LCID locale = LOCALE_USER_DEFAULT) const
		{
			static const unsigned __int64 nMB = 1024 * 1024;
			static const unsigned __int64 nKB = 1024;

			LPCTSTR szUnitType = _T(" kb");
			unsigned __int64 divisor = nKB;

			// KB or MB
			if (_size > nMB)
			{
				divisor = nMB;
				szUnitType = _T(" mb");
			}

			CString str;
			str.Format(_T("%I64u.%02I64u"), _size / divisor, ((_size % divisor) * 100) / divisor);

			static const int nBufferSize = 128;
			TCHAR szFileSize[nBufferSize];

			if (::GetNumberFormat(locale, 0, str, NULL, szFileSize, nBufferSize) > 0)
			{
				str = szFileSize;
			}

			str += szUnitType;
			return str;
		}
	};



	inline CString Format(LPCTSTR szFormat, ... )
	{
		CString str;
		va_list argList;
		va_start( argList, szFormat );
		str.FormatV( szFormat, argList );
		va_end( argList );
		return str;
	}

	inline void AddToList(CString &list, const CString &item)
	{
		if (!list.IsEmpty()) list += _T(", ");
		list += item;
	}

	inline CSize Half(const CSize &size)
	{
		return CSize(size.cx / 2, size.cy / 2);
	}

	inline int Half(const int n)
	{
		return n / 2;
	}

}

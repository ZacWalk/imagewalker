// ImageWalker by Zac Walker
//
// Purpose: The stream classes the formats read and write through, plus
//          IW::CFile and the write-to-temp-then-rename CFileTemp.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "UtilBase.h"
#include "Imaging.h"

namespace IW
{
	//////////////////////////////////////////////////////////////////////////////////
	/// StreamBlob : Abstract base class to allow loading from a BLOB
	class StreamConstBlob : public IStreamIn
	{
	protected:
		LPCBYTE m_pBytes;
		FileSize m_nSize;
		DWORD _nPosition;

		bool m_bDelete;

	public:

		StreamConstBlob(LPCBYTE pBytes, const FileSize &size)
		{
			m_pBytes = pBytes;
			_nPosition = 0;
			m_nSize = size;
			m_bDelete = false;
		}

		template<class TBlob>
		StreamConstBlob(const TBlob &blob)
		{
			m_pBytes = blob.GetData();
			_nPosition = 0;
			m_nSize = blob.GetDataSize();
			m_bDelete = false;
		}

		~StreamConstBlob()
		{
			if (m_bDelete && m_pBytes)
			{
				IW::Free((LPVOID)m_pBytes);
			}
		}

		bool Read(LPVOID lpBuf, DWORD nCount, LPDWORD pdwRead = 0)
		{
			UINT nAbsoluteSize = min(nCount, m_nSize - _nPosition);
			IW::MemCopy(lpBuf, m_pBytes + _nPosition, nAbsoluteSize);
			_nPosition += nAbsoluteSize;

			if (pdwRead) *pdwRead = nAbsoluteSize;

			return nAbsoluteSize > 0;
		}




		DWORD Seek(ePosition ePos, LONG lOff)
		{
			UINT nPosNew = _nPosition;

			switch(ePos)
			{
			case IW::IStreamCommon::eBegin:
				nPosNew = lOff;
				break;
			case IW::IStreamCommon::eEnd:
				nPosNew = m_nSize - lOff;
				break;
			default:
				// Unknown seek?
				assert(0);
			case IW::IStreamCommon::eCurrent:
				nPosNew += lOff;
				break;
			}

			return _nPosition = Clamp(nPosNew,0,m_nSize-1);
		}

		bool Flush()
		{
			return true;
		}

		FileSize GetFileSize()
		{
			return m_nSize;
		}


		CString GetFileName() { return g_szEmptyString; };
		bool Abort() { return true; };
		bool Close(IW::IStatus *) { return true; };

	};


	template<class TBlob>
	class StreamBlob :
		public IStreamIn,
		public IStreamOut
	{
	protected:
		TBlob &m_blob;
		DWORD _nPosition;
		FileSize m_nSize;

	public:

		StreamBlob(TBlob &blob) : m_blob(blob)
		{
			_nPosition = 0;
			m_nSize = m_blob.GetDataSize();
		}

		~StreamBlob()
		{
		}

		bool Read(LPVOID lpBuf, DWORD nCount, LPDWORD pdwRead = 0)
		{
			UINT nAbsoluteSize = min(nCount, GetFileSize() - _nPosition);

			if (nAbsoluteSize)
			{
				IW::MemCopy(lpBuf, GetData() + _nPosition, nAbsoluteSize);
				_nPosition += nAbsoluteSize;
			}

			if (pdwRead) *pdwRead = nAbsoluteSize;

			return nAbsoluteSize > 0;
		}

		bool Write(LPCVOID lpBuf, DWORD nCount, LPDWORD pdwWritten = 0)
		{
			UINT nNewSize = IW::Max(_nPosition + nCount, GetFileSize());
			ReAlloc(nNewSize);

			IW::MemCopy((LPBYTE)GetData() + _nPosition, lpBuf, nCount);
			_nPosition += nCount;
			m_nSize = nNewSize;

			if (pdwWritten) *pdwWritten = nCount;

			return true;
		}

		void ReAlloc(int nRequired)
		{
			if( m_blob.GetDataSize() < (unsigned)nRequired )
			{
				int nAllocate = m_blob.GetDataSize();
				const int nStep = 1024 * 1024;

				if( nAllocate > nStep )
				{
					nAllocate += nStep;
				}
				else
				{
					nAllocate *= 2;
				}

				if( nAllocate < nRequired )
				{
					nAllocate = nRequired;
				}

				m_blob.ReAlloc(nAllocate);
			}
		}

		DWORD Seek(ePosition ePos, LONG lOff)
		{
			UINT nPosNew = _nPosition;

			switch(ePos)
			{
			case IW::IStreamCommon::eBegin:
				nPosNew = lOff;
				break;
			case IW::IStreamCommon::eEnd:
				nPosNew = GetFileSize() - lOff;
				break;
			default:
				// Unknown seek?
				assert(0);
			case IW::IStreamCommon::eCurrent:
				nPosNew += lOff;
				break;
			}

			_nPosition = nPosNew;

			UINT nNewSize = IW::Max(_nPosition, GetFileSize());
			ReAlloc(nNewSize);

			return _nPosition;
		}

		bool Flush()
		{
			return true;
		}

		
		CString GetFileName() { return g_szEmptyString; };

		bool Abort() { return true; };

		bool Close(IW::IStatus *) 
		{
			m_blob.ReAlloc(m_nSize);			
			return true; 
		};

		inline FileSize GetFileSize() { return m_nSize; };
		inline LPCBYTE GetData() const { return m_blob.GetData(); };
	};


	//////////////////////////////////////////////////////////////////////////////////
	/// Stream : Abstract base class to allow loading from resource files
	class StreamResource : public StreamConstBlob
	{
	protected:
		HRSRC m_hrsrc;
		HGLOBAL m_hg;

	public:

		StreamResource(HINSTANCE hInstance, const int nID) : StreamConstBlob(0, 0)
		{

			m_hrsrc = ::FindResource(hInstance, MAKEINTRESOURCE(nID), _T("IMAGE"));

			if (!m_hrsrc) 
			{
				throw std::exception("Failed to load bitmap resource"); 
			}

			m_hg = ::LoadResource(hInstance, m_hrsrc);

			if (!m_hg) 
			{
				throw std::exception("Failed to load bitmap resource");
			}

			m_pBytes = (LPCBYTE)::LockResource(m_hg);
			_nPosition = 0;
			m_nSize = SizeofResource(hInstance, m_hrsrc);
		}

		~StreamResource()
		{
			//::UnlockResource(hg);
			::FreeResource(m_hg);
		}
	};




	///////////////////////////////////////////////////////////////
	//
	// Class to wrapper file API
	//
	class CFile :
		public IStreamIn,
		public IStreamOut

	{
	protected:
		HANDLE _hFile;
		bool m_bCloseOnDelete;
		CString _strFileName;

	public:

		operator HFILE() const { return (HFILE)_hFile; };


		CFile()
		{
			_hFile = INVALID_HANDLE_VALUE;
			m_bCloseOnDelete = FALSE;
		}


		~CFile()
		{
			if (_hFile != INVALID_HANDLE_VALUE && m_bCloseOnDelete)
				Close(0);
		}

		bool OpenForWrite(const CString &strFileName)
		{
			return Open(strFileName, GENERIC_WRITE, 0, CREATE_ALWAYS);
		}

		bool OpenForRead(const CString &strFileName)
		{
			return Open(strFileName, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING);
		}

	protected:
		bool Open(const CString &strFileName, DWORD dwAccess, DWORD dwShareMode, DWORD dwCreateFlag)
		{

			m_bCloseOnDelete = FALSE;
			_hFile = INVALID_HANDLE_VALUE;		

			// attempt file creation
			HANDLE hFile = ::CreateFile(strFileName, dwAccess, dwShareMode, NULL, dwCreateFlag, 0, NULL);

			if (hFile == INVALID_HANDLE_VALUE)
			{
				return FALSE;
			}

			_hFile = hFile;
			m_bCloseOnDelete = TRUE;
			_strFileName = strFileName;

			return TRUE;
		}

	public:

		// String helpers
		void WriteString(const CStringA &str)
		{
			int nSize = str.GetLength();

			Write(&nSize, sizeof(nSize));

			if (nSize == 0)
				return;

			Write(str, nSize);
		}

		bool ReadString(CStringA &str)
		{
			UINT nSize = str.GetLength();

			if (!Read(&nSize, sizeof(nSize)))
				return false;

			if (nSize == 0)
			{
				str.Empty();
			}
			else
			{
				LPSTR p = str.GetBufferSetLength(nSize + 1);

				if (!Read(p, nSize))
				{
					return false;
				}

				p[nSize] = 0;
			}

			return true;
		}

		bool Read(LPVOID lpBuf, DWORD nCount, LPDWORD pdwRead = 0)
		{
			assert(_hFile != INVALID_HANDLE_VALUE);

			if (nCount == 0)
				return true;   // avoid Win32 "null-read"

			assert(lpBuf != NULL);			

			DWORD dw;
			if (pdwRead == 0) pdwRead = &dw;

			return ::ReadFile(_hFile, lpBuf, nCount, pdwRead, NULL) != 0;
		}

		bool Write(LPCVOID lpBuf, DWORD nCount, LPDWORD pdwWritten = 0)
		{
			assert(_hFile != INVALID_HANDLE_VALUE);

			if (nCount == 0)
				return true;     // avoid Win32 "null-write" option

			assert(lpBuf != NULL);

			DWORD dw;
			if (pdwWritten == 0) pdwWritten = &dw;

			return ::WriteFile(_hFile, lpBuf, nCount, pdwWritten, NULL) != 0;
		}

		DWORD Seek(ePosition ePos, LONG offset)
		{
			DWORD dwMoveMethod = FILE_BEGIN;

			/* we use this as a special code, so avoid accepting it */
			if( offset == 0xFFFFFFFF )
				return 0xFFFFFFFF;

			switch(ePos)
			{
			case IStreamIn::eCurrent:
				dwMoveMethod = FILE_CURRENT;
				break;

			case IStreamIn::eEnd:
				dwMoveMethod = FILE_END;
				break;

			default:
				dwMoveMethod = FILE_BEGIN;
				break;
			}

			LARGE_INTEGER liMove, liNew;
			liMove.QuadPart = offset;

			if (!::SetFilePointerEx(_hFile, liMove, &liNew, dwMoveMethod))
				return 0xFFFFFFFF;

			return static_cast<DWORD>(liNew.QuadPart);
		};

		bool Flush()
		{
			if (_hFile == INVALID_HANDLE_VALUE)
				return false;

			return ::FlushFileBuffers(_hFile) != 0;
		}

		bool Close(IStatus *pStatus)
		{

			assert(_hFile != INVALID_HANDLE_VALUE);

			BOOL bError = FALSE;
			if (_hFile != INVALID_HANDLE_VALUE)
				bError = !::CloseHandle(_hFile);

			_hFile = INVALID_HANDLE_VALUE;
			m_bCloseOnDelete = FALSE;

			if (bError)
				throw std::exception("Failed to close file");

			return true;
		}

		bool Abort()
		{
			if (_hFile != INVALID_HANDLE_VALUE)
			{
				// close but ignore errors
				::CloseHandle(_hFile);
				_hFile = INVALID_HANDLE_VALUE;
			}

			return true;
		}

		IW::FileSize GetFileSize()
		{
			return IW::FileSize::FromHandle(_hFile);
		}

		bool GetFileInformationByHandle(LPBY_HANDLE_FILE_INFORMATION lpFileInformation)
		{
			return ::GetFileInformationByHandle(_hFile, lpFileInformation) != 0;
		}

		virtual CString GetFileName() { return _strFileName; };

		// insertion operations
		CFile& operator<<(BYTE by);
		CFile& operator<<(WORD w);
		CFile& operator<<(LONG l);
		CFile& operator<<(DWORD dw);
		CFile& operator<<(float f);
		CFile& operator<<(double d);

		CFile& operator<<(int i);
		CFile& operator<<(short w);
		CFile& operator<<(char ch);
		CFile& operator<<(unsigned u);

		// extraction operations
		CFile& operator>>(BYTE& by);
		CFile& operator>>(WORD& w);
		CFile& operator>>(DWORD& dw);
		CFile& operator>>(LONG& l);
		CFile& operator>>(float& f);
		CFile& operator>>(double& d);

		CFile& operator>>(int& i);
		CFile& operator>>(short& w);
		CFile& operator>>(char& ch);
		CFile& operator>>(unsigned& u);
	};

	inline CFile& CFile::operator<<(int i)
	{ return CFile::operator<<((LONG)i); }
	inline CFile& CFile::operator<<(unsigned u)
	{ return CFile::operator<<((LONG)u); }
	inline CFile& CFile::operator<<(short w)
	{ return CFile::operator<<((WORD)w); }
	inline CFile& CFile::operator<<(char ch)
	{ return CFile::operator<<((BYTE)ch); }
	inline CFile& CFile::operator<<(BYTE by)
	{ Write(&by, sizeof(by)); return *this; }
	inline CFile& CFile::operator<<(WORD w)
	{ Write(&w, sizeof(w)); return *this; }
	inline CFile& CFile::operator<<(LONG l)
	{ Write(&l, sizeof(l)); return *this; }
	inline CFile& CFile::operator<<(DWORD dw)
	{ Write(&dw, sizeof(dw)); return *this; }
	inline CFile& CFile::operator<<(float f)
	{ Write(&f, sizeof(f)); return *this; }
	inline CFile& CFile::operator<<(double d)
	{ Write(&d, sizeof(d)); return *this; }


	inline CFile& CFile::operator>>(int& i)
	{ return CFile::operator>>((LONG&)i); }
	inline CFile& CFile::operator>>(unsigned& u)
	{ return CFile::operator>>((LONG&)u); }
	inline CFile& CFile::operator>>(short& w)
	{ return CFile::operator>>((WORD&)w); }
	inline CFile& CFile::operator>>(char& ch)
	{ return CFile::operator>>((BYTE&)ch); }
	inline CFile& CFile::operator>>(BYTE& by)
	{ Read(&by, sizeof(by)); return *this; }
	inline CFile& CFile::operator>>(WORD& w)
	{ Read(&w, sizeof(w)); return *this; }
	inline CFile& CFile::operator>>(DWORD& dw)
	{ Read(&dw, sizeof(dw)); return *this; }
	inline CFile& CFile::operator>>(float& f)
	{ Read(&f, sizeof(f)); return *this; }
	inline CFile& CFile::operator>>(double& d)
	{ Read(&d, sizeof(d)); return *this; }
	inline CFile& CFile::operator>>(LONG& l)
	{ Read(&l, sizeof(l)); return *this; }

	// Everything written lands in a temporary file beside the destination, and is
	// only put in place by an explicit Close. A failed write, a loader that throws
	// or an early return leaves the original file exactly as it was.
	class CFileTemp : public IW::CFile
	{
	protected:
		IW::CFilePath _pathTempFileName;
		IW::CFilePath _pathRealFileName;

	public:
		CFileTemp()
		{
		}

		~CFileTemp()
		{
			Abort();
		}

		bool Close(IW::IStatus *pStatus)
		{
			if (_hFile == INVALID_HANDLE_VALUE || !m_bCloseOnDelete)
				return true;

			::FlushFileBuffers(_hFile);
			CFile::Close(pStatus);

			const DWORD dwError = Commit();

			if (dwError != NO_ERROR)
			{
				if (pStatus != nullptr)
					pStatus->SetError(FormatOsError(dwError));

				Abort();
				return false;
			}

			return true;
		}

		bool OpenForWrite(const CString &strFileName)
		{
			return Open(strFileName, GENERIC_WRITE, 0, CREATE_ALWAYS);
		}

		bool Abort()
		{
			if (_hFile != INVALID_HANDLE_VALUE)
			{
				// close but ignore errors
				::CloseHandle(_hFile);
				_hFile = INVALID_HANDLE_VALUE;
				m_bCloseOnDelete = FALSE;
			}

			if (!_pathTempFileName.ToString().IsEmpty())
			{
				::DeleteFile(_pathTempFileName);
				_pathTempFileName = CString();
			}

			return true;
		}


	private:

		// ReplaceFile keeps the destination's security descriptor, attributes and
		// alternate streams, and leaves the original alone if any step of it fails.
		DWORD Commit()
		{
			if (::ReplaceFile(_pathRealFileName, _pathTempFileName, nullptr,
			                  REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr))
			{
				_pathTempFileName = CString();
				return NO_ERROR;
			}

			const DWORD dwError = ::GetLastError();

			// The first time a name is written there is nothing to replace, and
			// nothing to preserve either, so a rename is the whole job.
			if (dwError == ERROR_FILE_NOT_FOUND || dwError == ERROR_PATH_NOT_FOUND)
			{
				if (::MoveFileEx(_pathTempFileName, _pathRealFileName,
				                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
				{
					_pathTempFileName = CString();
					return NO_ERROR;
				}

				return ::GetLastError();
			}

			return dwError;
		}

		static CString FormatOsError(DWORD dwError)
		{
			LPTSTR lpMsgBuffer = nullptr;

			::FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER,
			                nullptr, dwError, MAKELCID(LANG_NEUTRAL, SORT_DEFAULT),
			                reinterpret_cast<LPTSTR>(&lpMsgBuffer), 0, nullptr);

			CString str;

			if (lpMsgBuffer != nullptr)
			{
				str = lpMsgBuffer;
				::LocalFree(lpMsgBuffer);
				str.Replace(_T('\r'), _T(' '));
				str.Replace(_T('\n'), _T(' '));
				str.Trim();
			}

			if (str.IsEmpty())
				str.Format(_T("Error %u"), dwError);

			return str;
		}

		bool Open(const CString &strFileName, DWORD dwAccess, DWORD dwShareMode, DWORD dwCreateFlag)
		{
			_pathRealFileName = strFileName;

			// The temp must share a volume with the destination or ReplaceFile
			// cannot rename it into place.
			if (!MakeTempPathBeside(strFileName))
				_pathTempFileName.GetTempFilePath();

			if (CFile::Open(_pathTempFileName, dwAccess, dwShareMode, dwCreateFlag))
				return true;

			::DeleteFile(_pathTempFileName);
			_pathTempFileName = CString();
			return false;
		}

		bool MakeTempPathBeside(const CString &strFileName)
		{
			IW::CFilePath pathFolder(strFileName);
			pathFolder.RemoveFileName();

			if (pathFolder.ToString().IsEmpty())
				return false;

			TCHAR szTempFileName[MAX_PATH] = { 0 };

			// A zero unique number makes the call create the file, so the name
			// cannot be handed out twice.
			if (::GetTempFileName(pathFolder, _T("IW"), 0, szTempFileName) == 0)
				return false;

			_pathTempFileName = szTempFileName;
			return true;
		}
	};
}

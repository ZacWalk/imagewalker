// ImageWalker by Zac Walker
//
// Purpose: The stream classes the formats read and write through, plus
//          IW::CFile and the write-to-temp-then-rename CFileTemp.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "core/file_publication.h"

#include <array>
#include <bcrypt.h>

#include "UtilBase.h"
#include "Imaging.h"

#pragma comment(lib, "bcrypt.lib")

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

		std::span<const BYTE> GetMemory() const override
		{
			const auto size = static_cast<int>(m_nSize);
			if (size <= 0) return {};
			return {m_pBytes, static_cast<size_t>(size)};
		}

		bool Read(LPVOID lpBuf, DWORD nCount, LPDWORD pdwRead = 0)
		{
			const auto size = static_cast<DWORD>(static_cast<int>(m_nSize));
			const DWORD nAbsoluteSize = _nPosition < size ? (std::min)(nCount, size - _nPosition) : 0;
			if (nAbsoluteSize) IW::MemCopy(lpBuf, m_pBytes + _nPosition, nAbsoluteSize);
			_nPosition += nAbsoluteSize;

			if (pdwRead) *pdwRead = nAbsoluteSize;

			return nAbsoluteSize > 0;
		}




		DWORD Seek(ePosition ePos, LONG lOff)
		{
			__int64 nPosNew = _nPosition;

			switch(ePos)
			{
			case IW::IStreamCommon::eBegin:
				nPosNew = lOff;
				break;
			case IW::IStreamCommon::eEnd:
				nPosNew = static_cast<__int64>(static_cast<int>(m_nSize)) + lOff;
				break;
			default:
				// Unknown seek?
				return INVALID_SET_FILE_POINTER;
			case IW::IStreamCommon::eCurrent:
				nPosNew += lOff;
				break;
			}

			if (nPosNew < 0 || nPosNew > static_cast<int>(m_nSize)) return INVALID_SET_FILE_POINTER;
			return _nPosition = static_cast<DWORD>(nPosNew);
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
			const auto size = static_cast<DWORD>(static_cast<int>(GetFileSize()));
			const DWORD nAbsoluteSize = _nPosition < size ? (std::min)(nCount, size - _nPosition) : 0;

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
			if (nCount > INT_MAX || _nPosition > static_cast<DWORD>(INT_MAX) - nCount)
				throw std::length_error("Image stream exceeds the supported size.");
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
			__int64 nPosNew = _nPosition;

			switch(ePos)
			{
			case IW::IStreamCommon::eBegin:
				nPosNew = lOff;
				break;
			case IW::IStreamCommon::eEnd:
				nPosNew = static_cast<__int64>(static_cast<int>(GetFileSize())) + lOff;
				break;
			default:
				// Unknown seek?
				return INVALID_SET_FILE_POINTER;
			case IW::IStreamCommon::eCurrent:
				nPosNew += lOff;
				break;
			}

			if (nPosNew < 0 || nPosNew > INT_MAX) return INVALID_SET_FILE_POINTER;
			_nPosition = static_cast<DWORD>(nPosNew);

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
		std::span<const BYTE> GetMemory() const override
		{
			const auto size = static_cast<int>(m_nSize);
			if (size <= 0) return {};
			return {GetData(), static_cast<size_t>(size)};
		}
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




	using FileDigest = std::array<BYTE, 32>;

	struct EditFileStamp
	{
		iw::publication::Stamp file;
		FileDigest digest;

		bool operator==(const EditFileStamp &other) const
		{
			auto left = file, right = other.file;
			// A read can change access time and therefore ChangeTime on NTFS.
			// Keep checking the actual bytes, even if a writer restored mtime.
			left.changed = right.changed = 0;
			return left == right && digest == other.digest;
		}
	};

	// Call while the handle denies other writers. Leave the stream position
	// where the decoder/encoder left it.
	inline std::optional<FileDigest> DigestOpenFile(HANDLE file, IStatus *status)
	{
		struct Hash
		{
			BCRYPT_ALG_HANDLE algorithm = nullptr;
			BCRYPT_HASH_HANDLE handle = nullptr;
			~Hash()
			{
				if (handle) BCryptDestroyHash(handle);
				if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
			}
		} hash;
		if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hash.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) ||
			!BCRYPT_SUCCESS(BCryptCreateHash(hash.algorithm, &hash.handle, nullptr, 0, nullptr, 0, 0)))
			return {};

		std::vector<BYTE> bytes(64 * 1024);
		LARGE_INTEGER zero{}, position{};
		if (!SetFilePointerEx(file, zero, &position, FILE_CURRENT) ||
			!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN))
			return {};

		// QueryCancel may pump messages; restore the position even if it throws.
		struct RestorePosition
		{
			HANDLE file;
			LARGE_INTEGER position;
			~RestorePosition() { SetFilePointerEx(file, position, nullptr, FILE_BEGIN); }
		} restore{file, position};
		for (;;)
		{
			if (status && status->QueryCancel())
				return {};
			DWORD read = 0;
			if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr))
				return {};
			if (!read)
				break;
			if (!BCRYPT_SUCCESS(BCryptHashData(hash.handle, bytes.data(), read, 0)))
				return {};
		}
		FileDigest digest;
		if (!BCRYPT_SUCCESS(BCryptFinishHash(hash.handle, digest.data(), static_cast<ULONG>(digest.size()), 0)))
			return {};
		return digest;
	}

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

		std::optional<iw::publication::Stamp> FileStamp() const { return iw::publication::stamp(_hFile); }

		std::optional<EditFileStamp> CaptureEditStamp(IStatus *status) const
		{
			const auto info = FileStamp();
			if (!info) return {};
			const auto digest = DigestOpenFile(_hFile, status);
			return digest ? std::optional<EditFileStamp>(EditFileStamp{*info, *digest}) : std::nullopt;
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

	class CFileWriteStatus : public CNullStatus
	{
	public:
		CString error;
		void SetError(const CString& value) override { error = value; }
	};

	// Everything written lands in a temporary file beside the destination, and is
	// only put in place by an explicit Close. A failed write, a loader that throws
	// or an early return leaves the original file exactly as it was.
	class CFileTemp : public IW::CFile
	{
	protected:
		std::filesystem::path _pathTempFileName;
		std::filesystem::path _pathRealFileName;
		std::optional<iw::publication::Stamp> _expected;
		std::optional<FileDigest> _expectedDigest;
		std::optional<EditFileStamp> _committed;
		bool _captureStamp = false;
		bool _ownsTemporary = false;
		DWORD _writeError = NO_ERROR;

	public:
		CFileTemp()
		{
		}

		CFileTemp(const CFileTemp&) = delete;
		CFileTemp& operator=(const CFileTemp&) = delete;

		~CFileTemp()
		{
			Abort();
		}

		bool Write(LPCVOID bytes, DWORD count, LPDWORD written = nullptr) override
		{
			DWORD actual{};
			bool success = false;
			if (_writeError == NO_ERROR)
			{
				success = CFile::Write(bytes, count, &actual);
				if (!success)
				{
					_writeError = GetLastError();
					if (_writeError == NO_ERROR) _writeError = ERROR_WRITE_FAULT;
				}
				else if (actual != count) _writeError = ERROR_WRITE_FAULT;
			}
			if (written) *written = actual;
			return success && _writeError == NO_ERROR;
		}

		bool Flush() override
		{
			if (_writeError != NO_ERROR) return false;
			if (CFile::Flush()) return true;
			_writeError = GetLastError();
			if (_writeError == NO_ERROR) _writeError = ERROR_WRITE_FAULT;
			return false;
		}

		bool Close(IW::IStatus *pStatus)
		{
			if (_hFile == INVALID_HANDLE_VALUE || !m_bCloseOnDelete)
				return _writeError == NO_ERROR;

			if (!Flush())
			{
				if (pStatus) pStatus->SetError(FormatOsError(_writeError));
				Abort();
				return false;
			}
			const auto digest = _captureStamp ? DigestOpenFile(_hFile, pStatus) : std::nullopt;
			if (pStatus && pStatus->QueryCancel())
			{
				Abort();
				return false;
			}
			CString recovery;
			const DWORD dwError = Commit(recovery);

			if (dwError != NO_ERROR)
			{
				_writeError = dwError;
				if (pStatus != nullptr)
					pStatus->SetError(recovery.IsEmpty() ? FormatOsError(dwError) : recovery);

				Abort();
				return false;
			}

			_ownsTemporary = false;
			if (digest)
			{
				if (const auto info = FileStamp())
					_committed = EditFileStamp{*info, *digest};
			}
			return CFile::Close(pStatus);
		}

		std::optional<EditFileStamp> CommittedStamp() const { return _committed; }

		bool OpenForWrite(const CString &strFileName, bool captureStamp = false)
		{
			return Open(strFileName, {}, {}, captureStamp);
		}

		bool OpenForReplace(const CString& strFileName, const iw::publication::Stamp& expected)
		{
			return Open(strFileName, expected, {}, false);
		}

		bool OpenForReplace(const CString& strFileName, const EditFileStamp& expected)
		{
			return Open(strFileName, expected.file, expected.digest, true);
		}

		static CString RotatedCopyPath(const CString& name)
		{
			const std::filesystem::path source(static_cast<LPCTSTR>(name));
			if (source.filename().empty()) return {};
			const auto stem = source.stem().wstring() + L"-rotated";
			const auto extension = source.extension().wstring();
			for (int index = 1; index <= 1000; ++index)
			{
				const auto suffix = index == 1 ? L"" : L" (" + std::to_wstring(index) + L")";
				const auto candidate = source.parent_path() / (stem + suffix + extension);
				if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
				if (GetLastError() != ERROR_FILE_NOT_FOUND) return {};
				return CString(candidate.c_str());
			}
			return {};
		}

		bool Abort()
		{
			if (_hFile != INVALID_HANDLE_VALUE)
			{
				if (_ownsTemporary) iw::publication::remove(_hFile);
				// close but ignore errors
				::CloseHandle(_hFile);
				_hFile = INVALID_HANDLE_VALUE;
				m_bCloseOnDelete = FALSE;
			}

			_ownsTemporary = false;
			return true;
		}


	private:

		DWORD Commit(CString& recovery)
		{
			using namespace iw::publication;
			AvailableName destination(_pathRealFileName);
			if (!_expected) return destination.move(_hFile);

			// Pin the original against edits and rename it by handle. A late arrival still wins the target name.
			const auto originalPath = extended(_pathRealFileName);
			const File original(CreateFileW(originalPath.c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ,
			                                nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
			if (!original) return GetLastError();
			BY_HANDLE_FILE_INFORMATION info{};
			if (!::GetFileInformationByHandle(original.get(), &info)) return GetLastError();
			if (info.dwFileAttributes & FILE_ATTRIBUTE_READONLY) return ERROR_ACCESS_DENIED;
			const auto observed = stamp(original.get());
			bool matches = observed == _expected;
			if (observed && _expectedDigest)
			{
				const auto observedDigest = DigestOpenFile(original.get(), nullptr);
				matches = observedDigest &&
					EditFileStamp{*observed, *observedDigest} == EditFileStamp{*_expected, *_expectedDigest};
			}
			if (!matches)
			{
				recovery = _T("That file changed after it was opened or approved. Save a new copy instead.");
				return ERROR_FILE_EXISTS;
			}
			const auto stem = _pathRealFileName.stem().wstring() + L"-original";
			const auto extension = _pathRealFileName.extension().wstring();
			for (int index = 1; index <= 1000; ++index)
			{
				const auto suffix = index == 1 ? L"" : L" (" + std::to_wstring(index) + L")";
				const auto backup = _pathRealFileName.parent_path() / (stem + suffix + extension);
				AvailableName backupName(backup);
				const DWORD staged = backupName.move(original.get());
				if (staged == ERROR_FILE_EXISTS || staged == ERROR_ALREADY_EXISTS) continue;
				if (staged != ERROR_SUCCESS) return staged;

				const DWORD published = destination.move(_hFile);
				if (published != ERROR_SUCCESS && destination.move(original.get()) != ERROR_SUCCESS)
					recovery.Format(_T("Nothing was overwritten. The original is retained at:\n%s"), backup.c_str());
				return published;
			}
			return ERROR_FILE_EXISTS;
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

		bool Open(const CString& strFileName, std::optional<iw::publication::Stamp> expected,
		          std::optional<FileDigest> expectedDigest, bool captureStamp)
		{
			Abort();
			_writeError = NO_ERROR;
			_committed.reset();
			_expected = std::move(expected);
			_expectedDigest = std::move(expectedDigest);
			_captureStamp = captureStamp;
			std::error_code error;
			_pathRealFileName = std::filesystem::absolute(static_cast<LPCTSTR>(strFileName), error).lexically_normal();
			if (error || _pathRealFileName.filename().empty() ||
			    _pathRealFileName.filename().wstring().find(L':') != std::wstring::npos)
				return false;
			_pathTempFileName = iw::publication::temporary_sibling(_pathRealFileName, L".iw-output-");
			if (_pathTempFileName.empty()) return false;
			_ownsTemporary = CFile::Open(iw::publication::extended(_pathTempFileName).c_str(),
			                            GENERIC_WRITE | FILE_READ_ATTRIBUTES | DELETE | (captureStamp ? GENERIC_READ : 0),
			                            FILE_SHARE_READ, CREATE_NEW);
			return _ownsTemporary;
		}
	};
}

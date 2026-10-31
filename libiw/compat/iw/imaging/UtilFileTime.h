// ImageWalker by Zac Walker
//
// Purpose: FILETIME arithmetic and formatting.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

namespace IW
{

	class FileTime
	{
	private:
		FILETIME _ft;

	public:

		FileTime()
		{
			_ft.dwLowDateTime = 0;
			_ft.dwHighDateTime = 0;
		}

		FileTime(DWORD dwLowDateTime, DWORD dwHighDateTime)
		{
			_ft.dwLowDateTime = dwLowDateTime;
			_ft.dwHighDateTime = dwHighDateTime;
		}

		FileTime(const FileTime &other) : _ft(other._ft)
		{
		}

		FileTime(const SYSTEMTIME &st)
		{
			SystemTimeToFileTime(&st, &_ft);
		}

		FileTime(const FILETIME &ft) : _ft(ft)
		{
		}

		FileTime(__int64 n)
		{
			*((__int64*)&_ft) = n;
		}

		void operator=(const FILETIME &other)
		{
			_ft.dwLowDateTime = other.dwLowDateTime;
			_ft.dwHighDateTime = other.dwHighDateTime;
		}

		void operator=(const FileTime &other)
		{
			_ft.dwLowDateTime = other._ft.dwLowDateTime;
			_ft.dwHighDateTime = other._ft.dwHighDateTime;
		}

		void operator=(const DATE &dateIn)
		{
			SYSTEMTIME st;

			VariantTimeToSystemTime(dateIn, &st);
			st.wHour = 0; st.wMinute = 0; st.wSecond = 0; st.wMilliseconds = 0;
			SystemTimeToFileTime(&st, &_ft);
		}

		void operator=(__int64 n)
		{
			*((__int64*)&_ft) = n;
		}




		bool IsEmpty() const
		{
			return _ft.dwHighDateTime == 0 && _ft.dwLowDateTime == 0;
		}

		bool operator==(const FileTime &other) const
		{
			return Compare(other) == 0;
		}

		bool operator==(const FILETIME &other) const
		{
			return ::CompareFileTime(&_ft, &other) == 0;
		}

		bool operator!=(const FileTime &other) const
		{
			return Compare(other) != 0;
		}

		bool operator<=(const FileTime &other) const
		{
			return Compare(other) <= 0;
		}

		bool operator>(const FileTime &other) const
		{
			return Compare(other) > 0;
		}

		int Compare(const FileTime &other) const
		{
			return CompareFileTime(&_ft, &other._ft);
		}


		void SetDaysPrevious(int n)
		{
			SYSTEMTIME st;
			GetSystemTime(&st);
			SystemTimeToFileTime( &st, &_ft );

			ULARGE_INTEGER *puli = (ULARGE_INTEGER*)&_ft;

			const ULONGLONG n100Nanoseconds = 10000000;
			const ULONGLONG nDays = n * 24 * 60 * 60;

			puli->QuadPart -= n100Nanoseconds * nDays;
		}

		int GetYear() const
		{
			SYSTEMTIME st = ToSystemTime();
			return st.wYear;
		}

		int GetMonth() const
		{
			SYSTEMTIME st = ToSystemTime();
			return st.wMonth;
		}

		CString GetDateFormat(LCID locale = LOCALE_USER_DEFAULT, bool bShortDate = true) const
		{
			const int nBufferSize = 128;
			TCHAR sz[nBufferSize];
			SYSTEMTIME st;
			DWORD dwFlags = bShortDate ? DATE_SHORTDATE : DATE_LONGDATE;

			::FileTimeToSystemTime( &_ft, &st );
			::GetDateFormat(locale, dwFlags, &st, NULL, sz, nBufferSize );

			return sz;
		}

		CString GetTimeFormat(LCID locale = LOCALE_USER_DEFAULT) const
		{
			const int nBufferSize = 128;
			TCHAR sz[nBufferSize];

			SYSTEMTIME st = ToSystemTime();
			::GetTimeFormat(locale, 0, &st, NULL, sz, nBufferSize );
			return sz;
		}

		void FileTimeToDosDateTime(LPWORD lpFatDate, LPWORD lpFatTime) const
		{
			::FileTimeToDosDateTime(&_ft, lpFatDate, lpFatTime);
		}

		static FileTime Now()
		{
			SYSTEMTIME st;
			GetSystemTime( &st );
			return FileTime(st);
		}

		static FileTime FromFile(const CString &strFileName)
		{
			FileTime ft;
			WIN32_FIND_DATA   ff32; MemZero(&ff32, sizeof(ff32));
			HANDLE hFind = FindFirstFile(strFileName, &ff32);

			if (INVALID_HANDLE_VALUE != hFind)
			{
				ft = ff32.ftLastWriteTime;
				FindClose(hFind);
			}

			return ft;
		}

		SYSTEMTIME ToSystemTime() const
		{
			SYSTEMTIME st;
			FileTimeToSystemTime(&_ft, &st);
			return st;
		}


		// A time as a clock in this machine's zone showed it - a camera's, which EXIF and libfiles' date keep
		// without moving it to another zone. Fields that make no date leave this time as it was:
		// SystemTimeToFileTime refuses them, which would otherwise leave ftLocal as stack garbage to be
		// stored as a photo's date taken.
		void SetLocalClock(const int year, const int month, const int day, const int hour, const int minute, const int second)
		{
			SYSTEMTIME st = {0};
			st.wYear = static_cast<WORD>(year);
			st.wMonth = static_cast<WORD>(month);
			st.wDay = static_cast<WORD>(day);
			st.wHour = static_cast<WORD>(hour);
			st.wMinute = static_cast<WORD>(minute);
			st.wSecond = static_cast<WORD>(second);

			FILETIME ftLocal;

			if (SystemTimeToFileTime(&st, &ftLocal))
				LocalFileTimeToFileTime(&ftLocal, &_ft);
		}

		FileTime ToLocalTime() const
		{
			FILETIME ftLocal;
			FileTimeToLocalFileTime(&_ft, &ftLocal);
			return FileTime(ftLocal);
		}
	};

};

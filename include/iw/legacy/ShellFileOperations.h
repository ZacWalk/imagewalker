// ImageWalker by Zac Walker
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.

#pragma once

#include "core/recycle.h"

namespace IW
{
	CWindow GetMainWindow();

	inline bool IsShellFilePath(const CString &strPath)
	{
		if (strPath.GetLength() < 3 || strPath.FindOneOf(_T("*?\"<>|\r\n")) >= 0)
			return false;

		if (strPath.Left(2) == _T("\\\\"))
			return strPath[2] != _T('.') && strPath.Find(_T(':')) < 0;

		const TCHAR drive = strPath[0];
		return ((drive >= _T('A') && drive <= _T('Z')) || (drive >= _T('a') && drive <= _T('z'))) &&
			strPath[1] == _T(':') && strPath[2] == _T('\\') && strPath.Find(_T(':'), 2) < 0;
	}

	class FileOperation
	{
		std::vector<CString> _files;
		bool _bAborted = false;

	public:
		explicit FileOperation(const CString &strFiles)
		{
			int nStart = 0;
			while (nStart >= 0)
			{
				CString strPath = strFiles.Tokenize(_T("\n"), nStart);
				if (!strPath.IsEmpty())
					_files.push_back(strPath);
			}
		}

		bool Recycle() const
		{
			std::vector<std::filesystem::path> paths;
			paths.reserve(_files.size());
			for (const auto &strPath : _files)
				paths.emplace_back(strPath.GetString());
			return iw::recycle::to_bin(paths);
		}

		bool Copy(const CString &strDestFolder, bool bMove)
		{
			_bAborted = false;
			if (_files.empty() || !IsShellFilePath(strDestFolder) || !::PathIsDirectory(strDestFolder))
				return false;

			// SHFileOperation requires double-null-terminated lists.
			std::vector<TCHAR> from;
			for (const auto &strPath : _files)
			{
				if (!IsShellFilePath(strPath))
					return false;
				from.insert(from.end(), strPath.GetString(), strPath.GetString() + strPath.GetLength() + 1);
			}
			from.push_back(0);
			std::vector<TCHAR> to(strDestFolder.GetString(), strDestFolder.GetString() + strDestFolder.GetLength() + 1);
			to.push_back(0);

			SHFILEOPSTRUCT op{};
			op.hwnd = GetMainWindow();
			op.wFunc = bMove ? FO_MOVE : FO_COPY;
			op.pFrom = from.data();
			op.pTo = to.data();
			op.fFlags = FOF_ALLOWUNDO | FOF_RENAMEONCOLLISION | FOF_NO_CONNECTED_ELEMENTS;

			const int nError = ::SHFileOperation(&op);
			_bAborted = op.fAnyOperationsAborted != FALSE;
			return nError == 0 && !_bAborted;
		}

		bool WasAborted() const { return _bAborted; }
	};
}

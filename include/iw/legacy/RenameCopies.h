// ImageWalker by Zac Walker
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.

#pragma once

struct RenameItem
{
	CString strFolder;
	CString strPath;
	CString strName;
	CString strBase;
	CString strExt;
	CString strNewName;
	CString strProblem;
};

typedef std::vector<RenameItem> RENAMEPLAN;

// A run of #'s is the zero-padded number; ? is the whole original base name.
inline CString FormatRenameName(const CString &strTemplate, const CString &strOriginal, int nPosition)
{
	CString strOut;
	const int nLength = strTemplate.GetLength();

	for (int i = 0; i < nLength;)
	{
		const TCHAR ch = strTemplate[i];

		if (ch == _T('#'))
		{
			int nRun = 0;
			while (i + nRun < nLength && strTemplate[i + nRun] == _T('#'))
				nRun += 1;

			CString strNumber;
			strNumber.Format(_T("%0*d"), nRun, nPosition);
			strOut += strNumber;
			i += nRun;
		}
		else
		{
			if (ch == _T('?'))
				strOut += strOriginal;
			else
				strOut += ch;
			i += 1;
		}
	}

	return strOut;
}

inline bool IsRenameCopyName(const CString &strName)
{
	const int nLength = strName.GetLength();
	if (nLength == 0 || nLength > 215 || strName.FindOneOf(_T("\\/:*?\"<>|")) >= 0 ||
		strName[nLength - 1] == _T('.') || strName[nLength - 1] == _T(' '))
		return false;

	for (int i = 0; i < nLength; ++i)
		if (strName[i] < _T(' '))
			return false;

	CString strDevice = strName.SpanExcluding(_T("."));
	strDevice.TrimRight(_T(' '));
	for (LPCTSTR szDevice : {_T("CON"), _T("PRN"), _T("AUX"), _T("NUL"), _T("CONIN$"), _T("CONOUT$")})
		if (strDevice.CompareNoCase(szDevice) == 0)
			return false;

	return strDevice.GetLength() != 4 ||
		(strDevice.Left(3).CompareNoCase(_T("COM")) != 0 && strDevice.Left(3).CompareNoCase(_T("LPT")) != 0) ||
		!_tcschr(_T("123456789\u00b9\u00b2\u00b3"), strDevice[3]);
}

inline bool RenameCopyUnchanged(const RenameItem &item)
{
	return item.strNewName.CompareNoCase(item.strName) == 0;
}

inline void BuildRenamePlan(RENAMEPLAN &plan, const CString &strTemplate, int nStart)
{
	int nPosition = nStart;
	for (auto &item : plan)
	{
		item.strNewName = FormatRenameName(strTemplate, item.strBase, nPosition++) + item.strExt;
		item.strProblem.Empty();
	}

	for (auto &item : plan)
	{
		if (!IW::IsShellFilePath(item.strPath))
			item.strProblem = _T("Not a file system path");
		else if (item.strNewName.IsEmpty() || item.strNewName == item.strExt)
			item.strProblem = _T("No name");
		else if (!IsRenameCopyName(item.strNewName))
			item.strProblem = _T("Not a usable file name");
		else if (!RenameCopyUnchanged(item))
		{
			for (const auto &other : plan)
			{
				if (&other != &item && item.strFolder.CompareNoCase(other.strFolder) == 0 &&
					item.strNewName.CompareNoCase(other.strNewName) == 0)
				{
					item.strProblem = _T("Two files would get this name");
					break;
				}
			}

			if (!item.strProblem.IsEmpty())
				continue;

			const CString strTo = IW::Path::Combine(item.strFolder, item.strNewName);
			if (::GetFileAttributes(strTo) != INVALID_FILE_ATTRIBUTES)
				item.strProblem = _T("That name is already in use");
			else if (::GetLastError() != ERROR_FILE_NOT_FOUND)
				item.strProblem = _T("Cannot check this destination");
		}
	}
}

struct RenameCopyResult
{
	unsigned nCopied = 0;
	CString strFailed;
};

inline RenameCopyResult CopyRenamePlan(const RENAMEPLAN &plan)
{
	RenameCopyResult result;
	for (const auto &item : plan)
	{
		if (!item.strProblem.IsEmpty())
			return {0, item.strName.IsEmpty() ? item.strProblem : item.strName};
	}

	for (const auto &item : plan)
	{
		if (RenameCopyUnchanged(item))
			continue;

		const CString strTo = IW::Path::Combine(item.strFolder, item.strNewName);
		// Check at the time of copying too: a late arrival, including a dangling
		// symbolic link, must not be overwritten or followed.
		if (::CopyFileEx(item.strPath, strTo, nullptr, nullptr, nullptr,
			COPY_FILE_FAIL_IF_EXISTS | COPY_FILE_COPY_SYMLINK))
			result.nCopied += 1;
		else
			IW::AddToList(result.strFailed, strTo);
	}
	return result;
}

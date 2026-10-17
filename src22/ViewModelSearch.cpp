// ImageWalker by Zac Walker
//
// Purpose: Search implementation - the query tree and the per-item match.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"

IW::CSearchNode::CSearchNode() : _se(seAND), _str(g_szEmptyString)
{
}

IW::CSearchNode::CSearchNode(DWORD se, const CString& str, bool bNot) :
	_se(se)
{
	SetText(str);
}

IW::CSearchNode::CSearchNode(const CSearchNode& sn) :
	_str(sn._str), _strUpper(sn._strUpper), _strUpperWild(sn._strUpperWild), _se(sn._se)
{
}

void IW::CSearchNode::SetText(const CString& str)
{
	_str = str;
	_str.Trim();
	UpdateMatchPattern();
}

void IW::CSearchNode::UpdateMatchPattern()
{
	_strUpper = _str;
	_strUpper.MakeUpper();

	if (!_strUpper.IsEmpty() &&
		_tcschr(_strUpper, _T('*')) == nullptr &&
		_tcschr(_strUpper, _T('?')) == nullptr)
	{
		_strUpperWild = _T("*");
		_strUpperWild += _strUpper;
		_strUpperWild += _T("*");
	}
	else
	{
		_strUpperWild = _strUpper;
	}
}

bool IW::CSearchNode::ParseFromString(const CString& strPrefix, const CString& strText)
{
	TCHAR szSeps[] = _T(" ");
	int curPos = 0;
	CString token = strPrefix.Tokenize(szSeps, curPos);

	while (!token.IsEmpty())
	{
		if (_tcsicmp(token, App.LoadString(IDS_AND)) == 0 ||
			_tcsicmp(token, _T("+")) == 0)
		{
			_se &= ~seOR;
		}
		else if (_tcsicmp(token, App.LoadString(IDS_OR)) == 0 ||
			_tcsicmp(token, _T(";")) == 0)
		{
			_se |= seOR;
		}
		else if (_tcsicmp(token, App.LoadString(IDS_NOT)) == 0 ||
			_tcsicmp(token, _T("-")) == 0)
		{
			_se |= seNOT;
		}

		token = strPrefix.Tokenize(szSeps, curPos);
	}

	SetText(strText);

	return true;
}

IW::CSearchNodeList::CSearchNodeList()
{
}

IW::CSearchNodeList::~CSearchNodeList()
{
}


IW::CSearchNodeList::CSearchNodeList(const CSearchNodeList& ss)
{
	Copy(ss);
}

void IW::CSearchNodeList::operator=(const CSearchNodeList& ss)
{
	Copy(ss);
}

void IW::CSearchNodeList::Copy(const CSearchNodeList& ss)
{
	_children.RemoveAll();

	for (int i = 0; i < ss._children.GetSize(); i++)
	{
		_children.Add(ss._children[i]);
	}
}

/////////////////////////////////////////////////////////////////////////////
// CFolderWindow

// Iterative wildcard match with a single restart point. The recursive form this
// replaces was exponential on a pattern like "*a*a*a*a*".
static bool SimpleMatchElement(LPCTSTR pszExpression, LPCTSTR pszObject)
{
	LPCTSTR pszStar = nullptr;
	LPCTSTR pszResume = nullptr;

	while (*pszObject != 0)
	{
		if (*pszExpression == _T('*'))
		{
			pszStar = ++pszExpression;
			pszResume = pszObject;
		}
		else if (*pszExpression == _T('?') || *pszExpression == *pszObject)
		{
			++pszExpression;
			++pszObject;
		}
		else if (pszStar != nullptr)
		{
			pszExpression = pszStar;
			pszObject = ++pszResume;
		}
		else
		{
			return false;
		}
	}

	while (*pszExpression == _T('*'))
		++pszExpression;

	return *pszExpression == 0;
}

bool IW::SimpleMatch(const CString& strFilter, const CString& strFileNameOrg)
{
	CString strFileName(strFileNameOrg);
	strFileName.MakeUpper();

	TCHAR szSeps[] = _T(";");
	CString str(strFilter);
	str.MakeUpper();

	CString token;
	int curPos = 0;

	token = str.Tokenize(szSeps, curPos);
	while (!token.IsEmpty())
	{
		CString strEntry(token);
		strEntry.Trim();

		if (_tcsclen(token) > 0 &&
			_tcschr(token, '*') == nullptr &&
			_tcschr(token, '?') == nullptr)
		{
			CString strWild(_T("*"));
			strWild += strEntry;
			strWild += _T("*");

			// While there are tokens in "string" 
			if (SimpleMatchElement(strWild, strFileName))
				return true;
		}
		else
		{
			// While there are tokens in "string" 
			if (SimpleMatchElement(strEntry, strFileName))
				return true;
		}

		token = str.Tokenize(szSeps, curPos);
	}

	return false;
}

// The pattern is already upper-cased; only the subject needs converting.
static bool SimpleMatch2(LPCTSTR szFilterUpper, const CString& strSourceUpper)
{
	TCHAR szSeps[] = _T(" ,\t\r\n[]{}().");

	int curPos = 0;
	CString token = strSourceUpper.Tokenize(szSeps, curPos);

	while (!token.IsEmpty())
	{
		if (SimpleMatchElement(szFilterUpper, token))
			return true;

		token = strSourceUpper.Tokenize(szSeps, curPos);
	}

	return false;
}


bool IW::CSearchNodeList::Match(const CString& strSource, bool bMatchSubString) const
{
	bool bMatchTotal = false;
	bool bFirst = true;

	CString strSourceUpper(strSource);
	strSourceUpper.MakeUpper();

	for (int i = 0; i < _children.GetSize(); i++)
	{
		const CSearchNode& sn = _children[i];
		bool bMatch = SimpleMatch2(sn.GetMatchPattern(bMatchSubString), strSourceUpper);

		if (sn._se & seNOT)
		{
			bMatch = !bMatch;
		}

		if (bFirst)
		{
			bMatchTotal = bMatch;
			bFirst = false;
		}
		else if (sn._se & seOR)
		{
			bMatchTotal = bMatchTotal || bMatch;
		}
		else
		{
			bMatchTotal = bMatchTotal && bMatch;
		}
	}

	return bMatchTotal;
}

bool IW::CSearchNodeList::ParseFromString(DWORD se, const CString& strSource)
{
	CString strWord;
	bool bInQuotes = false;
	LPCTSTR sz = strSource;

	while (*sz != 0)
	{
		if (bInQuotes)
		{
			strWord += *sz;

			bInQuotes = *sz != '\'' && *sz != '\"';
		}
		else
		{
			switch (*sz)
			{
			case ' ':
			case '\t':
				if (!strWord.IsEmpty())
				{
					_children.Add(CSearchNode(se, strWord));
					strWord = g_szEmptyString;
				}
				break;


			case '\'':
			case '\"':
				bInQuotes = true;

			default:
				strWord += *sz;
			}
		}

		sz++;
	}

	if (!strWord.IsEmpty())
	{
		_children.Add(CSearchNode(se, strWord));
		strWord = g_szEmptyString;
	}

	return true;
}

bool IW::CSearchNodeList::ParseFromString(const CString& strNOT, const CString& strAND, const CString& strOR)
{
	_children.RemoveAll();

	ParseFromString(seAND, strAND);
	ParseFromString(seOR, strOR);
	ParseFromString(seNOT, strNOT);

	return true;
}

bool IW::CSearchNodeList::ParseFromString(const CString& strSource)
{
	_children.RemoveAll();

	CString strPrefix, strWord;
	bool bInQuotes = false;
	LPCTSTR sz = strSource;

	while (*sz != 0)
	{
		if (bInQuotes)
		{
			strWord += *sz;

			bInQuotes = *sz != '\'' && *sz != '\"';
		}
		else
		{
			switch (*sz)
			{
			case ' ':
			case '\t':
				if (!strWord.IsEmpty())
				{
					if (_tcsicmp(strWord, App.LoadString(IDS_AND)) == 0 ||
						_tcsicmp(strWord, App.LoadString(IDS_OR)) == 0 ||
						_tcsicmp(strWord, App.LoadString(IDS_NOT)) == 0 ||
						_tcsicmp(strWord, _T("!")) == 0 ||
						_tcsicmp(strWord, _T(";")) == 0 ||
						_tcsicmp(strWord, _T("+")) == 0 ||
						_tcsicmp(strWord, _T("-")) == 0)
					{
						strPrefix += strWord;
						strPrefix += ' ';
						strWord = g_szEmptyString;
					}
					else
					{
						CSearchNode n;
						n.ParseFromString(strPrefix, strWord);
						_children.Add(n);

						strPrefix = g_szEmptyString;
						strWord = g_szEmptyString;
					}
				}
				break;

			case '+':
			case '-':
			case ';':
				if (!strWord.IsEmpty())
				{
					if (*sz == ';' && strPrefix.IsEmpty())
					{
						strPrefix = App.LoadString(IDS_OR);
					}

					CSearchNode n;
					n.ParseFromString(strPrefix, strWord);
					_children.Add(n);

					strPrefix = g_szEmptyString;
					strWord = g_szEmptyString;
				}
				strPrefix += *sz;
				strPrefix += ' ';
				break;

			case '\'':
			case '\"':
				bInQuotes = true;

			default:
				strWord += *sz;
			}
		}

		sz++;
	}

	if (!strWord.IsEmpty())
	{
		CSearchNode n;
		n.ParseFromString(strPrefix, strWord);
		_children.Add(n);

		strPrefix = g_szEmptyString;
		strWord = g_szEmptyString;
	}

	return true;
}

bool IW::CSearchNodeList::Format(CString& strNOT, CString& strAND, CString& strOR)
{
	strNOT.Empty();
	strAND.Empty();
	strOR.Empty();

	for (int i = 0; i < _children.GetSize(); ++i)
	{
		if (_children[i]._se & seNOT)
		{
			if (!strNOT.IsEmpty()) strNOT += ' ';
			strNOT += _children[i].GetText();
		}
		else if (_children[i]._se & seOR)
		{
			if (!strOR.IsEmpty()) strOR += ' ';
			strOR += _children[i].GetText();
		}
		else
		{
			if (!strAND.IsEmpty()) strAND += ' ';
			strAND += _children[i].GetText();
		}
	}

	return true;
}

bool IW::CSearchNodeList::Format(CString& strOut)
{
	strOut.Empty();

	for (int i = 0; i < _children.GetSize(); ++i)
	{
		if (!strOut.IsEmpty()) strOut += g_szSpace;

		if (_children[i]._se & seOR)
		{
			strOut += App.LoadString(IDS_OR);
		}
		else
		{
			strOut += App.LoadString(IDS_AND);
		}

		if (_children[i]._se & seNOT)
		{
			strOut += g_szSpace;
			strOut += App.LoadString(IDS_NOT);
		}

		strOut += g_szSpace;
		strOut += _children[i].GetText();
	}

	return true;
}

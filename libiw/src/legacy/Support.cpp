#include "LegacyPch.h"

SIZE_T IW::MemoryUsage = 0;

static IW::CNullStatus nullStatus;
IW::CNullStatus* IW::CNullStatus::Instance = &nullStatus;


#ifdef _DEBUG // use debug heap

LPVOID IW::Alloc(SIZE_T dwBytes, LPCSTR szFile, int nLine)
{
	assert(dwBytes > 0);
	LPVOID p = _malloc_dbg(dwBytes, _NORMAL_BLOCK, szFile, nLine);
	if (p == nullptr) throw std::bad_alloc();
	MemoryUsage += dwBytes;
	return p;
}

LPVOID IW::ReAlloc(LPVOID lpMem, SIZE_T dwBytes, LPCSTR szFile, int nLine)
{
	assert(dwBytes > 0);
	if (lpMem) MemoryUsage -= _msize_dbg(lpMem, _NORMAL_BLOCK);
	LPVOID p = _realloc_dbg(lpMem, dwBytes, _NORMAL_BLOCK, szFile, nLine);
	if (p == nullptr) throw std::bad_alloc();
	MemoryUsage += dwBytes;
	return p;
}


void IW::Free(LPVOID lpMem)
{
	if (lpMem) MemoryUsage -= _msize_dbg(lpMem, _NORMAL_BLOCK);
	_free_dbg(lpMem, _NORMAL_BLOCK);
}


#else

LPVOID IW::Alloc(SIZE_T dwBytes)
{
	assert(dwBytes > 0);
	LPVOID p = malloc(dwBytes);
	if (p == NULL) throw std::bad_alloc();
	MemoryUsage += dwBytes;
	return p;
}

LPVOID IW::ReAlloc(LPVOID lpMem, SIZE_T dwBytes)
{
	assert(dwBytes > 0);
	if (lpMem) MemoryUsage -= _msize(lpMem);
	LPVOID p = realloc(lpMem, dwBytes);
	if (p == NULL) throw std::bad_alloc();
	MemoryUsage += dwBytes;
	return p;
}


void IW::Free(LPVOID lpMem)
{
	if (lpMem) MemoryUsage -= _msize(lpMem);
	free(lpMem);
}

#endif


static inline void CopyInc(LPTSTR& szOut, LPCTSTR szSource)
{
	while (*szSource != 0)
	{
		*szOut++ = *szSource++;
	}
}

static inline void CopyInc(LPTSTR& szOut, LPCTSTR szSource, int nCount)
{
	while (nCount-- > 0 && *szSource != 0)
	{
		*szOut++ = *szSource++;
	}
}

CString IW::TextToHtmlFriendly(const CString& strIn, int nLength)
{
	if (nLength == -1)
		nLength = strIn.GetLength();

	CString str;
	constexpr int nTempBufferSize = 20;
	TCHAR szTemp[nTempBufferSize];
	LPTSTR szOutFinal = str.GetBuffer(10 + nLength * 6);
	LPTSTR szOut = szOutFinal;
	*szOut = 0;
	LPCTSTR szIn = strIn;

	for (int i = 0; i < nLength; i++)
	{
		switch (szIn[i])
		{
		case _T('&'):
			CopyInc(szOut, _T("&amp;"));
			break;
		case _T('<'):
			CopyInc(szOut, _T("&lt;"));
			break;
		case _T('>'):
			CopyInc(szOut, _T("&gt;"));
			break;

		case _T('\"'):
			CopyInc(szOut, _T("&quot;"));
			break;
		case 9:
		case 10:
		case 13:
		case _T('\''):
			_stprintf_s(szTemp, nTempBufferSize, _T("&#%d;"), szIn[i]);
			CopyInc(szOut, szTemp);
			break;

		default:
			*szOut++ = szIn[i];
			break;
		}
	}

	*szOut++ = 0;
	str.ReleaseBuffer();

	return str;
}

CString IW::HtmlFriendlyToText(const CString& strIn, int nLength)
{
	if (nLength == 0)
		return g_szEmptyString;

	if (nLength == -1)
		nLength = strIn.GetLength();

	CString str;
	LPTSTR szOutFinal = str.GetBuffer(nLength + 10);
	LPTSTR szOut = szOutFinal;
	*szOut = 0;
	LPCTSTR szIn = strIn;


	int nLast = IW::LowerLimit<0>(nLength - 2);
	int i = 0;

	for (; i < nLast; i++)
	{
		if (szIn[i] == '<')
		{
			if (_tcsnicmp(_T("<BR/>"), szIn + i, 5) == 0)
			{
				*szOut++ = '\r';
				*szOut++ = '\n';
				i += 4;
			}
			else if (_tcsnicmp(_T("<SQ/>"), szIn + i, 5) == 0)
			{
				*szOut++ = '\'';
				i += 4;
			}
			else if (_tcsnicmp(_T("<DQ/>"), szIn + i, 5) == 0)
			{
				*szOut++ = '\"';
				i += 4;
			}
			else
			{
				*szOut++ = szIn[i]; //??
			}
		}
		else if (szIn[i] == '&')
		{
			if (szIn[i + 1] == '#')
			{
				// Wide, not char: &#8217; (a curly apostrophe) is common in
				// descriptions and truncated to 0x11 in a signed char.
				unsigned int ch = 0;

				if (szIn[i + 2] == 'x')
				{
					for (i = i + 3; szIn[i] && szIn[i] != ';'; i++)
					{
						if (szIn[i] >= 'a')
						{
							ch = (ch * 16) + (10 + szIn[i] - 'a');
						}
						else if (szIn[i] >= 'A')
						{
							ch = (ch * 16) + (10 + szIn[i] - 'A');
						}
						else
						{
							ch = (ch * 16) + szIn[i] - '0';
						}
					}
				}
				else
				{
					for (i = i + 2; szIn[i] && szIn[i] != ';'; i++)
					{
						ch = (ch * 10) + szIn[i] - '0';
					}
				}

				if (ch != 0 && ch <= 0xffff)
					*szOut++ = static_cast<TCHAR>(ch);
			}
			else if (_tcsnicmp(_T("&amp;"), szIn + i, 5) == 0)
			{
				*szOut++ = '&';
				i += 4;
			}
			else if (_tcsnicmp(_T("&lt;"), szIn + i, 4) == 0)
			{
				*szOut++ = '<';
				i += 3;
			}
			else if (_tcsnicmp(_T("&gt;"), szIn + i, 4) == 0)
			{
				*szOut++ = '>';
				i += 3;
			}
			else if (_tcsnicmp(_T("&quot;"), szIn + i, 6) == 0)
			{
				*szOut++ = '\"';
				i += 5;
			}
			else
			{
				*szOut++ = szIn[i]; //??
			}
		}
		else
		{
			*szOut++ = szIn[i];
		}
	}

	// Complete the last chars
	CopyInc(szOut, szIn + i, nLength - nLast);
	*szOut++ = 0;

	str.ReleaseBuffer();
	return str;
}

CString IW::MakeURLSafe(const CString& strIn)
{
	CString str(strIn);
	str.Replace(g_szSpace, _T("%20"));
	str.Replace(_T("&"), _T("%26"));
	str.Replace(_T("'"), _T("%27"));
	str.Replace(_T("\""), _T("%22"));
	str.Replace(_T("\\"), _T("/"));
	return str;
}

CString IW::MakeURLUnSafe(const CString& strIn)
{
	CString str(strIn);
	str.Replace(_T("%20"), g_szSpace);
	str.Replace(_T("%26"), _T("&"));
	str.Replace(_T("%27"), _T("'"));
	str.Replace(_T("%22"), _T("\""));
	return str;
}

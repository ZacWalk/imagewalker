// ImageWalker by Zac Walker
//
// Purpose: Spell checking edit implementation.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"
#include "ViewSpellEdit.h"

#include <hunspell/hunspell.hxx>

#define MES_UNDO          _T("&Undo")
#define MES_CUT           _T("Cu&t")
#define MES_COPY          _T("&Copy")
#define MES_PASTE         _T("&Paste")
#define MES_DELETE        _T("&Delete")
#define MES_SELECTALL     _T("Select &All")
#define MES_CORRECTIONS   _T("Corr&ections")
#define MES_NOCORRECTIONS _T("(No corrections)")
#define MES_IGNOREALL     _T("&Ignore All")

namespace
{
	const UINT_PTR SPELLTIMER = 10;
	const UINT SPELLINTERVAL = 300;

	enum
	{
		ME_SELECTALL = WM_USER + 0x7000,
		ME_IGNOREALL,
		ME_NOCORRECTIONS,
		ME_SUGGESTION // ME_SUGGESTION + n is the nth correction
	};

	bool IsWordChar(TCHAR ch)
	{
		return ::IsCharAlpha(ch) != FALSE;
	}

	bool IsDigitChar(TCHAR ch)
	{
		return ch >= _T('0') && ch <= _T('9');
	}

	// a straight apostrophe or the typographic one, both of which occur inside words
	bool IsWordJoiner(TCHAR ch)
	{
		return ch == _T('\'') || ch == 0x2019;
	}

	// hunspell speaks the dictionary's own encoding, not the active code page
	UINT CodePageFromEncoding(const std::string &encoding)
	{
		CStringA name(encoding.c_str());
		name.MakeUpper();
		name.Remove('-');
		name.Remove('_');
		name.Remove(' ');

		if (name == "UTF8")
			return CP_UTF8;
		if (name == "KOI8R")
			return 20866;
		if (name == "KOI8U")
			return 21866;
		if (name.Find("TIS620") == 0)
			return 874;

		if (name.Find("ISO8859") == 0)
		{
			const int n = atoi(name.Mid(7));
			return (n >= 1 && n <= 16) ? 28590 + n : CP_ACP;
		}

		int nCodePage = 0;
		if (name.Find("MICROSOFTCP") == 0)
			nCodePage = atoi(name.Mid(11));
		else if (name.Find("WINDOWS") == 0)
			nCodePage = atoi(name.Mid(7));
		else if (name.Find("CP") == 0)
			nCodePage = atoi(name.Mid(2));

		return nCodePage > 0 ? static_cast<UINT>(nCodePage) : CP_ACP;
	}

	void DrawSquiggle(CDCHandle dc, int x1, int x2, int y)
	{
		if (x2 <= x1)
			return;

		std::vector<POINT> points;
		points.reserve(static_cast<size_t>((x2 - x1) / 2 + 2));

		for (int x = x1, nUp = 1; x <= x2; x += 2, nUp ^= 1)
		{
			const POINT pt = { x, nUp ? y - 2 : y };
			points.push_back(pt);
		}

		if (points.size() > 1)
			dc.Polyline(&points[0], static_cast<int>(points.size()));
	}

	// an installed country variant of the user's language beats English
	void AppendLanguageVariants(const CString &sFolder, const CString &sLanguage,
	                            std::vector<CString> &names)
	{
		WIN32_FIND_DATA fd = { 0 };
		HANDLE hFind = ::FindFirstFile(sFolder + sLanguage + _T("_*.dic"), &fd);
		if (hFind == INVALID_HANDLE_VALUE)
			return;

		do
		{
			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
				continue;

			CString name(fd.cFileName);
			const int nDot = name.ReverseFind(_T('.'));
			if (nDot > 0)
				name = name.Left(nDot);

			if (std::find(names.begin(), names.end(), name) == names.end())
				names.push_back(name);
		}
		while (::FindNextFile(hFind, &fd) != FALSE);

		::FindClose(hFind);
	}
}

CSpellEdit::CSpellEdit() :
	m_nDictCodePage(CP_ACP),
	m_ErrColor(RGB(200, 0, 0)),
	m_timer(0),
	m_nLastFirstLine(0),
	m_nLastLeft(0)
{
}

// out of line so the unique_ptr sees a complete Hunspell
CSpellEdit::~CSpellEdit()
{
	if (m_timer != 0 && m_hWnd != nullptr && ::IsWindow(m_hWnd))
		KillTimer(m_timer);
}

LRESULT CSpellEdit::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	if (m_timer != 0)
	{
		KillTimer(m_timer);
		m_timer = 0;
	}

	bHandled = FALSE; // WTL and the edit's own window proc both still want this
	return 0;
}

LRESULT CSpellEdit::OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	// let the control paint its text, then mark over the top of it. Drawing
	// from the timer instead lost every underline on the next repaint and had
	// to erase stale ones by painting a background-coloured line over them.
	const LRESULT lResult = DefWindowProc();

	if (m_pChecker)
	{
		if (wParam != 0)
		{
			DrawMisspellings(reinterpret_cast<HDC>(wParam));
		}
		else
		{
			CClientDC dc(m_hWnd);
			DrawMisspellings(dc.m_hDC);
		}
	}

	return lResult;
}

LRESULT CSpellEdit::OnTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
	if (m_timer == 0 || wParam != m_timer)
	{
		bHandled = FALSE;
		return 0;
	}

	// the control scrolls with ScrollWindow, which drags the underlines along
	// with the text and only invalidates the newly exposed band
	const int nFirstLine = GetFirstVisibleLine();
	const int nLineIndex = LineIndex(nFirstLine);
	const int nLeft = nLineIndex >= 0 ? PosFromChar(nLineIndex).x : 0;

	if (nFirstLine != m_nLastFirstLine || nLeft != m_nLastLeft)
	{
		m_nLastFirstLine = nFirstLine;
		m_nLastLeft = nLeft;
		Invalidate();
	}

	return 0;
}

void CSpellEdit::DrawMisspellings(CDCHandle dc)
{
	CRect rcClip;
	GetClientRect(&rcClip);

	CRect rcFormat(0, 0, 0, 0);
	SendMessage(EM_GETRECT, 0, reinterpret_cast<LPARAM>(&rcFormat));
	if (!rcFormat.IsRectEmpty())
		rcClip.IntersectRect(&rcClip, &rcFormat);
	if (rcClip.IsRectEmpty())
		return;

	const int nSaved = dc.SaveDC();
	dc.IntersectClipRect(&rcClip);

	HFONT hFont = GetFont();
	if (hFont != nullptr)
		dc.SelectFont(hFont);

	CPen pen;
	pen.CreatePen(PS_SOLID, 1, m_ErrColor);
	dc.SelectPen(pen);

	TEXTMETRIC tm = { 0 };
	dc.GetTextMetrics(&tm);

	const int nLines = GetLineCount();
	std::vector<Range> words;

	for (int nLine = GetFirstVisibleLine(); nLine < nLines; nLine++)
	{
		const int nLineIndex = LineIndex(nLine);
		if (nLineIndex < 0)
			break;

		const int nLineLength = LineLength(nLineIndex);
		if (nLineLength <= 0)
			continue;

		// every word on one wrapped line shares its baseline
		const CPoint ptLine = PosFromChar(nLineIndex);
		if (ptLine.y > rcClip.bottom)
			break;

		CString line;
		LPTSTR pBuffer = line.GetBufferSetLength(nLineLength + 2);
		line.ReleaseBuffer(GetLine(nLine, pBuffer, nLineLength + 1));

		SplitWords(line, words);

		for (std::vector<Range>::const_iterator it = words.begin(); it != words.end(); ++it)
		{
			const CString word = line.Mid(it->nStart, it->nEnd - it->nStart);
			if (!IsMisspelled(word))
				continue;

			const CPoint pt = PosFromChar(nLineIndex + it->nStart);
			if (pt.x == -1 && pt.y == -1)
				continue;
			if (pt.x > rcClip.right)
				break;

			CSize size;
			dc.GetTextExtent(word, word.GetLength(), &size);
			DrawSquiggle(dc, pt.x, pt.x + size.cx, pt.y + tm.tmAscent + tm.tmDescent - 1);
		}
	}

	dc.RestoreDC(nSaved);
}

void CSpellEdit::SplitWords(const CString &text, std::vector<Range> &words)
{
	words.clear();

	const int nLength = text.GetLength();
	int i = 0;

	while (i < nLength)
	{
		while (i < nLength && !IsWordChar(text[i]))
			i++;
		if (i >= nLength)
			break;

		Range word = { i, i };
		while (i < nLength && (IsWordChar(text[i]) || IsDigitChar(text[i]) ||
			(IsWordJoiner(text[i]) && i + 1 < nLength && IsWordChar(text[i + 1]))))
			i++;

		word.nEnd = i;
		words.push_back(word);
	}
}

CSpellEdit::Range CSpellEdit::WordAt(const CString &text, int nPos)
{
	std::vector<Range> words;
	SplitWords(text, words);

	for (std::vector<Range>::const_iterator it = words.begin(); it != words.end(); ++it)
	{
		if (nPos >= it->nStart && nPos <= it->nEnd)
			return *it;
	}

	const Range none = { nPos, nPos };
	return none;
}

bool CSpellEdit::IsMisspelled(const CString &word)
{
	if (!m_pChecker || word.IsEmpty())
		return false;

	// file names, model numbers and acronyms are not the checker's business
	bool bHasLower = false;
	for (int i = 0; i < word.GetLength(); i++)
	{
		if (IsDigitChar(word[i]))
			return false;
		if (::IsCharLower(word[i]))
			bHasLower = true;
	}
	if (!bHasLower && word.GetLength() > 1)
		return false;

	const std::string encoded = ToDictionary(word);
	if (encoded.empty())
		return false;

	return !m_pChecker->spell(encoded);
}

void CSpellEdit::Suggest(const CString &word, std::vector<CString> &suggestions)
{
	suggestions.clear();

	if (!m_pChecker)
		return;

	const std::string encoded = ToDictionary(word);
	if (encoded.empty())
		return;

	// hunspell returns the suggestions by value; MySpell handed back a
	// malloc'd char** that the caller had to free element by element
	const std::vector<std::string> list = m_pChecker->suggest(encoded);

	for (std::vector<std::string>::const_iterator it = list.begin(); it != list.end(); ++it)
	{
		const CString suggestion = FromDictionary(*it);
		if (!suggestion.IsEmpty())
			suggestions.push_back(suggestion);
	}
}

std::string CSpellEdit::ToDictionary(const CString &word) const
{
	const CStringW wide(word);
	const bool bUnicode = (m_nDictCodePage == CP_UTF8 || m_nDictCodePage == CP_UTF7);

	const int nBytes = ::WideCharToMultiByte(m_nDictCodePage, 0, wide, -1,
		nullptr, 0, nullptr, nullptr);
	if (nBytes <= 1)
		return std::string();

	std::string encoded(static_cast<size_t>(nBytes), '\0');
	BOOL bDefaultUsed = FALSE;
	if (::WideCharToMultiByte(m_nDictCodePage, 0, wide, -1, &encoded[0], nBytes,
		nullptr, bUnicode ? nullptr : &bDefaultUsed) == 0 || bDefaultUsed)
	{
		// the dictionary's encoding cannot spell this word at all
		return std::string();
	}

	encoded.resize(static_cast<size_t>(nBytes) - 1);
	return encoded;
}

CString CSpellEdit::FromDictionary(const std::string &word) const
{
	if (word.empty())
		return CString();

	const int nChars = ::MultiByteToWideChar(m_nDictCodePage, 0, word.c_str(), -1, nullptr, 0);
	if (nChars <= 1)
		return CString();

	CStringW wide;
	const int nWritten = ::MultiByteToWideChar(m_nDictCodePage, 0, word.c_str(), -1,
		wide.GetBuffer(nChars), nChars);
	wide.ReleaseBuffer(nWritten > 0 ? nWritten - 1 : 0);

	return CString(wide);
}


BOOL CSpellEdit::SubclassWindow(HWND hWnd)
{
	if (!CWindowImpl<CSpellEdit, CEdit>::SubclassWindow(hWnd))
		return FALSE;

	LoadDefaultDictionary();

	m_nLastFirstLine = GetFirstVisibleLine();
	m_nLastLeft = 0;

	// SetTimer does not fail on a duplicate id, it replaces the timer, so the
	// retry loop that used to be here spun forever on a real failure
	m_timer = SetTimer(SPELLTIMER, SPELLINTERVAL, nullptr);
	ATLASSERT(m_timer != 0);

	return TRUE;
}

void CSpellEdit::LoadDefaultDictionary()
{
	TCHAR buf[MAX_PATH] = { 0 };
	if (GetModuleFileName(nullptr, buf, countof(buf)) == 0)
		return;

	CString sFolder(buf);
	const int nSlash = sFolder.ReverseFind(_T('\\'));
	if (nSlash < 0)
		return;
	sFolder = sFolder.Left(nSlash + 1);

	CString sLanguage;
	CString sCountry;
	if (GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, buf, countof(buf)) != 0)
		sLanguage = buf;
	if (GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, buf, countof(buf)) != 0)
		sCountry = buf;

	const CString sDicFolder = sFolder + _T("dic\\");

	std::vector<CString> names;
	if (!sLanguage.IsEmpty() && !sCountry.IsEmpty())
		names.push_back(sLanguage + _T("_") + sCountry);
	if (!sLanguage.IsEmpty())
	{
		AppendLanguageVariants(sFolder, sLanguage, names);
		AppendLanguageVariants(sDicFolder, sLanguage, names);
	}
	names.push_back(_T("en_GB"));
	names.push_back(_T("en_US"));

	for (std::vector<CString>::const_iterator it = names.begin(); it != names.end(); ++it)
	{
		if (TryLoadDictionary(sFolder, *it) || TryLoadDictionary(sDicFolder, *it))
			return;
	}
}

bool CSpellEdit::TryLoadDictionary(const CString &sFolder, const CString &sName)
{
	const CString sAff = sFolder + sName + _T(".aff");
	const CString sDic = sFolder + sName + _T(".dic");

	if (!PathFileExists(sAff) || !PathFileExists(sDic))
		return false;

	return SetDictPaths(sAff, sDic);
}

bool CSpellEdit::SetDictPaths(const CString &sAff, const CString &sDic)
{
	try
	{
		std::unique_ptr<Hunspell> checker(new Hunspell(CStringA(sAff), CStringA(sDic)));
		m_nDictCodePage = CodePageFromEncoding(checker->get_dict_encoding());
		m_pChecker = std::move(checker);
	}
	catch (...)
	{
		// a truncated or unreadable dictionary must not take the dialog with it
		return false;
	}

	if (m_hWnd != nullptr)
		Invalidate();

	return true;
}


LRESULT CSpellEdit::OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
	CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	const bool bKeyboard = (point.x == -1 && point.y == -1);

	SetFocus();

	int nSelStart = 0;
	int nSelEnd = 0;
	GetSel(nSelStart, nSelEnd);

	int nCaret = nSelEnd;
	if (!bKeyboard)
	{
		CPoint clientPoint(point);
		ScreenToClient(&clientPoint);
		nCaret = LOWORD(CharFromPos(clientPoint));
	}

	CString text;
	GetWindowText(text);
	if (nCaret > text.GetLength())
		nCaret = text.GetLength();

	// right-clicking inside a selection must not throw the selection away
	const bool bInSelection = (nSelStart != nSelEnd) &&
		(nCaret >= nSelStart) && (nCaret <= nSelEnd);

	const Range word = WordAt(text, nCaret);
	const CString sWord = text.Mid(word.nStart, word.nEnd - word.nStart);

	std::vector<CString> suggestions;
	const bool bMisspelled = !bInSelection && IsMisspelled(sWord);

	if (bMisspelled)
	{
		Suggest(sWord, suggestions);
		SetSel(word.nStart, word.nEnd);
	}
	else if (!bInSelection)
	{
		SetSel(nCaret, nCaret);
	}

	GetSel(nSelStart, nSelEnd);
	const bool bHasSelection = (nSelStart != nSelEnd);
	const bool bReadOnly = (GetStyle() & ES_READONLY) != 0;
	const int nTextLength = GetWindowTextLength();
	const int nSuggestions = static_cast<int>(suggestions.size());

	CMenu corrections;
	CMenu menu;
	menu.CreatePopupMenu();

	if (nSuggestions > 0)
	{
		corrections.CreatePopupMenu();
		for (int i = 0; i < nSuggestions; i++)
		{
			CString sItem(suggestions[i]);
			sItem.Replace(_T("&"), _T("&&")); // or it reads as an accelerator
			corrections.AppendMenu(MF_STRING, ME_SUGGESTION + i, sItem);
		}
		menu.AppendMenu(MF_POPUP, reinterpret_cast<UINT_PTR>(corrections.m_hMenu), MES_CORRECTIONS);
	}

	if (bMisspelled)
	{
		if (nSuggestions == 0)
			menu.AppendMenu(MF_STRING | MF_GRAYED, ME_NOCORRECTIONS, MES_NOCORRECTIONS);
		menu.AppendMenu(MF_STRING, ME_IGNOREALL, MES_IGNOREALL);
		menu.AppendMenu(MF_SEPARATOR);
	}

	menu.AppendMenu(MF_STRING | ((CanUndo() && !bReadOnly) ? MF_ENABLED : MF_GRAYED),
		EM_UNDO, MES_UNDO);
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | ((bHasSelection && !bReadOnly) ? MF_ENABLED : MF_GRAYED),
		WM_CUT, MES_CUT);
	menu.AppendMenu(MF_STRING | (bHasSelection ? MF_ENABLED : MF_GRAYED),
		WM_COPY, MES_COPY);
	menu.AppendMenu(MF_STRING | ((!bReadOnly &&
			(::IsClipboardFormatAvailable(CF_UNICODETEXT) || ::IsClipboardFormatAvailable(CF_TEXT)))
		? MF_ENABLED : MF_GRAYED), WM_PASTE, MES_PASTE);
	menu.AppendMenu(MF_STRING | ((bHasSelection && !bReadOnly) ? MF_ENABLED : MF_GRAYED),
		WM_CLEAR, MES_DELETE);
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | ((nTextLength > 0 &&
		!(nSelStart == 0 && nSelEnd == nTextLength)) ? MF_ENABLED : MF_GRAYED),
		ME_SELECTALL, MES_SELECTALL);

	if (bKeyboard)
	{
		CPoint pt = PosFromChar(nCaret);
		if (pt.x == -1 && pt.y == -1)
		{
			CRect rc;
			GetClientRect(&rc);
			pt = rc.CenterPoint();
		}
		point = pt;
		ClientToScreen(&point);
	}

	const int nCmd = menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON,
		point.x, point.y, m_hWnd);

	if (nSuggestions > 0)
		corrections.Detach(); // once inserted as MF_POPUP it belongs to menu
	menu.DestroyMenu();

	if (nCmd >= ME_SUGGESTION && nCmd < ME_SUGGESTION + nSuggestions)
	{
		SetSel(word.nStart, word.nEnd);
		ReplaceSel(suggestions[nCmd - ME_SUGGESTION], TRUE);
		Invalidate();
		return 0;
	}

	switch (nCmd)
	{
	case EM_UNDO:
	case WM_CUT:
	case WM_COPY:
	case WM_CLEAR:
	case WM_PASTE:
		SendMessage(nCmd);
		Invalidate();
		break;

	case ME_SELECTALL:
		SetSel(0, -1);
		break;

	case ME_IGNOREALL:
		if (m_pChecker)
		{
			const std::string encoded = ToDictionary(sWord);
			if (!encoded.empty())
				m_pChecker->add(encoded);
			Invalidate();
		}
		break;

	default:
		break; // 0 - the menu was dismissed
	}

	return 0;
}

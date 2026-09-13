// ImageWalker by Zac Walker
//
// Purpose: An edit control that spell checks as you type, over hunspell.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

class Hunspell;

/**
 * A subclass of the standard edit control that adds hunspell spell checking:
 * misspelled words are underlined and the context menu offers corrections.
 *
 * You need to provide the necessary dictionary files. CSpellEdit looks in the
 * program folder and in its \dic\ subfolder, trying the user's locale first
 * ("en_GB.aff" / "en_GB.dic"), then any country variant of the user's language
 * that is installed, then en_GB and en_US.
 *
 * Words are converted to the encoding the dictionary declares, so a dictionary
 * that is not in the active code page still matches.
 */
class CSpellEdit : public CWindowImpl<CSpellEdit, CEdit>
{
public:
	CSpellEdit();
	~CSpellEdit();

	BOOL SubclassWindow(HWND hWnd);

	bool SetDictPaths(const CString &sAff, const CString &sDic);

	BEGIN_MSG_MAP(CSpellEdit)

		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)

	END_MSG_MAP()

	LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnContextMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

private:
	struct Range
	{
		int nStart;
		int nEnd;
	};

	void LoadDefaultDictionary();
	bool TryLoadDictionary(const CString &sFolder, const CString &sName);

	bool IsMisspelled(const CString &word);
	void Suggest(const CString &word, std::vector<CString> &suggestions);
	std::string ToDictionary(const CString &word) const;
	CString FromDictionary(const std::string &word) const;

	void DrawMisspellings(CDCHandle dc);
	static void SplitWords(const CString &text, std::vector<Range> &words);
	static Range WordAt(const CString &text, int nPos);

	std::unique_ptr<Hunspell> m_pChecker;
	UINT m_nDictCodePage;
	COLORREF m_ErrColor;
	UINT_PTR m_timer;
	int m_nLastFirstLine;
	int m_nLastLeft;
};

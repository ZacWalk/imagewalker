#pragma once

// Explorer-style selection: a focus, an anchor and a selected set.
//
// The focused item is the one the image pane shows. Nothing else in the app
// decides what is displayed, so there is exactly one path from a click to a
// picture.

#include <algorithm>
#include <vector>

class CSelection
{
public:
	void Reset(int nCount)
	{
		m_nCount = (std::max)(nCount, 0);
		m_selected.assign(static_cast<size_t>(m_nCount), false);
		m_nSelected = 0;
		m_nFocus = -1;
		m_nAnchor = -1;
	}

	// bControl toggles, bShift extends from the anchor, neither replaces.
	void Click(int n, bool bControl, bool bShift)
	{
		if (!IsValid(n))
			return;

		if (bShift && m_nAnchor >= 0)
		{
			ExtendRange(n, bControl);
		}
		else if (bControl)
		{
			SetSelected(n, !Contains(n));
			m_nAnchor = n;
		}
		else
		{
			SelectOnly(n);
			m_nAnchor = n;
		}

		m_nFocus = n;
	}

	void SelectOnly(int n)
	{
		ClearAll();
		SetSelected(n, true);
		m_nFocus = m_nAnchor = IsValid(n) ? n : -1;
	}

	void SelectAll()
	{
		m_selected.assign(static_cast<size_t>(m_nCount), true);
		m_nSelected = static_cast<size_t>(m_nCount);

		if (m_nCount && m_nFocus < 0)
			m_nFocus = m_nAnchor = 0;
	}

	void Invert()
	{
		for (int i = 0; i < m_nCount; i++)
			SetSelected(i, !Contains(i));
	}

	void Clear()
	{
		ClearAll();
		m_nFocus = -1;
		m_nAnchor = -1;
	}

	bool Contains(int n) const
	{
		return IsValid(n) && m_selected[static_cast<size_t>(n)];
	}

	std::vector<int> Selected() const
	{
		std::vector<int> result;
		result.reserve(m_nSelected);

		for (int i = 0; i < m_nCount; i++)
			if (Contains(i))
				result.push_back(i);

		return result;
	}

	int Count() const { return static_cast<int>(m_nSelected); }
	int Size() const { return m_nCount; }
	int Focus() const { return m_nFocus; }

private:
	bool IsValid(int n) const
	{
		return n >= 0 && n < m_nCount && static_cast<size_t>(n) < m_selected.size();
	}

	void SetSelected(int n, bool bValue)
	{
		if (!IsValid(n) || m_selected[static_cast<size_t>(n)] == bValue)
			return;

		m_selected[static_cast<size_t>(n)] = bValue;

		if (bValue)
			++m_nSelected;
		else
			--m_nSelected;
	}

	void ClearAll()
	{
		m_selected.assign(m_selected.size(), false);
		m_nSelected = 0;
	}

	void ExtendRange(int n, bool bControl)
	{
		if (!bControl)
			ClearAll();

		const int nFirst = (std::min)(m_nAnchor, n);
		const int nLast = (std::max)(m_nAnchor, n);

		for (int i = nFirst; i <= nLast; i++)
			SetSelected(i, true);
	}

	int m_nCount = 0;
	int m_nFocus = -1;
	int m_nAnchor = -1;
	size_t m_nSelected = 0;
	std::vector<bool> m_selected;
};

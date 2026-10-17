// ImageWalker by Zac Walker
//
// Purpose: The read-only name/value list the description window's IPTC, XMP
//          and EXIF pages show. A report list view with one group per section.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

class CMetadataListCtrl : public CListViewCtrl
{
public:

	void Init()
	{
		SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP | LVS_EX_DOUBLEBUFFER);

		CRect rectClient;
		GetClientRect(rectClient);

		const int cxName = IW::Max(80, rectClient.Width() / 3);

		InsertColumn(0, _T("Property"), LVCFMT_LEFT, cxName);
		InsertColumn(1, _T("Value"), LVCFMT_LEFT, rectClient.Width() - cxName);

		_bGroups = EnableGroupView(TRUE) != -1;
	}

	void Reset()
	{
		DeleteAllItems();

		if (_bGroups)
			RemoveAllGroups();

		_nGroup = 0;
	}

	void AddCategory(const CString &strName)
	{
		if (!_bGroups)
			return;

		// LVGROUP is wide even in an ANSI build.
		CT2W header(strName);

		LVGROUP group = { 0 };
		group.cbSize = sizeof(LVGROUP);
		group.mask = LVGF_HEADER | LVGF_GROUPID;
		group.iGroupId = _nGroup + 1;
		group.pszHeader = header;

		if (-1 != InsertGroup(-1, &group))
			_nGroup = group.iGroupId;
	}

	int AddProperty(const CString &strName, const CString &strValue)
	{
		LVITEM item = { 0 };
		item.mask = LVIF_TEXT;
		item.iItem = GetItemCount();
		item.pszText = const_cast<LPTSTR>(static_cast<LPCTSTR>(strName));

		if (_nGroup > 0)
		{
			item.mask |= LVIF_GROUPID;
			item.iGroupId = _nGroup;
		}

		const int n = InsertItem(&item);

		if (n != -1)
			SetItemText(n, 1, strValue);

		return n;
	}

private:

	int _nGroup = 0;
	bool _bGroups = false;
};

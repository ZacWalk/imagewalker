// ImageWalker by Zac Walker
//
// Purpose: IPTC implementation - the dataset tag table and the grouping the
//          property page shows.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"
#include "Metadata.h"

IptcSpec g_iptc_tags[] = {
	{0, IDS_IPTC_RECORDVERSION},
	{5, IDS_IPTC_OBJECTNAME},
	{7, IDS_IPTC_EDITSTATUS},
	{8, IDS_IPTC_EDITORIALUPDATE},
	{10, IDS_IPTC_URGENCY},
	{12, IDS_IPTC_SUBJECTREFERENCE},
	{15, IDS_IPTC_CATEGORY},
	{20, IDS_IPTC_SUPPLEMENTALCATEGORY},
	{22, IDS_IPTC_FIXTUREIDENTIFIER},
	{25, IDS_IPTC_KEYWORDS},
	{26, IDS_IPTC_CONTENTLOCATIONCODE},
	{27, IDS_IPTC_CONTENTLOCATIONNAME},
	{30, IDS_IPTC_RELEASEDATE},
	{35, IDS_IPTC_RELEASETIME},
	{37, IDS_IPTC_EXPIRATIONDATE},
	{38, IDS_IPTC_EXPIRATIONTIME},
	{40, IDS_IPTC_SPECIALINSTRUCTIONS},
	{42, IDS_IPTC_ACTIONADVISED},
	{45, IDS_IPTC_REFERENCESERVICE},
	{47, IDS_IPTC_REFERENCEDATE},
	{50, IDS_IPTC_REFERENCENUMBER},
	{55, IDS_IPTC_DATECREATED},
	{60, IDS_IPTC_TIMECREATED},
	{62, IDS_IPTC_DIGITALCREATIONDATE},
	{63, IDS_IPTC_DIGITALCREATIONTIME},
	{65, IDS_IPTC_ORIGINATINGPROGRAM},
	{70, IDS_IPTC_PROGRAMVERSION},
	{75, IDS_IPTC_OBJECTCYCLE},
	{80, IDS_IPTC_BYLINE},
	{85, IDS_IPTC_BYLINETITLE},
	{90, IDS_IPTC_CITY},
	{92, IDS_IPTC_SUBLOCATION},
	{95, IDS_IPTC_PROVINCESTATE},
	{100, IDS_IPTC_COUNTRYPRIMARYLOCATIONCODE},
	{101, IDS_IPTC_COUNTRYPRIMARYLOCATIONNAME},
	{103, IDS_IPTC_ORIGINALTRANSMISSIONREFERENCE},
	{105, IDS_IPTC_HEADLINE},
	{110, IDS_IPTC_CREDIT},
	{115, IDS_IPTC_SOURCE},
	{116, IDS_IPTC_COPYRIGHTNOTICE},
	{118, IDS_IPTC_CONTACT},
	{120, IDS_IPTC_CAPTIONABSTRACT},
	{122, IDS_IPTC_WRITEREDITOR},
	{125, IDS_IPTC_RASTERIZEDCAPTION},
	{130, IDS_IPTC_IMAGETYPE},
	{131, IDS_IPTC_IMAGEORIENTATION},
	{135, IDS_IPTC_LANGUAGEIDENTIFIER},
	{-1, static_cast<DWORD>(-1)}
};

// IIM values are length-prefixed and NOT NUL-terminated, so the conversion has
// to be given the length rather than scan for a terminator that is not there.
static CString FromIim(LPCSTR pValue, int nLength)
{
	CString str;
	const int cch = ::MultiByteToWideChar(CP_ACP, 0, pValue, nLength, nullptr, 0);

	if (cch > 0)
	{
		::MultiByteToWideChar(CP_ACP, 0, pValue, nLength, str.GetBuffer(cch), cch);
		str.ReleaseBuffer(cch);
	}

	return str;
}

CString MetadataIPTC::GetKey(int nDataset, int nRecord)
{
	for (int i = 0; g_iptc_tags[i].nId != -1; ++i)
	{
		if (g_iptc_tags[i].nId == nRecord)
			return App.LoadString(g_iptc_tags[i].nStringID);
	}

	CString str;
	str.Format(_T("IPTC:%d:%d"), nDataset, nRecord);
	return str;
}

bool MetadataIPTC::Read(int nDataset, int nRecord, CString& strOut) const
{
	strOut.Empty();

	ForEachRecord([&](int ds, int rec, LPCSTR pValue, int nLength)
	{
		if (ds != nDataset || rec != nRecord)
			return;

		if (!strOut.IsEmpty()) strOut += _T("; ");
		strOut += FromIim(pValue, nLength);
	});

	return true;
}

// The panel groups the records the way every other IPTC editor does, so a
// photographer can find a field without knowing its dataset number. Anything
// the file carries that is not in the list still shows, under Other.
void MetadataIPTC::Load(IW::MetadataProperties& propertiesOut) const
{
	struct Group
	{
		LPCTSTR* pszSection;
		std::initializer_list<int> records;
	};

	const Group groups[] = {
		{&g_szCaption, {120, 122, 105, 40}},
		{&g_szKeywordsCategories, {25, 15, 20}},
		{&g_szCredits, {80, 85, 110, 115}},
		{&g_szOrigin, {5, 55, 60, 62, 63, 65, 70, 75, 90, 92, 95, 100, 101, 103}},
		{&g_szCopyright, {116, 118}},
		{&g_szEditorial, {7, 8, 10, 12, 22, 26, 27, 30, 35, 37, 38, 42, 45, 47, 50}},
		{&g_szImage, {130, 131, 135}},
	};

	std::map<int, CString> values;
	std::vector<int> order;

	ForEachRecord([&](int ds, int rec, LPCSTR pValue, int nLength)
	{
		if (ds != 2)
			return;

		auto it = values.find(rec);

		if (it == values.end())
		{
			values[rec] = FromIim(pValue, nLength);
			order.push_back(rec);
		}
		else
		{
			it->second += _T("; ");
			it->second += FromIim(pValue, nLength);
		}
	});

	if (values.empty())
		return;

	std::set<int> shown;

	for (const auto& group : groups)
	{
		bool bSectionStarted = false;

		for (int record : group.records)
		{
			auto it = values.find(record);

			if (it == values.end() || it->second.IsEmpty())
				continue;

			if (!bSectionStarted)
			{
				propertiesOut.Begin(*group.pszSection);
				bSectionStarted = true;
			}

			propertiesOut.Add(GetKey(2, record), it->second);
			shown.insert(record);
		}
	}

	bool bOtherStarted = false;

	for (int record : order)
	{
		if (shown.contains(record) || values[record].IsEmpty())
			continue;

		if (!bOtherStarted)
		{
			propertiesOut.Begin(_T("Other"));
			bOtherStarted = true;
		}

		propertiesOut.Add(GetKey(2, record), values[record]);
	}
}

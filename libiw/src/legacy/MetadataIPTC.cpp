// ImageWalker by Zac Walker
//
// Purpose: IPTC implementation - the dataset tag table, the datasets libfiles
//          reads, and the grouping the property page shows.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "LegacyPch.h"
#include "Metadata.h"

#include <libfiles/iptc.h>
#include <libfiles/scan.h>

IptcSpec g_iptc_tags[] = {
	{0, IW::Text::IPTC_RECORDVERSION},
	{5, IW::Text::IPTC_OBJECTNAME},
	{7, IW::Text::IPTC_EDITSTATUS},
	{8, IW::Text::IPTC_EDITORIALUPDATE},
	{10, IW::Text::IPTC_URGENCY},
	{12, IW::Text::IPTC_SUBJECTREFERENCE},
	{15, IW::Text::IPTC_CATEGORY},
	{20, IW::Text::IPTC_SUPPLEMENTALCATEGORY},
	{22, IW::Text::IPTC_FIXTUREIDENTIFIER},
	{25, IW::Text::IPTC_KEYWORDS},
	{26, IW::Text::IPTC_CONTENTLOCATIONCODE},
	{27, IW::Text::IPTC_CONTENTLOCATIONNAME},
	{30, IW::Text::IPTC_RELEASEDATE},
	{35, IW::Text::IPTC_RELEASETIME},
	{37, IW::Text::IPTC_EXPIRATIONDATE},
	{38, IW::Text::IPTC_EXPIRATIONTIME},
	{40, IW::Text::IPTC_SPECIALINSTRUCTIONS},
	{42, IW::Text::IPTC_ACTIONADVISED},
	{45, IW::Text::IPTC_REFERENCESERVICE},
	{47, IW::Text::IPTC_REFERENCEDATE},
	{50, IW::Text::IPTC_REFERENCENUMBER},
	{55, IW::Text::IPTC_DATECREATED},
	{60, IW::Text::IPTC_TIMECREATED},
	{62, IW::Text::IPTC_DIGITALCREATIONDATE},
	{63, IW::Text::IPTC_DIGITALCREATIONTIME},
	{65, IW::Text::IPTC_ORIGINATINGPROGRAM},
	{70, IW::Text::IPTC_PROGRAMVERSION},
	{75, IW::Text::IPTC_OBJECTCYCLE},
	{80, IW::Text::IPTC_BYLINE},
	{85, IW::Text::IPTC_BYLINETITLE},
	{90, IW::Text::IPTC_CITY},
	{92, IW::Text::IPTC_SUBLOCATION},
	{95, IW::Text::IPTC_PROVINCESTATE},
	{100, IW::Text::IPTC_COUNTRYPRIMARYLOCATIONCODE},
	{101, IW::Text::IPTC_COUNTRYPRIMARYLOCATIONNAME},
	{103, IW::Text::IPTC_ORIGINALTRANSMISSIONREFERENCE},
	{105, IW::Text::IPTC_HEADLINE},
	{110, IW::Text::IPTC_CREDIT},
	{115, IW::Text::IPTC_SOURCE},
	{116, IW::Text::IPTC_COPYRIGHTNOTICE},
	{118, IW::Text::IPTC_CONTACT},
	{120, IW::Text::IPTC_CAPTIONABSTRACT},
	{122, IW::Text::IPTC_WRITEREDITOR},
	{125, IW::Text::IPTC_RASTERIZEDCAPTION},
	{130, IW::Text::IPTC_IMAGETYPE},
	{131, IW::Text::IPTC_IMAGEORIENTATION},
	{135, IW::Text::IPTC_LANGUAGEIDENTIFIER},
	{-1, nullptr}
};

// libfiles gives the text as UTF-8: as stored where that is valid UTF-8, and
// read as Windows-1252 where it is not.
static CString FromUtf8(const std::string& text)
{
	CString str;
	const int cch = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);

	if (cch > 0)
	{
		::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), str.GetBuffer(cch), cch);
		str.ReleaseBuffer(cch);
	}

	return str;
}

MetadataIPTC::MetadataIPTC(const IW::MetaData& data)
{
	const auto iim = GetIim(data);

	for (const auto& entry : libfiles::list_iptc(iim))
		_datasets.push_back({entry.record, entry.dataset, FromUtf8(entry.value)});
}

std::vector<BYTE> MetadataIPTC::GetIim(const IW::MetaData& data)
{
	if (data.IsEmpty())
		return {};

	const std::span<const std::uint8_t> blob(data.GetData(), data.GetDataSize());

	if (blob[0] == 0x1c)
		return {blob.begin(), blob.end()};

	constexpr std::string_view signature("Photoshop 3.0\0", 14);
	const bool bSigned = blob.size() >= signature.size() && memcmp(blob.data(), signature.data(), signature.size()) == 0;

	libfiles::metadata_blocks blocks;
	libfiles::parse_photoshop_resources(blocks, blob, bSigned);
	return std::move(blocks.iptc);
}

CString MetadataIPTC::GetKey(int nDataset, int nRecord)
{
	for (int i = 0; g_iptc_tags[i].nId != -1; ++i)
	{
		if (g_iptc_tags[i].nId == nRecord)
			return CString(g_iptc_tags[i].name);
	}

	CString str;
	str.Format(_T("IPTC:%d:%d"), nDataset, nRecord);
	return str;
}

bool MetadataIPTC::Read(int nDataset, int nRecord, CString& strOut) const
{
	strOut.Empty();

	for (const auto& dataset : _datasets)
	{
		if (dataset.nRecord != nDataset || dataset.nNumber != nRecord)
			continue;

		if (!strOut.IsEmpty()) strOut += _T("; ");
		strOut += dataset.strValue;
	}

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

	for (const auto& dataset : _datasets)
	{
		if (dataset.nRecord != 2)
			continue;

		auto it = values.find(dataset.nNumber);

		if (it == values.end())
		{
			values[dataset.nNumber] = dataset.strValue;
			order.push_back(dataset.nNumber);
		}
		else
		{
			it->second += _T("; ");
			it->second += dataset.strValue;
		}
	}

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

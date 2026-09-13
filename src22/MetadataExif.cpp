// ImageWalker by Zac Walker
//
// Purpose: EXIF property reader - every IFD plus maker notes, with each tag
//          name, title and description.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"
#include "Metadata.h"
#include "MetadataExif.h"

MetadataExif::MetadataExif(const IW::MetaData& data)
{
	_pExif = exif_data_new();

	if (!data.IsEmpty())
		exif_data_load_data(_pExif, data.GetData(), data.GetDataSize());
}

MetadataExif::~MetadataExif()
{
	exif_data_unref(_pExif);
}

static void AddEntry(IW::MetadataProperties& propertiesOut, ExifEntry* e)
{
	constexpr int nValLen = 1024;
	char szValue[nValLen] = {""};

	if (e->data && e->size > 0)
		exif_entry_get_value(e, szValue, nValLen);

	propertiesOut.Add(
		CString(exif_tag_get_name(e->tag)),
		CString(exif_tag_get_title(e->tag)),
		CString(exif_tag_get_description(e->tag)),
		CString(szValue));
}

void MetadataExif::Load(IW::MetadataProperties& propertiesOut) const
{
	if (!_pExif)
		return;

	LPCTSTR szSection[] = {g_szIFD0, g_szIFD1, g_szIFD, g_szIFDGPS, g_szIFDInteroperability};

	for (unsigned i = 0; i < EXIF_IFD_COUNT; i++)
	{
		ExifContent* pContent = _pExif->ifd[i];

		if (pContent == nullptr || pContent->count == 0)
			continue;

		propertiesOut.Begin(szSection[i]);

		for (unsigned j = 0; j < pContent->count; j++)
			AddEntry(propertiesOut, pContent->entries[j]);
	}

	ExifMnoteData* md = exif_data_get_mnote_data(_pExif);

	if (md == nullptr)
		return;

	constexpr int nValLen = 1025 * 8;
	char szValue[nValLen + 1];

	propertiesOut.Begin(_T("Maker Notes"));

	const unsigned c = exif_mnote_data_count(md);

	for (unsigned i = 0; i < c; i++)
	{
		CString strName = exif_mnote_data_get_name(md, i);

		if (strName.IsEmpty())
			continue;

		char* szValOut = exif_mnote_data_get_value(md, i, szValue, nValLen);

		propertiesOut.Add(
			strName,
			CString(exif_mnote_data_get_title(md, i)),
			CString(exif_mnote_data_get_description(md, i)),
			CString(szValOut == nullptr ? "" : szValOut));
	}
}

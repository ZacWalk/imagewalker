// ImageWalker by Zac Walker
//
// Purpose: MetadataProperties implementation and the field lookups
//          ImageMetaData exposes.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"
#include "Metadata.h"
#include "ViewMetadataList.h"

void IW::MetadataProperties::Begin(const CString& strSection)
{
	sections.push_back(MetadataSection());
	sections.back().name = strSection;
}

void IW::MetadataProperties::Add(const CString& strTitle, const CString& strValue)
{
	Add(g_szEmptyString, strTitle, g_szEmptyString, strValue);
}

void IW::MetadataProperties::Add(const CString& strKey, const CString& strTitle,
                                 const CString& strDescription, const CString& strValue)
{
	if (sections.empty())
		Begin(g_szEmptyString);

	MetadataProperty property;
	property.key = strKey;
	property.title = strTitle;
	property.description = strDescription;
	property.value = strValue;

	sections.back().properties.push_back(property);
}

const IW::MetadataProperty* IW::MetadataProperties::Find(const CString& strTitle) const
{
	for (const auto& section : sections)
	{
		for (const auto& property : section.properties)
		{
			if (property.title == strTitle)
				return &property;
		}
	}

	return nullptr;
}

void IW::MetadataProperties::Apply(CMetadataListCtrl& list) const
{
	list.SetRedraw(FALSE);

	for (const auto& section : sections)
	{
		if (section.properties.empty())
			continue;

		list.AddCategory(section.name);

		for (const auto& property : section.properties)
			list.AddProperty(property.title, property.value);
	}

	list.SetRedraw(TRUE);
}

ImageMetaData::ImageMetaData(const IW::Image& image) :
	_iptc(image.GetMetaData(IW::MetaDataTypes::PROFILE_IPTC)),
	_xmp(image.GetMetaData(IW::MetaDataTypes::PROFILE_XMP))
{
}

ImageMetaData::ImageMetaData(const IW::MetaData& iptc, const IW::MetaData& xmp) :
	_iptc(iptc),
	_xmp(xmp)
{
}

static CString photoshop = _T("http://ns.adobe.com/photoshop/1.0/");
static CString dc = _T("http://purl.org/dc/elements/1.1/");
static CString imagewalker = _T("http://www.imagewalker.com/1.0/");

CString ImageMetaData::GetTitle() const
{
	CString str;
	if (!_xmp.Read(photoshop, _T("Headline"), str))
	{
		_iptc.Read(2, 105, str);
	}
	return str;
}

CString ImageMetaData::GetTags() const
{
	CString str;
	if (!_xmp.ReadArray(dc, _T("subject"), str))
	{
		_iptc.Read(2, 25, str);
	}
	return str;
}

CString ImageMetaData::GetDescription() const
{
	CString str;
	if (!_xmp.ReadAltText(dc, _T("description"), str))
	{
		_iptc.Read(2, 120, str);
	}
	return str;
}

CString ImageMetaData::GetCaptionWriter() const
{
	CString str;
	if (!_xmp.Read(photoshop, _T("CaptionWriter"), str))
	{
		_iptc.Read(2, 122, str);
	}
	return str;
}

CString ImageMetaData::GetSpecialInstructions() const
{
	CString str;
	if (!_xmp.Read(photoshop, _T("Instructions"), str))
	{
		_iptc.Read(2, 40, str);
	}
	return str;
}

CString ImageMetaData::GetByLine() const
{
	CString str;
	if (!_xmp.ReadArray(dc, _T("creator"), str))
	{
		_iptc.Read(2, 80, str);
	}
	return str;
}

CString ImageMetaData::GetByLineTitle() const
{
	CString str;
	if (!_xmp.Read(photoshop, _T("AuthorsPosition"), str))
	{
		_iptc.Read(2, 85, str);
	}
	return str;
}

CString ImageMetaData::GetCredit() const
{
	CString str;
	if (!_xmp.Read(photoshop, _T("Credit"), str))
	{
		_iptc.Read(2, 110, str);
	}
	return str;
}

CString ImageMetaData::GetSource() const
{
	CString str;
	if (!_xmp.Read(photoshop, _T("Source"), str))
	{
		_iptc.Read(2, 115, str);
	}
	return str;
}

CString ImageMetaData::GetCopyright() const
{
	CString str;
	if (!_xmp.ReadAltText(dc, _T("rights"), str))
	{
		_iptc.Read(2, 116, str);
	}
	return str;
}

CString ImageMetaData::GetCategory() const
{
	CString str;
	if (!_xmp.Read(photoshop, _T("Category"), str))
	{
		_iptc.Read(2, 15, str);
	}
	return str;
}

CString ImageMetaData::GetSubCategory() const
{
	CString str;
	if (!_xmp.ReadArray(photoshop, _T("SupplementalCategory"), str))
	{
		_iptc.Read(2, 20, str);
	}
	return str;
}

CString ImageMetaData::GetObjectName() const
{
	CString str;
	if (!_xmp.ReadAltText(dc, _T("title"), str))
	{
		_iptc.Read(2, 5, str);
	}
	return str;
}

CString ImageMetaData::GetDateCreated() const
{
	CString str;
	if (!_xmp.Read(photoshop, _T("DateCreated"), str))
	{
		_iptc.Read(2, 55, str);
	}
	return str;
}

CString ImageMetaData::GetCity() const
{
	CString str;
	if (!_xmp.Read(photoshop, _T("City"), str))
	{
		_iptc.Read(2, 90, str);
	}
	return str;
}

CString ImageMetaData::GetProvenceState() const
{
	CString str;
	if (!_xmp.Read(photoshop, _T("State"), str))
	{
		_iptc.Read(2, 95, str);
	}
	return str;
}

CString ImageMetaData::GetCountryName() const
{
	CString str;
	if (!_xmp.Read(photoshop, _T("Country"), str))
	{
		_iptc.Read(2, 101, str);
	}
	return str;
}

CString ImageMetaData::GetOriginalTR() const
{
	CString str;
	if (!_xmp.Read(photoshop, _T("TransmissionReference"), str))
	{
		_iptc.Read(2, 103, str);
	}
	return str;
}


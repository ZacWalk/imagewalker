// ImageWalker by Zac Walker
//
// Purpose: XMP reader: the property lookups the 2.x apps make and the listing
//          their property view shows, over libfiles' XMP data model. Read-only.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

// libfiles parses the packet as Adobe's XMP Toolkit does - structs, qualifiers,
// URI values, aliases and malformed packets included - and this translates its
// model into the lookups and the listing. Read-only by design: nothing here
// writes metadata back into an image or a file (see ImageMetaData).

#include <memory>

namespace IW
{
	struct MetadataProperties;
}

namespace libfiles::xmp
{
	class meta;
}

class MetadataXMP
{
private:
	std::unique_ptr<libfiles::xmp::meta> _meta;

	MetadataXMP(const MetadataXMP &other);
	void operator=(const MetadataXMP &other);

public:
	explicit MetadataXMP(const IW::MetaData &data);
	~MetadataXMP();

	bool Read(const CString &schemaNS, const CString &propName, CString &value) const;
	bool ReadArray(const CString &schemaNS, const CString &propName, CString &value) const;
	bool ReadAltText(const CString &schemaNS, const CString &propName, CString &value) const;

	void Load(IW::MetadataProperties &propertiesOut) const;
};

// ImageWalker by Zac Walker
//
// Purpose: The parsed metadata model: sections of properties that a reader
//          fills and a property page shows, plus the fields the app reads
//          from XMP first and IPTC second.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

#include "MetadataIPTC.h"
#include "MetadataXMP.h"

class CMetadataListCtrl;

namespace IW
{
	struct MetadataProperty
	{
		CString key; // stable identifier; empty when the source has none
		CString title;
		CString description;
		CString value;
	};

	struct MetadataSection
	{
		CString name;
		std::vector<MetadataProperty> properties;
	};

	// One image's metadata once it has been parsed. Every reader fills one of
	// these and the property pages show one. This replaced a pair of visitor
	// interfaces the readers pushed into, which meant nothing could look at a
	// property twice or ask what came before it.
	struct MetadataProperties
	{
		std::vector<MetadataSection> sections;

		void Begin(const CString &strSection);
		void Add(const CString &strTitle, const CString &strValue);
		void Add(const CString &strKey, const CString &strTitle, const CString &strDescription,
		         const CString &strValue);

		bool IsEmpty() const { return sections.empty(); }
		const MetadataProperty *Find(const CString &strTitle) const;

		void Apply(CMetadataListCtrl &list) const;
	};
}

// The fields the app shows and searches, each read from XMP first and IPTC
// second.
//
// Read-only. Nothing here writes metadata back into an image or a file: the
// IPTC record editor mangled Photoshop resource blocks and the XMP serialiser
// could only re-emit the subset of a packet it understood, so both were removed
// rather than left able to overwrite a photographer's metadata.
class ImageMetaData
{
private:
	MetadataIPTC _iptc;
	MetadataXMP _xmp;

	ImageMetaData(const ImageMetaData &other);
	void operator=(const ImageMetaData &other);

public:
	ImageMetaData(const IW::Image &image);
	ImageMetaData(const IW::MetaData &iptc, const IW::MetaData &xmp);

	CString GetTitle() const;
	CString GetTags() const;
	CString GetDescription() const;
	CString GetCaptionWriter() const;
	CString GetSpecialInstructions() const;
	CString GetByLine() const;
	CString GetByLineTitle() const;
	CString GetCredit() const;
	CString GetSource() const;
	CString GetCopyright() const;
	CString GetCategory() const;
	CString GetSubCategory() const;
	CString GetObjectName() const;
	CString GetDateCreated() const;
	CString GetCity() const;
	CString GetProvenceState() const;
	CString GetCountryName() const;
	CString GetOriginalTR() const;
};

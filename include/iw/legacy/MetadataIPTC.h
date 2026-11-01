// ImageWalker by Zac Walker
//
// Purpose: IPTC IIM reader: the datasets libfiles reads from the record
//          stream, looked up by number and grouped for the property page.
//          Read-only.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

namespace IW
{
	struct MetadataProperties;
}

typedef struct tagIptcSpec
{
	short nId;
	LPCWSTR name;
} IptcSpec;

extern IptcSpec g_iptc_tags[];

// Read-only. The record editor this used to carry rewrote the whole IIM block
// in place -- through a refcount-shared buffer, at ANSI byte lengths taken from
// wide character counts -- and its callers reassembled a Photoshop resource
// block from just the one resource they understood. All of it is gone.
//
// libfiles walks the records and reads their text, as Diffractor does: UTF-8
// where it is valid and Windows-1252 where it is not, with binary datasets left
// out and numeric ones shown as numbers.
class MetadataIPTC
{
private:
	struct Dataset
	{
		int nRecord;
		int nNumber;
		CString strValue;
	};

	std::vector<Dataset> _datasets;

public:
	explicit MetadataIPTC(const IW::MetaData &data);

	bool Read(int nDataset, int nRecord, CString &strOut) const;
	void Load(IW::MetadataProperties &propertiesOut) const;

	static CString GetKey(int nDataset, int nRecord);

	// The IIM records a blob holds. The blob is either a bare IIM stream, from a
	// format that stores one, or the whole Photoshop APP13 segment a JPEG carries:
	// "Photoshop 3.0\0" followed by a chain of 8BIM resources, whose IPTC resource
	// libfiles finds as Diffractor does - the first that holds anything. The JPEG
	// loader keeps the segment intact so a save can put it back byte for byte,
	// which is why finding the records is this class's job and not the loader's.
	static std::vector<BYTE> GetIim(const IW::MetaData &data);
};

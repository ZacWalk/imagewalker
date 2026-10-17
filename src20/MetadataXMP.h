// ImageWalker by Zac Walker
//
// Purpose: XMP reader: a small expat-backed subset of RDF covering simple,
//          array and alt-text properties. Read-only.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

// A small XMP reader, covering only what 2.31 uses: parse an XMP packet, read
// simple, array and alt-text properties, and list them for the property view.
// Parsing is done with expat, which the app already links.
//
// Read-only by design. It models a strict subset of RDF -- no structs, no
// qualifiers, no URI values -- so serialising back out would silently drop
// everything it did not understand. That is what the write side used to do.
//
// This was written against the shape of the Adobe XMP Toolkit -- SXMPMeta,
// SXMPIterator, SXMPUtils, XMP_OptionBits -- long after the toolkit itself was
// dropped. Nothing needed that shape, and the option bits were decoded into a
// string of flag names the caller then threw away.

#include <string>
#include <vector>

namespace IW
{
	struct MetadataProperties;
}

namespace Xmp
{
	struct Item
	{
		std::string lang; // empty unless the parent is an alt-text array
		std::string value;
	};

	struct Property
	{
		std::string name; // local name, unprefixed
		std::string value; // simple properties only
		bool isArray = false;
		bool isOrdered = false;
		bool isAltText = false;
		std::vector<Item> items;
	};

	struct Schema
	{
		std::string uri;
		std::string prefix;
		std::vector<Property> properties;
	};

	// The parsed packet. Malformed XMP is common, so Parse reports failure
	// rather than throwing; the caller shows the image either way.
	class Packet
	{
	public:
		Packet();

		bool Parse(const char *buffer, size_t length, std::string &strErrorOut);

		const std::vector<Schema> &Schemas() const { return _schemas; }
		const Property *Find(const char *schemaNS, const char *propName) const;
		Property &FindOrAdd(const char *schemaNS, const char *propName);

		// Array items joined with "; ", quoting any item that contains the
		// separator so the join stays reversible by eye.
		std::string Catenate(const char *schemaNS, const char *propName) const;

		// Best language match: the one asked for, then x-default, then first.
		const Item *LocalizedText(const char *schemaNS, const char *propName, const char *lang) const;

	private:
		std::string PrefixFor(const std::string &uri) const;

		std::vector<Schema> _schemas;
		std::vector<std::pair<std::string, std::string>> _prefixes; // uri -> prefix
	};
}

class MetadataXMP
{
private:
	Xmp::Packet _packet;

	MetadataXMP(const MetadataXMP &other);
	void operator=(const MetadataXMP &other);

public:
	explicit MetadataXMP(const IW::MetaData &data);

	bool Read(const CString &schemaNS, const CString &propName, CString &value) const;
	bool ReadArray(const CString &schemaNS, const CString &propName, CString &value) const;
	bool ReadAltText(const CString &schemaNS, const CString &propName, CString &value) const;

	void Load(IW::MetadataProperties &propertiesOut) const;
};

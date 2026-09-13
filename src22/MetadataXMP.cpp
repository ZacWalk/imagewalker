// ImageWalker by Zac Walker
//
// Purpose: XMP packet parsing and the property lookups built on it.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "stdafx.h"
#include "Metadata.h"
#include "iw/logfile.h"

#include <expat.h>

namespace
{
	constexpr char kNsSeparator = '\x01';

	const auto kRdfNS = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
	const auto kXmlNS = "http://www.w3.org/XML/1998/namespace";

	struct KnownPrefix
	{
		const char* uri;
		const char* prefix;
	};

	const KnownPrefix kKnownPrefixes[] = {
		{"http://purl.org/dc/elements/1.1/", "dc"},
		{"http://ns.adobe.com/photoshop/1.0/", "photoshop"},
		{"http://ns.adobe.com/xap/1.0/", "xmp"},
		{"http://ns.adobe.com/xap/1.0/rights/", "xmpRights"},
		{"http://ns.adobe.com/xap/1.0/mm/", "xmpMM"},
		{"http://ns.adobe.com/exif/1.0/", "exif"},
		{"http://ns.adobe.com/tiff/1.0/", "tiff"},
		{"http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/", "Iptc4xmpCore"},
		{"http://www.imagewalker.com/1.0/", "iw"},
	};

	void SplitName(const char* qualified, std::string& uri, std::string& local)
	{
		const char* sep = strchr(qualified, kNsSeparator);
		if (sep == nullptr)
		{
			uri.clear();
			local = qualified;
		}
		else
		{
			uri.assign(qualified, sep - qualified);
			local = sep + 1;
		}
	}

	bool IsRdf(const std::string& uri, const std::string& local, const char* name)
	{
		return uri == kRdfNS && local == name;
	}

	std::string Trim(const std::string& s)
	{
		size_t b = s.find_first_not_of(" \t\r\n");
		if (b == std::string::npos) return std::string();
		size_t e = s.find_last_not_of(" \t\r\n");
		return s.substr(b, e - b + 1);
	}

	// Parser state. The shape we care about is
	// x:xmpmeta > rdf:RDF > rdf:Description > property > [rdf:Bag|Seq|Alt > rdf:li]
	struct ParseContext
	{
		Xmp::Packet* meta;
		int depth;
		int descriptionDepth;
		Xmp::Property* property;
		std::string propertyNS;
		bool inItem;
		std::string text;
		std::string itemLang;

		ParseContext() : meta(nullptr), depth(0), descriptionDepth(-1),
		                 property(nullptr), inItem(false)
		{
		}
	};

	void XMLCALL OnStartElement(void* userData, const XML_Char* name, const XML_Char** atts)
	{
		auto ctx = static_cast<ParseContext*>(userData);
		std::string uri, local;
		SplitName(name, uri, local);
		ctx->depth++;

		if (IsRdf(uri, local, "Description"))
		{
			if (ctx->descriptionDepth >= 0)
				return;   // nested rdf:Description is a struct value, not a new block

			ctx->descriptionDepth = ctx->depth;

			// Properties may also appear as attributes of rdf:Description.
			for (int i = 0; atts != nullptr && atts[i] != nullptr; i += 2)
			{
				std::string attUri, attLocal;
				SplitName(atts[i], attUri, attLocal);
				if (attUri.empty() || attUri == kRdfNS || attUri == kXmlNS)
					continue;

				Xmp::Property& p = ctx->meta->FindOrAdd(attUri.c_str(), attLocal.c_str());
				p.value = atts[i + 1];
			}
			return;
		}

		if (ctx->descriptionDepth < 0)
			return;

		if (ctx->depth == ctx->descriptionDepth + 1)
		{
			ctx->property = &ctx->meta->FindOrAdd(uri.c_str(), local.c_str());
			ctx->propertyNS = uri;
			ctx->text.clear();
			return;
		}

		if (ctx->property == nullptr)
			return;

		if (IsRdf(uri, local, "Bag") || IsRdf(uri, local, "Seq") || IsRdf(uri, local, "Alt"))
		{
			ctx->property->isArray = true;
			ctx->property->isOrdered = (local != "Bag");
			ctx->property->isAltText = (local == "Alt");
			ctx->property->items.clear();
			return;
		}

		if (IsRdf(uri, local, "li"))
		{
			ctx->inItem = true;
			ctx->text.clear();
			ctx->itemLang.clear();
			for (int i = 0; atts != nullptr && atts[i] != nullptr; i += 2)
			{
				std::string attUri, attLocal;
				SplitName(atts[i], attUri, attLocal);
				if (attUri == kXmlNS && attLocal == "lang")
					ctx->itemLang = atts[i + 1];
			}
		}
	}

	void XMLCALL OnEndElement(void* userData, const XML_Char* name)
	{
		auto ctx = static_cast<ParseContext*>(userData);
		std::string uri, local;
		SplitName(name, uri, local);

		if (ctx->property != nullptr)
		{
			if (IsRdf(uri, local, "li") && ctx->inItem)
			{
				Xmp::Item item;
				item.lang = ctx->itemLang;
				item.value = Trim(ctx->text);
				ctx->property->items.push_back(item);
				if (!item.lang.empty())
					ctx->property->isAltText = true;
				ctx->inItem = false;
				ctx->text.clear();
			}
			else if (ctx->depth == ctx->descriptionDepth + 1)
			{
				if (!ctx->property->isArray)
					ctx->property->value = Trim(ctx->text);
				ctx->property = nullptr;
				ctx->text.clear();
			}
		}

		if (IsRdf(uri, local, "Description") && ctx->depth == ctx->descriptionDepth)
			ctx->descriptionDepth = -1;

		ctx->depth--;
	}

	void XMLCALL OnCharacterData(void* userData, const XML_Char* s, int len)
	{
		auto ctx = static_cast<ParseContext*>(userData);
		if (ctx->property != nullptr)
			ctx->text.append(s, len);
	}
}

Xmp::Packet::Packet()
{
	for (const auto& known : kKnownPrefixes)
		_prefixes.emplace_back(known.uri, known.prefix);
}

std::string Xmp::Packet::PrefixFor(const std::string& uri) const
{
	for (const auto& prefix : _prefixes)
	{
		if (prefix.first == uri)
			return prefix.second;
	}

	return std::string();
}

const Xmp::Property* Xmp::Packet::Find(const char* schemaNS, const char* propName) const
{
	for (const auto& schema : _schemas)
	{
		if (schema.uri != schemaNS) continue;

		for (const auto& property : schema.properties)
		{
			if (property.name == propName)
				return &property;
		}
	}

	return nullptr;
}

Xmp::Property& Xmp::Packet::FindOrAdd(const char* schemaNS, const char* propName)
{
	for (auto& schema : _schemas)
	{
		if (schema.uri != schemaNS) continue;

		for (auto& property : schema.properties)
		{
			if (property.name == propName)
				return property;
		}

		schema.properties.push_back(Property());
		schema.properties.back().name = propName;
		return schema.properties.back();
	}

	Schema schema;
	schema.uri = schemaNS;
	schema.prefix = PrefixFor(schemaNS);

	if (schema.prefix.empty())
	{
		char buf[32];
		sprintf_s(buf, "ns%u", static_cast<unsigned>(_schemas.size() + 1));
		schema.prefix = buf;
		_prefixes.emplace_back(schema.uri, schema.prefix);
	}

	schema.properties.push_back(Property());
	schema.properties.back().name = propName;
	_schemas.push_back(schema);
	return _schemas.back().properties.back();
}

bool Xmp::Packet::Parse(const char* buffer, size_t length, std::string& strErrorOut)
{
	XML_Parser parser = XML_ParserCreateNS(nullptr, kNsSeparator);

	if (parser == nullptr)
	{
		strErrorOut = "could not create the XML parser";
		return false;
	}

	ParseContext ctx;
	ctx.meta = this;

	XML_SetUserData(parser, &ctx);
	XML_SetElementHandler(parser, OnStartElement, OnEndElement);
	XML_SetCharacterDataHandler(parser, OnCharacterData);

	// The packet is wrapped in <?xpacket ...?> processing instructions, which
	// expat skips, but anything trailing the closing one would be junk.
	auto end = static_cast<const char*>(memchr(buffer, '\0', length));
	if (end != nullptr)
		length = end - buffer;

	const bool bOk = XML_Parse(parser, buffer, static_cast<int>(length), 1) != XML_STATUS_ERROR;

	if (!bOk)
		strErrorOut = XML_ErrorString(XML_GetErrorCode(parser));

	XML_ParserFree(parser);
	return bOk;
}

std::string Xmp::Packet::Catenate(const char* schemaNS, const char* propName) const
{
	const Property* p = Find(schemaNS, propName);

	if (p == nullptr)
		return std::string();

	if (p->items.empty())
		return p->value;

	const std::string sep = "; ";
	std::string out;

	for (size_t i = 0; i < p->items.size(); ++i)
	{
		if (i != 0) out += sep;

		const std::string& value = p->items[i].value;

		if (value.find(sep) != std::string::npos)
		{
			out += '"';
			out += value;
			out += '"';
		}
		else
		{
			out += value;
		}
	}

	return out;
}

const Xmp::Item* Xmp::Packet::LocalizedText(const char* schemaNS, const char* propName, const char* lang) const
{
	const Property* p = Find(schemaNS, propName);

	if (p == nullptr || p->items.empty())
		return nullptr;

	if (lang != nullptr)
	{
		for (const auto& item : p->items)
		{
			if (_stricmp(item.lang.c_str(), lang) == 0)
				return &item;
		}
	}

	for (const auto& item : p->items)
	{
		if (_stricmp(item.lang.c_str(), "x-default") == 0)
			return &item;
	}

	return &p->items[0];
}

MetadataXMP::MetadataXMP(const IW::MetaData& data)
{
	const int size = data.GetDataSize();

	if (size <= 16)
		return;

	auto sz = reinterpret_cast<LPCSTR>(data.GetData());
	const size_t nLength = strnlen(sz, size);

	// The <?xpacket?> wrapper is OPTIONAL in XMP -- a writer that emits a bare
	// <x:xmpmeta>, or leads with a BOM or whitespace, is still valid. Requiring
	// it made those files look as though they carried no XMP at all.
	if (memchr(sz, '<', nLength) == nullptr)
		return;

	std::string strError;

	// Malformed XMP in a file is common and must not stop the load, but
	// silently discarding it made every such file look like it simply had no
	// XMP at all.
	if (!_packet.Parse(sz, nLength, strError))
		IW::Logging::Warn(_T("XMP parse failed: %hs"), strError.c_str());
}

bool MetadataXMP::Read(const CString& schemaNS, const CString& propName, CString& value) const
{
	USES_CONVERSION;
	const Xmp::Property* p = _packet.Find(CT2CA(schemaNS), CT2CA(propName));

	if (p == nullptr || p->isArray || p->value.empty())
		return false;

	value = p->value.c_str();
	return true;
}

bool MetadataXMP::ReadArray(const CString& schemaNS, const CString& propName, CString& value) const
{
	USES_CONVERSION;
	value = _packet.Catenate(CT2CA(schemaNS), CT2CA(propName)).c_str();
	return !value.IsEmpty();
}

bool MetadataXMP::ReadAltText(const CString& schemaNS, const CString& propName, CString& value) const
{
	USES_CONVERSION;
	const Xmp::Item* item = _packet.LocalizedText(CT2CA(schemaNS), CT2CA(propName), "en");

	if (item == nullptr)
		return false;

	value = item->value.c_str();
	return true;
}

void MetadataXMP::Load(IW::MetadataProperties& propertiesOut) const
{
	for (const auto& schema : _packet.Schemas())
	{
		if (schema.properties.empty())
			continue;

		// The prefix is what the photographer recognises; the URI is what makes
		// it unambiguous, so the section carries both.
		CString strSection;
		strSection.Format(_T("%hs (%hs)"), schema.prefix.c_str(), schema.uri.c_str());
		propertiesOut.Begin(strSection);

		for (const auto& property : schema.properties)
		{
			CString strValue;

			if (property.isAltText)
			{
				const Xmp::Item* item = _packet.LocalizedText(schema.uri.c_str(), property.name.c_str(), "en");

				if (item != nullptr)
				{
					strValue = item->value.c_str();

					if (!item->lang.empty())
					{
						CString strLang;
						strLang.Format(_T(" (%hs)"), item->lang.c_str());
						strValue += strLang;
					}
				}
			}
			else if (property.isArray)
			{
				strValue = _packet.Catenate(schema.uri.c_str(), property.name.c_str()).c_str();
			}
			else
			{
				strValue = property.value.c_str();
			}

			propertiesOut.Add(CString(property.name.c_str()), strValue);
		}
	}
}


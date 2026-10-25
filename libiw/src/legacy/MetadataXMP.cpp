// ImageWalker by Zac Walker
//
// Purpose: XMP packet parsing, through libfiles, and the property lookups and
//          listing built on it.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "LegacyPch.h"
#include "Metadata.h"

#include <libfiles/xmp.h>

namespace
{
	namespace xmp = libfiles::xmp;
	using Nodes = std::vector<xmp::property_info>;

	std::string Narrow(const CString& text)
	{
		return std::string(CT2CA(text));
	}

	// Values are shown and searched without the whitespace around them, as they always were.
	std::string Trim(const std::string& s)
	{
		const size_t b = s.find_first_not_of(" \t\r\n");
		if (b == std::string::npos) return std::string();
		const size_t e = s.find_last_not_of(" \t\r\n");
		return s.substr(b, e - b + 1);
	}

	std::string Qualified(const std::string& schema, const std::string& name)
	{
		std::string prefix;
		return xmp::namespace_prefix(schema, &prefix) ? prefix + name : name;
	}

	// Whether `node` lies under the node at `base`: one of its fields, items or
	// qualifiers, or theirs.
	bool Under(const xmp::property_info& base, const xmp::property_info& node)
	{
		const auto& b = base.path;
		const auto& p = node.path;
		return node.schema == base.schema && p.size() > b.size() && p.compare(0, b.size(), b) == 0 &&
			(p[b.size()] == '/' || p[b.size()] == '[');
	}

	size_t Find(const Nodes& nodes, const std::string& schema, const std::string& path)
	{
		for (size_t i = 0; i < nodes.size(); ++i)
		{
			if (nodes[i].schema == schema && nodes[i].path == path)
				return i;
		}

		return nodes.size();
	}

	// A struct, or an array within one, shown as the text of its fields in
	// document order. Qualifiers describe a value rather than being one.
	std::string Composite(const Nodes& nodes, const size_t at)
	{
		std::string out;

		for (size_t i = at + 1; i < nodes.size() && Under(nodes[at], nodes[i]); ++i)
		{
			if (nodes[i].path.find("/?", nodes[at].path.size()) != std::string::npos)
				continue;

			const auto value = Trim(nodes[i].value);

			if (!xmp::is_simple(nodes[i].options) || value.empty())
				continue;

			if (!out.empty()) out += "; ";
			out += value;
		}

		return out;
	}

	std::vector<std::string> Items(const Nodes& nodes, const size_t at)
	{
		std::vector<std::string> items;
		const auto& base = nodes[at].path;

		for (size_t i = at + 1; i < nodes.size() && Under(nodes[at], nodes[i]); ++i)
		{
			const auto& path = nodes[i].path;

			if (path[base.size()] != '[' || path.find(']', base.size()) + 1 != path.size())
				continue;

			items.push_back(xmp::is_simple(nodes[i].options) ? Trim(nodes[i].value) : Composite(nodes, i));
		}

		return items;
	}

	// Array items joined with "; ", quoting any item that contains the separator
	// so the join stays reversible by eye.
	std::string Join(const std::vector<std::string>& items)
	{
		const std::string sep = "; ";
		std::string out;

		for (size_t i = 0; i < items.size(); ++i)
		{
			if (i != 0) out += sep;

			if (items[i].find(sep) != std::string::npos)
				out += '"' + items[i] + '"';
			else
				out += items[i];
		}

		return out;
	}

	// The alt-text item for English, then the default, then the first; and its language.
	bool English(const xmp::meta& meta, const std::string& schema, const std::string& name, std::string& value,
	             std::string& lang)
	{
		if (!meta.get_localized_text(schema, name, {}, "en", &lang, &value))
			return false;

		value = Trim(value);
		return true;
	}

	// A top-level property's value as the property view shows it.
	std::string Shown(const xmp::meta& meta, const Nodes& nodes, const size_t at)
	{
		const auto& node = nodes[at];

		if (xmp::is_alt_text(node.options))
		{
			std::string value, lang;

			if (!English(meta, node.schema, node.path.substr(node.path.find(':') + 1), value, lang))
				return {};

			return lang.empty() ? value : value + " (" + lang + ")";
		}

		if (xmp::is_array(node.options))
			return Join(Items(nodes, at));

		if (xmp::is_struct(node.options))
			return Composite(nodes, at);

		return Trim(node.value);
	}
}

MetadataXMP::MetadataXMP(const IW::MetaData& data) : _meta(std::make_unique<xmp::meta>())
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

	// Malformed XMP in a file is common and must not stop the load: libfiles keeps
	// whatever precedes the first fault, as Adobe's toolkit does.
	_meta->parse(std::string_view(sz, nLength));
}

MetadataXMP::~MetadataXMP() = default;

// A namespace the packet never declared is unregistered, and asking about one is
// misuse that libfiles reports by throwing, as the toolkit does: here it is a
// property the image does not have.
bool MetadataXMP::Read(const CString& schemaNS, const CString& propName, CString& value) const
try
{
	const auto schema = Narrow(schemaNS);
	const auto name = Narrow(propName);
	std::string text;
	xmp::option_bits options = 0;

	if (!_meta->get_property(schema, name, &text, &options) || xmp::is_array(options))
		return false;

	text = Trim(text);

	if (xmp::is_struct(options))
	{
		const auto nodes = _meta->properties();
		const auto at = Find(nodes, schema, Qualified(schema, name));
		text = at < nodes.size() ? Composite(nodes, at) : std::string();
	}

	if (text.empty())
		return false;

	value = text.c_str();
	return true;
}
catch (const xmp::error&)
{
	return false;
}

bool MetadataXMP::ReadArray(const CString& schemaNS, const CString& propName, CString& value) const
try
{
	const auto schema = Narrow(schemaNS);
	const auto name = Narrow(propName);
	std::string text;
	xmp::option_bits options = 0;

	if (!_meta->get_property(schema, name, &text, &options))
		return false;

	text = Trim(text);

	if (!xmp::is_simple(options))
	{
		const auto nodes = _meta->properties();
		const auto at = Find(nodes, schema, Qualified(schema, name));
		text = at >= nodes.size() ? std::string() : xmp::is_array(options) ? Join(Items(nodes, at)) : Composite(nodes, at);
	}

	value = text.c_str();
	return !value.IsEmpty();
}
catch (const xmp::error&)
{
	return false;
}

bool MetadataXMP::ReadAltText(const CString& schemaNS, const CString& propName, CString& value) const
try
{
	const auto schema = Narrow(schemaNS);
	const auto name = Narrow(propName);
	xmp::option_bits options = 0;

	if (!_meta->get_property(schema, name, nullptr, &options) || !xmp::is_alt_text(options))
		return false;

	std::string text, lang;

	if (!English(*_meta, schema, name, text, lang))
		return false;

	value = text.c_str();
	return true;
}
catch (const xmp::error&)
{
	return false;
}

void MetadataXMP::Load(IW::MetadataProperties& propertiesOut) const
try
{
	const auto nodes = _meta->properties();
	const std::string* pending = nullptr;

	for (size_t i = 0; i < nodes.size(); ++i)
	{
		const auto& node = nodes[i];

		if (xmp::is_schema(node.options))
		{
			pending = &node.schema;
			continue;
		}

		// Top-level properties only: their fields, items and qualifiers are shown in them.
		if (node.path.find_first_of("/[") != std::string::npos)
			continue;

		if (pending != nullptr)
		{
			// The prefix is what the photographer recognises; the URI is what makes
			// it unambiguous, so the section carries both.
			std::string prefix;
			xmp::namespace_prefix(*pending, &prefix);
			if (!prefix.empty() && prefix.back() == ':') prefix.pop_back();
			CString strSection;
			strSection.Format(_T("%hs (%hs)"), prefix.c_str(), pending->c_str());
			propertiesOut.Begin(strSection);
			pending = nullptr;
		}

		propertiesOut.Add(CString(node.path.substr(node.path.find(':') + 1).c_str()),
		                  CString(Shown(*_meta, nodes, i).c_str()));
	}
}
catch (const xmp::error&)
{
	// What was listed before the fault stays listed.
}

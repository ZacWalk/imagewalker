// ImageWalker by Zac Walker
//
// Purpose: The historical file-dialog format catalog. Native codec ownership
// is libfiles; these rows retain only the 2.x UI labels, flags and filter
// ordering required by the old dialogs.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

// The set of format labels the historical executable presents.
//
// This is not a codec registry: native reads/writes cross the legacy boundary
// through libfiles adapters. The rows exist only for old dialog labels and
// compatibility calls that still name a historical loader.
//
// The rows are stable for the lifetime of the object, so ImageLoaderInfoPtr
// values handed out here stay valid; nothing may be added after Init.

class ImageLoaders
{
public:

	ImageLoaders();

	// Sorted by key: this is the order they appear in the format combo boxes.
	const std::vector<IW::ImageLoaderInfoPtr> &All() const { return _sorted; }

	IW::ImageLoaderInfoPtr Find(const CString &strKey) const;
	IW::ImageLoaderInfoPtr Find(WORD w) const;

private:

	ImageLoaders(const ImageLoaders&);
	void operator=(const ImageLoaders&);

	void Add(const IW::ImageLoaderInfo &info, bool unclaimedOnly = false);
	void AddHeaderWord(WORD w, const CString &strKey);

	std::vector<IW::ImageLoaderInfo> _loaders;
	std::vector<IW::ImageLoaderInfoPtr> _sorted;
	std::map<CString, size_t> _byKey;
	std::map<CString, size_t> _byExtension;
	std::map<WORD, size_t> _byHeaderWord;
};

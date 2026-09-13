// ImageWalker by Zac Walker
//
// Purpose: ImageLoaders: the registry every format registers into, and the
//          filter strings the file dialogs show.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

// The set of image loaders the executable was built with.
//
// This used to be a plug-in host: a ref-counted IImageLoaderFactory interface, a
// factory template per loader and three maps that owned the heap objects. None
// of it was ever loaded from a DLL. What is left is a table of
// IW::ImageLoaderInfo rows built once at startup, plus the two lookups the app
// actually performs -- by key or extension, and by the file's leading word.
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

	void Add(const IW::ImageLoaderInfo &info);
	void AddHeaderWord(WORD w, const CString &strKey);

	std::vector<IW::ImageLoaderInfo> _loaders;
	std::vector<IW::ImageLoaderInfoPtr> _sorted;
	std::map<CString, size_t> _byKey;
	std::map<CString, size_t> _byExtension;
	std::map<WORD, size_t> _byHeaderWord;
};

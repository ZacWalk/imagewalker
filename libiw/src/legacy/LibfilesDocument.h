// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// Purpose: Adapting libfiles native rows to the historical image stream.

#pragma once

#include <libfiles/document.h>

namespace iw::imaging::detail
{
	bool read_libfiles_document(libfiles::image_format format, const CString& key,
		IW::IStreamIn* input, IW::IImageStream* output, IW::IStatus* status);
}

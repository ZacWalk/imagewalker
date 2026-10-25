#pragma once
#include "FileJpegTran.h"

enum class JpegTransformResult { NotJpeg, Done, Failed };

namespace IW
{
	bool IsJpegStream(IStreamIn& input);
	JpegTransformResult TransformJpegStream(IStreamIn& input, IStreamOut& output,
		JXFORM_CODE transform, IStatus* status, std::uint64_t maximumBytes = kMaxPageBytes);
}

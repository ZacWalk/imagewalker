#include <iw/legacy/LegacyPch.h>
#include <iw/legacy/Legacy.h>
#include <iw/legacy/FilePng.h>
#include <iw/legacy/ImagingStreams.h>
#include <iw/legacy/NativeCodec.h>

bool check_legacy_other_translation_unit(const IW::Image& image)
{
	IW::Image copy(image);
	CLoadPng png;
	IW::SimpleBlob bytes;
	IW::StreamBlob<IW::SimpleBlob> output(bytes);
	return png.Write(png.GetKey(), &output, copy, IW::CodecSettings(), IW::CNullStatus::Instance) &&
		output.Close(IW::CNullStatus::Instance) && bytes.GetDataSize() != 0 &&
		copy.Compare(image);
}

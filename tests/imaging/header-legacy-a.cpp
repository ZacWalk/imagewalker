#include <iw/legacy/LegacyPch.h>
#include <iw/legacy/Legacy.h>
#include <iw/legacy/NativeCodec.h>
#include <iw/legacy/ImagingStreams.h>
#include <iw/legacy/FilePng.h>

bool check_legacy_other_translation_unit(const IW::Image& image);

bool check_legacy_headers()
{
	IW::Image image;
	auto& page = image.CreatePage(2, 1, IW::PixelFormat::PF24);
	const auto lock = page.GetSurfaceLock();
	const COLORREF row[] = {IW::RGBA(17, 34, 51), IW::RGBA(68, 85, 102)};
	lock->SetLine(row, 0, 0, 2);
	return check_legacy_other_translation_unit(image) &&
		iw::imaging::image_pixels(image).pixels.size() == 2;
}

#include <iw/imaging/JpegTransform.h>
#include <iw/imaging/Document.h>
#include <iw/imaging/ImageEdits.h>
#include <iw/imaging/PixelOps.h>
#include <iw/imaging/Diagnostics.h>

void set_sink_from_other_translation_unit(iw::imaging::DiagnosticSink sink)
{
	iw::imaging::set_diagnostic_sink(sink);
}

bool check_other_translation_unit()
{
	const iw::imaging::PixelImage image{1, 2, {0xff112233, 0xff445566}, 1, 2};
	const auto rotated = iw::imaging::rotate_quarters(image, 1);
	return rotated.width == 2 && rotated.height == 1 &&
		iw::edits::apply(image, {}).pixels == image.pixels &&
		iw::imaging::native_supports_extension(L".png");
}

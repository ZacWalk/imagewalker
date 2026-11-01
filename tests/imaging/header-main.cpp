namespace iw::platform {}

#include <iw/imaging/Pixel.h>
#include <iw/imaging/PixelOps.h>
#include <iw/imaging/ImageEdits.h>
#include <iw/imaging/Document.h>
#include <iw/imaging/JpegTransform.h>
#include <iw/imaging/Diagnostics.h>
#include <cstdio>

extern const std::string_view s_app_name = "ImageWalker-Header-Tests";
bool check_other_translation_unit();
bool check_legacy_headers();
void set_sink_from_other_translation_unit(iw::imaging::DiagnosticSink sink);

namespace
{
	int notifications = 0;
	void receive(std::wstring_view) { ++notifications; }
}

int main()
{
	set_sink_from_other_translation_unit(receive);
	iw::imaging::report_diagnostic(L"shared diagnostic state");
	iw::imaging::set_diagnostic_sink(nullptr);
	const iw::imaging::PixelImage image{2, 1, {0xff112233, 0xff445566}, 2, 1};
	const auto rotated = iw::imaging::rotate_quarters(image, 2);
	const bool passed = notifications == 1 && check_other_translation_unit() && check_legacy_headers() &&
		rotated.pixels == std::vector<std::uint32_t>{0xff445566, 0xff112233} &&
		iw::edits::apply(image, {}).pixels == image.pixels;
	std::printf("Header-only multi-TU consumer: %s\n", passed ? "passed" : "failed");
	return passed ? 0 : 1;
}

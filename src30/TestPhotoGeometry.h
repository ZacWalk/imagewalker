// ImageWalker by Zac Walker
// Reporter-compatible regression coverage; called by the application's existing /test runner.

#pragma once

#include "ImageEdits.h"
#include "TaskEdit.h"
#include "WebPCodec.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <format>
#include <fstream>
#include <limits>

namespace iw::tests
{
	struct PhotoGeometryAccess
	{
		static void prepare(TaskEdit& task, const files::DecodedImage& source)
		{
			task.source_ = source;
			task.path_ = L"geometry.png";
			task.targets_ = {task.path_};
			task.imageBounds_ = {10, 10, 200, 160};
		}
		static bool mouse(TaskEdit& task, const platform::MouseMessage message, const pointi point, const bool down = false)
		{
			platform::MouseInput input;
			input.point = point;
			input.leftButton = down;
			return task.content_mouse(message, input);
		}
		static const edits::ImageEdits& draft(const TaskEdit& task) { return task.edits_; }
		static void undo(TaskEdit& task, const bool redo = false) { task.undo(redo); }
		static void corners(TaskEdit& task) { task.perspectiveHandles_ = true; }
		static bool dragging(const TaskEdit& task) { return task.content_dragging(); }
		static bool can_undo(const TaskEdit& task) { return task.history_.can_undo(); }
		static void cancel(TaskEdit& task)
		{
			platform::KeyInput input;
			input.key = platform::KeyCode::escape;
			task.content_key(input);
		}
	};

	template<class Reporter>
	void test_photo_geometry(Reporter& reporter)
	{
		reporter.section(L"Photo Edit inverse homography, alpha, crop handles and draft undo");
		files::DecodedImage image;
		image.width = image.height = image.originalWidth = image.originalHeight = 101;
		image.pixels.resize(101 * 101);
		for (int y = 0; y < 101; ++y)
			for (int x = 0; x < 101; ++x)
				image.pixels[static_cast<size_t>(y) * 101 + x] = 0xff000000u |
					static_cast<std::uint32_t>(x * 2) | static_cast<std::uint32_t>(y * 2) << 16;
		image.pixels[0] = 0x00123456u;
		const auto identity = edits::correct_perspective(image, edits::whole_picture);
		reporter.check(identity.width == image.width && identity.height == image.height &&
			identity.pixels == image.pixels, L"identity perspective preserves every bit, including RGB under zero alpha");
		const edits::Quadrilateral trapezoid{{{0.25, 0}, {0.75, 0}, {1, 1}, {0, 1}}};
		const auto corrected = edits::correct_perspective(image, trapezoid);
		reporter.check(corrected.width == 101 && corrected.height == 104 && !corrected.pixels.empty(),
			L"a trapezoid's longest opposing edges establish a bounded output resolution");
		if (!corrected.pixels.empty())
		{
			reporter.check(corrected.pixels[0] == image.pixels[25] &&
				corrected.pixels[100] == image.pixels[75] &&
				corrected.pixels[corrected.pixels.size() - 101] == image.pixels[101 * 100] &&
				corrected.pixels.back() == image.pixels.back(),
				L"all four output corners inverse-map to their reviewed source corners");
			const int y = corrected.height / 2;
			const double v = static_cast<double>(y) / (corrected.height - 1);
			const int expectedRed = static_cast<int>(std::lround(200 * v / (2 - v)));
			const auto pixel = corrected.pixels[static_cast<size_t>(y) * corrected.width + 50];
			reporter.check(std::abs(static_cast<int>(pixel >> 16 & 255) - expectedRed) <= 1 &&
				std::abs(static_cast<int>(pixel & 255) - 100) <= 1,
				L"interior samples use the projective denominator, not affine or bilinear corner interpolation");
		}
		for (const auto invalid : {
			edits::Quadrilateral{{{0, 0}, {1, 1}, {1, 0}, {0, 1}}},
			edits::Quadrilateral{{{0, 0}, {0, 0}, {1, 1}, {0, 1}}},
			edits::Quadrilateral{{{-0.01, 0}, {1, 0}, {1, 1}, {0, 1}}},
			edits::Quadrilateral{{{0, 0}, {1, 0}, {0.1, 0.1}, {0, 1}}},
			edits::Quadrilateral{{{std::numeric_limits<double>::quiet_NaN(), 0}, {1, 0}, {1, 1}, {0, 1}}}})
		{
			reporter.check(!edits::valid_quadrilateral(invalid) &&
				edits::correct_perspective(image, invalid).pixels.empty(),
				L"crossed, degenerate, out-of-bounds, concave and non-finite quadrilaterals are refused");
		}
		auto malformed = image;
		malformed.pixels.resize(2);
		reporter.check(edits::correct_perspective(malformed, trapezoid).pixels.empty(),
			L"perspective validates the source buffer before sampling");
		const edits::Quadrilateral amplified{{{0.49, 0}, {0.51, 0}, {1, 1}, {0, 1}}};
		reporter.check(edits::perspective_size({100000, 500}, amplified).width == 0,
			L"panoramic perspective amplification is rejected before a multi-gigabyte allocation");
		files::DecodedImage thin;
		thin.width = thin.originalWidth = 10000;
		thin.height = thin.originalHeight = 2;
		thin.pixels.assign(20000, 0xffabcdefu);
		reporter.check(edits::correct_perspective(thin, amplified).pixels.empty(),
			L"rendering obeys the same bounded geometry decision as preview and drag validation");

		auto alpha = image;
		for (int y = 0; y < alpha.height; ++y)
			for (int x = 0; x < alpha.width; ++x)
				alpha.pixels[static_cast<size_t>(y) * alpha.width + x] = x < 50 ? 0x800000ffu : 0x00ff0000u;
		const auto sampled = edits::correct_perspective(alpha, trapezoid);
		reporter.check(!sampled.pixels.empty() && std::ranges::all_of(sampled.pixels, [](const auto p)
			{ return (p >> 24) <= 128 && ((p >> 24) == 0 || (p & 0x00ffffffu) == 0x000000ffu); }),
			L"projective interpolation preserves translucency without bleeding invisible colour");

		edits::ImageEdits stack;
		stack.perspective = trapezoid;
		stack.rotation = 1;
		stack.crop = {10, 20, 50, 60};
		stack.brightness = 20;
		const auto saved = edits::apply(image, stack);
		auto geometryOnly = stack;
		geometryOnly.crop = {};
		geometryOnly.reset_color();
		const auto frame = edits::apply(image, geometryOnly, false);
		edits::ImageEdits tail;
		tail.crop = stack.crop;
		tail.brightness = stack.brightness;
		const auto expected = edits::apply(frame, tail);
		reporter.check(saved.width == 50 && saved.height == 60 && saved.pixels == expected.pixels,
			L"the stack consistently applies perspective, quarter turns, crop, then colour");
		edits::PreviewSource preview;
		preview.update(image, {51, 51});
		const auto scaled = edits::scaled_edits(stack, {101, 101}, {preview.image().width, preview.image().height});
		const auto previewResult = edits::apply(preview.image(), scaled);
		reporter.check(scaled.perspective == stack.perspective && previewResult.width > 0 &&
			std::abs(previewResult.width * 2 - saved.width) <= 2 &&
			std::abs(previewResult.height * 2 - saved.height) <= 2,
			L"preview and save share normalized perspective corners and full-resolution crop geometry");

		const recti bounds{10, 20, 100, 80}, crop{30, 40, 40, 30};
		reporter.check(edits::crop_handle(crop, {30, 40}, 4) == edits::CropHandle::topLeft &&
			edits::crop_handle(crop, {70, 70}, 4) == edits::CropHandle::bottomRight &&
			edits::crop_handle(crop, {50, 55}, 4) == edits::CropHandle::move,
			L"crop hit testing distinguishes corners and an interior move");
		for (const auto handle : {edits::CropHandle::move, edits::CropHandle::left, edits::CropHandle::right,
			edits::CropHandle::top, edits::CropHandle::bottom, edits::CropHandle::topLeft,
			edits::CropHandle::topRight, edits::CropHandle::bottomLeft, edits::CropHandle::bottomRight})
			for (const auto delta : {pointi{-10000, -10000}, pointi{10000, 10000}, pointi{INT_MAX, INT_MIN}})
			{
				const auto dragged = edits::drag_crop(crop, bounds, handle, delta);
				reporter.check(dragged.width >= 1 && dragged.height >= 1 &&
					dragged.x >= bounds.x && dragged.y >= bounds.y &&
					dragged.right() <= bounds.right() && dragged.bottom() <= bounds.bottom(),
					L"every crop handle and move clamps at the source bounds without inverting or overflowing");
			}
		edits::EditHistory history;
		const auto original = stack;
		stack.crop.x += 2;
		history.record(original, stack);
		auto before = stack;
		stack.perspective = edits::whole_picture;
		history.record(before, stack);
		reporter.check(history.undo(stack) && stack == before && history.undo(stack) && stack == original &&
			history.redo(stack) && stack == before,
			L"undo and redo preserve entire independent geometry/colour stack snapshots");
		auto changed = stack;
		changed.tint = 12;
		history.record(stack, changed);
		reporter.check(!history.can_redo(), L"a new edit discards the obsolete redo branch");
		history.clear();
		reporter.check(!history.can_undo() && !history.can_redo(), L"a saved baseline clears draft-only undo history");

		files::DecodedImage uiImage;
		uiImage.width = 200; uiImage.height = 160;
		uiImage.originalWidth = 2000; uiImage.originalHeight = 1600;
		uiImage.pixels.assign(200 * 160, 0xffeeeeeeu);
		TaskEdit task;
		PhotoGeometryAccess::prepare(task, uiImage);
		PhotoGeometryAccess::mouse(task, platform::MouseMessage::leftButtonDown, {210, 170});
		PhotoGeometryAccess::mouse(task, platform::MouseMessage::leftButtonUp, {210, 170});
		reporter.check(PhotoGeometryAccess::draft(task).empty() && !PhotoGeometryAccess::can_undo(task),
			L"clicking a crop handle without moving does not dirty the draft or create undo history");
		PhotoGeometryAccess::mouse(task, platform::MouseMessage::leftButtonDown, {210, 170});
		PhotoGeometryAccess::mouse(task, platform::MouseMessage::move, {160, 130}, true);
		PhotoGeometryAccess::mouse(task, platform::MouseMessage::move, {110, 90}, true);
		PhotoGeometryAccess::mouse(task, platform::MouseMessage::leftButtonUp, {110, 90});
		reporter.check(PhotoGeometryAccess::draft(task).crop == recti{0, 0, 1000, 800} &&
			!PhotoGeometryAccess::dragging(task), L"interactive crop drags convert preview distance into full-resolution pixels");
		PhotoGeometryAccess::undo(task);
		reporter.check(PhotoGeometryAccess::draft(task).empty() && !PhotoGeometryAccess::can_undo(task),
			L"a complete drag is exactly one undo step, not one step per mouse move");
		PhotoGeometryAccess::undo(task, true);
		const auto reviewed = PhotoGeometryAccess::draft(task);
		PhotoGeometryAccess::mouse(task, platform::MouseMessage::leftButtonDown, {110, 90});
		PhotoGeometryAccess::mouse(task, platform::MouseMessage::move, {50, 50}, true);
		PhotoGeometryAccess::cancel(task);
		reporter.check(PhotoGeometryAccess::draft(task) == reviewed && !PhotoGeometryAccess::dragging(task),
			L"Escape cancels a drag without losing the prior reviewed crop");
		PhotoGeometryAccess::corners(task);
		PhotoGeometryAccess::mouse(task, platform::MouseMessage::leftButtonDown, {10, 10});
		PhotoGeometryAccess::mouse(task, platform::MouseMessage::move, {30, 30}, true);
		PhotoGeometryAccess::mouse(task, platform::MouseMessage::leftButtonUp, {30, 30});
		reporter.check(PhotoGeometryAccess::draft(task).perspective.has_value() &&
			!PhotoGeometryAccess::draft(task).has_crop(), L"dragging a perspective corner records a new source-normalized quad and resets crop");
		PhotoGeometryAccess::undo(task);
		reporter.check(PhotoGeometryAccess::draft(task) == reviewed,
			L"undoing perspective also restores the crop it invalidated");
	}

	template<class Reporter>
	void test_document_detection(Reporter& reporter)
	{
		reporter.section(L"Conservative document quadrilateral detection");
		for (const double degrees : {-75.0, -48.0, -24.0, -12.0, 0.0, 17.0, 32.0, 48.0, 75.0})
			for (const bool dark : {false, true})
			{
				files::DecodedImage image;
				image.width = 320; image.height = 280;
				image.pixels.resize(320 * 280);
				const double radians = degrees * 3.14159265358979323846 / 180;
				const double c = std::cos(radians), s = std::sin(radians);
				for (int y = 0; y < 280; ++y)
					for (int x = 0; x < 320; ++x)
					{
						const double u = (x - 160) * c + (y - 140) * s;
						const double v = -(x - 160) * s + (y - 140) * c;
						const bool page = std::abs(u) < 85 && std::abs(v) < 70;
						const bool text = std::abs(u) < 60 && std::abs(v) < 50 &&
							static_cast<int>(v + 50) % 14 < 2;
						const bool bright = page && !text;
						const auto level = static_cast<std::uint32_t>((bright != dark) ? 230 : 35);
						image.pixels[static_cast<size_t>(y) * 320 + x] = 0xff000000u | level * 0x010101u;
					}
				const auto detected = edits::detect_document(image);
				const auto detectionMessage = std::format(
					L"a rotated, contrasting page with text is detected ({:.0f} degrees, {} page)",
					degrees, dark ? L"dark" : L"light");
				reporter.check(detected.has_value(), detectionMessage.c_str());
				if (detected)
				{
					bool cornersOnPage = true;
					for (const auto p : *detected)
					{
						const double x = p.x * 319 - 160, y = p.y * 279 - 140;
						const double u = x * c + y * s, v = -x * s + y * c;
						cornersOnPage = cornersOnPage && std::abs(std::abs(u) - 85) < 4 &&
							std::abs(std::abs(v) - 70) < 4;
					}
					reporter.check(cornersOnPage, L"detected corners lie on the rotated page, not an axis-aligned bounding box");
				}
			}
		files::DecodedImage background;
		background.width = background.height = 240;
		background.pixels.assign(240 * 240, 0xff888888u);
		reporter.check(!edits::detect_document(background), L"a uniform background explicitly has no detected document");
		for (int y = 0; y < 240; ++y)
			for (int x = 0; x < 240; ++x)
				background.pixels[static_cast<size_t>(y) * 240 + x] =
					(x - 120) * (x - 120) + (y - 120) * (y - 120) < 85 * 85 ? 0xffeeeeeeu : 0xff222222u;
		reporter.check(!edits::detect_document(background), L"a bright circular object is rejected rather than fabricated into four corners");
		for (int y = 0; y < 240; ++y)
			for (int x = 0; x < 240; ++x)
				background.pixels[static_cast<size_t>(y) * 240 + x] =
					((x / 8 + y / 8) % 2) == 0 ? 0xffeeeeeeu : 0xff222222u;
		reporter.check(!edits::detect_document(background), L"a textured checkerboard is not arbitrarily called a document");
		const edits::Quadrilateral perspectivePage{{{0.22, 0.16}, {0.82, 0.25}, {0.88, 0.88}, {0.12, 0.78}}};
		for (int y = 0; y < 240; ++y)
			for (int x = 0; x < 240; ++x)
			{
				bool inside = true;
				for (size_t i = 0; i < 4; ++i)
				{
					const auto a = perspectivePage[i], b = perspectivePage[(i + 1) % 4];
					inside = inside && (b.x - a.x) * (y / 239.0 - a.y) -
						(b.y - a.y) * (x / 239.0 - a.x) > 0;
				}
				background.pixels[static_cast<size_t>(y) * 240 + x] = inside ? 0xffeeeeeeu : 0xff222222u;
			}
		const auto trapezoid = edits::detect_document(background);
		bool detectedPerspective = trapezoid.has_value();
		if (trapezoid)
			for (size_t i = 0; i < 4; ++i)
				detectedPerspective = detectedPerspective && std::hypot((*trapezoid)[i].x - perspectivePage[i].x,
					(*trapezoid)[i].y - perspectivePage[i].y) < 0.02;
		reporter.check(detectedPerspective, L"a skewed quadrilateral is detected without replacing it by a rotated rectangle");
		for (int y = 0; y < 240; ++y)
			for (int x = 0; x < 240; ++x)
				background.pixels[static_cast<size_t>(y) * 240 + x] = x < 160 && y > 25 && y < 220 ?
					0xffeeeeeeu : 0xff222222u;
		reporter.check(!edits::detect_document(background), L"a page clipped by the photo border has no fabricated fourth edge");
		std::fill(background.pixels.begin(), background.pixels.end(), 0x00ffffffu);
		reporter.check(!edits::detect_document(background), L"invisible pixels provide no document evidence");
	}

	template<class Reporter>
	void test_bundled_webp(Reporter& reporter)
	{
		reporter.section(L"Bundled WebP lossless/lossy alpha and transactional failure");
		files::DecodedImage image;
		image.width = 64; image.height = 48;
		image.originalWidth = 64; image.originalHeight = 48;
		image.pixels.resize(64 * 48);
		for (int y = 0; y < image.height; ++y)
			for (int x = 0; x < image.width; ++x)
				image.pixels[static_cast<size_t>(y) * 64 + x] = static_cast<std::uint32_t>(x * 4) << 24 |
					static_cast<std::uint32_t>(y * 4) << 16 | static_cast<std::uint32_t>(x * 3) << 8 | 70u;
		for (const bool lossless : {false, true})
		{
			const auto encoded = webp::encode(image, {90, lossless});
			int width = 0, height = 0;
			reporter.check(!encoded.empty() && webp::probe(encoded, width, height) && width == 64 && height == 48,
				L"the bundled encoder produces a probeable WebP independently of WIC extensions");
			const auto decoded = webp::decode(encoded);
			reporter.check(decoded.width == 64 && decoded.height == 48 && decoded.pixels.size() == image.pixels.size(),
				L"both WebP modes decode to original dimensions");
			if (decoded.pixels.size() == image.pixels.size())
			{
				bool alpha = true;
				std::uint64_t colourError = 0, colourSamples = 0;
				for (size_t i = 0; i < image.pixels.size(); ++i)
				{
					alpha = alpha && (decoded.pixels[i] >> 24) == (image.pixels[i] >> 24);
					if ((image.pixels[i] >> 24) < 64) continue;
					for (const int shift : {0, 8, 16})
					{
						colourError += static_cast<std::uint64_t>(std::abs(static_cast<int>(decoded.pixels[i] >> shift & 255) -
							static_cast<int>(image.pixels[i] >> shift & 255)));
						++colourSamples;
					}
				}
				reporter.check(alpha, L"lossy and lossless WebP retain exact source alpha");
				if (lossless) reporter.check(decoded.pixels == image.pixels,
					L"lossless WebP round-trips BGRA exactly, including RGB beneath fully transparent pixels");
				else reporter.check(colourSamples > 0 && colourError < colourSamples * 15,
					L"lossy WebP retains the generated photo's visible colour within compression tolerance");
			}
			const auto thumb = webp::decode(encoded, 16, 16);
			reporter.check(thumb.width == 16 && thumb.height == 12 && thumb.originalWidth == 64 &&
				thumb.originalHeight == 48, L"WebP thumbnails obey both bounds and retain full-resolution dimensions");
			if (encoded.size() > 16)
				reporter.check(webp::decode(std::span<const std::uint8_t>(encoded.data(), 16)).pixels.empty(),
					L"a truncated bitstream returns no partial image");
		}
		auto invalid = image;
		invalid.pixels.clear();
		reporter.check(webp::encode(invalid).empty() && webp::decode({}).pixels.empty(),
			L"invalid pixel buffers and empty WebP inputs are rejected");

		const auto root = files::unique_destination(std::filesystem::current_path() / L"iw30-webp-tests");
		std::error_code error;
		if (root.empty() || !std::filesystem::create_directory(root, error))
		{
			reporter.check(false, L"the WebP fixture directory can be reserved");
			return;
		}
		struct Cleanup
		{
			std::filesystem::path path;
			~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
		} cleanup{root};
		const auto path = root / L"alpha.webp";
		const bool saved = files::save_image(image, path, files::ImageSaveFormat::webp, {90, true}, false);
		reporter.check(saved, L"Files publishes bundled WebP through its temporary sibling protocol");
		if (!saved) return;
		const auto snapshot = files::snapshot_file(path);
		reporter.check(files::load_image(path).pixels == image.pixels,
			L"normal file loading uses the bundled decoder for an exact lossless round trip");
		int width = 0, height = 0;
		reporter.check(files::read_dimensions(path, width, height) && width == 64 && height == 48,
			L"normal dimension probing recognises bundled WebP");
		reporter.check(!files::save_image(invalid, path, files::ImageSaveFormat::webp, {90, true}) &&
			snapshot && files::matches_snapshot(path, *snapshot), L"failed encoding leaves existing target identity and bytes unchanged");
		reporter.check(!files::save_image(image, path, files::ImageSaveFormat::webp, {90, true}, true, [] { return false; }) &&
			snapshot && files::matches_snapshot(path, *snapshot), L"a refused commit discards temporary WebP without touching its destination");
		const auto absent = root / L"refused.webp";
		reporter.check(!files::save_image(image, absent, files::ImageSaveFormat::webp, {}, false, [] { return false; }) &&
			!std::filesystem::exists(absent), L"refusing publication creates no destination");
		const auto race = root / L"race.webp";
		reporter.check(!files::save_image(image, race, files::ImageSaveFormat::webp, {}, false, [&]
			{ std::ofstream(race, std::ios::binary) << "racing output"; return true; }) &&
			std::filesystem::file_size(race) == 13, L"a destination appearing during WebP encoding is never silently replaced");
		reporter.check(!files::save_image(image, root / L"missing" / L"failure.webp", files::ImageSaveFormat::webp),
			L"a failed temporary write never publishes a target");
		bool leaked = false;
		for (const auto& entry : std::filesystem::directory_iterator(root))
			leaked = leaked || entry.path().filename().wstring().starts_with(L".iw30-");
		reporter.check(!leaked, L"failed and refused WebP saves leave no temporary sibling behind");
	}
}

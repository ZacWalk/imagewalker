// ImageWalker by Zac Walker
// The drawn folder tree's model and control, driven and drawn without a window.

#pragma once

#include "FolderTree.h"
#include "TestUiPlatform.h"

#include <libdesktop/ui/test_support.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

namespace iw::tests
{
	namespace folder_tree_fixtures
	{
		inline std::vector<folders::Folder> drives()
		{
			return {{L"C:\\", L"C:", true}, {L"D:\\", L"D:", true}};
		}

		inline std::vector<folders::Folder> subfolders(const std::filesystem::path& parent,
		                                               const std::initializer_list<const wchar_t*> names)
		{
			std::vector<folders::Folder> result;
			for (const auto* name : names) result.push_back({parent / name, name, false});
			return result;
		}

		inline bool same_colour(const ::core::color drawn, const color expected)
		{
			const auto channel = [](const float value)
			{
				return static_cast<int>(std::lround(value * 255));
			};
			return channel(drawn.r) == expected.r && channel(drawn.g) == expected.g && channel(drawn.b) == expected.b;
		}

		inline color surface_pixel(const ::core::surface& surface, const int x, const int y)
		{
			std::uint32_t pixel{};
			std::memcpy(&pixel, surface.pixels_line(y) + static_cast<size_t>(x) * 4, sizeof(pixel));
			return {static_cast<std::uint8_t>(pixel >> 16), static_cast<std::uint8_t>(pixel >> 8),
			        static_cast<std::uint8_t>(pixel), static_cast<std::uint8_t>(pixel >> 24)};
		}
	}

	template <class Reporter> void test_folder_tree_model(Reporter& reporter)
	{
		using namespace folder_tree_fixtures;
		using Move = folders::FolderTreeModel::Move;
		std::vector<std::filesystem::path> requests;
		folders::FolderTreeModel model([&requests](const std::filesystem::path& folder)
		{ requests.push_back(folder); });
		model.set_roots(drives());
		reporter.check(model.rows().size() == 2 && model.rows()[0].expandable && !model.rows()[0].expanded &&
		                   !model.selected_row() && requests.empty(),
		               L"drives start closed and expandable, unlisted and unselected");
		reporter.check(model.expand(0) && requests == std::vector<std::filesystem::path>{L"C:\\"} &&
		                   model.rows()[0].listing && model.rows().size() == 2,
		               L"expanding an unlisted folder asks for its subfolders and shows it listing");
		model.listed(L"E:\\", subfolders(L"E:\\", {L"Stray"}));
		reporter.check(model.rows().size() == 2, L"a listing no folder asked for is ignored");
		model.listed(L"C:\\", subfolders(L"C:\\", {L"Users", L"Windows"}));
		const auto& rows = model.rows();
		reporter.check(rows.size() == 4 && rows[1].folder->label == L"Users" && rows[1].depth == 1 &&
		                   rows[2].folder->path == L"C:\\Windows" && rows[3].depth == 0 && rows[0].expanded &&
		                   !rows[0].listing,
		               L"listed subfolders appear in order beneath their folder");
		reporter.check(model.select(1) && model.selected_path() == std::filesystem::path(L"C:\\Users") &&
		                   !model.select(1),
		               L"a row is selected once");
		reporter.check(model.collapse(0) && model.rows().size() == 2 && model.selected_row() == 0,
		               L"collapsing the folder that holds the selection selects it");
		requests.clear();
		reporter.check(model.toggle(0) && model.rows().size() == 4 && requests.empty(),
		               L"a listed folder opens again without listing it again");
		model.expand(1);
		model.listed(L"C:\\Users", {});
		reporter.check(!model.rows()[1].expandable && !model.rows()[1].expanded && !model.expand(1),
		               L"a folder without subfolders loses its expander");

		folders::FolderTreeModel keys([&requests](const std::filesystem::path& folder) { requests.push_back(folder); });
		keys.set_roots(drives());
		requests.clear();
		reporter.check(keys.move(Move::down) && keys.selected_row() == 0, L"Down selects the first folder");
		reporter.check(!keys.move(Move::right) && requests.size() == 1 && keys.selected_row() == 0,
		               L"Right opens a closed folder without moving");
		keys.listed(L"C:\\", subfolders(L"C:\\", {L"Users", L"Windows"}));
		reporter.check(keys.move(Move::right) && keys.selected_row() == 1, L"Right on an open folder enters it");
		reporter.check(keys.move(Move::down) && keys.selected_row() == 2 && keys.move(Move::left) &&
		                   keys.selected_row() == 0,
		               L"Down moves on, and Left from a closed folder goes to its parent");
		reporter.check(!keys.move(Move::left) && keys.rows().size() == 2 && keys.selected_row() == 0,
		               L"Left on an open folder closes it");
		reporter.check(keys.move(Move::end) && keys.selected_row() == 1 && keys.move(Move::home) &&
		                   keys.selected_row() == 0 && keys.move(Move::pageDown, 10) && keys.selected_row() == 1 &&
		                   keys.move(Move::pageUp, 10) && keys.selected_row() == 0 && !keys.move(Move::up),
		               L"Home, End and the page keys stop at the ends");

		folders::FolderTreeModel reveal([&requests](const std::filesystem::path& folder)
		{ requests.push_back(folder); });
		requests.clear();
		reveal.reveal(L"c:\\users\\ZAC\\pictures\\");
		reporter.check(reveal.revealing() && requests.empty(), L"a folder revealed before the drives waits for them");
		reveal.set_roots(drives());
		reveal.listed(L"C:\\", subfolders(L"C:\\", {L"Users"}));
		reveal.listed(L"C:\\Users", subfolders(L"C:\\Users", {L"Public", L"zac"}));
		reveal.listed(L"C:\\Users\\zac", subfolders(L"C:\\Users\\zac", {L"Music", L"Pictures"}));
		reporter.check(
			requests == std::vector<std::filesystem::path>{L"C:\\", L"C:\\Users", L"C:\\Users\\zac"} &&
				!reveal.revealing() && reveal.selected_path() == std::filesystem::path(L"C:\\Users\\zac\\Pictures") &&
				reveal.selected_row() == 5,
			L"revealing lists and opens each folder above, then selects without regard to case or a final separator");
		reveal.set_roots(drives());
		reveal.listed(L"C:\\", subfolders(L"C:\\", {L"Users"}));
		reveal.listed(L"C:\\Users", subfolders(L"C:\\Users", {L"zac"}));
		reveal.listed(L"C:\\Users\\zac", subfolders(L"C:\\Users\\zac", {L"Pictures"}));
		reporter.check(reveal.selected_path() == std::filesystem::path(L"C:\\Users\\zac\\Pictures"),
		               L"the drives listed again show the selected folder again");
		reveal.reveal(L"C:\\Users\\zac\\AppData");
		reporter.check(!reveal.revealing() && !reveal.selected_row(), L"a folder the tree cannot show selects nothing");
		reveal.reveal(L"\\\\server\\share\\photos");
		reporter.check(!reveal.revealing() && !reveal.selected_row(), L"a folder under no drive selects nothing");
		reveal.reveal(L"D:\\Photos");
		reveal.select(0);
		reveal.listed(L"D:\\", subfolders(L"D:\\", {L"Photos"}));
		reporter.check(!reveal.revealing() && reveal.selected_row() == 0,
		               L"choosing a folder stops a reveal still waiting for a listing");
	}

	template <class Reporter> void test_folder_tree_view(Reporter& reporter)
	{
		using namespace folder_tree_fixtures;
		std::vector<std::filesystem::path> chosen, wanted;
		const auto view = std::make_shared<folders::FolderTreeView>(
			folders::FolderTreeView::Events{[&chosen](const std::filesystem::path& folder)
		{ chosen.push_back(folder); }, [&wanted](const std::filesystem::path& folder) { wanted.push_back(folder); }});
		::ui::test::fake_measure_context measure;
		view->set_roots(drives());
		view->layout(measure, {0, 0, 200, 60});
		const int row = view->row_height();
		reporter.check(row == 22, L"a row holds a line of text or a 16-pixel glyph with room around it");
		view->on_key(::platform::keys::DOWN, {});
		reporter.check(chosen == std::vector<std::filesystem::path>{L"C:\\"}, L"a key that selects reports the folder");
		view->on_key(::platform::keys::RIGHT, {});
		reporter.check(wanted == std::vector<std::filesystem::path>{L"C:\\"} && chosen.size() == 1,
		               L"opening a folder asks for its subfolders and reports nothing");
		view->listed(L"C:\\", subfolders(L"C:\\", {L"Alpha", L"Beta", L"Gamma", L"Delta"}));
		const auto middle = [row](const int index)
		{
			return 4 + index * row + row / 2;
		};
		view->on_mouse_down({100, middle(1)}, {});
		reporter.check(chosen.back() == std::filesystem::path(L"C:\\Alpha"), L"a click on a row selects its folder");
		view->on_mouse_down({10, middle(0)}, {});
		reporter.check(view->model().rows().size() == 2 && chosen.back() == std::filesystem::path(L"C:\\"),
		               L"a click on an expander closes the folder, and the selection it hid moves up to it");
		const auto reports = chosen.size();
		view->on_double_click({100, middle(0)}, {});
		reporter.check(view->model().rows().size() == 6 && chosen.size() == reports && wanted.size() == 1,
		               L"a double-click on a folder opens it");
		reporter.check(view->folder_at({100, middle(2)}) == std::filesystem::path(L"C:\\Beta") &&
		                   !view->folder_at({100, 2}),
		               L"a point finds the folder drawn there, and none above the first");
		reporter.check(view->on_wheel({50, 30}, -120, {}) && view->scroll_offset().y == 3 * row,
		               L"a wheel notch scrolls three rows");
		view->on_key(::platform::keys::END, {});
		reporter.check(chosen.back() == std::filesystem::path(L"D:\\") &&
		                   view->scroll_offset().y == 4 + 6 * row + 4 - 60,
		               L"End selects the last folder and scrolls it into view");
		view->on_key(::platform::keys::HOME, {});
		reporter.check(view->scroll_offset().y == 0, L"Home scrolls back to the top");
		const auto before = chosen.size();
		view->reveal(L"c:\\beta");
		const int offset = 4 + 3 * row + 4 - 60;
		reporter.check(chosen.size() == before && view->model().selected_row() == 2 &&
		                   view->scroll_offset().y == offset,
		               L"a folder revealed from outside is selected and scrolled into view without a report");

		// Rows 0 to 3 are in view: the drive and three of its folders, each of which may hold subfolders.
		const auto line = [row, offset](const int index)
		{
			return ::core::recti{0, 4 + index * row - offset, 200, 4 + (index + 1) * row - offset};
		};
		const auto highlight = platform::system_color(platform::SystemColor::highlight);
		::ui::test::recording_draw_context draw({0, 0, 200, 60});
		const auto filled = [&draw](const ::core::recti rect, const color expected)
		{
			return std::ranges::any_of(draw.fills, [rect, expected](const auto& fill)
			{ return fill.rect == rect && same_colour(fill.color, expected); });
		};
		view->paint(draw, true);
		reporter.check(filled(line(2), highlight) && draw.drawn_text() == "C:AlphaBetaGamma" &&
		                   draw.polylines.size() == 4,
		               L"the focused tree draws its selection and the label and expander of each folder in view");
		const auto glyphAt = [&draw, &line, row](const size_t index, const int depth)
		{
			const int x = 4 + depth * 16 + 16;
			const int middle = line(static_cast<int>(index)).top + row / 2;
			return index < draw.textures.size() && draw.textures[index].texture &&
			       draw.textures[index].bounds == ::core::recti{x, middle - 8, x + 16, middle + 8};
		};
		reporter.check(draw.textures.size() == 4 && glyphAt(0, 0) && glyphAt(1, 1) && glyphAt(3, 1) &&
		                   draw.textures[1].texture == draw.textures[3].texture &&
		                   draw.textures[0].texture != draw.textures[1].texture,
		               L"each folder carries a glyph beside its expander, a drive its own");
		const auto glyph = draw.textures.size() > 1 && draw.textures[1].texture ? draw.textures[1].texture->surface
		                                                                        : ::core::const_surface_ptr{};
		const auto centre = glyph && glyph->dimensions() == ::core::sizei{16, 16} ? surface_pixel(*glyph, 8, 8)
		                                                                          : color{};
		reporter.check(centre.r > 180 && centre.g > 120 && centre.b < 100 && centre.a == 255,
		               L"the folder glyph is the application's folder picture scaled to its row");

		draw.reset();
		view->on_mouse_move({100, line(1).top + row / 2}, false);
		view->paint(draw, false);
		reporter.check(filled(line(2), {217, 217, 217}) && !filled(line(2), highlight) &&
		                   filled(line(1), {229, 241, 251}),
		               L"a tree without the keyboard shows its selection quietly, and the row under the pointer");
		draw.reset();
		view->on_mouse_leave();
		view->paint(draw, false);
		reporter.check(!filled(line(1), {229, 241, 251}), L"the pointer leaving the tree clears its row");

		TestControlHost host;
		host.tree->elements.push_back(view);
		const auto manager = std::make_shared<::ui::input_manager>(&host);
		manager->layout();
		manager->keyboard_focus(true);
		reporter.check(manager->focus(view) && view->has_focus(), L"the tree takes the keyboard its frame gives it");
		manager->key_down(::platform::keys::UP, {});
		reporter.check(chosen.back() == std::filesystem::path(L"C:\\Alpha"),
		               L"the frame's keys reach the focused tree");
		manager->detach();
		const auto narrow = std::make_shared<folders::FolderTreeView>(folders::FolderTreeView::Events{});
		narrow->set_roots({{L"C:\\", L"A very long drive label", true}});
		narrow->layout(measure, {0, 0, 120, 40});
		::ui::test::fake_draw_context clipped({0, 0, 120, 40});
		narrow->paint(clipped, false);
		reporter.check(clipped.drawn_text() == "A very...", L"a label too wide for the tree ends in an ellipsis");
	}

	template <class Reporter> void test_folder_tree(Reporter& reporter)
	{
		reporter.section(L"Drawn folder tree: lazy listing, reveal, keys, pointer and drawing");
		test_folder_tree_model(reporter);
		test_folder_tree_view(reporter);
	}
}

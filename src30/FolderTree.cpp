// ImageWalker by Zac Walker
// Implements the folder tree's lazily listed model and draws it through libdesktop's render context.

#include "FolderTree.h"
#include "Paths.h"
#include "Platform.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace iw::folders
{
	struct FolderTreeModel::Node
	{
		enum class Listing
		{
			unknown,
			pending,
			listed
		};

		Folder folder;
		Node* parent{};
		std::vector<std::unique_ptr<Node>> children;
		Listing listing{Listing::unknown};
		bool expanded{};
	};

	namespace
	{
		using Node = FolderTreeModel::Node;

		// Windows names a folder without regard to case or a separator after it, but a drive's root keeps one.
		std::wstring_view trimmed(const std::filesystem::path& path)
		{
			std::wstring_view text = path.native();
			while (text.size() > 1 && (text.back() == L'\\' || text.back() == L'/') &&
			       !(text.size() == 3 && text[1] == L':'))
				text.remove_suffix(1);
			return text;
		}

		bool same_folder(const std::filesystem::path& left, const std::filesystem::path& right)
		{
			return paths::iequals(trimmed(left), trimmed(right));
		}

		bool holds(const std::filesystem::path& folder, const std::filesystem::path& candidate)
		{
			return same_folder(folder, candidate) || paths::contains(folder, candidate);
		}

		bool has_subfolders(const Node& node)
		{
			return node.listing != Node::Listing::listed || !node.children.empty();
		}

		bool is_open(const Node& node)
		{
			return node.expanded && has_subfolders(node);
		}

		std::unique_ptr<Node> make_node(Folder folder, Node* parent)
		{
			auto node = std::make_unique<Node>();
			node->folder = std::move(folder);
			node->parent = parent;
			return node;
		}
	}

	FolderTreeModel::FolderTreeModel(ListRequest request) : request_(std::move(request))
	{
	}

	FolderTreeModel::~FolderTreeModel() = default;

	void FolderTreeModel::set_roots(std::vector<Folder> roots)
	{
		auto target = !reveal_.empty() ? reveal_ : selected_ ? selected_->folder.path : std::filesystem::path{};
		selected_ = nullptr;
		reveal_.clear();
		roots_.clear();
		for (auto& folder : roots) roots_.push_back(make_node(std::move(folder), nullptr));
		rebuild();
		if (!target.empty()) reveal(target);
	}

	void FolderTreeModel::listed(const std::filesystem::path& folder, std::vector<Folder> subfolders)
	{
		auto* node = find_listing(roots_, folder);
		if (!node) return;
		node->children.clear();
		for (auto& subfolder : subfolders) node->children.push_back(make_node(std::move(subfolder), node));
		node->listing = Node::Listing::listed;
		rebuild();
		continue_reveal();
	}

	std::optional<size_t> FolderTreeModel::selected_row() const
	{
		if (!selected_) return {};
		const auto found = std::ranges::find(rowNodes_, selected_);
		if (found == rowNodes_.end()) return {};
		return static_cast<size_t>(found - rowNodes_.begin());
	}

	std::optional<std::filesystem::path> FolderTreeModel::selected_path() const
	{
		if (!selected_) return {};
		return selected_->folder.path;
	}

	bool FolderTreeModel::select(const size_t row)
	{
		reveal_.clear();
		if (row >= rowNodes_.size() || rowNodes_[row] == selected_) return false;
		selected_ = rowNodes_[row];
		return true;
	}

	bool FolderTreeModel::expand(const size_t row)
	{
		reveal_.clear();
		if (row >= rowNodes_.size()) return false;
		auto& node = *rowNodes_[row];
		if (is_open(node) || !has_subfolders(node)) return false;
		open(node);
		rebuild();
		return true;
	}

	bool FolderTreeModel::collapse(const size_t row)
	{
		reveal_.clear();
		if (row >= rowNodes_.size()) return false;
		auto& node = *rowNodes_[row];
		if (!is_open(node)) return false;
		node.expanded = false;
		// A selection the folder now hides moves up to it, as a native tree's does.
		for (const auto* above = selected_; above; above = above->parent)
		{
			if (above != &node) continue;
			selected_ = &node;
			break;
		}
		rebuild();
		return true;
	}

	bool FolderTreeModel::toggle(const size_t row)
	{
		if (row >= rowNodes_.size()) return false;
		return is_open(*rowNodes_[row]) ? collapse(row) : expand(row);
	}

	bool FolderTreeModel::move(const Move move, const size_t page)
	{
		reveal_.clear();
		if (rowNodes_.empty()) return false;
		const auto last = rowNodes_.size() - 1;
		const auto current = selected_row();
		if (!current) return select(move == Move::end ? last : 0);
		const auto row = *current;
		const auto step = (std::max)(size_t{1}, page);
		switch (move)
		{
		case Move::up: return select(row > 0 ? row - 1 : 0);
		case Move::down: return select((std::min)(row + 1, last));
		case Move::home: return select(0);
		case Move::end: return select(last);
		case Move::pageUp: return select(row > step ? row - step : 0);
		case Move::pageDown: return select((std::min)(row + step, last));
		case Move::left:
		{
			const auto& node = *rowNodes_[row];
			if (is_open(node))
			{
				collapse(row);
				return false;
			}
			const auto parent = std::ranges::find(rowNodes_, node.parent);
			return node.parent && parent != rowNodes_.end() && select(static_cast<size_t>(parent - rowNodes_.begin()));
		}
		case Move::right:
		{
			const auto& node = *rowNodes_[row];
			if (!is_open(node))
			{
				expand(row);
				return false;
			}
			return node.listing == Node::Listing::listed && select(row + 1);
		}
		}
		return false;
	}

	void FolderTreeModel::reveal(const std::filesystem::path& path)
	{
		reveal_ = path;
		continue_reveal();
	}

	void FolderTreeModel::rebuild()
	{
		rows_.clear();
		rowNodes_.clear();
		for (const auto& root : roots_) add_rows(*root, 0);
	}

	void FolderTreeModel::add_rows(Node& node, const int depth)
	{
		rows_.push_back(
			{&node.folder, depth, has_subfolders(node), is_open(node), node.listing == Node::Listing::pending});
		rowNodes_.push_back(&node);
		if (!is_open(node)) return;
		for (const auto& child : node.children) add_rows(*child, depth + 1);
	}

	void FolderTreeModel::open(Node& node)
	{
		node.expanded = true;
		if (node.listing != Node::Listing::unknown) return;
		node.listing = Node::Listing::pending;
		if (request_) request_(node.folder.path);
	}

	void FolderTreeModel::continue_reveal()
	{
		// The roots arrive after the first folder is shown, which waits for them.
		if (reveal_.empty() || roots_.empty()) return;
		const auto target = reveal_;
		auto* nodes = &roots_;
		for (;;)
		{
			const auto next = std::ranges::find_if(*nodes, [&target](const auto& child)
			{ return holds(child->folder.path, target); });
			if (next == nodes->end()) break;
			auto& node = **next;
			if (same_folder(node.folder.path, target))
			{
				reveal_.clear();
				selected_ = &node;
				rebuild();
				return;
			}
			if (node.listing != Node::Listing::listed)
			{
				// listed() carries on from here once the subfolders arrive.
				open(node);
				rebuild();
				return;
			}
			node.expanded = true;
			nodes = &node.children;
		}
		reveal_.clear();
		selected_ = nullptr;
		rebuild();
	}

	FolderTreeModel::Node* FolderTreeModel::find_listing(std::vector<std::unique_ptr<Node>>& nodes,
	                                                     const std::filesystem::path& folder) const
	{
		for (const auto& node : nodes)
		{
			if (!holds(node->folder.path, folder)) continue;
			if (node->listing == Node::Listing::pending && same_folder(node->folder.path, folder)) return node.get();
			if (auto* found = find_listing(node->children, folder)) return found;
		}
		return nullptr;
	}

	namespace
	{
		const ::ui::view_theme& tree_theme()
		{
			static const auto theme = []
			{
				::ui::view_theme value;
				// The tree fills its own ground in the window's colour.
				value.window_background = 0;
				value.handle = core::rgba(232, 232, 232);
				value.handle_hover = core::rgba(166, 166, 166);
				value.handle_tracking = core::rgba(120, 120, 120);
				return value;
			}();
			return theme;
		}

		core::color to_core(const color value)
		{
			return core::color(core::rgba(value.r, value.g, value.b, value.a));
		}

		constexpr color hotBackground{229, 241, 251};
		constexpr color inactiveSelection{217, 217, 217};

		// A glyph scaled to size, averaged with its alpha premultiplied so the transparent pixels around it -
		// white in the folder bitmap - leave no fringe.
		core::surface_ptr scaled_glyph(const platform::BitmapResource& image, const core::recti source, const int size)
		{
			if (size <= 0 || source.is_empty() || source.left < 0 || source.top < 0 ||
			    source.right > image.size.width || source.bottom > image.size.height ||
			    image.pixels.size() < static_cast<size_t>(image.size.width) * image.size.height)
				return {};
			auto surface = std::make_shared<core::surface>();
			surface->alloc({size, size}, core::texture_format::ARGB);
			const double stepX = static_cast<double>(source.width()) / size;
			const double stepY = static_cast<double>(source.height()) / size;
			for (int y = 0; y < size; ++y)
			{
				auto* row = reinterpret_cast<std::uint32_t*>(surface->pixels_line(y));
				const double top = source.top + y * stepY;
				const double bottom = top + stepY;
				for (int x = 0; x < size; ++x)
				{
					const double left = source.left + x * stepX;
					const double right = left + stepX;
					std::array<double, 4> sum{};
					double area = 0;
					const int lastY = (std::min)(static_cast<int>(std::ceil(bottom)), source.bottom);
					const int lastX = (std::min)(static_cast<int>(std::ceil(right)), source.right);
					for (int sy = static_cast<int>(top); sy < lastY; ++sy)
					{
						const double height = (std::min)(bottom, sy + 1.0) - (std::max)(top, static_cast<double>(sy));
						for (int sx = static_cast<int>(left); sx < lastX; ++sx)
						{
							const double weight = height * ((std::min)(right, sx + 1.0) -
							                                (std::max)(left, static_cast<double>(sx)));
							if (weight <= 0) continue;
							const auto pixel = image.pixels[static_cast<size_t>(sy) * image.size.width + sx];
							const double alpha = (pixel >> 24) * weight;
							sum[0] += alpha;
							for (size_t channel = 1; channel < 4; ++channel)
								sum[channel] += ((pixel >> (24 - 8 * channel)) & 0xff) * alpha;
							area += weight;
						}
					}
					std::uint32_t value = 0;
					if (sum[0] > 0 && area > 0)
					{
						value = static_cast<std::uint32_t>(std::lround((std::min)(255.0, sum[0] / area))) << 24;
						for (size_t channel = 1; channel < 4; ++channel)
							value |= static_cast<std::uint32_t>(std::lround((std::min)(255.0, sum[channel] / sum[0])))
							         << (24 - 8 * channel);
					}
					row[x] = value;
				}
			}
			return surface;
		}

		std::wstring fitted_label(render::draw_context& dc, const render::font_face font, const std::wstring& label,
		                          const int width)
		{
			const auto measure = [&dc, font](const std::wstring_view text)
			{
				return dc
				    .measure_text(::core::utf16_to_utf8(text), font, render::text_style::single_line,
				                  (std::numeric_limits<int>::max)() / 4)
				    .cx;
			};
			if (measure(label) <= width) return label;
			constexpr std::wstring_view dots = L"...";
			const int available = width - measure(dots);
			size_t low = 0, high = label.size();
			while (low < high)
			{
				const size_t middle = low + (high - low + 1) / 2;
				if (measure(std::wstring_view(label).substr(0, middle)) <= available) low = middle;
				else high = middle - 1;
			}
			// Never end between the halves of a surrogate pair.
			if (low > 0 && low < label.size() && label[low - 1] >= 0xd800 && label[low - 1] <= 0xdbff) --low;
			return label.substr(0, low) + std::wstring(dots);
		}
	}

	FolderTreeView::FolderTreeView(Events events) :
		view_base(tree_theme()), model_(
									 [this](const std::filesystem::path& folder)
	{
		if (const auto list = events_.list) list(folder);
	}),
		events_(std::move(events))
	{
	}

	void FolderTreeView::set_roots(std::vector<Folder> roots)
	{
		model_.set_roots(std::move(roots));
		hot_ = -1;
		_scroll_offset = {};
		rows_changed();
		if (const auto row = model_.selected_row()) ensure_visible(*row);
	}

	void FolderTreeView::listed(const std::filesystem::path& folder, std::vector<Folder> subfolders)
	{
		const bool revealing = model_.revealing();
		model_.listed(folder, std::move(subfolders));
		rows_changed();
		if (revealing && !model_.revealing())
			if (const auto row = model_.selected_row()) ensure_visible(*row);
	}

	void FolderTreeView::reveal(const std::filesystem::path& path)
	{
		model_.reveal(path);
		rows_changed();
		if (!model_.revealing())
			if (const auto row = model_.selected_row()) ensure_visible(*row);
	}

	std::optional<std::filesystem::path> FolderTreeView::folder_at(const core::pointi loc) const
	{
		const auto hit = hit_test(loc);
		if (hit.part == Part::none) return {};
		return model_.rows()[hit.row].folder->path;
	}

	void FolderTreeView::on_placed(render::measure_context& mc)
	{
		update_metrics(mc);
		view_base::on_placed(mc);
		rows_changed();
		if (const auto row = model_.selected_row(); row && !model_.revealing()) ensure_visible(*row);
	}

	void FolderTreeView::paint(render::draw_context& dc, const bool focused) const
	{
		const auto client = client_rect();
		const auto background = to_core(platform::system_color(platform::SystemColor::window));
		dc.draw_rect(client, background);
		const auto& rows = model_.rows();
		if (rowHeight_ <= 0 || rows.empty())
		{
			draw_scrollbars(dc);
			return;
		}
		const auto selected = model_.selected_row();
		const auto text = to_core(platform::system_color(platform::SystemColor::windowText));
		const auto highlightText = to_core(platform::system_color(platform::SystemColor::highlightText));
		const auto expander = to_core(platform::system_color(platform::SystemColor::grayText));
		const int lineHeight = dc.text_line_height(font_);
		const int textRight = client.right - padding_ -
		                      (_vscroll.can_scroll() ? _vscroll.thumb_thickness() + _vscroll.edge_margin() * 2 : 0);
		const auto first = static_cast<size_t>((std::max)(0, _scroll_offset.y - padding_) / rowHeight_);
		for (size_t index = first; index < rows.size(); ++index)
		{
			const int top = padding_ + static_cast<int>(index) * rowHeight_ - _scroll_offset.y;
			if (top >= client.bottom) break;
			const auto& row = rows[index];
			const core::recti line{client.left, top, client.right, top + rowHeight_};
			const bool isSelected = selected && *selected == index;
			const bool emphasised = isSelected && focused;
			if (isSelected)
				dc.draw_rect(line, to_core(emphasised ? platform::system_color(platform::SystemColor::highlight)
				                                      : inactiveSelection));
			else if (static_cast<int>(index) == hot_) dc.draw_rect(line, to_core(hotBackground));

			const int x = padding_ + row.depth * indent_;
			const int middle = top + rowHeight_ / 2;
			if (row.expandable)
			{
				const double centre = x + indent_ / 2.0;
				const double arm = 3.5 * scale_;
				const auto open = row.expanded;
				const std::array points = open ? std::array{core::pointd(centre - arm, middle - arm / 2),
				                                            core::pointd(centre, middle + arm / 2),
				                                            core::pointd(centre + arm, middle - arm / 2)}
				                               : std::array{core::pointd(centre - arm / 2, middle - arm),
				                                            core::pointd(centre + arm / 2, middle),
				                                            core::pointd(centre - arm / 2, middle + arm)};
				dc.draw_polyline(points, emphasised ? highlightText : expander, (std::max)(1.0, 1.4 * scale_));
			}

			const core::recti icon{x + indent_, middle - iconSize_ / 2, x + indent_ + iconSize_,
			                       middle - iconSize_ / 2 + iconSize_};
			if (const auto& texture = glyph(dc, row.folder->drive))
				dc.draw_texture(texture, icon, 1.0f, render::texture_sampler::bilinear);

			const int left = icon.right + padding_;
			if (left >= textRight) continue;
			const auto label = fitted_label(dc, font_, row.folder->label, textRight - left);
			const int textTop = middle - lineHeight / 2;
			dc.draw_text(::core::utf16_to_utf8(label), {left, textTop, textRight, textTop + lineHeight}, font_,
			             render::text_style::single_line, emphasised ? highlightText : text, core::color(0, 0, 0, 0));
		}
		draw_scrollbars(dc);
	}

	bool FolderTreeView::on_key(const char32_t key, const ::platform::key_state keys)
	{
		if (keys.control || keys.alt) return false;
		using Move = FolderTreeModel::Move;
		namespace keys_ = ::platform::keys;
		const auto before = model_.selected_path();
		const auto page = static_cast<size_t>((std::max)(1, _view_extent.cy / (std::max)(1, rowHeight_)));
		const auto selected = model_.selected_row();
		// clang-format off
		if (key == keys_::UP) model_.move(Move::up);
		else if (key == keys_::DOWN) model_.move(Move::down);
		else if (key == keys_::LEFT) model_.move(Move::left);
		else if (key == keys_::RIGHT) model_.move(Move::right);
		else if (key == keys_::HOME) model_.move(Move::home);
		else if (key == keys_::END) model_.move(Move::end);
		else if (key == keys_::PRIOR) model_.move(Move::pageUp, page);
		else if (key == keys_::NEXT) model_.move(Move::pageDown, page);
		else if (key == keys_::RETURN) { if (selected) model_.toggle(*selected); }
		else if (key == keys_::ADD) { if (selected) model_.expand(*selected); }
		else if (key == keys_::SUBTRACT) { if (selected) model_.collapse(*selected); }
		else return false;
		// clang-format on
		rows_changed();
		if (const auto row = model_.selected_row()) ensure_visible(*row);
		report_if_changed(before);
		return true;
	}

	void FolderTreeView::on_mouse_down(const core::pointi loc, const ::platform::key_state keys)
	{
		if (_vscroll.can_scroll() && loc.x >= _view_extent.cx - _vscroll.thumb_thickness() - _vscroll.edge_margin() * 2)
		{
			view_base::on_mouse_down(loc, keys);
			return;
		}
		const auto hit = hit_test(loc);
		if (hit.part == Part::none) return;
		const auto before = model_.selected_path();
		if (hit.part == Part::expander) model_.toggle(hit.row);
		else model_.select(hit.row);
		rows_changed();
		report_if_changed(before);
	}

	void FolderTreeView::on_mouse_move(const core::pointi loc, const bool captured)
	{
		view_base::on_mouse_move(loc, captured);
		if (_vscroll._tracking) return;
		const auto hit = hit_test(loc);
		const int hot = hit.part == Part::none ? -1 : static_cast<int>(hit.row);
		if (hot == hot_) return;
		hot_ = hot;
		invalidate();
	}

	void FolderTreeView::on_double_click(const core::pointi loc, const ::platform::key_state keys)
	{
		const auto hit = hit_test(loc);
		if (hit.part != Part::folder)
		{
			on_mouse_down(loc, keys);
			return;
		}
		const auto before = model_.selected_path();
		model_.select(hit.row);
		model_.toggle(hit.row);
		rows_changed();
		report_if_changed(before);
	}

	void FolderTreeView::on_mouse_leave()
	{
		view_base::on_mouse_leave();
		if (hot_ < 0) return;
		hot_ = -1;
		invalidate();
	}

	bool FolderTreeView::on_wheel(const core::pointi, const int delta, const ::platform::key_state)
	{
		if (delta == 0 || !_vscroll.can_scroll()) return false;
		const auto before = _scroll_offset;
		// Three rows a notch, as a native tree scrolls.
		scroll_to({0, _scroll_offset.y - delta * 3 * rowHeight_ / 120});
		return _scroll_offset != before;
	}

	void FolderTreeView::scale_changed()
	{
		view_base::scale_changed();
		if (auto* const mc = metrics()) update_metrics(*mc);
		rows_changed();
	}

	void FolderTreeView::dispatch_event(const ::ui::element_event& event)
	{
		if (event.type != ::ui::element_event_type::free_graphics_resources) return;
		folderGlyph_.reset();
		driveGlyph_.reset();
	}

	void FolderTreeView::update_metrics(render::measure_context& mc)
	{
		scale_ = mc.scale_factor > 0 ? mc.scale_factor : 1.0;
		const auto scaled = [this](const double value)
		{
			return (std::max)(1, static_cast<int>(std::lround(value * scale_)));
		};
		if (const auto font = platform::create_message_font(static_cast<unsigned int>(std::lround(96 * scale_)));
		    font && *font)
			font_ = static_cast<render::font_face>(font->native_handle());
		const int icon = scaled(16);
		if (icon != iconSize_)
		{
			folderGlyph_.reset();
			driveGlyph_.reset();
		}
		iconSize_ = icon;
		indent_ = scaled(16);
		padding_ = scaled(4);
		rowHeight_ = (std::max)(mc.text_line_height(font_), iconSize_) + scaled(6);
	}

	void FolderTreeView::rows_changed()
	{
		const auto count = static_cast<int>(model_.rows().size());
		if (hot_ >= count) hot_ = -1;
		_content_extent = {_view_extent.cx, count * rowHeight_ + padding_ * 2};
		clamp_scroll_offset();
		invalidate();
	}

	void FolderTreeView::ensure_visible(const size_t row)
	{
		if (_view_extent.cy <= 0) return;
		const int top = padding_ + static_cast<int>(row) * rowHeight_;
		auto offset = _scroll_offset;
		if (top - padding_ < offset.y) offset.y = top - padding_;
		else if (top + rowHeight_ + padding_ > offset.y + _view_extent.cy)
			offset.y = top + rowHeight_ + padding_ - _view_extent.cy;
		scroll_to(offset);
	}

	FolderTreeView::Hit FolderTreeView::hit_test(const core::pointi loc) const
	{
		const auto& rows = model_.rows();
		if (rowHeight_ <= 0 || loc.x < 0 || loc.y < 0 || loc.x >= _view_extent.cx || loc.y >= _view_extent.cy)
			return {};
		const int y = loc.y + _scroll_offset.y - padding_;
		if (y < 0) return {};
		const auto row = static_cast<size_t>(y / rowHeight_);
		if (row >= rows.size()) return {};
		const int x = padding_ + rows[row].depth * indent_;
		if (rows[row].expandable && loc.x >= x && loc.x < x + indent_) return {row, Part::expander};
		return {row, Part::folder};
	}

	void FolderTreeView::report_if_changed(const std::optional<std::filesystem::path>& before)
	{
		const auto after = model_.selected_path();
		if (!after || (before && same_folder(*before, *after))) return;
		if (const auto selected = events_.selected) selected(*after);
	}

	const render::texture_ptr& FolderTreeView::glyph(render::draw_context& dc, const bool drive) const
	{
		auto& texture = drive ? driveGlyph_ : folderGlyph_;
		if (texture) return texture;
		core::surface_ptr surface;
		if (drive)
		{
			// The drive tile of the toolbar strip; see platform::CommandBitmap.
			constexpr int tile = 24;
			const auto strip = platform::load_bitmap_resource(platform::BitmapAsset::toolbarSymbols);
			const int left = static_cast<int>(platform::CommandBitmap::drive) * tile;
			surface = scaled_glyph(strip, {left, 0, left + tile, tile}, iconSize_);
		}
		else
		{
			const auto folder = platform::load_bitmap_resource(platform::BitmapAsset::fileFolder);
			surface = scaled_glyph(folder, {0, 0, folder.size.width, folder.size.height}, iconSize_);
		}
		if (!surface) return texture;
		texture = dc.create_texture();
		if (texture && texture->update(surface) == render::texture_update_result::failed) texture.reset();
		return texture;
	}
}

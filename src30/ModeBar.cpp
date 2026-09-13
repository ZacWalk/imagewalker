// ImageWalker by Zac Walker
// Implements the drawn mode strip and folder shortcut bar at the left edge.

#include "ModeBar.h"

#include <algorithm>

namespace iw
{
	bool ModeBar::create(const platform::WindowFramePtr& parent)
	{
		if (!parent) return false;
		platform::WindowOptions options;
		options.className = "ImageWalker30.ModeBar";
		options.child = true;
		options.visible = true;
		options.eraseBackground = false;
		frame_ = parent->create_child(shared_from_this(), options);
		if (!frame_) return false;
		dpi_ = frame_->dpi();
		return true;
	}

	void ModeBar::set_modes(std::vector<Button> modes)
	{
		modes_ = std::move(modes);
		layout();
		invalidate();
	}

	void ModeBar::set_shortcuts(std::vector<Button> shortcuts)
	{
		shortcuts_ = std::move(shortcuts);
		layout();
		invalidate();
	}

	void ModeBar::set_checked(const int id)
	{
		bool changed = false;
		for (auto& mode : modes_)
		{
			// Folders is a toggle, not a mode, so it keeps whatever state it was given.
			if (mode.glyph == Glyph::folders || mode.glyph == Glyph::fullscreen) continue;
			const bool checked = mode.id == id;
			if (mode.checked == checked) continue;
			mode.checked = checked;
			changed = true;
		}
		if (changed) invalidate();
	}

	void ModeBar::set_enabled(const int id, const bool enabled)
	{
		for (auto& mode : modes_)
		{
			if (mode.id != id || mode.enabled == enabled) continue;
			mode.enabled = enabled;
			invalidate();
			return;
		}
	}

	void ModeBar::set_dpi(const unsigned int dpi)
	{
		dpi_ = dpi ? dpi : 96;
		layout();
		invalidate();
	}

	void ModeBar::invalidate()
	{
		if (frame_) frame_->invalidate();
	}

	void ModeBar::layout()
	{
		modeBounds_.clear();
		shortcutBounds_.clear();
		if (!frame_) return;
		const recti client = frame_->client_rect();
		const int edge = metric(buttonEdge);
		const int left = (client.width - edge) / 2;
		int y = metric(2);
		modeBounds_.reserve(modes_.size());
		for (size_t index = 0; index < modes_.size(); ++index)
		{
			modeBounds_.push_back({left, y, edge, edge});
			y += edge;
		}
		separatorY_ = y + metric(4);
		y = separatorY_ + metric(5);
		shortcutBounds_.reserve(shortcuts_.size());
		for (size_t index = 0; index < shortcuts_.size(); ++index)
		{
			// The shortcut list is as long as the user's, so it clips rather than scrolls.
			if (y + edge > client.bottom()) break;
			shortcutBounds_.push_back({left, y, edge, edge});
			y += edge;
		}
	}

	void ModeBar::draw_glyph(const ui::CanvasRenderer& renderer, const Glyph glyph, const recti bounds,
	                         const color value) const
	{
		const int unit = (std::max)(2, bounds.width / 8);
		const int thin = (std::max)(1, unit / 2);
		switch (glyph)
		{
		case Glyph::browse:
			for (int row = 0; row < 2; ++row)
				for (int column = 0; column < 2; ++column)
					renderer.fill({
						              bounds.x + column * (unit * 4 + unit), bounds.y + row * (unit * 4 + unit),
						              unit * 3, unit * 3
					              }, value);
			break;

		case Glyph::folders:
		case Glyph::folder:
			renderer.fill({bounds.x, bounds.y + unit, unit * 3, unit}, value);
			renderer.outline({bounds.x, bounds.y + unit * 2, bounds.width, bounds.height - unit * 3}, value,
			                 thin);
			break;

		case Glyph::convert:
			renderer.outline({bounds.x, bounds.y, unit * 4, unit * 4}, value, thin);
			renderer.fill({bounds.x + unit * 3, bounds.y + unit * 3, unit * 4, unit * 4}, value);
			break;

		case Glyph::rename:
			renderer.fill({bounds.x, bounds.y + unit * 2, bounds.width, thin}, value);
			renderer.fill({bounds.x + unit * 3, bounds.y, thin * 2, bounds.height}, value);
			renderer.fill({bounds.x, bounds.bottom() - thin, bounds.width, thin}, value);
			break;

		case Glyph::edit:
			renderer.fill({bounds.x + unit * 2, bounds.y, thin, bounds.height}, value);
			renderer.fill({bounds.x, bounds.y + unit * 2, bounds.width, thin}, value);
			renderer.fill({bounds.x + unit * 5, bounds.y, thin, bounds.height}, value);
			renderer.fill({bounds.x, bounds.y + unit * 5, bounds.width, thin}, value);
			break;

		case Glyph::sync:
			renderer.fill({bounds.x, bounds.y + unit, bounds.width - unit, thin * 2}, value);
			renderer.fill({bounds.right() - unit * 2, bounds.y, unit * 2, unit * 3}, value);
			renderer.fill({bounds.x + unit, bounds.bottom() - unit * 3, bounds.width - unit, thin * 2}, value);
			renderer.fill({bounds.x, bounds.bottom() - unit * 4, unit * 2, unit * 3}, value);
			break;

		case Glyph::fullscreen:
			renderer.outline(bounds, value, thin);
			renderer.fill({bounds.x + unit * 2, bounds.y + unit * 2, unit * 3, unit * 3}, value);
			break;

		case Glyph::pin:
			renderer.fill({bounds.x + bounds.width / 2 - thin, bounds.y, thin * 2, bounds.height}, value);
			renderer.fill({bounds.x, bounds.y + bounds.height / 2 - thin, bounds.width, thin * 2}, value);
			break;
		}
	}

	void ModeBar::draw_button(const ui::CanvasRenderer& renderer, const Button& button, const recti bounds,
	                          const bool hot) const
	{
		const int captionHeight = (std::max)(metric(12), renderer.measure_text(L"Wg").height);
		const int glyphEdge = metric(24);
		if (hot || button.checked)
			renderer.fill(bounds, platform::calc_hande_color(hot, false, button.checked));
		if (button.checked)
			renderer.outline(bounds, platform::system_color(platform::SystemColor::highlight));
		const recti glyph{
			bounds.x + (bounds.width - glyphEdge) / 2,
			bounds.y + (bounds.height - captionHeight - glyphEdge) / 2, glyphEdge, glyphEdge
		};
		const color foreground = button.enabled
			                         ? platform::system_color(platform::SystemColor::buttonText)
			                         : platform::system_color(platform::SystemColor::grayText);
		draw_glyph(renderer, button.glyph, glyph, foreground);
		renderer.text(button.caption, {
			              bounds.x + metric(2), bounds.bottom() - captionHeight - metric(4),
			              bounds.width - metric(4), captionHeight
		              }, foreground,
		              platform::TextFormat::center | platform::TextFormat::singleLine |
		              platform::TextFormat::endEllipsis);
	}

	void ModeBar::paint(const platform::WindowFramePtr& frame, platform::DrawContext& draw)
	{
		if (frame != frame_) return;
		const ui::CanvasRenderer renderer(draw, platform::create_message_font(dpi_));
		const recti client = frame_->client_rect();
		renderer.fill(client, platform::system_color(platform::SystemColor::face));
		layout();
		for (size_t index = 0; index < modeBounds_.size(); ++index)
			draw_button(renderer, modes_[index], modeBounds_[index], modes_[index].id == hotId_);
		if (!shortcutBounds_.empty() || !modeBounds_.empty())
			renderer.outline({metric(6), separatorY_, client.width - metric(12), 1},
			                 platform::system_color(platform::SystemColor::shadow));
		for (size_t index = 0; index < shortcutBounds_.size(); ++index)
			draw_button(renderer, shortcuts_[index], shortcutBounds_[index], shortcuts_[index].id == hotId_);
		renderer.outline({client.right() - 1, client.y, 1, client.height},
		                 platform::system_color(platform::SystemColor::shadow));
	}

	void ModeBar::size(const platform::WindowFramePtr& frame, sizei, platform::MeasureContext&)
	{
		if (frame != frame_) return;
		layout();
		invalidate();
	}

	const ModeBar::Button* ModeBar::button_at(const pointi point) const
	{
		for (size_t index = 0; index < modeBounds_.size(); ++index)
			if (modeBounds_[index].contains(point)) return &modes_[index];
		for (size_t index = 0; index < shortcutBounds_.size(); ++index)
			if (shortcutBounds_[index].contains(point)) return &shortcuts_[index];
		return nullptr;
	}

	platform::MessageResult ModeBar::mouse(const platform::WindowFramePtr& frame,
	                                       const platform::MouseMessage message,
	                                       const platform::MouseInput& input)
	{
		if (frame != frame_) return 0;
		switch (message)
		{
		case platform::MouseMessage::move:
			{
				if (!trackingMouse_)
				{
					trackingMouse_ = true;
					frame_->track_mouse_leave();
				}
				const auto* button = button_at(input.point);
				const int hot = button && button->enabled ? button->id : 0;
				if (hot != hotId_)
				{
					hotId_ = hot;
					invalidate();
				}
				break;
			}

		case platform::MouseMessage::leave:
			trackingMouse_ = false;
			if (hotId_)
			{
				hotId_ = 0;
				invalidate();
			}
			break;

		case platform::MouseMessage::leftButtonDown:
		case platform::MouseMessage::leftButtonDoubleClick:
			if (const auto* button = button_at(input.point); button && button->enabled && commandHandler_)
			{
				// Copy first: the handler may rebuild the vector this points into.
				const Button chosen = *button;
				commandHandler_(chosen);
			}
			break;

		case platform::MouseMessage::contextMenu:
			if (const auto* button = button_at(input.point); button && button->removable && removeHandler_)
			{
				const Button chosen = *button;
				removeHandler_(chosen);
			}
			break;

		default:
			break;
		}
		return 0;
	}
}

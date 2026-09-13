// ImageWalker by Zac Walker
// Implements layout, drawing, and hit testing for the drawn controls panel.

#include "ControlPanel.h"

#include <algorithm>
#include <format>

namespace iw::ui
{
	namespace
	{
		constexpr int rowSpacing = 6;
		constexpr int checkSize = 15;
		constexpr int sliderHeight = 20;
		constexpr int buttonHeight = 24;
		constexpr int fieldHeight = 24;
		constexpr int indentStep = 18;

		// The renderer draws one line at a time, so wrapping is the panel's own job.
		std::vector<std::wstring> wrap(const CanvasRenderer& renderer, const std::wstring_view text,
		                               const int width)
		{
			std::vector<std::wstring> lines;
			if (text.empty() || width <= 0) return lines;
			std::wstring line;
			for (size_t index = 0; index <= text.size();)
			{
				const size_t space = text.find(L' ', index);
				const auto word = text.substr(index, space == std::wstring_view::npos
					                                  ? std::wstring_view::npos
					                                  : space - index);
				std::wstring candidate = line.empty() ? std::wstring(word) : line + L" " + std::wstring(word);
				if (!line.empty() &&
					renderer.measure_text(candidate, platform::TextFormat::singleLine).width > width)
				{
					lines.push_back(std::move(line));
					line.assign(word);
				}
				else line = std::move(candidate);
				if (space == std::wstring_view::npos) break;
				index = space + 1;
			}
			if (!line.empty()) lines.push_back(std::move(line));
			return lines;
		}
	}

	Control& ControlPanel::add(Control control)
	{
		controls_.push_back(std::move(control));
		return controls_.back();
	}

	Control* ControlPanel::find(const int id)
	{
		const auto found = std::ranges::find(controls_, id, &Control::id);
		return found == controls_.end() ? nullptr : &*found;
	}

	const Control* ControlPanel::find(const int id) const
	{
		const auto found = std::ranges::find(controls_, id, &Control::id);
		return found == controls_.end() ? nullptr : &*found;
	}

	void ControlPanel::layout(const recti bounds, const CanvasRenderer& renderer)
	{
		const int lineHeight = (std::max)(metric(16), renderer.measure_text(L"Wg").height);
		int y = bounds.y;
		for (auto& control : controls_)
		{
			const int left = bounds.x + control.indent * metric(indentStep);
			const int width = (std::max)(metric(40), bounds.right() - left);
			int height = lineHeight;
			switch (control.kind)
			{
			case ControlKind::heading:
				height = lineHeight + metric(4);
				break;
			case ControlKind::label:
				{
					control.wrapped = wrap(renderer, control.label, width);
					height = lineHeight * static_cast<int>((std::max<size_t>)(1, control.wrapped.size()));
					break;
				}
			case ControlKind::checkBox:
			case ControlKind::radio:
				height = (std::max)(lineHeight, metric(checkSize));
				break;
			case ControlKind::slider:
				height = lineHeight + metric(sliderHeight);
				break;
			case ControlKind::choice:
			case ControlKind::folder:
			case ControlKind::text:
				height = lineHeight + metric(fieldHeight);
				break;
			case ControlKind::button:
				height = metric(buttonHeight);
				break;
			case ControlKind::link:
				height = lineHeight;
				break;
			case ControlKind::separator:
				height = metric(rowSpacing) * 2;
				break;
			}
			control.bounds = {left, y, width, height};
			switch (control.kind)
			{
			case ControlKind::checkBox:
			case ControlKind::radio:
			case ControlKind::link:
			case ControlKind::button:
				control.hit = control.bounds;
				break;
			case ControlKind::slider:
				control.hit = {left, y + lineHeight, width, metric(sliderHeight)};
				break;
			case ControlKind::choice:
				control.hit = {left, y + lineHeight, width, metric(fieldHeight)};
				break;
			case ControlKind::folder:
				{
					const int browse = (std::min)(width, metric(72));
					control.hit = {
						left + width - browse, y + lineHeight, browse, metric(fieldHeight)
					};
					break;
				}
			case ControlKind::text:
				control.hit = {left, y + lineHeight, width, metric(fieldHeight)};
				break;
			default:
				control.hit = {};
				break;
			}
			y += height + metric(rowSpacing);
		}
		contentHeight_ = y - bounds.y;
	}

	recti ControlPanel::text_bounds(const int id) const
	{
		const auto* control = find(id);
		if (!control) return {};
		if (control->kind == ControlKind::text) return control->hit;
		if (control->kind != ControlKind::folder) return {};
		return {control->bounds.x, control->hit.y,
			(std::max)(0, control->hit.x - control->bounds.x - metric(4)), control->hit.height};
	}

	void ControlPanel::draw(const CanvasRenderer& renderer) const
	{
		const color text = platform::system_color(platform::SystemColor::windowText);
		const color disabled = platform::system_color(platform::SystemColor::grayText);
		const color face = platform::system_color(platform::SystemColor::face);
		const color shadow = platform::system_color(platform::SystemColor::shadow);
		const color highlight = platform::system_color(platform::SystemColor::highlight);
		const int lineHeight = (std::max)(metric(16), renderer.measure_text(L"Wg").height);

		for (const auto& control : controls_)
		{
			const bool enabled = interactive_ && control.enabled;
			const color foreground = enabled ? text : disabled;
			const bool hot = enabled && control.id == hotId_;
			switch (control.kind)
			{
			case ControlKind::heading:
				renderer.text(control.label, control.bounds, foreground,
				              platform::TextFormat::left | platform::TextFormat::singleLine);
				renderer.outline({
					                 control.bounds.x, control.bounds.bottom() - 1, control.bounds.width, 1
				                 }, shadow);
				break;

			case ControlKind::label:
				for (size_t line = 0; line < control.wrapped.size(); ++line)
					renderer.text(control.wrapped[line], {
						              control.bounds.x, control.bounds.y + static_cast<int>(line) * lineHeight,
						              control.bounds.width, lineHeight
					              }, foreground,
					              platform::TextFormat::left | platform::TextFormat::singleLine);
				break;

			case ControlKind::separator:
				renderer.outline({
					                 control.bounds.x, control.bounds.y + control.bounds.height / 2,
					                 control.bounds.width, 1
				                 }, shadow);
				break;

			case ControlKind::checkBox:
			case ControlKind::radio:
				{
					const int box = metric(checkSize);
					const recti mark{
						control.bounds.x, control.bounds.y + (control.bounds.height - box) / 2, box, box
					};
					renderer.fill(mark, platform::calc_hande_color(hot, false, false));
					renderer.outline(mark, enabled ? shadow : disabled);
					if (control.checked)
					{
						const int inset = (std::max)(2, box / 4);
						renderer.fill({
							              mark.x + inset, mark.y + inset, box - inset * 2, box - inset * 2
						              }, enabled ? highlight : disabled);
					}
					renderer.text(control.label, {
						              mark.right() + metric(6), control.bounds.y,
						              control.bounds.width - box - metric(6), control.bounds.height
					              }, foreground,
					              platform::TextFormat::left | platform::TextFormat::verticalCenter |
					              platform::TextFormat::singleLine | platform::TextFormat::endEllipsis);
					break;
				}

			case ControlKind::slider:
				{
					const auto readout = std::format(L"{}{}{}", control.label.empty() ? L"" : L" ", control.value,
					                                 control.suffix);
					renderer.text(control.label + readout, {
						              control.bounds.x, control.bounds.y, control.bounds.width, lineHeight
					              }, foreground,
					              platform::TextFormat::left | platform::TextFormat::singleLine);
					const recti track{
						control.hit.x, control.hit.y + control.hit.height / 2 - metric(2),
						control.hit.width, metric(4)
					};
					renderer.fill(track, face);
					renderer.outline(track, shadow);
					const auto span = (std::max)(1LL, static_cast<long long>(control.maximum) - control.minimum);
					const int travel = (std::max)(0, control.hit.width - metric(10));
					const int offset = static_cast<int>(travel *
						std::clamp(static_cast<long long>(control.value) - control.minimum, 0LL, span) / span);
					const recti thumb{
						control.hit.x + offset, control.hit.y + metric(2), metric(10),
						control.hit.height - metric(4)
					};
					renderer.fill(thumb, platform::calc_hande_color(hot, dragging_ && control.id == pressedId_,
					                                                false));
					renderer.outline(thumb, enabled ? shadow : disabled);
					break;
				}

			case ControlKind::choice:
				{
					renderer.text(control.label, {
						              control.bounds.x, control.bounds.y, control.bounds.width, lineHeight
					              }, foreground,
					              platform::TextFormat::left | platform::TextFormat::singleLine);
					renderer.fill(control.hit, platform::calc_hande_color(hot, false, false));
					renderer.outline(control.hit, enabled ? shadow : disabled);
					const auto value = control.value >= 0 && static_cast<size_t>(control.value) < control.choices.
						size()
						                   ? control.choices[static_cast<size_t>(control.value)]
						                   : std::wstring{};
					renderer.text(value, {
						              control.hit.x + metric(6), control.hit.y,
						              control.hit.width - metric(24), control.hit.height
					              }, foreground,
					              platform::TextFormat::left | platform::TextFormat::verticalCenter |
					              platform::TextFormat::singleLine | platform::TextFormat::endEllipsis);
					renderer.text(L"\u25be", {
						              control.hit.right() - metric(20), control.hit.y, metric(16),
						              control.hit.height
					              }, foreground,
					              platform::TextFormat::center | platform::TextFormat::verticalCenter |
					              platform::TextFormat::singleLine);
					break;
				}

			case ControlKind::folder:
			case ControlKind::text:
				{
					renderer.text(control.label, {
						              control.bounds.x, control.bounds.y, control.bounds.width, lineHeight
					              }, foreground,
					              platform::TextFormat::left | platform::TextFormat::singleLine);
					const auto field = text_bounds(control.id);
					renderer.fill(field, platform::system_color(platform::SystemColor::window));
					renderer.outline(field, enabled ? shadow : disabled);
					renderer.text(control.text, {field.x + metric(4), field.y,
						(std::max)(0, field.width - metric(8)), field.height}, foreground,
						platform::TextFormat::left | platform::TextFormat::verticalCenter |
						platform::TextFormat::singleLine | platform::TextFormat::endEllipsis);
					if (control.kind != ControlKind::folder) break;
					renderer.fill(control.hit, platform::calc_hande_color(hot, false, false));
					renderer.outline(control.hit, enabled ? shadow : disabled);
					renderer.text(L"Browse...", control.hit, foreground,
					              platform::TextFormat::center | platform::TextFormat::verticalCenter |
					              platform::TextFormat::singleLine);
					break;
				}

			case ControlKind::button:
				renderer.fill(control.hit, platform::calc_hande_color(hot, control.id == pressedId_, false));
				renderer.outline(control.hit, enabled ? shadow : disabled);
				renderer.text(control.label, control.hit, foreground,
				              platform::TextFormat::center | platform::TextFormat::verticalCenter |
				              platform::TextFormat::singleLine);
				break;

			case ControlKind::link:
				renderer.text(control.label, control.bounds,
				              enabled ? highlight : disabled,
				              platform::TextFormat::left | platform::TextFormat::verticalCenter |
				              platform::TextFormat::singleLine);
				break;
			}
			if (enabled && control.id == focusedId_)
				renderer.outline(control.hit, highlight);
		}
	}

	Control* ControlPanel::control_at(const pointi point)
	{
		if (!interactive_) return nullptr;
		for (auto& control : controls_)
		{
			if (!control.enabled || control.hit.width <= 0) continue;
			if (control.hit.contains(point)) return &control;
		}
		return nullptr;
	}

	void ControlPanel::apply_radio(const Control& chosen)
	{
		if (!chosen.group) return;
		for (auto& control : controls_)
			if (control.kind == ControlKind::radio && control.group == chosen.group && control.id != chosen.id)
				control.checked = false;
	}

	int ControlPanel::slider_value_at(const Control& control, const int x) const
	{
		const int travel = (std::max)(1, control.hit.width - metric(10));
		const int offset = std::clamp(x - control.hit.x - metric(5), 0, travel);
		const auto span = static_cast<long long>((std::max)(control.minimum, control.maximum)) - control.minimum;
		return static_cast<int>(control.minimum + (offset * span + travel / 2) / travel);
	}

	bool ControlPanel::focusable(const Control& control)
	{
		return control.enabled && control.id != 0 && control.kind != ControlKind::heading &&
			control.kind != ControlKind::label && control.kind != ControlKind::separator;
	}

	void ControlPanel::notify_changed(const Control& control) const
	{
		// A callback may rebuild the entire panel, including the std::function being invoked.
		const auto changed = control.changed;
		const auto invalidate = invalidate_;
		if (invalidate) invalidate();
		if (changed) changed();
	}

	bool ControlPanel::focus(const int id)
	{
		const auto* control = find(id);
		if (id && (!interactive_ || !control || !focusable(*control))) return false;
		focusedId_ = id;
		if (invalidate_) invalidate_();
		return true;
	}

	bool ControlPanel::focus_next(const bool reverse)
	{
		if (!interactive_) return false;
		const auto current = std::ranges::find(controls_, focusedId_, &Control::id);
		auto index = current == controls_.end()
			? (reverse ? static_cast<std::ptrdiff_t>(controls_.size()) : -1)
			: std::distance(controls_.begin(), current);
		for (index += reverse ? -1 : 1; index >= 0 && index < static_cast<std::ptrdiff_t>(controls_.size());
			index += reverse ? -1 : 1)
		{
			if (focusable(controls_[static_cast<size_t>(index)]))
				return focus(controls_[static_cast<size_t>(index)].id);
		}
		focus(0);
		return false;
	}

	bool ControlPanel::key(const platform::KeyInput& input)
	{
		if (!interactive_ || input.control || input.alt) return false;
		auto* control = find(focusedId_);
		if (!control || !focusable(*control)) return false;
		if (control->kind == ControlKind::radio &&
			(input.key == platform::KeyCode::left || input.key == platform::KeyCode::right ||
				input.key == platform::KeyCode::up || input.key == platform::KeyCode::down))
		{
			if (!control->group) return true;
			const bool reverse = input.key == platform::KeyCode::left || input.key == platform::KeyCode::up;
			const auto origin = static_cast<size_t>(control - controls_.data());
			for (size_t step = 1; step < controls_.size(); ++step)
			{
				const auto index = reverse ? (origin + controls_.size() - step) % controls_.size()
					: (origin + step) % controls_.size();
				auto& next = controls_[index];
				if (!next.enabled || next.kind != ControlKind::radio || next.group != control->group) continue;
				focusedId_ = next.id;
				next.checked = true;
				apply_radio(next);
				notify_changed(next);
				break;
			}
			return true;
		}
		if (control->kind == ControlKind::slider || control->kind == ControlKind::choice)
		{
			const int minimum = control->kind == ControlKind::choice ? 0 : control->minimum;
			const int maximum = control->kind == ControlKind::choice
				? (std::max)(0, static_cast<int>(control->choices.size()) - 1)
				: (std::max)(minimum, control->maximum);
			const auto step = (std::max)(1LL, (static_cast<long long>(maximum) - minimum) / 10);
			long long value = control->value;
			switch (input.key)
			{
			case platform::KeyCode::left: --value; break;
			case platform::KeyCode::right: ++value; break;
			case platform::KeyCode::down: value += control->kind == ControlKind::choice ? 1 : -1; break;
			case platform::KeyCode::up: value += control->kind == ControlKind::choice ? -1 : 1; break;
			case platform::KeyCode::home: value = minimum; break;
			case platform::KeyCode::end: value = maximum; break;
			case platform::KeyCode::pageDown: value += control->kind == ControlKind::choice ? step : -step; break;
			case platform::KeyCode::pageUp: value += control->kind == ControlKind::choice ? -step : step; break;
			default: break;
			}
			const int next = static_cast<int>(std::clamp(value, static_cast<long long>(minimum),
				static_cast<long long>(maximum)));
			if (next != control->value)
			{
				control->value = next;
				notify_changed(*control);
				return true;
			}
			if (input.key == platform::KeyCode::left || input.key == platform::KeyCode::right ||
				input.key == platform::KeyCode::up || input.key == platform::KeyCode::down ||
				input.key == platform::KeyCode::home || input.key == platform::KeyCode::end ||
				input.key == platform::KeyCode::pageUp || input.key == platform::KeyCode::pageDown)
				return true;
		}
		if (input.key != platform::KeyCode::space && input.key != platform::KeyCode::enter) return false;
		if (control->kind == ControlKind::checkBox)
		{
			control->checked = !control->checked;
			notify_changed(*control);
		}
		else if (control->kind == ControlKind::radio)
		{
			if (control->checked) return true;
			control->checked = true;
			apply_radio(*control);
			notify_changed(*control);
		}
		else
		{
			const auto activate = activate_;
			if (activate) activate(*control);
		}
		return true;
	}

	void ControlPanel::set_interactive(const bool enabled)
	{
		interactive_ = enabled;
		if (!enabled)
		{
			dragging_ = false;
			pressedId_ = hotId_ = focusedId_ = 0;
		}
		if (invalidate_) invalidate_();
	}

	bool ControlPanel::mouse_move(const pointi point)
	{
		if (dragging_)
		{
			auto* control = find(pressedId_);
			if (interactive_ && control && control->enabled && control->kind == ControlKind::slider)
			{
				const int value = slider_value_at(*control, point.x);
				if (value != control->value)
				{
					control->value = value;
					notify_changed(*control);
				}
			}
			return true;
		}
		const auto* control = control_at(point);
		const int hot = control ? control->id : 0;
		if (hot == hotId_) return control != nullptr;
		hotId_ = hot;
		if (invalidate_) invalidate_();
		return control != nullptr;
	}

	bool ControlPanel::mouse_down(const pointi point)
	{
		auto* control = control_at(point);
		if (!control) return false;
		focusedId_ = control->id;
		pressedId_ = control->id;
		switch (control->kind)
		{
		case ControlKind::checkBox:
			control->checked = !control->checked;
			notify_changed(*control);
			return true;

		case ControlKind::radio:
			if (control->checked) return true;
			control->checked = true;
			apply_radio(*control);
			notify_changed(*control);
			return true;

		case ControlKind::slider:
			{
				dragging_ = true;
				const int value = slider_value_at(*control, point.x);
				if (value != control->value)
				{
					control->value = value;
					notify_changed(*control);
				}
				return true;
			}

		case ControlKind::choice:
		case ControlKind::folder:
		case ControlKind::text:
		case ControlKind::button:
		case ControlKind::link:
		{
			const auto activate = activate_;
			const auto invalidate = invalidate_;
			if (invalidate) invalidate();
			if (activate) activate(*control);
			return true;
		}

		default:
			return false;
		}
	}

	bool ControlPanel::mouse_up(const pointi)
	{
		const bool wasDragging = dragging_ || pressedId_ != 0;
		dragging_ = false;
		pressedId_ = 0;
		if (invalidate_) invalidate_();
		return wasDragging;
	}

	void ControlPanel::mouse_leave()
	{
		if (!hotId_) return;
		hotId_ = 0;
		if (invalidate_) invalidate_();
	}

	void ControlPanel::set_checked(const int id, const bool checked)
	{
		if (auto* control = find(id))
		{
			control->checked = checked;
			if (checked) apply_radio(*control);
		}
	}

	void ControlPanel::set_value(const int id, const int value)
	{
		if (auto* control = find(id))
			control->value = std::clamp(value, control->minimum, (std::max)(control->minimum, control->maximum));
	}

	void ControlPanel::set_enabled(const int id, const bool enabled)
	{
		if (auto* control = find(id)) control->enabled = enabled;
		if (!enabled)
		{
			if (focusedId_ == id) focusedId_ = 0;
			if (hotId_ == id) hotId_ = 0;
			if (pressedId_ == id)
			{
				pressedId_ = 0;
				dragging_ = false;
			}
		}
	}

	void ControlPanel::set_text(const int id, std::wstring text)
	{
		if (auto* control = find(id)) control->text = std::move(text);
	}
}

// ImageWalker by Zac Walker
// Adapts the application dialog model to libdesktop's neutral modal controls.

#include "Platform.h"

#include <chrono>
#include <exception>
#include <future>
#include <map>

namespace iw::platform
{
	namespace
	{
		pf::modal_field_kind modal_kind(const DialogFieldKind kind)
		{
			switch (kind)
			{
			case DialogFieldKind::checkBox: return pf::modal_field_kind::check_box;
			case DialogFieldKind::choice: return pf::modal_field_kind::choice;
			case DialogFieldKind::searchList: return pf::modal_field_kind::search_list;
			case DialogFieldKind::action: return pf::modal_field_kind::action;
			default: return pf::modal_field_kind::text;
			}
		}

		pf::modal_value modal_value(const DialogValue& value)
		{
			if (const auto* text = std::get_if<std::wstring>(&value))
				return pf::utf16_to_utf8(*text);
			if (const auto* checked = std::get_if<bool>(&value)) return *checked;
			if (const auto* number = std::get_if<std::int64_t>(&value)) return *number;
			return std::string{};
		}

		DialogValue application_value(const pf::modal_value& value)
		{
			if (const auto* text = std::get_if<std::string>(&value))
				return pf::utf8_to_utf16(*text);
			if (const auto* checked = std::get_if<bool>(&value)) return *checked;
			if (const auto* number = std::get_if<std::int64_t>(&value)) return *number;
			return {};
		}

		DialogValues application_values(const std::map<std::uint32_t, pf::modal_value>& values)
		{
			DialogValues result;
			for (const auto& [id, value] : values)
				result.emplace(id, application_value(value));
			return result;
		}
	}

	std::optional<DialogValues> show_modal_dialog(const WindowFramePtr& owner, DialogDefinition definition)
	{
		const auto frame = native_frame(owner);
		if (!frame) return std::nullopt;
		pf::modal_definition modal;
		modal.title = pf::utf16_to_utf8(definition.title);
		modal.message = pf::utf16_to_utf8(definition.message);
		modal.accept = pf::utf16_to_utf8(definition.acceptText);
		modal.cancel = pf::utf16_to_utf8(definition.cancelText);
		for (const auto& preview : definition.previews)
			modal.previews.push_back({preview.size.width, preview.size.height, preview.pixels});
		for (const auto& field : definition.fields)
		{
			pf::modal_field item;
			item.id = field.id;
			item.kind = modal_kind(field.kind);
			item.label = pf::utf16_to_utf8(field.label);
			item.value = modal_value(field.value);
			item.enabled = field.enabled;
			item.action = field.invoke;
			for (const auto& choice : field.choices)
				item.choices.emplace_back(pf::utf16_to_utf8(choice.label), choice.value);
			modal.fields.push_back(std::move(item));
		}
		modal.validate = [validate = std::move(definition.validate)](
			const std::map<std::uint32_t, pf::modal_value>& values) -> std::optional<std::string>
		{
			if (!validate) return std::nullopt;
			const auto message = validate(application_values(values));
			return message ? std::optional<std::string>(pf::utf16_to_utf8(*message)) : std::nullopt;
		};
		const auto values = pf::show_modal_dialog(frame, std::move(modal));
		return values ? std::optional<DialogValues>(application_values(*values)) : std::nullopt;
	}

	int show_choice(const WindowFramePtr& owner, const ChoiceDefinition& definition)
	{
		const auto frame = native_frame(owner);
		if (!frame) return 0;
		pf::choice_definition choice;
		choice.title = pf::utf16_to_utf8(definition.title);
		choice.heading = pf::utf16_to_utf8(definition.heading);
		choice.message = pf::utf16_to_utf8(definition.message);
		choice.default_id = definition.defaultButton;
		choice.warning = definition.warning;
		for (const auto& button : definition.buttons)
			choice.buttons.emplace_back(button.id, pf::utf16_to_utf8(button.label));
		return pf::show_choice(frame, choice);
	}

	bool run_with_status(const WindowFramePtr&, const std::wstring_view,
	                     const std::wstring_view, std::function<bool()> work)
	{
		auto completion = std::make_shared<std::promise<bool>>();
		auto result = completion->get_future();
		pf::run_async([work = std::move(work), completion]() mutable
		{
			try
			{
				completion->set_value(work ? work() : false);
			}
			catch (...)
			{
				completion->set_exception(std::current_exception());
			}
		});
		while (result.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready)
			pf::pump_ui_tasks(10);
		return result.get();
	}
}

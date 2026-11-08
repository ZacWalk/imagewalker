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
		::platform::modal_field_kind modal_kind(const DialogFieldKind kind)
		{
			switch (kind)
			{
			case DialogFieldKind::checkBox: return ::platform::modal_field_kind::check_box;
			case DialogFieldKind::choice: return ::platform::modal_field_kind::choice;
			case DialogFieldKind::searchList: return ::platform::modal_field_kind::search_list;
			case DialogFieldKind::action: return ::platform::modal_field_kind::action;
			default: return ::platform::modal_field_kind::text;
			}
		}

		::platform::modal_value modal_value(const DialogValue& value)
		{
			if (const auto* text = std::get_if<std::wstring>(&value)) return ::core::utf16_to_utf8(*text);
			if (const auto* checked = std::get_if<bool>(&value)) return *checked;
			if (const auto* number = std::get_if<std::int64_t>(&value)) return *number;
			return std::string{};
		}

		DialogValue application_value(const ::platform::modal_value& value)
		{
			if (const auto* text = std::get_if<std::string>(&value)) return ::core::utf8_to_utf16(*text);
			if (const auto* checked = std::get_if<bool>(&value)) return *checked;
			if (const auto* number = std::get_if<std::int64_t>(&value)) return *number;
			return {};
		}

		DialogValues application_values(const std::map<std::uint32_t, ::platform::modal_value>& values)
		{
			DialogValues result;
			for (const auto& [id, value] : values) result.emplace(id, application_value(value));
			return result;
		}
	}

	std::optional<DialogValues> show_modal_dialog(const WindowFramePtr&, DialogDefinition definition)
	{
		::platform::modal_definition modal;
		modal.title = ::core::utf16_to_utf8(definition.title);
		modal.message = ::core::utf16_to_utf8(definition.message);
		modal.accept = ::core::utf16_to_utf8(definition.acceptText);
		modal.cancel = ::core::utf16_to_utf8(definition.cancelText);
		for (const auto& preview : definition.previews)
			modal.previews.push_back({preview.size.width, preview.size.height, preview.pixels});
		for (const auto& field : definition.fields)
		{
			::platform::modal_field item;
			item.id = field.id;
			item.kind = modal_kind(field.kind);
			item.label = ::core::utf16_to_utf8(field.label);
			item.value = modal_value(field.value);
			item.enabled = field.enabled;
			item.action = field.invoke;
			for (const auto& choice : field.choices)
				item.choices.emplace_back(::core::utf16_to_utf8(choice.label), choice.value);
			// The platform modal field does not support folder completion, text selection, or edit_values.
			modal.fields.push_back(std::move(item));
		}
		modal.validate =
			[validate = std::move(definition.validate)](
				const std::map<std::uint32_t, ::platform::modal_value>& values) -> std::optional<std::string>
		{
			if (!validate) return {};
			const auto message = validate(application_values(values));
			return message ? std::optional<std::string>(::core::utf16_to_utf8(*message)) : std::nullopt;
		};
		const auto values = ::platform::show_modal_dialog(nullptr, std::move(modal));
		return values ? std::optional<DialogValues>(application_values(*values)) : std::nullopt;
	}

	int show_choice(const WindowFramePtr&, const ChoiceDefinition& definition)
	{
		::platform::choice_definition choice;
		choice.title = ::core::utf16_to_utf8(definition.title);
		choice.heading = ::core::utf16_to_utf8(definition.heading);
		choice.message = ::core::utf16_to_utf8(definition.message);
		choice.default_id = definition.defaultButton;
		choice.warning = definition.warning;
		for (const auto& button : definition.buttons)
			choice.buttons.emplace_back(button.id, ::core::utf16_to_utf8(button.label));
		return ::platform::show_choice(nullptr, choice);
	}

	bool run_with_status(const WindowFramePtr&, std::wstring_view, std::wstring_view, std::function<bool()> work)
	{
		// The platform dialog does not support an owned status dialog; this pumps the task queue instead.
		auto completion = std::make_shared<std::promise<bool>>();
		auto result = completion->get_future();
		::platform::run_async([work = std::move(work), completion]() mutable
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
			::platform::pump_ui_tasks(10);
		return result.get();
	}
}

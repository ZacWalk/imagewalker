#pragma once

#include "Platform.h"

namespace iw::platform
{
	enum class Appearance
	{
		light,
		dark,
		highContrast
	};

	constexpr Appearance select_appearance(const bool appsUseLightTheme, const bool highContrast)
	{
		return highContrast ? Appearance::highContrast : appsUseLightTheme ? Appearance::light : Appearance::dark;
	}

	color appearance_color(Appearance appearance, SystemColor role);
	::ui::style::theme appearance_palette(Appearance appearance);
	render::color_style frame_colors();
}

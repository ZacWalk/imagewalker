#pragma once

// Preserve historical min/max macros around inline modern code.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max

// The historical WTL applications still use the Windows min/max macros.
// libdesktop's geometry contracts use std::min/std::max internally, so keep
// those macros out of the system header without changing the legacy callers'
// view after this header is included.
#pragma push_macro("max")
#pragma push_macro("min")
#undef max
#undef min
#include <libdesktop/system.h>
#pragma pop_macro("min")
#pragma pop_macro("max")

#pragma pop_macro("max")
#pragma pop_macro("min")

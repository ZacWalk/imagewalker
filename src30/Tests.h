// ImageWalker by Zac Walker
// Declares the ImageWalker 3.0 /test command-line entry point.

#pragma once

namespace iw::tests
{
	// Runs every self-contained test and returns the number of failures (0 == all passed).
	// Suitable as a process exit code for `imagewalker30 /test`.
	int run_all();
}

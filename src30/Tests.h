// ImageWalker by Zac Walker
// Declares the ImageWalker 3.0 /test command-line entry point.

#pragma once

namespace iw::tests
{
	// Runs every self-contained test and returns the number of failures (0 == all passed).
	// `skipClipboard` is an explicit manual-validation mode; the default keeps the complete
	// suite, including its clipboard assertions.
	int run_all(bool skipClipboard = false);
}

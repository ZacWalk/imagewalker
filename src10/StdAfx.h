// stdafx.h : the headers that everything in ImageWalker 1.0 uses.

#pragma once

// STL first. Under /permissive, pulling these in after the app headers breaks
// std::plus<void> inside xutility.
#include <string>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <algorithm>
#include <memory>

// <mutex> belongs in this block for the same reason as the rest. iw/workqueue.h
// pulls it in, and reaching it only after the app headers makes the debug
// build's front end fall over with an internal compiler error inside
// xcall_once.h. 2.21 hit exactly this.
#include <mutex>

#include <stdio.h>
#include <tchar.h>

#include "SdkUi.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <objbase.h>

#ifndef ASSERT
#define ASSERT(expr) assert(expr)
#endif
#ifndef VERIFY
#define VERIFY(expr) ((void)(expr))
#endif
#ifndef ASSERT_VALID
#define ASSERT_VALID(p) assert((p) != nullptr)
#endif

// MFC's leak-tracking allocator; the CRT debug heap covers this now.
#define DEBUG_NEW new

// The shell headers no longer define this flag.
#ifndef SHGDN_INCLUDE_NONFILESYS
#define SHGDN_INCLUDE_NONFILESYS 0x2000
#endif

using CString = CText;

#include "Error.h"
#include "Settings.h"

#include "shell.h"
#include "Dib.h"
#include "Status.h"

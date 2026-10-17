// ImageWalker by Zac Walker
//
// Purpose: Precompiled header. STL first, then resources, then ATL/WTL, then
//          the app headers - the order matters under /permissive.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#define WINVER 0x0502
#define _WIN32_WINNT 0x0502
#define _WIN32_WINDOWS 0x0502
#define _WIN32_IE 0x0700 

#define _ATL_APARTMENT_THREADED
#define _ATL_ALL_WARNINGS
#define _WTL_NO_CSTRING
#define _WTL_CMDBAR_VISTA_MENUS 0
#define _WTL_USE_VSSYM32

#include <AtlStr.h>
#include <AtlBase.h>
#include <atlApp.h>

extern CAppModule _Module;

#include <string>
#include <map>
#include <vector>
#include <set>
#include <memory>
#include <algorithm>
#include <functional>
#include <list>
#include <iostream>
#include <queue>
#include <iterator>

// <mutex> belongs in this block for the same reason as the rest: reaching it
// only after the app headers makes the debug front end fall over with an
// internal compiler error inside xcall_once.h.
#include <mutex>

#include "iw/buffer.h"

#include "UtilMemory.h"

#include <WinInet.h>
#include <ShlObj.h>

//#include <AtlTypes.h>
#include <AtlComTime.h>
#include <AtlCom.h>
#include <Atlctl.h>
#include <AtlCtrls.h>
#include <atlctrlw.h>
#include <AtlCtrlx.h>
#include <AtlDlgs.h>
#include <atlframe.h>
#include <AtlGdi.h>
#include <AtlMisc.h>
#include <atlPrint.h>
#include <AtlScrl.h>
#include <AtlSync.h>
#include <AtlTheme.h>
#include <AtlMem.h>

#define _USE_MATH_DEFINES  1
#include <Math.h> 
#include <time.h>
#include <urlmon.h>
#include <process.h>

#include "resource.h"
#include "Ver.h"
#include "UtilStrings.h"
#include "UtilCore.h"
// ColorButton lives in include/ and takes these from whichever app includes it.
#define COLORBUTTON_HIGHLIGHT     IW::Style::Color::Highlight
#define COLORBUTTON_HIGHLIGHTTEXT IW::Style::Color::HighlightText

#include "UtilShell.h"
#include "FileStreams.h"
#include "UtilSerialize.h"
#include "Imaging.h"
#include "ViewRender.h"
#include "UtilThreads.h"
#include "ViewModelItems.h"
#include "Settings.h"
#include "App.h"
#include "AppHelp.h"
#include "AppCoupling.h"
#include "ViewIcons.h"
#include "ViewSplitter.h"

// ImageWalker by Zac Walker
//
// Purpose: The command table - one row per command id giving its icon, menu
//          behaviour and toolbar placement.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#include "StdAfx.h"
#include "AppCommands.h"

CRect ImageWalkerCommandBarCtrl::m_rcButton;

CommandInfo s_commands[] = {
	{ImageIndex::About, ID_APP_ABOUT, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{ImageIndex::None, ID_APP_EXIT, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{
		ImageIndex::Back, ID_BROWSE_BACK, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR,
		BTNS_DROPDOWN, -1
	},
	{
		ImageIndex::Next, ID_BROWSE_FORWARD, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR,
		BTNS_DROPDOWN, -1
	},
	{
		ImageIndex::NewFolder, ID_BROWSE_NEWFOLDER, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR,
		0, -1
	},
	{ImageIndex::Parent, ID_BROWSE_PARENT, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1},
	{ImageIndex::Rename, ID_BROWSE_RENAME, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1},
	{ImageIndex::CopyToNewLocation, ID_COPYTO_NEWLOCATION, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{ImageIndex::Copy, ID_EDIT_COPY, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1},
	{ImageIndex::Cut, ID_EDIT_CUT, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1},
	{ImageIndex::Delete, ID_EDIT_DELETE, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1},
	{ImageIndex::None, ID_EDIT_INVERTSELECTION, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{ImageIndex::Copy, ID_EDIT_COPY_PATHS, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{ImageIndex::Paste, ID_EDIT_PASTE, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1},
	{ImageIndex::Save, ID_EDIT_SAVE, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, ID_EDIT_SAVE},
	{
		ImageIndex::None, ID_EDIT_SAVE_MOVENEXT, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0,
		-1
	},
	{
		ImageIndex::SaveAs, ID_EDIT_SAVEAS, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0,
		ID_EDIT_SAVEAS
	},
	{ImageIndex::None, ID_EDIT_SELECT_ALL, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{ImageIndex::None, ID_EDIT_SELECTALLIMAGES, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{ImageIndex::Undo, ID_EDIT_UNDO_IMAGE, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1},
	{
		ImageIndex::CopyTo, ID_FILE_COPYTO, CUpdateUIBase::UPDUI_MENUBAR | CUpdateUIBase::UPDUI_TOOLBAR,
		BTNS_WHOLEDROPDOWN, -1
	},
	{
		ImageIndex::GoTo, ID_FILE_GOTO, CUpdateUIBase::UPDUI_MENUBAR | CUpdateUIBase::UPDUI_TOOLBAR,
		BTNS_WHOLEDROPDOWN, -1
	},
	{
		ImageIndex::MoveTo, ID_FILE_MOVETO, CUpdateUIBase::UPDUI_MENUBAR | CUpdateUIBase::UPDUI_TOOLBAR,
		BTNS_WHOLEDROPDOWN, -1
	},
	{ImageIndex::CopyTo, ID_FILE_COPYTO_POPUP, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{ImageIndex::MoveTo, ID_FILE_MOVETO_POPUP, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},	{ImageIndex::Open, ID_FILE_OPEN, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{ImageIndex::None, ID_FILE_OPEN_WITH, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{ImageIndex::OpenContaining, ID_FILE_OPENCONTAINING, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{ImageIndex::OpenContaining, ID_FILE_EXPLORE, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{
		ImageIndex::PageSetup, ID_FILE_PAGE_SETUP, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR,
		0, -1
	},
	{
		ImageIndex::Print, ID_FILE_PRINT, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0,
		IDS_PRINT_NOW
	},
	{
		ImageIndex::PrintPreview, ID_VIEW_PRINT, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR,
		BTNS_CHECK, IDS_PRINT
	},
	{
		ImageIndex::Properties, ID_FILE_PROPERTIES, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR,
		0, -1
	},
	{ImageIndex::None, ID_FILTER, CUpdateUIBase::UPDUI_MENUBAR, BTNS_WHOLEDROPDOWN, -1},
	{ImageIndex::None, ID_GOTO_ADDCURRENT, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{ImageIndex::GotoNewLocation, ID_GOTO_NEWLOCATION, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{ImageIndex::Help, ID_HELP_FINDER, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1},
	{ImageIndex::Home, ID_HELP_IMAGEWALKERHOMEPAGE, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{ImageIndex::CopyImage, ID_IMAGE_COPY, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1},
	{
		ImageIndex::EditDescription, ID_IMAGE_EDITDESCRIPTION,
		CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1
	},
	{ImageIndex::None, ID_MOVECOPY_CLEARLOCATIONS, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{ImageIndex::MoveToNewlocation, ID_MOVETO_NEWLOCATION, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{
		ImageIndex::SaveContactSheet, ID_FILE_PRINTING_SAVECONTACTSHEET,
		CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, ID_FILE_PRINTING_SAVECONTACTSHEET
	},
	{ImageIndex::Up, ID_PP_BACK, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1},
	{ImageIndex::Down, ID_PP_FORWARD, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1},
	{ImageIndex::Fit, ID_SCALE_FIT, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1},
	{ImageIndex::None, ID_SCALE_FILL, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1},
	{ImageIndex::ActualSize, ID_SCALE_100, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1},
	{ImageIndex::None, ID_SCALE_200, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{ImageIndex::None, ID_SCALE_50, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{ImageIndex::None, ID_SCALE_UP, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{ImageIndex::None, ID_SCALE_DOWN, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{ImageIndex::None, ID_SCALE_TOGGLE, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{
		ImageIndex::ImageNext, ID_IMAGE_NEXT, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1
	},
	{
		ImageIndex::ImageBack, ID_IMAGE_PREVIOUS,
		CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1
	},
	{ImageIndex::Options, ID_TOOLS_OPTIONS, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1},
	{ImageIndex::Refresh, ID_VIEW_REFRESHX, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1},
	{
		ImageIndex::Normal, ID_VIEW_NORMAL, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0,
		ID_VIEW_NORMAL
	},
	{
		ImageIndex::FilterColor, ID_VIEW_EDIT, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR,
		BTNS_CHECK, ID_VIEW_EDIT
	},
	{
		ImageIndex::Undo, ID_EDIT_CANCEL_EDITS, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0,
		ID_EDIT_CANCEL_EDITS
	},
	{
		ImageIndex::FilterCrop, ID_EDIT_CROP, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR,
		0, ID_EDIT_CROP
	},
	{
		ImageIndex::FilterMultiple, ID_EDIT_COMPARE, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR,
		BTNS_CHECK, ID_EDIT_COMPARE
	},

	{
		ImageIndex::ArrangeIconsName, ID_ARRANGEICONS_NAME,
		CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1
	},
	{
		ImageIndex::ArrangeIconsSize, ID_ARRANGEICONS_SIZE,
		CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1
	},
	{
		ImageIndex::ArrangeIconsType, ID_ARRANGEICONS_TYPE,
		CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1
	},
	{
		ImageIndex::ArrangeIconsModified, ID_ARRANGEICONS_MODIFIED,
		CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1
	},
	{
		ImageIndex::ArrangeIconsModified, ID_ARRANGEICONS_DATETAKEN,
		CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1
	},
	{
		ImageIndex::None, ID_ARRANGEICONS_MORE, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0,
		-1
	},

	{
		ImageIndex::ShowFullScreen, ID_VIEW_IMAGEFULLSCREEN, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1
	},
	{
		ImageIndex::ArrangeIconsName, ID_VIEW_ARRANGEICONS,
		CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, BTNS_WHOLEDROPDOWN, -1
	},
	{
		ImageIndex::ThumbnailView, ID_THUMBNAILS, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR,
		BTNS_WHOLEDROPDOWN, -1
	},

	{ImageIndex::ThumbnailNormal, ID_THUMBNAILS_THUMBNAIL, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{ImageIndex::ThumbnailDetail,ID_THUMBNAILS_DETAIL, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},
	{ImageIndex::ThumbnailMatrix, ID_THUMBNAILS_MATRIX, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},

	{
		ImageIndex::Folders, ID_VIEW_FOLDERS, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR,
		BTNS_CHECK, IDS_FOLDERS
	},
	{
		ImageIndex::Description, ID_VIEW_DESCRIPTION, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1
	},
	{
		ImageIndex::None, ID_VIEW_ADVANCEDIMAGE, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0,
		-1
	},

	{
		ImageIndex::None, ID_OPTIONS_SHOWDESCRIPTIONSINDETAIL,
		CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1
	},
	{
		ImageIndex::None, ID_OPTIONS_SHOWHIDDENFILES, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1
	},
	{
		ImageIndex::None, ID_OPTIONS_SHOWMETADATAMARKERS, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1
	},
	{
		ImageIndex::None, ID_OPTIONS_SHOWTHUMBNAILTOOLTIPS,
		CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1
	},
	{
		ImageIndex::None, ID_OPTIONS_USESHORTDATES, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1
	},
	{
		ImageIndex::None, ID_OPTIONS_ZOOMTHUMBNAILSINMATRIXVIEW,
		CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1
	},

	{
		ImageIndex::SearchAdvanced, ID_VIEW_SEARCHADVANCED,
		CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, BTNS_CHECK, IDS_SEARCH
	},

	// Tools
	{
		ImageIndex::ToolConvert, ID_TOOLS_CONVERTIMAGES, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1
	},
	{
		ImageIndex::ToolJpeg, ID_TOOLS_LOSSLESS, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0,
		-1
	},
	{ImageIndex::None, ID_TOOLS_RESIZE, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},

	// Web
	{ImageIndex::OK, ID_OK, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, ID_OK},

	// Search
	{
		ImageIndex::SearchHere, ID_SEARCH_CURRENT, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR,
		0, -1
	},
	{
		ImageIndex::SearchMyPictures, ID_SEARCH_MYPICTURES,
		CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, -1
	},
	{
		ImageIndex::SearchStop, ID_SEARCH_STOP, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0,
		-1
	},
	{ImageIndex::None, ID_SEARCH_IMAGESINSUBFOLDERS, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},

	{ImageIndex::None, ID_TAG_SELECT, CUpdateUIBase::UPDUI_MENUPOPUP, 0, -1},

	{
		ImageIndex::RotateFilesLeft, ID_EDIT_ROTATELEFT, CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, ID_EDIT_ROTATELEFT
	},
	{
		ImageIndex::RotateFilesRight, ID_EDIT_ROTATERIGHT,
		CUpdateUIBase::UPDUI_MENUPOPUP | CUpdateUIBase::UPDUI_TOOLBAR, 0, ID_EDIT_ROTATERIGHT
	},

	{ImageIndex::None, -1, 0, 0, 0}
};


// Every toolbar opens with the same four mode buttons, so the way into and out
// of a mode is in the same place whichever mode is up. They are toggles: the
// button that is already down goes back to the plain items view.
#define IW_MODE_BUTTONS \
	ID_VIEW_FOLDERS, \
	ID_VIEW_SEARCHADVANCED, \
	ID_VIEW_EDIT, \
	ID_VIEW_PRINT, \
	0

// The thumbnail layout and the sort order live on the strip in the status bar
// (CMainFrame::CreateFolderBar), so they are deliberately not repeated here.
int s_toolbarMain[] = {
	IW_MODE_BUTTONS,
	ID_BROWSE_BACK,
	ID_BROWSE_FORWARD,
	ID_BROWSE_PARENT,
	0,
	ID_VIEW_DESCRIPTION,
	0,
	ID_EDIT_UNDO_IMAGE,
	ID_EDIT_SAVE,
	0,
	ID_IMAGE_PREVIOUS,
	ID_VIEW_IMAGEFULLSCREEN,
	ID_IMAGE_NEXT,
	0,
	ID_EDIT_DELETE,
	ID_VIEW_REFRESHX,
	0,
	ID_FILE_GOTO,
	ID_FILE_COPYTO,
	ID_FILE_MOVETO,
	0,
	ID_HELP_FINDER,
	-1
};

// The two focused modes replace the rest of the main toolbar rather than adding
// to it. Edit mode reads left to right the way the work does: reframe, compare,
// commit.
int s_toolbarEdit[] = {
	IW_MODE_BUTTONS,
	ID_EDIT_ROTATELEFT,
	ID_EDIT_ROTATERIGHT,
	0,
	ID_EDIT_COMPARE,
	0,
	ID_EDIT_SAVE,
	ID_EDIT_SAVEAS,
	ID_EDIT_CANCEL_EDITS,
	0,
	ID_HELP_FINDER,
	-1
};

int s_toolbarPrint[] = {
	IW_MODE_BUTTONS,
	ID_FILE_PRINT,
	ID_FILE_PAGE_SETUP,
	0,
	ID_PP_BACK,
	ID_PP_FORWARD,
	0,
	ID_FILE_PRINTING_SAVECONTACTSHEET,
	0,
	ID_HELP_FINDER,
	-1
};

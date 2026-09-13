//{{NO_DEPENDENCIES}}
// Used by ArtMate.rc

#define IDD_ABOUTBOX                    100
#define IDR_MAINFRAME                   128
#define IDR_ICONMATE8                   131
#define IDB_BAR_HOT                     134
#define IDB_BAR_COLD                    135
#define IDR_BACKGROUND                  147
#define IDB_VIEWS                       150
#define IDB_PREVIEW_MODE                151

#define IDS_APP_TITLE                   1
#define IDS_ADDRESS                     131
#define IDS_STATUS_IMAGE                160

#define IDC_ABOUT_DETAILS               1900
#define IDC_THEME_SONG                  1901

// The address combo's control id. Deliberately not ID_VIEW_ADDRESSBAR: sharing
// the menu item's id sends every combo notification to the menu handler.
#define IDC_ADDRESS_COMBO               1902

// Browsing
#define ID_BROWSE_PARENT                32772
#define ID_BROWSE_BACK                  32775
#define ID_BROWSE_FORWARD               32783
#define ID_VIEW_REFRESH                 32785
#define ID_VIEW_ADDRESSBAR              32789
#define ID_EDIT_INVERTSELECTION         32790

// Items pane
#define ID_VIEW_SORT_NAME               32782
#define ID_VIEW_SORT_TYPE               32786
#define ID_VIEW_SORT_SIZE               32787
#define ID_VIEW_SORT_DATE               32788
#define ID_ITEMS_THUMBNAILS             32800
#define ID_ITEMS_DETAILS                32801
#define ID_ITEMS_OPTIONS                32802

// Image pane. ID_MODE_25 through ID_MODE_200 are a contiguous range; the
// handler is a COMMAND_RANGE_HANDLER over them.
#define ID_MODE_FITTOWINDOW             32810
#define ID_MODE_ACTUALSIZE              32811
#define ID_MODE_ZOOM                    32812
#define ID_NAVIGATE                     32813
#define ID_MODE_SCALEDOWNTOFIT          32814
#define ID_MODE_SCALEUPTOFIT            32815
#define ID_MODE_25                       32820
#define ID_MODE_50                       32821
#define ID_MODE_75                       32822
#define ID_MODE_100                      32823
#define ID_MODE_150                      32824
#define ID_MODE_200                      32825
#define ID_VIEW_PREVIOUSIMAGE           32830
#define ID_VIEW_NEXTIMAGE               32831

// Next default values for new objects
#ifdef APSTUDIO_INVOKED
#ifndef APSTUDIO_READONLY_SYMBOLS
#define _APS_NEXT_RESOURCE_VALUE        161
#define _APS_NEXT_COMMAND_VALUE         32840
#define _APS_NEXT_CONTROL_VALUE         1011
#define _APS_NEXT_SYMED_VALUE           101
#endif
#endif

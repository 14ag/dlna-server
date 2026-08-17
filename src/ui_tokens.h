#ifndef UI_TOKENS_H
#define UI_TOKENS_H

// single source of truth for every ui dimension color and font size
// used by both the win32 front end and the gtk4 front end
// tools gen_gtk_style py parses this file to emit resources gtk style css

struct RgbColor {
    int r;
    int g;
    int b;
};

namespace UiTokens {

// main window
constexpr int kWindowWidth = 440;
constexpr int kWindowHeight = 600;
constexpr int kToolbarHeight = 56;
constexpr int kStatusHeight = 40;
constexpr int kListTopGap = 8;
constexpr int kButtonHeight = 32;
constexpr int kButtonGap = 8;
constexpr int kGutter = 16;
constexpr int kAddButtonWidth = 56;
constexpr int kDeleteButtonWidth = 72;
constexpr int kStartStopButtonWidth = 72;
constexpr int kSettingsButtonWidth = 82;
constexpr int kCornerRadius = 8;
constexpr int kWin10TitlebarHeight = 32;
constexpr int kFocusRingThickness = 1;
constexpr int kFocusRingGap = 2;

constexpr RgbColor kPageColor = { 31, 31, 31 };
constexpr RgbColor kToolbarColor = { 37, 37, 37 };
constexpr RgbColor kControlColor = { 51, 51, 51 };
constexpr RgbColor kControlHoverColor = { 62, 62, 62 };
constexpr RgbColor kControlPressedColor = { 74, 74, 74 };
constexpr RgbColor kBorderColor = { 88, 88, 88 };
constexpr RgbColor kFocusColor = { 96, 165, 250 };
constexpr RgbColor kTextColor = { 255, 255, 255 };
constexpr RgbColor kDisabledTextColor = { 132, 132, 132 };
constexpr RgbColor kSecondaryTextColor = { 200, 200, 200 };

constexpr RgbColor kWin10InactiveTitlebarColor = { 50, 50, 50 };
constexpr RgbColor kWin10ActiveTitlebarColor = { 49, 49, 49 };

constexpr int kTitleFontSizePx = 20;
constexpr int kBodyFontSizePx = 14;
constexpr const char* kTitleFontFamilyStack =
    "Segoe UI Variable Display, Segoe UI, Cantarell, Noto Sans, sans-serif";
constexpr const char* kBodyFontFamilyStack =
    "Segoe UI Variable Text, Segoe UI, Cantarell, Noto Sans, sans-serif";

// settings dialog dimensions filled in from the settings geometry dump
// produced by LogSettingsControlGeometryProc in settingsdlg cpp
// values below come from the real debug log capture task 0 step 3 4
constexpr int kSettingsWindowWidth = 700;
constexpr int kSettingsWindowHeight = 745;
constexpr int kSettingsServerGroupX = 18;
constexpr int kSettingsServerGroupY = 21;
constexpr int kSettingsServerGroupW = 665;
constexpr int kSettingsServerGroupH = 223;
constexpr int kSettingsServerNameLabelX = 39;
constexpr int kSettingsServerNameLabelY = 64;
constexpr int kSettingsServerNameLabelW = 140;
constexpr int kSettingsServerNameLabelH = 21;
constexpr int kSettingsServerNameEditX = 193;
constexpr int kSettingsServerNameEditY = 55;
constexpr int kSettingsServerNameEditW = 333;
constexpr int kSettingsServerNameEditH = 40;
constexpr int kSettingsHttpPortLabelX = 39;
constexpr int kSettingsHttpPortLabelY = 123;
constexpr int kSettingsHttpPortLabelW = 140;
constexpr int kSettingsHttpPortLabelH = 21;
constexpr int kSettingsHttpPortEditX = 193;
constexpr int kSettingsHttpPortEditY = 115;
constexpr int kSettingsHttpPortEditW = 333;
constexpr int kSettingsHttpPortEditH = 40;
constexpr int kSettingsIpWhitelistLabelX = 39;
constexpr int kSettingsIpWhitelistLabelY = 183;
constexpr int kSettingsIpWhitelistLabelW = 140;
constexpr int kSettingsIpWhitelistLabelH = 21;
constexpr int kSettingsIpWhitelistEditX = 193;
constexpr int kSettingsIpWhitelistEditY = 174;
constexpr int kSettingsIpWhitelistEditW = 455;
constexpr int kSettingsIpWhitelistEditH = 40;
constexpr int kSettingsGeneralGroupX = 18;
constexpr int kSettingsGeneralGroupY = 270;
constexpr int kSettingsGeneralGroupW = 324;
constexpr int kSettingsGeneralGroupH = 140;
constexpr int kSettingsRunOnStartupX = 39;
constexpr int kSettingsRunOnStartupY = 317;
constexpr int kSettingsRunOnStartupW = 277;
constexpr int kSettingsRunOnStartupH = 26;
constexpr int kSettingsDebugLogX = 39;
constexpr int kSettingsDebugLogY = 359;
constexpr int kSettingsDebugLogW = 277;
constexpr int kSettingsDebugLogH = 26;
constexpr int kSettingsPlaylistGroupX = 359;
constexpr int kSettingsPlaylistGroupY = 270;
constexpr int kSettingsPlaylistGroupW = 324;
constexpr int kSettingsPlaylistGroupH = 140;
constexpr int kSettingsDefaultPlaylistX = 380;
constexpr int kSettingsDefaultPlaylistY = 317;
constexpr int kSettingsDefaultPlaylistW = 193;
constexpr int kSettingsDefaultPlaylistH = 26;
constexpr int kSettingsPlaylistAddX = 553;
constexpr int kSettingsPlaylistAddY = 353;
constexpr int kSettingsPlaylistAddW = 102;
constexpr int kSettingsPlaylistAddH = 40;
constexpr int kSettingsMediaGroupX = 18;
constexpr int kSettingsMediaGroupY = 436;
constexpr int kSettingsMediaGroupW = 665;
constexpr int kSettingsMediaGroupH = 230;
constexpr int kSettingsArtistAlbumsX = 39;
constexpr int kSettingsArtistAlbumsY = 482;
constexpr int kSettingsArtistAlbumsW = 298;
constexpr int kSettingsArtistAlbumsH = 26;
constexpr int kSettingsHideAllMediaX = 39;
constexpr int kSettingsHideAllMediaY = 525;
constexpr int kSettingsHideAllMediaW = 298;
constexpr int kSettingsHideAllMediaH = 26;
constexpr int kSettingsSortByTitleX = 39;
constexpr int kSettingsSortByTitleY = 567;
constexpr int kSettingsSortByTitleW = 312;
constexpr int kSettingsSortByTitleH = 26;
constexpr int kSettingsFlatFoldersX = 368;
constexpr int kSettingsFlatFoldersY = 482;
constexpr int kSettingsFlatFoldersW = 228;
constexpr int kSettingsFlatFoldersH = 26;
constexpr int kSettingsShowFileNamesX = 368;
constexpr int kSettingsShowFileNamesY = 525;
constexpr int kSettingsShowFileNamesW = 287;
constexpr int kSettingsShowFileNamesH = 26;
constexpr int kSettingsProxyStreamsX = 368;
constexpr int kSettingsProxyStreamsY = 567;
constexpr int kSettingsProxyStreamsW = 228;
constexpr int kSettingsProxyStreamsH = 26;
constexpr int kSettingsBackgroundScanX = 39;
constexpr int kSettingsBackgroundScanY = 614;
constexpr int kSettingsBackgroundScanW = 403;
constexpr int kSettingsBackgroundScanH = 26;
constexpr int kSettingsCancelX = 462;
constexpr int kSettingsCancelY = 691;
constexpr int kSettingsCancelW = 102;
constexpr int kSettingsCancelH = 40;
constexpr int kSettingsOkX = 578;
constexpr int kSettingsOkY = 691;
constexpr int kSettingsOkW = 105;
constexpr int kSettingsOkH = 40;

// log dialog dimensions from the log geometry dump capture
constexpr int kLogWindowWidth = 770;
constexpr int kLogWindowHeight = 680;
constexpr int kLogTextX = 18;
constexpr int kLogTextY = 21;
constexpr int kLogTextW = 735;
constexpr int kLogTextH = 591;
constexpr int kLogRefreshX = 525;
constexpr int kLogRefreshY = 629;
constexpr int kLogRefreshW = 109;
constexpr int kLogRefreshH = 40;
constexpr int kLogCloseX = 648;
constexpr int kLogCloseY = 629;
constexpr int kLogCloseW = 105;
constexpr int kLogCloseH = 40;

// help dialog dimensions from the help geometry dump capture
constexpr int kHelpWindowWidth = 544;
constexpr int kHelpWindowHeight = 400;
constexpr int kHelpTextX = 10;
constexpr int kHelpTextY = 10;
constexpr int kHelpTextW = 530;
constexpr int kHelpTextH = 390;

// playlist entry dialog dimensions from the playlist entry geometry dump
constexpr int kPlaylistWindowWidth = 536;
constexpr int kPlaylistWindowHeight = 157;
constexpr int kPlaylistMovieLabelX = 16;
constexpr int kPlaylistMovieLabelY = 24;
constexpr int kPlaylistMovieLabelW = 84;
constexpr int kPlaylistMovieLabelH = 18;
constexpr int kPlaylistMovieEditX = 112;
constexpr int kPlaylistMovieEditY = 16;
constexpr int kPlaylistMovieEditW = 324;
constexpr int kPlaylistMovieEditH = 32;
constexpr int kPlaylistMovieBrowseX = 444;
constexpr int kPlaylistMovieBrowseY = 16;
constexpr int kPlaylistMovieBrowseW = 92;
constexpr int kPlaylistMovieBrowseH = 32;
constexpr int kPlaylistSubtitleLabelX = 16;
constexpr int kPlaylistSubtitleLabelY = 68;
constexpr int kPlaylistSubtitleLabelW = 87;
constexpr int kPlaylistSubtitleLabelH = 18;
constexpr int kPlaylistSubtitleEditX = 112;
constexpr int kPlaylistSubtitleEditY = 60;
constexpr int kPlaylistSubtitleEditW = 324;
constexpr int kPlaylistSubtitleEditH = 32;
constexpr int kPlaylistSubtitleBrowseX = 444;
constexpr int kPlaylistSubtitleBrowseY = 60;
constexpr int kPlaylistSubtitleBrowseW = 92;
constexpr int kPlaylistSubtitleBrowseH = 32;
constexpr int kPlaylistAddX = 444;
constexpr int kPlaylistAddY = 108;
constexpr int kPlaylistAddW = 92;
constexpr int kPlaylistAddH = 32;

// source prompt dialog dimensions from the source prompt geometry dump
constexpr int kSourcePromptWindowWidth = 536;
constexpr int kSourcePromptWindowHeight = 177;
constexpr int kSourcePromptLabelX = 16;
constexpr int kSourcePromptLabelY = 16;
constexpr int kSourcePromptLabelW = 520;
constexpr int kSourcePromptLabelH = 20;
constexpr int kSourcePromptEditX = 16;
constexpr int kSourcePromptEditY = 48;
constexpr int kSourcePromptEditW = 520;
constexpr int kSourcePromptEditH = 32;
constexpr int kSourcePromptHintX = 16;
constexpr int kSourcePromptHintY = 88;
constexpr int kSourcePromptHintW = 520;
constexpr int kSourcePromptHintH = 20;
constexpr int kSourcePromptFolderX = 16;
constexpr int kSourcePromptFolderY = 128;
constexpr int kSourcePromptFolderW = 96;
constexpr int kSourcePromptFolderH = 32;
constexpr int kSourcePromptFileX = 120;
constexpr int kSourcePromptFileY = 128;
constexpr int kSourcePromptFileW = 200;
constexpr int kSourcePromptFileH = 32;
constexpr int kSourcePromptAddX = 372;
constexpr int kSourcePromptAddY = 128;
constexpr int kSourcePromptAddW = 78;
constexpr int kSourcePromptAddH = 32;
constexpr int kSourcePromptCancelX = 458;
constexpr int kSourcePromptCancelY = 128;
constexpr int kSourcePromptCancelW = 78;
constexpr int kSourcePromptCancelH = 32;

}  // namespace UiTokens

#endif  // UI_TOKENS_H

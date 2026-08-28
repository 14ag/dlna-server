#ifndef UI_TOKENS_POSIX_H
#define UI_TOKENS_POSIX_H

// POSIX/GTK4-only geometry that intentionally diverges from the values in
// src/ui_tokens.h. src/ui_tokens.h is shared with the Win32 build
// (src/mainwindow.cpp) and documents itself as such; it must not be
// edited to chase the dlna-server-14ag Figma file, or the Win32 GUI's
// pixel geometry changes as a side effect. See Section 1, Conflict B of
// dlna-server-posix-gui-figma-alignment-workflow-20-08-26.md.
//
// Included only by src/gtk4_gui_main.cpp.

namespace UiTokensPosix {

constexpr int kTitlebarHeight = 30;
constexpr int kTitlebarLeftPadding = 10;

constexpr int kMainWindowWidth = 426;
constexpr int kMainWindowHeight = 593;
constexpr int kMainToolbarHeight = 57;
constexpr int kMainSourceListX = 21;
constexpr int kMainSourceListYFromListArea = 52;
constexpr int kMainSourceListWidth = 385;
constexpr int kMainSourceListHeight = 430;

// Toolbar buttons, left to right, y is relative to the toolbar's own
// origin (not the window origin).
constexpr int kAddButtonX = 103, kAddButtonY = 14, kAddButtonW = 55, kAddButtonH = 31;
constexpr int kDeleteButtonX = 167, kDeleteButtonY = 14, kDeleteButtonW = 71, kDeleteButtonH = 31;
constexpr int kStartStopButtonX = 248, kStartStopButtonY = 14, kStartStopButtonW = 71, kStartStopButtonH = 31;
constexpr int kSettingsButtonX = 327, kSettingsButtonY = 14, kSettingsButtonW = 82, kSettingsButtonH = 31;

constexpr int kSettingsWindowWidth = 700;
constexpr int kSettingsWindowHeight = 797;
constexpr int kSettingsRibbonHeight = 23;
constexpr int kSettingsRibbonLogsX = 10;
constexpr int kSettingsRibbonHelpX = 54;

constexpr int kServerGroupX = 20, kServerGroupY = 28, kServerGroupW = 660, kServerGroupH = 213;
constexpr int kGeneralGroupX = 24, kGeneralGroupY = 281, kGeneralGroupW = 308, kGeneralGroupH = 124;
constexpr int kPlaylistGroupX = 348, kPlaylistGroupY = 281, kPlaylistGroupW = 330, kPlaylistGroupH = 124;
constexpr int kMediaGroupX = 24, kMediaGroupY = 445, kMediaGroupW = 660, kMediaGroupH = 216;

constexpr int kServerNameEditW = 332, kServerNameEditH = 41;
constexpr int kHttpPortEditW = 330, kHttpPortEditH = 41;
constexpr int kIpWhitelistEditW = 455, kIpWhitelistEditH = 40;
constexpr int kPlaylistAddButtonW = 99, kPlaylistAddButtonH = 36;

constexpr int kLogWindowWidth = 772;
constexpr int kLogWindowHeight = 712;

constexpr int kHelpWindowWidth = 530;
constexpr int kHelpWindowHeight = 400;

constexpr int kSourcePromptWindowWidth = 538;
constexpr int kSourcePromptWindowHeight = 209;
constexpr int kSourcePromptInputX = 18, kSourcePromptInputY = 48, kSourcePromptInputW = 514, kSourcePromptInputH = 33;
constexpr int kSourcePromptFolderW = 93, kSourcePromptFolderH = 31;
constexpr int kSourcePromptFileW = 198, kSourcePromptFileH = 30;
constexpr int kSourcePromptAddW = 76, kSourcePromptAddH = 30;
constexpr int kSourcePromptCancelW = 75, kSourcePromptCancelH = 28;

constexpr int kPlaylistWindowWidth = 538;
constexpr int kPlaylistWindowHeight = 189;
constexpr int kPlaylistMovieLabelX = 17, kPlaylistMovieLabelY = 24;
constexpr int kPlaylistMovieEditX = 96, kPlaylistMovieEditY = 18, kPlaylistMovieEditW = 323, kPlaylistMovieEditH = 31;
constexpr int kPlaylistMovieBrowseX = 430, kPlaylistMovieBrowseY = 18, kPlaylistMovieBrowseW = 88, kPlaylistMovieBrowseH = 28;
constexpr int kPlaylistSubtitleLabelX = 17, kPlaylistSubtitleLabelY = 62;
constexpr int kPlaylistSubtitleEditX = 95, kPlaylistSubtitleEditY = 62, kPlaylistSubtitleEditW = 325, kPlaylistSubtitleEditH = 31;
constexpr int kPlaylistSubtitleBrowseX = 445, kPlaylistSubtitleBrowseY = 62, kPlaylistSubtitleBrowseW = 91, kPlaylistSubtitleBrowseH = 31;
constexpr int kPlaylistAddX = 433, kPlaylistAddY = 99, kPlaylistAddW = 90, kPlaylistAddH = 30;

// Warning/restart dialog (Figma node 5:321). No equivalent window exists
// yet in gtk4_gui_main.cpp; MessageBoxShow/MessageBoxQuestion currently
// auto-size around their content. See Phase 9.
constexpr int kWarningWindowWidth = 245;
constexpr int kWarningWindowHeight = 150;
constexpr int kWarningMessageAreaH = 76;
constexpr int kWarningFooterH = 44;
constexpr int kWarningOkW = 73, kWarningOkH = 22;
constexpr int kWarningTriangleW = 32, kWarningTriangleH = 38;

}  // namespace UiTokensPosix

#endif  // UI_TOKENS_POSIX_H
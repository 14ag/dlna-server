#ifndef MEDIA_SOURCE_FILE_TYPES_H
#define MEDIA_SOURCE_FILE_TYPES_H

#include <string>
#include <vector>

// Single source of truth for the "Add media source" dialog's File...

// FEAT-01: the File... and Playlist... buttons were merged into one
// File... button, so this list is the union of what used to be two
// separate hand-maintained lists (BrowseMediaFile's media extensions in
// mainwindow.cpp, and BrowsePlaylist's/playlistButton's playlist
// extensions). Extensions are returned WITHOUT a leading dot; callers
// build their own platform-specific "*.ext" / "*.{a,b,c}" syntax from
// this list so the two platforms can never independently drift out of
// sync with each other or with dlna_utils.cpp's kFormats table again.
//
// If a new playable extension is added to kFormats in dlna_utils.cpp,
// or a new playlist extension is recognized by
// network_sources.h::IsPlaylistSourcePath, add it here too.
inline std::vector<std::wstring> GetMediaSourceFileExtensions() {
    return {
        // Video (mirrors dlna_utils.cpp kFormats video entries)
        L"mp4", L"m4v", L"mkv", L"webm", L"avi", L"divx", L"mov",
        L"mpg", L"mpeg", L"mpe", L"vob", L"ts", L"m2ts", L"mts",
        L"wmv", L"flv", L"3gp", L"3g2",
        // Audio (mirrors dlna_utils.cpp kFormats audio entries)
        L"mp3", L"flac", L"m4a", L"aac", L"wav", L"wma", L"ogg",
        L"oga", L"opus", L"aiff", L"aif", L"ac3", L"dts",
        // Playlists (mirrors network_sources.h IsPlaylistSourcePath)
        L"m3u", L"m3u8", L"pls",
    };
}

#endif // MEDIA_SOURCE_FILE_TYPES_H

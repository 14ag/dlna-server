#ifndef CONTENTDIRECTORY_H
#define CONTENTDIRECTORY_H

#include <string>
#include <map>
#include "media_sources.h"

class ContentDirectory {
public:
    static ContentDirectory& Get();

    std::string GetDeviceDescriptionXML();
    std::string GetContentDirectoryXML();
    std::string GetConnectionManagerXML();

    std::string HandleBrowse(const std::string& soapBody, const std::string& hostUrl);

private:
    ContentDirectory() {}
    
    std::string XMLEscape(const std::wstring& str);
    std::wstring GetMimeType(const std::wstring& ext);
    std::wstring GetUPnPClass(const std::wstring& ext);
};

#define AppContent ContentDirectory::Get()

#endif // CONTENTDIRECTORY_H

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

    std::string HandleContentDirectoryControl(const std::string& soapBody, const std::string& hostUrl);
    std::string HandleConnectionManagerControl(const std::string& soapBody);

private:
    ContentDirectory() {}
    
    std::string XMLEscape(const std::wstring& str);
};

#define AppContent ContentDirectory::Get()

#endif // CONTENTDIRECTORY_H

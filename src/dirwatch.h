#ifndef DIRWATCH_H
#define DIRWATCH_H

#include <functional>
#include <string>
#include <vector>

bool StartDirectoryWatch(const std::vector<std::wstring>& localFolders, std::function<void()> onChange);

void StopDirectoryWatch();

#endif

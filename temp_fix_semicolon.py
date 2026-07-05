with open('C:/Users/philip/sauce/dlna-server/src/contentdirectory.cpp', 'r') as f:
    content = f.read()

# Fix: add semicolon after the root xmlns line
old = '''       << "<root xmlns=\"urn:schemas-upnp-org:device-1-0\" xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\">\n"
    if (!endpoint.empty()) {'''

new = '''       << "<root xmlns=\"urn:schemas-upnp-org:device-1-0\" xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\">\n";
    if (!endpoint.empty()) {'''

content = content.replace(old, new)

with open('C:/Users/philip/sauce/dlna-server/src/contentdirectory.cpp', 'w') as f:
    f.write(content)

print('Semicolon fixed')

with open('C:/Users/philip/sauce/dlna-server/src/contentdirectory.cpp', 'r') as f:
    content = f.read()

# Fix the deviceType indentation
content = content.replace(
    '       << "    <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>\n"',
    '       << "  <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>\n"'
)

with open('C:/Users/philip/sauce/dlna-server/src/contentdirectory.cpp', 'w') as f:
    f.write(content)

print('Device indent fixed')

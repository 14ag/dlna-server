with open('C:/Users/philip/sauce/dlna-server/src/contentdirectory.cpp', 'r') as f:
    content = f.read()

# Fix the indentation issue on line 353
old_line = '       << "    <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>\n"'
new_line = '       << "  <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>\n"'

content = content.replace(old_line, new_line)

with open('C:/Users/philip/sauce/dlna-server/src/contentdirectory.cpp', 'w') as f:
    f.write(content)

print('Indent fixed')

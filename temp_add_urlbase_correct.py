with open('C:/Users/philip/sauce/dlna-server/src/contentdirectory.cpp', 'r') as f:
    lines = f.readlines()

# Find and replace the specific section
new_lines = []
i = 0
while i < len(lines):
    if i == 342:  # Line 343 (presentationUrl)
        new_lines.append(lines[i])
        i += 1
        # Add endpoint line
        new_lines.append('    std::string endpoint = WideToUtf8(Server::Get().GetEndpoint());\n')
        new_lines.append(lines[i])  # empty line
        i += 1
        new_lines.append(lines[i])  # std::stringstream ss;
        i += 1
        new_lines.append(lines[i])  # ss << xml version
        i += 1
        new_lines.append(lines[i])  # << root xmlns
        i += 1
        # Add URLBase
        new_lines.append('    if (!endpoint.empty()) {\n')
        new_lines.append('        ss << "  <URLBase>" << endpoint << "</URLBase>\\n";\n')
        new_lines.append('    }\n')
        new_lines.append('    ss << "  <specVersion><major>1</major><minor>0</minor></specVersion>\\n"\n')
        i += 1  # Skip the original specVersion line
    else:
        new_lines.append(lines[i])
    i += 1

with open('C:/Users/philip/sauce/dlna-server/src/contentdirectory.cpp', 'w') as f:
    f.writelines(new_lines)

print('URLBase added correctly')

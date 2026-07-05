with open('C:/Users/philip/sauce/dlna-server/src/contentdirectory.cpp', 'r') as f:
    lines = f.readlines()

# Find the section we need to fix (around lines 343-354)
new_lines = []
i = 0
while i < len(lines):
    if i >= 342 and i <= 353:
        # We're in the section to replace
        if i == 342:
            new_lines.append(lines[i])  # presentationUrl line
        elif i == 343:
            new_lines.append(lines[i])  # endpoint line
        elif i == 344:
            new_lines.append(lines[i])  # empty line
        elif i == 345:
            new_lines.append(lines[i])  # std::stringstream ss;
        elif i == 346:
            new_lines.append(lines[i])  # ss << xml version
        elif i == 347:
            new_lines.append(lines[i])  # << root xmlns
            # Now add the URLBase section
            new_lines.append('    if (!endpoint.empty()) {\n')
            new_lines.append('        ss << "  <URLBase>" << endpoint << "</URLBase>\n";\n')
            new_lines.append('    }\n')
            # Skip the old broken lines
            i = 352  # Skip to the specVersion line
            continue
        elif i == 352:
            new_lines.append(lines[i])  # << specVersion
        elif i == 353:
            new_lines.append(lines[i])  # << device
        else:
            i += 1
            continue
    else:
        new_lines.append(lines[i])
    i += 1

with open('C:/Users/philip/sauce/dlna-server/src/contentdirectory.cpp', 'w') as f:
    f.writelines(new_lines)

print('Fixed')

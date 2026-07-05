with open('C:/Users/philip/sauce/dlna-server/src/contentdirectory.cpp', 'r') as f:
    lines = f.readlines()

# Find line 348 (the <root xmlns line) and insert URLBase check after it
new_lines = []
for i, line in enumerate(lines):
    new_lines.append(line)
    if i == 347:  # Line 348 in 1-indexed
        # Add the URLBase block after this line
        new_lines.append('    if (!endpoint.empty()) {\n')
        new_lines.append('        ss << "  <URLBase>" << endpoint << "</URLBase>\\n";\n')
        new_lines.append('    }\n')

with open('C:/Users/philip/sauce/dlna-server/src/contentdirectory.cpp', 'w') as f:
    f.writelines(new_lines)

print('URLBase added')

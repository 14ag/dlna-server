with open('C:/Users/philip/sauce/dlna-server/src/contentdirectory.cpp', 'r') as f:
    lines = f.readlines()

# Find the line with <root xmlns and insert URLBase after it
new_lines = []
for i, line in enumerate(lines):
    new_lines.append(line)
    if i == 347:  # Line 348 in 1-indexed (the <root xmlns line)
        # Check if endpoint line exists (we added it earlier)
        if 'std::string endpoint' in ''.join(lines[:i]):
            new_lines.append('    if (!endpoint.empty()) {\n')
            new_lines.append('        ss << "  <URLBase>" << endpoint << "</URLBase>\n";\n')
            new_lines.append('    }\n')

with open('C:/Users/philip/sauce/dlna-server/src/contentdirectory.cpp', 'w') as f:
    f.writelines(new_lines)

print('File updated successfully')

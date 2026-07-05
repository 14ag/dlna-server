with open('C:/Users/philip/sauce/dlna-server/src/contentdirectory.cpp', 'r') as f:
    lines = f.readlines()

# Find and fix the broken lines around line 349-352
new_lines = []
i = 0
while i < len(lines):
    line = lines[i]
    # Look for the problematic pattern
    if 'if (!endpoint.empty())' in line:
        new_lines.append(line)
        i += 1
        # Next line should be the ss << line
        if i < len(lines) and 'ss <<' in lines[i] and '<URLBase>' in lines[i]:
            # Fix the line - it might be broken
            new_lines.append('        ss << "  <URLBase>" << endpoint << "</URLBase>\n";\n')
            i += 1
            # Skip any extra lines until we find the closing brace
            while i < len(lines) and '}' not in lines[i]:
                i += 1
            if i < len(lines):
                new_lines.append(lines[i])  # Add the closing brace
                i += 1
        else:
            new_lines.append(lines[i])
            i += 1
    else:
        new_lines.append(line)
        i += 1

with open('C:/Users/philip/sauce/dlna-server/src/contentdirectory.cpp', 'w') as f:
    f.writelines(new_lines)

print('Fixed')

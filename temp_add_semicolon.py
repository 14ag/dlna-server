with open('C:/Users/philip/sauce/dlna-server/src/contentdirectory.cpp', 'r') as f:
    lines = f.readlines()

# Add semicolon to line 348 (index 347)
for i, line in enumerate(lines):
    if i == 347 and '"<root xmlns' in line and not line.rstrip().endswith(';'):
        lines[i] = line.rstrip() + ';\n'

with open('C:/Users/philip/sauce/dlna-server/src/contentdirectory.cpp', 'w') as f:
    f.writelines(lines)

print('Semicolon added')

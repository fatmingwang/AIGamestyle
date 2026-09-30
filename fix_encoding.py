#!/usr/bin/env python3
import re
import os

# Resolve relative to this script's location so it works regardless of where the repo is cloned.
TARGET_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            'App', 'src', 'LangManager.cpp')

# Read the file
with open(TARGET_FILE, 'r', encoding='utf-8') as f:
    content = f.read()

# Function to convert UTF-8 hex escapes to actual characters
def convert_hex_escapes(match):
    hex_str = match.group(0)
    # Extract all \xHH sequences
    bytes_list = re.findall(r'\\x([0-9a-fA-F]{2})', hex_str)
    if bytes_list:
        try:
            byte_data = bytes([int(h, 16) for h in bytes_list])
            return byte_data.decode('utf-8')
        except:
            return hex_str
    return hex_str

# Replace patterns like \xe6\x89\xb9 with actual UTF-8 characters
# Match sequences of \xHH patterns
pattern = r'(?:\\x[0-9a-fA-F]{2})+'
content = re.sub(pattern, convert_hex_escapes, content)

# Write back with UTF-8 encoding with BOM
with open(TARGET_FILE, 'w', encoding='utf-8-sig') as f:
    f.write(content)

print('File converted successfully')

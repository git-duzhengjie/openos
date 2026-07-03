#!/usr/bin/env python3
import re, sys
with open('src/arch/x86_64/include/embed_hello_fork.h') as f:
    txt = f.read()
# only match 0xNN inside braces (skip any hex in comments)
body = txt.split('{',1)[1].rsplit('}',1)[0]
bytes_list = [int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', body)]
with open('/tmp/hello_fork.elf','wb') as f:
    f.write(bytes(bytes_list))
print('size=', len(bytes_list))

import re,sys
def decode(path,label):
    data=open(path).read()
    nums=re.findall(r'0x([0-9a-fA-F]{2})', data)
    b=bytes(int(x,16) for x in nums)
    print(f"=== {label} ({len(b)} bytes) ===")
    for m in re.finditer(rb'[ -~]{6,}', b):
        s=m.group().decode()
        if any(k in s for k in ('execve','thread_demo','hello_fork','M5.2d','A2.P5','H.4')):
            print(" ",repr(s))
decode('/mnt/e/openos/src/arch/x86_64/include/embed_hello64_v2.h','hello64_v2')
decode('/mnt/e/openos/src/arch/x86_64/include/embed_hello_fork.h','hello_fork')
decode('/mnt/e/openos/src/arch/x86_64/include/embed_thread_demo.h','thread_demo')

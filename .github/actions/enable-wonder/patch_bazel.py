#!/usr/bin/env python3
import os
import re
import sys

bazel_file = "BUILD.bazel"
if not os.path.exists(bazel_file):
    print(f"File {bazel_file} not found in {os.getcwd()}, skipping.")
    sys.exit(0)

with open(bazel_file, "r") as f:
    content = f.read()

outs_insert = '    module_outs = ["drivers/wonder/wonder.ko", "net/mac80211/mac80211.ko", "net/wireless/cfg80211.ko"],\n'

for name in ["kernel_aarch64", "kernel_aarch64_16k"]:
    pattern = rf'(name\s*=\s*"{name}",\n)'
    if re.search(pattern, content):
        content = re.sub(pattern, rf'\1{outs_insert}', content)

with open(bazel_file, "w") as f:
    f.write(content)

print(f"Successfully patched {bazel_file} with wonder/wireless module_outs!")

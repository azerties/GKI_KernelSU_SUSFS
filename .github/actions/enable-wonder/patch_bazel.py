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

extra_modules = ' + ["drivers/wonder/wonder.ko", "net/mac80211/mac80211.ko", "net/wireless/cfg80211.ko"]'

# common_kernel accepts module_implicit_outs, which it passes to kernel_build
pattern = r'(module_implicit_outs\s*=\s*get_gki_modules_list\("arm64"\)\s*\+\s*get_kunit_modules_list\("arm64"\))'
if re.search(pattern, content):
    content = re.sub(pattern, rf'\1{extra_modules}', content)
    print("Successfully appended wonder & wireless modules to module_implicit_outs in BUILD.bazel")
else:
    print("Warning: module_implicit_outs pattern not found in BUILD.bazel")

with open(bazel_file, "w") as f:
    f.write(content)

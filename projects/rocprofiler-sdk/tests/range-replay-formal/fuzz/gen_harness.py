"""Extracts qualified_kernel_name() verbatim from range_state.cpp into qkn_extracted.inc, so the
fuzz and differential harnesses test the shipped code rather than a copy that can drift."""
import pathlib
import re
import sys

src = pathlib.Path(sys.argv[1]).read_text()
m = re.search(r"std::string\nqualified_kernel_name\(std::string_view name\)\n\{.*?\n\}\n", src,
              re.S)
if not m:
    sys.exit("qualified_kernel_name not found")
pathlib.Path(sys.argv[2]).write_text(m.group(0))
print(f"extracted {m.group(0).count(chr(10))} lines")

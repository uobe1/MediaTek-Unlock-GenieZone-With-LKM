# SPDX-License-Identifier: GPL-3.0-or-later
#
# Embed a text file into the repository as a C string literal.
#
# Usage:
#   cmake -DMGZ_INPUT=<file> -DMGZ_OUTPUT=<header> -DMGZ_VAR=<name>
#         -P cmake/embed_file.cmake
#
# The generated header defines:
#
#   static const char *const <MGZ_VAR> = "...";
#
# Escaping order matters: backslashes first, then quotes, then newlines.
# The trailing newline of the input leaves an empty "" literal at the end,
# which is valid C and concatenated away by the compiler.

file(READ "${MGZ_INPUT}" mgz_content)

string(REPLACE "\\" "\\\\" mgz_content "${mgz_content}")
string(REPLACE "\"" "\\\"" mgz_content "${mgz_content}")
string(REPLACE "\n" "\\n\"\n  \"" mgz_content "${mgz_content}")

file(WRITE "${MGZ_OUTPUT}"
"/* Auto-generated from module/ by cmake/embed_file.cmake — do not edit;\n"
" * edit the script under module/ instead, it is the single source of truth. */\n"
"static const char *const ${MGZ_VAR} =\n"
"  \"${mgz_content}\";\n")

#!/usr/bin/env python3

"""
This script checks that all source headers are installed by inspecting
Makefile.am files.

Usage: ./tools/find-uninstalled-headers.py
"""

import pathlib
import re
import sys


def main():
    makefile_ams = sorted(pathlib.Path('./src/lib').glob('**/Makefile.am'))
    headers = sorted(pathlib.Path('./src/lib').glob('**/*.h'))

    headers_pattern = re.compile(r'_HEADERS [+]?= (.*\.h|)(.*)')
    backslash_pattern = re.compile(r'(.*\.h) \\$')

    failure = False

    for makefile_am in makefile_ams:
        with open(makefile_am, 'r', encoding="utf8") as f:
            lines = f.readlines()
            in_headers_block = False
            for line in lines:

                if len(line) == 0:
                    in_headers_block = False

                header = None

                headers_matches = headers_pattern.search(line)
                if headers_matches is None:
                    if not in_headers_block:
                        continue

                    backslash_matches = backslash_pattern.search(line)
                    if backslash_matches is None:
                        header = line
                        in_headers_block = False
                    else:
                        header = backslash_matches.group(1)
                else:
                    in_headers_block = True
                    if '\\' in headers_matches.group(2) and len(headers_matches.group(1)) != 0:
                        header = headers_matches.group(1)

                if header is not None:
                    relative_path = makefile_am.parent / header.strip()
                    if relative_path not in headers:
                        print(f'ERROR: Header {relative_path} not in Makefile.am')
                        failure = True
                        continue
                    headers.remove(relative_path)

    first = True
    for header in headers:
        if not any(i in header.parts for i in ['tests', 'testutils', 'unittests']):
            if first:
                print('The following headers are not in the _HEADERS section of '
                      'their respective Makefile.am file:')
                first = False
            print(f'- {header}')
            failure = True

    if failure:
        sys.exit(1)


if __name__ == '__main__':
    main()

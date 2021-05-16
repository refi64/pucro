# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import argparse
import os
import tempfile
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--make')
    parser.add_argument('--makefile')
    parser.add_argument('--fc')
    parser.add_argument('--te')
    parser.add_argument('--pp')

    args = parser.parse_args()

    pp_filename = os.path.basename(args.pp)
    pp_name, pp_ext = os.path.splitext(pp_filename)
    if pp_ext != '.pp':
        sys.exit(f'Binary policy file {args.pp} must end with .pp')

    with tempfile.TemporaryDirectory() as temp:
        shutil.copy(args.fc, os.path.join(temp, f'{pp_name}.fc'))
        shutil.copy(args.te, os.path.join(temp, f'{pp_name}.te'))

        result = subprocess.run([args.make, '-f', args.makefile, pp_filename],
                                cwd=temp)
        if result.returncode != 0:
            sys.exit(result.returncode)

        shutil.copy(os.path.join(temp, pp_filename), args.pp)


if __name__ == '__main__':
    main()

"""UW1's native builder adapter: add Winsock when building the co-op-capable Windows port.
The pinned Exhume submodule stays unchanged. All other build behavior is Exhume's.
"""
import argparse
import os
import sys

import exhume

sys.path.insert(0, os.path.join(exhume.EXHUME, 'tools'))
import portbuild as builder


def main(argv):
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--cc', default=builder.portcheck.host_cc())
    parser.add_argument('--web', action='store_true')
    args, _ = parser.parse_known_args(argv)
    original = builder.pkg_config
    if not args.web and builder.portcheck.layout_flags(args.cc):
        def pkg_config(*flags):
            result = original(*flags)
            return result + ['-lws2_32', '-Wl,--wrap=plat_audio_open'] if flags == ('--libs',) else result
        builder.pkg_config = pkg_config
    return builder.main(argv)


if __name__ == '__main__':
    sys.exit(main(builder.ARGV))

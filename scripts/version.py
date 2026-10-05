#!/usr/bin/env python3
import argparse
import datetime
import os
import re
import sys

MAJOR = 1
TAG_PATTERN = re.compile(r"^v?(\d+\.\d+\.\d+)(-build\d+)?$")


def scale(value, divisor):
    return "%02d" % ((value * 100) // divisor)


def build_tag(patch, now):
    year = "%02d" % (now.year % 100)
    month = scale(now.month, 13)
    day = scale(now.day, 32)
    return "v%d.%s.%s%s%s-build%d" % (MAJOR, year, month, day, patch, now.minute)


def numeric_of(tag):
    found = TAG_PATTERN.match(tag.strip())
    if not found:
        return None
    return found.group(1)


def read_patch(cli_value):
    raw = cli_value
    if raw is None:
        raw = os.environ.get("DLNA_PATCH")
    if raw is None:
        if sys.stdin.isatty():
            sys.stderr.write("enter the patch number (0-9): ")
            sys.stderr.flush()
            raw = sys.stdin.readline().strip()
        else:
            raw = "0"
    if raw == "":
        raw = "0"
    if len(raw) != 1 or not raw.isdigit():
        return None
    return raw


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--patch")
    parser.add_argument("--now")
    parser.add_argument("--numeric-of", dest="numeric_of")
    args = parser.parse_args(argv)

    if args.numeric_of is not None:
        number = numeric_of(args.numeric_of)
        if number is None:
            sys.stderr.write("invalid version tag\n")
            return 2
        print(number)
        return 0

    patch = read_patch(args.patch)
    if patch is None:
        sys.stderr.write("patch must be one digit from 0 to 9\n")
        return 2

    if args.now:
        now = datetime.datetime.fromisoformat(args.now)
    else:
        now = datetime.datetime.now()
    print(build_tag(patch, now))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

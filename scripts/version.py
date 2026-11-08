#!/usr/bin/env python3
import argparse
import datetime
import os
import re
import sys
from pathlib import Path

MAJOR = 2
TAG_PATTERN = re.compile(r"^v?(\d+\.\d+\.\d+)(-build\d+)?$")


def scale(value, divisor):
    return "%02d" % ((value * 100) // divisor)


def build_tag(patch, now, build=None):
    year = "%02d" % (now.year % 100)
    month = scale(now.month, 13)
    day = scale(now.day, 32)
    suffix = "" if build is None else "-build%s" % build
    return "v%d.%s.%s%s%s%s" % (MAJOR, year, month, day, patch, suffix)


def numeric_of(tag):
    found = TAG_PATTERN.match(tag.strip())
    if not found:
        return None
    return found.group(1)


def read_patch(cli_value):
    raw = cli_value
    if raw is None:
        raw = os.environ.get("DLNA_PATCH")
    if raw is None or raw == "":
        return None
    if len(raw) != 1 or not raw.isdigit():
        raise ValueError("patch must be one digit from 0 to 9")
    return raw


def version_exists(version, output_dir):
    pattern = re.compile(r"(?<!\d)" + re.escape(version) + r"(?!\d)")
    return any(
        path.is_file() and pattern.search(path.name)
        for path in output_dir.rglob("*")
    )


def select_tag(patch, now, output_dir):
    tag = build_tag(patch or "", now)
    if not version_exists(numeric_of(tag), output_dir):
        return tag

    build = 2
    while True:
        tag = build_tag(patch or "", now, str(build))
        version_number = tag[1:] if tag.startswith("v") else tag
        if not version_exists(version_number, output_dir):
            return tag
        build += 1


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

    try:
        patch = read_patch(args.patch)
    except ValueError as error:
        sys.stderr.write(str(error) + "\n")
        return 2

    if args.now:
        now = datetime.datetime.fromisoformat(args.now)
    else:
        now = datetime.datetime.now()
    output_dir = Path(__file__).resolve().parent.parent / "output"
    try:
        print(select_tag(patch, now, output_dir))
    except ValueError as error:
        sys.stderr.write(str(error) + "\n")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

#!/usr/bin/python3
# -*- coding: utf-8; mode: python; eval: (blacken-mode); -*-
"""
spa-json-po [OPTIONS] FILES...

Extract strings from SPA-JSON files and output a POT file. Values to
extract are addressed by \"key paths\", such as

    /wireplumber.settings.schema/device.restore-routes/name

which corresponds to the value of the key 'name' in the JSON object.
Only string values are supported.

"""
import os
import sys
import re
import argparse
import subprocess
import json


def main():
    parser = argparse.ArgumentParser(usage=__doc__.strip())
    parser.add_argument(
        "--key-match",
        "-k",
        metavar="REGEX",
        dest="keys",
        default=[],
        action="append",
        help="Regex applied on key path to extract a value",
    )
    parser.add_argument(
        "--output", "-o", default=None, help="Produce output to this file, not stdout"
    )
    parser.add_argument(
        "--spa-json-dump",
        default="spa-json-dump",
        help="Path to spa-json-dump executable",
    )
    parser.add_argument("files", nargs="+", metavar="FILES", help="Input files")
    args = parser.parse_args()

    strings = {}
    for fn in args.files:
        res = subprocess.run(
            [args.spa_json_dump, fn], stdout=subprocess.PIPE, check=True
        )
        strings.update(parse(res.stdout, keys=args.keys, filename=os.path.basename(fn)))

    if args.output is not None:
        stream = open(args.output, "w")
    else:
        stream = sys.stdout

    try:
        dump(stream, strings)
    finally:
        stream.close()


def parse(text, keys, filename):
    data = json.loads(text)
    return walk(data, "", keys, filename)


def walk(obj, path, keys, filename):
    result = {}

    if isinstance(obj, str):
        for key in keys:
            if re.match(key, path, flags=re.S):
                result.setdefault(obj, [])
                result[obj].append((filename, path))
    elif isinstance(obj, dict):
        for k, v in obj.items():
            result.update(walk(v, f"{path}/{k}", keys, filename))

    return result


def dump(stream, strings):
    stream.write(
        'msgid ""\n'
        'msgstr ""\n'
        '"Content-Type: text/plain; charset=UTF-8\\n"\n'
        '"Content-Transfer-Encoding: 8bit\\n"\n\n'
    )

    def sort_key(item):
        msgid, infos = item
        infos.sort()
        return infos

    for msgid, infos in sorted(strings.items(), key=sort_key):
        escaped = json.dumps(msgid)
        for filename, path in infos:
            stream.write(f"#. {path}\n#: {filename}\n")
        stream.write(f'msgid {escaped}\nmsgstr ""\n\n')


if __name__ == "__main__":
    main()

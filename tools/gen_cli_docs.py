import argparse
import gzip
import os
import re

ENTRY = re.compile(r'\{\s*L"((?:[^"\\]|\\.)*)"\s*,\s*L"((?:[^"\\]|\\.)*)"\s*\}')
FILE_VALUE_OPTIONS = {"--source"}

CLI_BEHAVIOR = [
    'Without --debug the server detaches to the background once it is up, prints "server is up" and exits with status 0.',
    "With --debug the process stays in the foreground, prints the log to the terminal and stops on Ctrl+C.",
    "Running the command again while an instance is running restarts that instance with the new arguments.",
    "Sources given with --source or as plain paths last for the session only. The values of --port, --name, --uuid and --debug are saved to the configuration file.",
]
GUI_BEHAVIOR = [
    "Running the command again shows the window of the running instance. When sources are given they replace the sources of the running session.",
    "--headless starts with a tray icon and no window. It is implied whenever sources are given.",
    "Sources given with --source or as plain paths last for the session only. The values of --port, --name, --uuid and --debug are saved to the configuration file.",
]
LOCALHOST = (
    "When no usable network interface exists the server keeps running in localhost mode. "
    "The HTTP server stays reachable at 127.0.0.1 on the configured port and SSDP advertisement is stopped. "
    "After the network returns, restart the server to advertise again. "
    "With --debug the command line version prints a warning to standard error and a status line to standard output."
)


def unescape(text):
    return re.sub(r"\\(.)", r"\1", text)


def read_options(header_path):
    options = []
    in_table = False
    skipping = False
    with open(header_path, encoding="utf-8") as handle:
        for raw in handle:
            line = raw.strip()
            if line.startswith("inline std::vector<CliFlagInfo> GetCliFlagTable"):
                in_table = True
                continue
            if not in_table:
                continue
            if line == "};":
                break
            if line == "#ifdef _WIN32":
                skipping = True
                continue
            if line == "#endif":
                skipping = False
                continue
            match = ENTRY.search(line)
            if skipping or not match:
                continue
            flag = unescape(match.group(1))
            meaning = unescape(match.group(2))
            names = []
            argument = None
            for token in flag.split(", "):
                parts = token.split(None, 1)
                if not parts or not parts[0].startswith("-"):
                    names = []
                    break
                names.append(parts[0])
                if len(parts) > 1:
                    argument = parts[1]
            if names:
                options.append({"names": names, "arg": argument, "meaning": meaning})
    return options


def long_name(option):
    for name in option["names"]:
        if name.startswith("--"):
            return name
    return option["names"][0]


def roff(text):
    text = text.replace("\\", "\\e").replace("-", "\\-")
    return "\\&" + text if text.startswith((".", "'")) else text


def man_option(option):
    tags = []
    for name in option["names"]:
        tag = "\\fB%s\\fR" % roff(name)
        if option["arg"]:
            tag += " \\fI%s\\fR" % roff(option["arg"])
        tags.append(tag)
    return ".TP\n%s\n%s." % (", ".join(tags), roff(option["meaning"]))


def build_man(prog, version, date, options, gui):
    summary = "DLNA media server with a graphical interface" if gui else "DLNA media server"
    other = prog[: -len("-gui")] if gui else prog + "-gui"
    lines = [
        '.TH %s 1 "%s" "%s %s" "User Commands"' % (prog.upper(), date, prog, version),
        ".SH NAME",
        "%s \\- %s" % (roff(prog), summary),
        ".SH SYNOPSIS",
        ".B %s" % roff(prog),
        "[\\fIOPTION\\fR]... [\\fIPATH\\fR]...",
        ".SH DESCRIPTION",
        roff("Shares local video, audio and image files with DLNA and UPnP clients on the local network. "
             "Network share URLs and playlists are also accepted as media sources."),
        ".SH OPTIONS",
    ]
    lines += [man_option(option) for option in options]
    lines += [".TP\n\\fBPATH\\fR...\nSame as \\fB\\-\\-source\\fR for each \\fBPATH\\fR."]
    lines += [".SH BEHAVIOR"]
    for paragraph in (GUI_BEHAVIOR if gui else CLI_BEHAVIOR):
        lines += [".PP", roff(paragraph)]
    lines += [".SH LOCALHOST MODE", roff(LOCALHOST)]
    lines += [
        ".SH FILES",
        ".TP\n\\fI$XDG_CONFIG_HOME/dlna\\-server/config.ini\\fR\nConfiguration file. The default base directory is \\fI~/.config\\fR.",
        ".TP\n\\fIdebug.log\\fR, \\fIdefault.m3u\\fR, \\fImedia\\-cache.tsv\\fR\nStored next to the configuration file.",
        ".SH EXIT STATUS",
        ".TP\n.B 0\nSuccess.",
        ".TP\n.B 1\nThe server could not start or no running instance was found.",
        ".TP\n.B 2\nInvalid command line.",
        ".SH SEE ALSO",
        ".BR %s (1)" % roff(other),
        ".SH REPORTING BUGS",
        roff("https://github.com/14ag/dlna-server/issues"),
    ]
    return "\n".join(lines) + "\n"


def build_bash(prog, options):
    words = " ".join(name for option in options for name in option["names"])
    plain = [long_name(o) for o in options if o["arg"] and long_name(o) not in FILE_VALUE_OPTIONS]
    function = "_" + prog.replace("-", "_")
    lines = [
        function + "() {",
        "    local cur=${COMP_WORDS[COMP_CWORD]}",
        "    local prev=${COMP_WORDS[COMP_CWORD-1]}",
    ]
    if plain:
        lines += ["    case $prev in", "        %s) COMPREPLY=(); return ;;" % "|".join(plain), "    esac"]
    lines += [
        "    if [[ $cur == -* ]]; then",
        '        COMPREPLY=($(compgen -W "%s" -- "$cur"))' % words,
        "        return",
        "    fi",
        "    local IFS=$'\\n'",
        '    COMPREPLY=($(compgen -f -- "$cur"))',
        "    compopt -o filenames 2>/dev/null",
        "}",
        "complete -F %s %s" % (function, prog),
    ]
    return "\n".join(lines) + "\n"


def zsh_text(text):
    assert ":" not in text, text
    return text.replace("'", "'\\''").replace("[", "\\[").replace("]", "\\]")


def build_zsh(prog, options):
    lines = ["#compdef " + prog, "_arguments -s \\"]
    for option in options:
        names = option["names"]
        explain = "[%s]" % zsh_text(option["meaning"])
        tail = ""
        if option["arg"]:
            tail = ":value:_files" if long_name(option) in FILE_VALUE_OPTIONS else ":value: "
        if len(names) > 1:
            head = "'(%s)'{%s}" % (" ".join(names), ",".join(names))
            lines.append("  %s'%s%s' \\" % (head, explain, tail))
        else:
            lines.append("  '%s%s%s' \\" % (names[0], explain, tail))
    lines.append("  '*:path:_files'")
    return "\n".join(lines) + "\n"


def fish_text(text):
    return text.replace("\\", "\\\\").replace("'", "\\'")


def build_fish(prog, options):
    lines = []
    for option in options:
        parts = ["complete -c " + prog]
        for name in option["names"]:
            parts.append(("-l " + name[2:]) if name.startswith("--") else ("-s " + name[1:]))
        if option["arg"]:
            parts.append("-r" if long_name(option) in FILE_VALUE_OPTIONS else "-x")
        parts.append("-d '%s'" % fish_text(option["meaning"]))
        lines.append(" ".join(parts))
    return "\n".join(lines) + "\n"


def write_text(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def write_gzip(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, compresslevel=9, mtime=0) as packed:
            packed.write(text.encode("utf-8"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--header", required=True)
    parser.add_argument("--product", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--date", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    options = read_options(args.header)
    if not options:
        raise SystemExit("no options found in " + args.header)
    for prog, gui in ((args.product, False), (args.product + "-gui", True)):
        write_gzip(os.path.join(args.out, "man", prog + ".1.gz"),
                   build_man(prog, args.version, args.date, options, gui))
        write_text(os.path.join(args.out, "completions", "bash", prog), build_bash(prog, options))
        write_text(os.path.join(args.out, "completions", "zsh", "_" + prog), build_zsh(prog, options))
        write_text(os.path.join(args.out, "completions", "fish", prog + ".fish"), build_fish(prog, options))


if __name__ == "__main__":
    main()

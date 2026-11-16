#!/usr/bin/env python3
"""Find translations that ship but can never be found at runtime.

Qt resolves tr() through the staticMetaObject of the class it is called in. A class
without Q_OBJECT has no metaobject of its own, so the call binds to the BASE class and
the lookup happens in the base's context ("QDialog"), while lupdate reads the source
and files the string under the enclosing class name ("gui::DestinationPicker"). The
catalog then LOOKS translated and ships that way: lrelease has the entry, the app asks
a different question, and that one widget stays English in every language while its
neighbours translate. Nothing fails, which is why this needs a check.

The same trap has a second shape: a class that hand rolls its own tr() forwarding to
QCoreApplication::translate("SomeContext", ...). lupdate still files the call site
under the enclosing class name, so unless that literal context IS the class name, the
two disagree again. (src/gui/exportdiag.h did exactly this, which made the whole
export dialog unreachable in all six catalogs.)

This check therefore asserts only what it can prove from the sources and the catalogs:

  1. every catalog context that names a class declared here belongs to a class that
     can translate on its own (Q_OBJECT or Q_DECLARE_TR_FUNCTIONS), or is asked for
     explicitly by name (QCoreApplication::translate / QT_TRANSLATE_NOOP);
  2. every hand rolled tr() shim forwards to its own class name;
  3. every qualified Class::tr() names a class that can translate on its own.

A context that names no class at all (a Ui form, a class behind Q_DECLARE_TR_FUNCTIONS
in a file this script does not parse) is printed under --all for review rather than
gated: an absence of evidence is not evidence.

Usage:
    scripts/i18n/check_tr_contexts.py [--root DIR] [--all]

Exit 0 when the catalogs and the code agree, 1 with the offenders otherwise.
"""

import argparse
import glob
import os
import re
import sys

SOURCE_DIRS = ("src",)
CATALOGS = "assets/locale/translation_*.ts"

# "class Name" ... up to the closing brace of the class body. A forward declaration
# ("class QLineEdit;") has no brace and is not a class body.
CLASS_RE = re.compile(r"^\s*(?:template\s*<[^>]*>\s*)?(?:class|struct)\s+(\w+)\b")
# CmndName::tr("..."): a qualified call, which asks the named class's own context.
QUALIFIED_TR_RE = re.compile(r"\b(\w+)::tr\s*\(")
# Our own shim: "static QString tr(const QString& input){".
SHIM_RE = re.compile(r"\b(?:static\s+)?QString\s+tr\s*\(")
# An explicit context: always right, whatever the enclosing class does.
EXPLICIT_CONTEXT_RE = re.compile(
    r"(?:QCoreApplication::translate|QT_TRANSLATE_NOOP)\s*\(\s*\"([^\"]+)\""
)
OWN_CONTEXT_RE = re.compile(r"Q_DECLARE_TR_FUNCTIONS\s*\(\s*([\w:]+)\s*\)")

# Contexts Qt itself answers for; a qualified call to one of these is not a defect.
QT_CONTEXTS = {
    "QCoreApplication",
    "QDialog",
    "QFileDialog",
    "QMessageBox",
    "QObject",
    "QWidget",
}


def source_files(root):
    for base in SOURCE_DIRS:
        for dirpath, _dirnames, filenames in os.walk(os.path.join(root, base)):
            for name in sorted(filenames):
                if name.endswith((".h", ".hpp", ".cpp", ".cc")):
                    yield os.path.join(dirpath, name)


def read(path):
    with open(path, encoding="utf-8", errors="replace") as handle:
        return handle.readlines()


def class_spans(lines):
    """{class name: text of the class body} for one file.

    A class declared inside another one (prop_CurrentKeyPanel.h declares a dozen) is
    reported on its own, because that is how lupdate files its strings."""
    spans = {}
    index = 0
    while index < len(lines):
        match = CLASS_RE.match(lines[index])
        if not match or "{" not in lines[index]:
            index += 1
            continue
        depth = 0
        started = False
        body = []
        cursor = index
        while cursor < len(lines):
            line = lines[cursor]
            depth += line.count("{") - line.count("}")
            started = started or "{" in line
            body.append(line)
            if started and depth <= 0:
                break
            cursor += 1
        spans[match.group(1)] = "".join(body)
        index = max(cursor, index) + 1
    return spans


# A tr() shim at FILE scope, not inside a class: the shape src/ctrl/ImageFileLoader.cpp
# had. It is invisible to the class check above and it is its own trap - the literal it
# forwards to is the context the app asks for, while lupdate files the call sites under
# the enclosing class or namespace name. It is reported for review rather than gated,
# because "the enclosing name" is a guess lupdate makes and this script does not.
FREE_SHIM_RE = re.compile(r"^\s*(?:static\s+)?QString\s+tr\s*\(", re.M)


def free_shims(lines):
    """(line number, context) for every file-scope QString tr() definition."""
    out = []
    depth = 0
    for number, line in enumerate(lines, start=1):
        if depth == 0 and FREE_SHIM_RE.match(line):
            context = None
            for ahead in lines[number - 1 : number + 6]:
                found = EXPLICIT_CONTEXT_RE.search(ahead)
                if found:
                    context = found.group(1)
                    break
            out.append((number, context))
        depth += line.count("{") - line.count("}")
    return out


def scan(root):
    """Returns (problems, dead, review, free_function_shims)."""
    files = sorted(source_files(root))

    # Every class, over every file first: a class is declared in a header and asked
    # for from a .cpp, and which of the two is read first is not something to depend
    # on. A short name that is capable in ANY file is capable.
    capable = {}  # short class name -> can translate on its own
    shims = {}  # class name -> context its own tr() forwards to
    explicit = set()
    declared_contexts = set()
    for path in files:
        lines = read(path)
        for line in lines:
            explicit.update(EXPLICIT_CONTEXT_RE.findall(line))
            declared_contexts.update(OWN_CONTEXT_RE.findall(line))
        for name, body in class_spans(lines).items():
            owns = "Q_OBJECT" in body or OWN_CONTEXT_RE.search(body) is not None
            capable[name] = capable.get(name, False) or owns
            if SHIM_RE.search(body):
                context = EXPLICIT_CONTEXT_RE.search(body)
                shims[name] = context.group(1) if context else None

    free_function_shims = []
    for path in files:
        for number, context in free_shims(read(path)):
            free_function_shims.append((path, number, context))

    problems = []

    # A shim answers in a context of its own choosing, and lupdate files the strings
    # under the class name. Those two have to be the same string.
    for name, context in sorted(shims.items()):
        if context != name:
            problems.append(
                f"{name} forwards tr() to {context!r}, but lupdate files its strings "
                f"under {name!r}: every one of them is unreachable"
            )

    # A qualified call names the class it wants, so a class without a metaobject of its
    # own answers from its base instead, and the catalog's entries go unread.
    for path in files:
        for number, line in enumerate(read(path), start=1):
            for name in QUALIFIED_TR_RE.findall(line):
                if name in QT_CONTEXTS or name in declared_contexts:
                    continue
                if name in capable and not capable[name]:
                    problems.append(
                        f"{os.path.relpath(path, root)}:{number}: {name}::tr() but {name} "
                        f"has no Q_OBJECT, so the lookup goes to its base class"
                    )

    # The catalogs: a context that names a class here, and that no call site can ask
    # for, is dead weight a translator would keep translating forever.
    dead = {}
    review = {}
    for path in sorted(glob.glob(os.path.join(root, CATALOGS))):
        text = "".join(read(path))
        for name, body in re.findall(
            r"<context>\s*<name>([\w:]+)</name>(.*?)</context>", text, re.S
        ):
            live = [
                message
                for message in re.findall(r"<message>(.*?)</message>", body, re.S)
                if 'type="vanished"' not in message
            ]
            if not live:
                continue
            short = name.split("::")[-1]
            if name in explicit or name in declared_contexts:
                continue
            if short in capable:
                if not capable[short]:
                    dead.setdefault(name, set()).add(os.path.basename(path))
            elif short in shims and shims[short] == name:
                continue
            else:
                review.setdefault(name, set()).add(os.path.basename(path))
    return problems, dead, review, free_function_shims


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--root", default=os.path.join(os.path.dirname(__file__), "..", "..")
    )
    parser.add_argument("--all", action="store_true", help="also list what needs review")
    args = parser.parse_args()
    root = os.path.abspath(args.root)

    problems, dead, review, free_function_shims = scan(root)

    for problem in problems:
        print(problem)

    if dead:
        print("catalog contexts whose class cannot ask for them:")
        for name in sorted(dead):
            print(f"  {name}  ({len(dead[name])} catalog(s))")

    if free_function_shims:
        print("file-scope tr() shims to check by hand (lupdate files their call sites "
              "under the enclosing name, not under this context):")
        for path, number, context in free_function_shims:
            print(f"  {os.path.relpath(path, root)}:{number} -> context {context!r}")

    if review and args.all:
        print("contexts with no class of that name here, to check by hand:")
        for name in sorted(review):
            print(f"  {name}  ({len(review[name])} catalog(s))")

    if problems or dead:
        print(
            f"\n{len(problems)} call site(s) and {len(dead)} context(s) that cannot translate"
        )
        return 1
    print("every catalog context can be asked for")
    return 0


if __name__ == "__main__":
    sys.exit(main())

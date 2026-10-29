"""Collect firmware notices from linked components; no build or device control."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys

SOURCE_EXTENSIONS = {".c", ".cpp", ".h", ".hpp", ".S", ".s"}
EXCLUDED = {"examples", "example", "test", "tests", "test_apps", "docs", "tools", "babblesim", "targets"}
COMMENTS = re.compile(r"/\*.*?\*/|(?m:(?://[^\n]*\n?)+)", re.S)
LEGAL = re.compile(r"copyright|SPDX-License-Identifier|permission is hereby|redistribution and use|licensed under", re.I)


def relevant(path):
    parts = set(path.parts)
    return not (EXCLUDED & parts or {"linux", "riscv", "bluedroid", "esp_ble_mesh"} & parts or
                any(re.fullmatch(r"esp32[a-z0-9]*", part) and part != "esp32s3" and
                    not (part == "esp32" and "controller" in parts) for part in parts))


def linked_inputs(text):
    if "Discarded input sections" not in text:
        raise ValueError("Link map has no archive selection boundary")
    text = text.split("Discarded input sections", 1)[0].replace("\\", "/")
    archives = {}
    for path, member in re.findall(r"^(\S.*?\.a)\(([^)]*)\)", text, re.M):
        archives.setdefault(path.rsplit("/", 1)[-1], set()).add(member)
    if "core.a" not in archives or len(archives) < 20:
        raise ValueError("Incomplete firmware link map")
    arduino = {}
    for library, member in re.findall(r"/compile/libraries/([^/]+)/([^()\r\n]+?\.o)", text):
        arduino.setdefault(library, set()).add(member)
    return archives, arduino


class Notices:
    def __init__(self):
        self.components = {}
        self.documents = {}
        self.notices = {}

    def component(self, name, evidence):
        return self.components.setdefault(name, dict(evidence=sorted(evidence), documents=set(), notices=set()))

    def document(self, component, label, data, origin):
        if not data.strip():
            raise ValueError(f"Empty license: {origin}")
        digest = hashlib.sha256(data).hexdigest()
        document = self.documents.setdefault(digest, dict(label=label, data=data, origins=set()))
        document["origins"].add(origin)
        self.components[component]["documents"].add(digest)

    def file(self, component, label, path, origin):
        self.document(component, label, path.read_bytes(), origin)

    def headers(self, component, root, paths):
        for path in sorted(set(paths)):
            if not path.is_file() or path.suffix not in SOURCE_EXTENSIONS or not relevant(path.relative_to(root)):
                continue
            origin = f"{component}/{path.relative_to(root).as_posix()}"
            for match in COMMENTS.finditer(path.read_text(encoding="utf-8", errors="strict")):
                text = match.group().strip()
                if not LEGAL.search(text):
                    continue
                digest = hashlib.sha256(text.encode()).hexdigest()
                notice = self.notices.setdefault(digest, dict(text=text, origins=set(), components=set()))
                notice["origins"].add(origin)
                notice["components"].add(component)
                self.components[component]["notices"].add(digest)

    def selected_sources(self, component, root, members, allow_missing=False):
        names = {Path(x.replace("\\", "/")).name.removesuffix(".obj").removesuffix(".o") for x in members}
        sources = [p for p in root.rglob("*") if p.is_file() and p.name in names
                   and p.suffix in SOURCE_EXTENSIONS and relevant(p.relative_to(root))]
        found = {p.name for p in sources}
        if names - found and not allow_missing:
            raise ValueError(f"Unmatched linked source in {component}: {sorted(names-found)}")
        headers = set()
        for source in sources:
            headers.update(source.parent.glob("*.h"))
            for parent in [source.parent, *source.parents]:
                if not parent.is_relative_to(root):
                    break
                include = parent / "include"
                if include.is_dir():
                    headers.update(include.rglob("*.h"))
        self.headers(component, root, [*sources, *headers])
        return names - found

    def write(self, destination):
        destination = Path(destination)
        parent = destination.parent.resolve()
        if destination.name != "licenses" or destination.is_symlink() or destination.resolve().parent != parent:
            raise ValueError("Unsafe firmware license output directory")
        if destination.exists():
            shutil.rmtree(destination)
        destination.mkdir(parents=True)
        filenames = {}
        for digest, document in sorted(self.documents.items()):
            name = re.sub(r"[^A-Za-z0-9.-]", "-", document["label"]) + "-" + digest[:12] + ".txt"
            (destination / name).write_bytes(document["data"])
            filenames[digest] = name
        notice_lines = ["# Copyright and source notices", "", "Original comment/notice text is retained below; identical blocks occur once.", ""]
        anchors = {}
        for name, component in sorted(self.components.items()):
            if not component["notices"]:
                continue
            anchor = "component-" + hashlib.sha256(name.encode()).hexdigest()[:12]
            anchors[name] = anchor
            notice_lines += [f"## {anchor}", "", name, "",
                             ", ".join(f"[notice-{x[:16]}](#notice-{x[:16]})" for x in sorted(component["notices"])), ""]
        for digest, notice in sorted(self.notices.items()):
            notice_lines += [f"## notice-{digest[:16]}", "", "Components: " + ", ".join(sorted(notice["components"])), "",
                             "Sources: " + ", ".join(f"`{x}`" for x in sorted(notice["origins"])), "",
                             *["    " + line for line in notice["text"].splitlines()], ""]
        (destination / "NOTICES.md").write_text("\n".join(notice_lines), encoding="utf-8", newline="\n")
        lines = ["# Firmware component licenses", "", "Only components selected by the firmware link map are listed. Full license texts are stored once by SHA256; original copyright/permission notices are retained in NOTICES.md.", "",
                 "| Component | Linked input / evidence | License texts | Copyright / notices |", "| --- | --- | --- | --- |"]
        mapping = {}
        for name, component in sorted(self.components.items()):
            docs = [filenames[x] for x in sorted(component["documents"])]
            notices = [f"notice-{x[:16]}" for x in sorted(component["notices"])]
            if not docs and not notices:
                raise ValueError(f"Missing license/attribution for {name}")
            mapping[name] = dict(evidence=component["evidence"], documents=docs, notices=notices)
            lines.append(f"| {name} | {', '.join(component['evidence'])} | " +
                         ", ".join(f"[{x}]({x})" for x in docs) + " | " +
                         (f"[{len(notices)} notices](NOTICES.md#{anchors[name]})" if notices else "See license text") + " |")
        lines += ["", "## License provenance", "", "| License file | Original locations |", "| --- | --- |"]
        for digest, document in sorted(self.documents.items()):
            lines.append(f"| [{filenames[digest]}]({filenames[digest]}) | " +
                         ", ".join(f"`{x}`" for x in sorted(document["origins"])) + " |")
        lines += ["", "License terms apply per component; the inventory does not relicense vendor code. Source versions and rebuild directions are in ../FIRMWARE-SOURCES.md.", ""]
        (destination / "README.md").write_text("\n".join(lines), encoding="utf-8", newline="\n")
        (destination / "components.json").write_text(json.dumps(mapping, indent=2) + "\n", encoding="utf-8", newline="\n")
        print(f"Firmware notices: {len(mapping)} components, {len(filenames)} unique license texts, {len(self.notices)} unique notice blocks")


def collect(config):
    root, work, cache = (Path(config[x]) for x in ("root", "work", "cache"))
    sources = {name: cache / "extracted" / item["hash"] / item["top"] for name, item in config["sources"].items()}
    def source(prefix):
        found = [path for name, path in sources.items() if name.startswith(prefix)]
        if len(found) != 1 or not found[0].is_dir():
            raise ValueError(f"Missing pinned source: {prefix}")
        return found[0]
    idf, arduino = source("esp-idf-"), source("arduino-esp32-")
    tinyusb, mbedtls, tlsf, nimble, diagnostics = (source(x) for x in ("tinyusb", "mbedtls", "tlsf", "nimble-", "esp-diagnostics-"))
    toolchain = Path(config["data"]) / "packages/esp32/tools/esp-x32" / config["ESP32_TOOLCHAIN_VERSION"] / "share/licenses"
    archives, libraries = linked_inputs((work / "compile/R2500Adapter.ino.map").read_text(encoding="utf-8"))
    unknown = set(archives) - set(config["FIRMWARE_RELINK_LIBRARIES"].split(";"))
    if unknown:
        raise ValueError(f"Unreviewed linked libraries: {sorted(unknown)}")
    result = Notices()
    for name, path in (("ALLPOWERS application", root / "LICENSE"), ("ALLPOWERS model definitions", root / "core/src/AllpowersModels.LICENSE.txt")):
        result.component(name, ["application sources"])
        result.file(name, "MIT", path, path.relative_to(root).as_posix())
    name = "Arduino ESP32 core"
    result.component(name, ["core.a"])
    for label, path in (("LGPL-2.1", arduino / "LICENSE.md"), ("LGPL-3.0", root / "docs/licenses/LGPL-3.0.md"), ("GPL-3.0", root / "docs/licenses/GPL-3.0.md")):
        result.file(name, label, path, "arduino-esp32/LICENSE.md" if path.is_relative_to(arduino) else path.relative_to(root).as_posix())
    result.selected_sources(name, arduino / "cores/esp32", archives["core.a"])
    for library, members in sorted(libraries.items()):
        if library == "AllpowersCore":
            continue
        if library not in {"BLE", "USB", "SPI", "Preferences"}:
            raise ValueError(f"Unreviewed Arduino library: {library}")
        name = "Arduino " + library
        result.component(name, [f"libraries/{library}"])
        result.file(name, "LGPL-2.1", arduino / "LICENSE.md", "arduino-esp32/LICENSE.md")
        result.selected_sources(name, arduino / "libraries" / library, members)
    runtime = {"libgcc.a", "libstdc++.a", "libc.a", "libm.a"}
    binary = {"libbtbb.a": "bt-LICENSE", "libbtdm_app.a": "bt-LICENSE", "libcoexist.a": "coex-LICENSE", "libphy.a": "phy-LICENSE"}
    for lib, members in sorted(archives.items()):
        if lib == "core.a":
            continue
        if lib in runtime:
            name = "GCC runtime" if lib in {"libgcc.a", "libstdc++.a"} else "newlib C and math runtime"
            result.component(name, sorted(set(archives) & ({"libgcc.a", "libstdc++.a"} if name == "GCC runtime" else {"libc.a", "libm.a"})))
            if name == "GCC runtime":
                for label, path in (("GPL-3.0", toolchain / "gcc/COPYING3"), ("GCC-Runtime-Exception", toolchain / "gcc/COPYING.RUNTIME")):
                    result.file(name, label, path, "toolchain/gcc/" + path.name)
            else:
                result.file(name, "newlib", toolchain / "newlib/COPYING.NEWLIB", "toolchain/newlib/COPYING.NEWLIB")
            continue
        if lib in binary:
            name = "Espressif binary " + binary[lib].removesuffix("-LICENSE")
            result.component(name, sorted(x for x in archives if binary.get(x) == binary[lib]))
            result.file(name, "Apache-2.0", cache / binary[lib], binary[lib])
            continue
        if lib == "libarduino_tinyusb.a":
            name = "TinyUSB"
            result.component(name, [lib])
            result.file(name, "TinyUSB-MIT", tinyusb / "LICENSE", "tinyusb/LICENSE")
            result.selected_sources(name, tinyusb / "src", members)
            continue
        if lib == "libmbedcrypto.a":
            name = "Mbed TLS crypto"
            result.component(name, [lib])
            for filename in ("LICENSE", "LICENSES/Apache-2.0.txt"):
                path = mbedtls / filename
                if path.is_file():
                    result.file(name, "Mbed-TLS-license", path, "mbedtls/" + filename)
            missing = result.selected_sources(name, mbedtls, members, allow_missing=True)
            if missing:
                result.file(name, "Apache-2.0", idf / "LICENSE", "esp-idf/LICENSE")
                result.selected_sources(name, idf / "components/mbedtls", missing)
            continue
        if lib == "libespressif__esp_diagnostics.a":
            name = "ESP Diagnostics"
            result.component(name, [lib])
            result.file(name, "Apache-2.0", diagnostics / "LICENSE", "esp_diagnostics/LICENSE")
            result.selected_sources(name, diagnostics, members)
            continue
        component = {"libcxx.a": "cxx", "libxt_hal.a": "xtensa"}.get(lib, lib[3:-2])
        component_root = idf / "components" / component
        if not component_root.is_dir():
            raise ValueError(f"No reviewed source component for {lib}")
        name = "ESP-IDF " + component
        result.component(name, [lib])
        result.file(name, "Apache-2.0", idf / "LICENSE", "esp-idf/LICENSE")
        if lib == "libbt.a":
            # The SDK links its modified TinyCrypt, rather than NimBLE's unused
            # ext/tinycrypt copy. Keep the actual Intel/MacKay terms and notices.
            tinycrypt = component_root / "common/tinycrypt"
            source_names = {p.name for p in tinycrypt.rglob("*.c")}
            selected = {m for m in members if Path(m).name.removesuffix(".obj").removesuffix(".o") in source_names}
            if selected:
                crypto_name = "TinyCrypt (ESP-IDF)"
                result.component(crypto_name, [f"{lib}:{m}" for m in selected])
                result.file(crypto_name, "TinyCrypt-BSD", tinycrypt / "LICENSE", "esp-idf/components/bt/common/tinycrypt/LICENSE")
                result.file(crypto_name, "Apache-2.0", idf / "LICENSE", "esp-idf/LICENSE")
                result.selected_sources(crypto_name, tinycrypt, selected)
                members = members - selected
        if lib == "libxt_hal.a":
            result.headers(name, component_root, [component_root / "include/xtensa/hal.h", component_root / "esp32s3/include/xtensa/config/core-isa.h"])
            continue
        missing = result.selected_sources(name, component_root, members, allow_missing=lib in {"libbt.a", "libheap.a"})
        if lib == "libbt.a" and missing:
            name = "NimBLE"
            result.component(name, [lib])
            for filename in ("LICENSE", "NOTICE"):
                result.file(name, "NimBLE-" + Path(filename).name, nimble / filename, "nimble/" + filename)
            result.selected_sources(name, nimble, missing)
            result.headers(name, nimble, [nimble / "porting/nimble/include/os/queue.h"])
        if lib == "libheap.a" and missing:
            name = "TLSF"
            result.component(name, [lib])
            result.file(name, "TLSF-BSD-3-Clause", root / "docs/licenses/TLSF-BSD-3-Clause.md", "tlsf SPDX-BSD-3-Clause + full terms")
            result.selected_sources(name, tlsf, missing)
        if lib == "libfreertos.a":
            result.file(name, "FreeRTOS-MIT", component_root / "FreeRTOS-Kernel/LICENSE.md", "esp-idf/components/freertos/FreeRTOS-Kernel/LICENSE.md")
    result.write(Path(config["output"]) / "licenses")


if __name__ == "__main__":
    collect(json.loads(Path(sys.argv[1]).read_text(encoding="utf-8")))

#!/usr/bin/env python3
"""Executa a aplicacao Unity em QEMU, sem placa e sem simular o radio ESP-NOW."""
import argparse
import json
from pathlib import Path
import queue
import re
import shutil
import subprocess
import sys
import threading
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("tests/firmware/build"))
    parser.add_argument("--qemu", default="qemu-system-xtensa")
    parser.add_argument("--timeout", type=float, default=45)
    args = parser.parse_args()
    executable = shutil.which(args.qemu)
    if not executable:
        parser.error("QEMU Xtensa da Espressif nao encontrado. Instale com idf_tools.py install qemu-xtensa.")
    build = args.build_dir.resolve()
    metadata = json.loads((build / "flasher_args.json").read_text(encoding="utf-8"))
    flash = bytearray(b"\xff" * (4 * 1024 * 1024))
    for offset, filename in metadata["flash_files"].items():
        start = int(offset, 0)
        payload = (build / filename).read_bytes()
        if start + len(payload) > len(flash):
            parser.error("Imagem de teste excedeu 4 MiB")
        flash[start : start + len(payload)] = payload
    image = build / "qemu-test-flash.bin"
    image.write_bytes(flash)
    command = [executable, "-M", "esp32", "-m", "4M", "-nographic", "-no-reboot", "-nic", "none",
               "-drive", f"file={image.as_posix()},if=mtd,format=raw"]
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, encoding="utf-8", errors="replace")
    lines = queue.Queue()

    def reader():
        for line in process.stdout:
            lines.put(line)
        lines.put(None)

    threading.Thread(target=reader, daemon=True).start()
    deadline = time.monotonic() + args.timeout
    result = None
    transcript = []
    try:
        while time.monotonic() < deadline:
            try:
                line = lines.get(timeout=0.25)
            except queue.Empty:
                continue
            if line is None:
                break
            print(line, end="", flush=True)
            transcript.append(line)
            match = re.search(r"(\d+) Tests (\d+) Failures (\d+) Ignored", line)
            if match:
                result = tuple(map(int, match.groups()))
                break
    finally:
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        (build / "qemu-results.log").write_text("".join(transcript), encoding="utf-8")
    if result is None:
        if process.returncode == -1073741515:
            print("QEMU no Windows: DLL ausente. Confira libiconv-2.dll no PATH do ambiente ESP-IDF.", file=sys.stderr)
        print("FALHA: QEMU terminou ou excedeu o tempo sem relatorio Unity.", file=sys.stderr)
        return 1
    return 0 if result[0] > 0 and result[1] == 0 and result[2] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

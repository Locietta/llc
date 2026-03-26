#!/usr/bin/env python3
"""Format hook: C++ via clang-format, Slang via slangd LSP."""

import json
import os
import shutil
import subprocess
import sys


def main():
    try:
        data = json.loads(sys.stdin.read())
    except (json.JSONDecodeError, ValueError):
        return

    filepath = data.get("tool_input", {}).get("file_path", "")
    if not filepath:
        return
    filepath = filepath.strip().replace("\r", "")
    if not os.path.isfile(filepath):
        return

    ext = os.path.splitext(filepath)[1].lower()

    if ext in (".cpp", ".h", ".hpp"):
        subprocess.run(
            ["clang-format", "-i", filepath], capture_output=True, timeout=30
        )
    elif ext in (".slang", ".slangh"):
        slangd = shutil.which("slangd")
        if slangd:
            _format_slangd(filepath, slangd)
        else:
            # Fallback to plain clang-format when slangd is unavailable.
            subprocess.run(
                ["clang-format", "-i", filepath], capture_output=True, timeout=30
            )


# ---------------------------------------------------------------------------
# LSP helpers
# ---------------------------------------------------------------------------


def _lsp_encode(obj):
    body = json.dumps(obj).encode("utf-8")
    return f"Content-Length: {len(body)}\r\n\r\n".encode("ascii") + body


def _lsp_read(stdout):
    buf = b""
    while True:
        ch = stdout.read(1)
        if not ch:
            return None
        buf += ch
        if buf.endswith(b"\r\n\r\n"):
            break

    content_length = 0
    for line in buf.decode("ascii").split("\r\n"):
        if line.lower().startswith("content-length:"):
            content_length = int(line.split(":", 1)[1].strip())

    if content_length == 0:
        return None

    body = b""
    while len(body) < content_length:
        chunk = stdout.read(content_length - len(body))
        if not chunk:
            return None
        body += chunk

    return json.loads(body.decode("utf-8"))


def _lsp_request(msg_id, method, params):
    return _lsp_encode(
        {"jsonrpc": "2.0", "id": msg_id, "method": method, "params": params}
    )


def _lsp_notify(method, params):
    return _lsp_encode({"jsonrpc": "2.0", "method": method, "params": params})


def _lsp_wait(stdout, expected_id, limit=50):
    for _ in range(limit):
        msg = _lsp_read(stdout)
        if msg is None:
            return None
        if msg.get("id") == expected_id:
            return msg
    return None


# ---------------------------------------------------------------------------
# LSP text-edit application
# ---------------------------------------------------------------------------


def _apply_edits(content, edits):
    if not edits:
        return content

    # Line-start offset table.
    offsets = [0]
    for i, ch in enumerate(content):
        if ch == "\n":
            offsets.append(i + 1)
    offsets.append(len(content))

    edits.sort(
        key=lambda e: (e["range"]["start"]["line"], e["range"]["start"]["character"]),
        reverse=True,
    )

    for edit in edits:
        s = edit["range"]["start"]
        e = edit["range"]["end"]
        start = offsets[min(s["line"], len(offsets) - 1)] + s["character"]
        end = offsets[min(e["line"], len(offsets) - 1)] + e["character"]
        content = content[:start] + edit["newText"] + content[end:]

    return content


# ---------------------------------------------------------------------------
# slangd formatting
# ---------------------------------------------------------------------------


def _format_slangd(filepath, slangd_path):
    abs_path = os.path.abspath(filepath)
    with open(abs_path, "r", encoding="utf-8") as f:
        content = f.read()

    file_uri = "file:///" + abs_path.replace("\\", "/")
    root_uri = "file:///" + os.getcwd().replace("\\", "/")

    proc = subprocess.Popen(
        [slangd_path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )

    try:
        # 1 – initialize
        proc.stdin.write(
            _lsp_request(
                1,
                "initialize",
                {
                    "processId": os.getpid(),
                    "capabilities": {},
                    "rootUri": root_uri,
                },
            )
        )
        proc.stdin.flush()
        if _lsp_wait(proc.stdout, 1) is None:
            return

        # 2 – initialized
        proc.stdin.write(_lsp_notify("initialized", {}))

        # 3 – textDocument/didOpen
        proc.stdin.write(
            _lsp_notify(
                "textDocument/didOpen",
                {
                    "textDocument": {
                        "uri": file_uri,
                        "languageId": "slang",
                        "version": 1,
                        "text": content,
                    }
                },
            )
        )

        # 4 – textDocument/formatting
        proc.stdin.write(
            _lsp_request(
                2,
                "textDocument/formatting",
                {
                    "textDocument": {"uri": file_uri},
                    "options": {"tabSize": 4, "insertSpaces": True},
                },
            )
        )
        proc.stdin.flush()

        resp = _lsp_wait(proc.stdout, 2)
        edits = resp.get("result") if resp else None

        if edits:
            new_content = _apply_edits(content, edits)
            if new_content != content:
                with open(abs_path, "w", encoding="utf-8") as f:
                    f.write(new_content)

        # 5 – shutdown + exit
        proc.stdin.write(_lsp_request(3, "shutdown", None))
        proc.stdin.flush()
        _lsp_wait(proc.stdout, 3, limit=10)
        proc.stdin.write(_lsp_notify("exit", None))
        proc.stdin.flush()

    except (BrokenPipeError, OSError):
        pass
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=5)
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass


if __name__ == "__main__":
    main()

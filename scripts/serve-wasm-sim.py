#!/usr/bin/env python3
"""Serve WASM sim output with COOP/COEP headers for AudioWorklet + Wasm workers."""

from __future__ import annotations

import argparse
import functools
import http.server
import socketserver


class CoiSimRequestHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self) -> None:
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?", default=".", help="Directory to serve")
    parser.add_argument("--port", type=int, default=8765, help="TCP port")
    args = parser.parse_args()

    handler = functools.partial(CoiSimRequestHandler, directory=args.directory)
    with socketserver.TCPServer(("", args.port), handler) as httpd:
        print(f"Serving {args.directory} with COOP/COEP on http://localhost:{args.port}/")
        httpd.serve_forever()


if __name__ == "__main__":
    main()

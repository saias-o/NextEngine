#!/usr/bin/env python3
"""Dev server for the SaidaEngine web build (Etape 16.6).

Serves a directory with the COOP/COEP headers required for SharedArrayBuffer
(WASM threads), plus correct MIME types for .wasm/.data and pre-compressed
Brotli files (.br) when present.

Usage: python web/serve.py [build-web] [port]
"""
import http.server
import os
import sys

directory = sys.argv[1] if len(sys.argv) > 1 else "build-web"
port = int(sys.argv[2]) if len(sys.argv) > 2 else 8080


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".wasm": "application/wasm",
        ".data": "application/octet-stream",
        ".js": "text/javascript",
    }

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=directory, **kwargs)

    def end_headers(self):
        # SharedArrayBuffer requirements (WASM threads).
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def send_head(self):
        # Serve foo.br as foo with Content-Encoding: br when the client accepts it.
        path = self.translate_path(self.path)
        br = path + ".br"
        if os.path.isfile(br) and "br" in self.headers.get("Accept-Encoding", ""):
            f = open(br, "rb")
            self.send_response(200)
            self.send_header("Content-Type", self.guess_type(path))
            self.send_header("Content-Encoding", "br")
            self.send_header("Content-Length", str(os.fstat(f.fileno()).st_size))
            self.end_headers()
            return f
        return super().send_head()


if __name__ == "__main__":
    print(f"serving {directory}/ on http://localhost:{port} (COOP/COEP enabled)")
    http.server.ThreadingHTTPServer(("", port), Handler).serve_forever()

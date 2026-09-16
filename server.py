#!/usr/bin/env python3
"""
Local HTTP server for Qt WebAssembly applications with Cross-Origin Isolation support.
Adds COOP and COEP headers required for SharedArrayBuffer / multi-threading.
"""

import os
import sys
import argparse
from http.server import HTTPServer, SimpleHTTPRequestHandler

class CrossOriginIsolatedHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        # Required headers for SharedArrayBuffer and WASM multi-threading
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "cross-origin")
        self.send_header("Access-Control-Allow-Origin", "*")
        # Disable caching during development
        self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        super().end_headers()

def run_server(port=8080, directory=None):
    if directory is None:
        # Default to build-wasm if available, otherwise script dir
        script_dir = os.path.dirname(os.path.abspath(__file__))
        build_wasm_dir = os.path.join(script_dir, "build-wasm")
        if os.path.exists(os.path.join(build_wasm_dir, "vesc_tool_7.00.html")):
            directory = build_wasm_dir
        elif os.path.exists(os.path.join(script_dir, "vesc_tool_7.00.html")):
            directory = script_dir
        else:
            directory = os.getcwd()

    # Automatically ensure HTML template is patched with mobile HUD and WebSerial bridge
    try:
        import patch_wasm_html
        patch_wasm_html.patch_html(directory)
    except Exception as e:
        print(f"[WARN] Could not run patch_wasm_html: {e}")

    # Ensure proper MIME types
    CrossOriginIsolatedHandler.extensions_map.update({
        ".wasm": "application/wasm",
        ".js": "application/javascript",
        ".mjs": "application/javascript",
        ".json": "application/json",
        ".svg": "image/svg+xml",
        ".html": "text/html; charset=utf-8",
        ".css": "text/css",
    })

    os.chdir(directory)

    # Use ThreadingHTTPServer if available (Python 3.7+)
    try:
        from http.server import ThreadingHTTPServer
        server_class = ThreadingHTTPServer
    except ImportError:
        server_class = HTTPServer

    server_address = ("0.0.0.0", port)
    
    try:
        httpd = server_class(server_address, CrossOriginIsolatedHandler)
    except OSError as e:
        print(f"Error binding to port {port}: {e}")
        print("Note: Another process might already be using this port.")
        sys.exit(1)

    print("=" * 60)
    print(" VESC Tool WebAssembly Server (Cross-Origin Isolated)")
    print("=" * 60)
    print(f" Serving directory : {directory}")
    print(f" Listening on      : http://localhost:{port}")
    print(f" Direct App URL    : http://localhost:{port}/vesc_tool_7.00.html")
    print(f" Headers enabled   : Cross-Origin-Opener-Policy: same-origin")
    print(f"                     Cross-Origin-Embedder-Policy: require-corp")
    print("=" * 60)
    print(" Press Ctrl+C to stop the server.\n")

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping server...")
        httpd.server_close()
        print("Server stopped.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Serve WebAssembly files with COOP/COEP headers.")
    parser.add_argument("-p", "--port", type=int, default=8080, help="Port to listen on (default: 8080)")
    parser.add_argument("-d", "--dir", type=str, default=None, help="Directory to serve (default: auto-detect)")
    args = parser.parse_args()

    run_server(port=args.port, directory=args.dir)

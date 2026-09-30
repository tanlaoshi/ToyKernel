#!/bin/bash
# serve-store-lan.sh — 导出并 HTTP 服务（PR-LAN-store-0）
# 用法: ./Tools/Scripts/serve-store-lan.sh [out-dir] [port]
#
# 用 TCP_NODELAY + sendall 整包写出；避免标准库头/体两次 write 触发 Nagle，
# 也避免过大单段在部分 NIC 上误标 ERR_RES（NUC Alx 曾 http empty）。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
Out="${1:-$ROOT/Build/store-lan}"
Port="${2:-8080}"

"$ROOT/Tools/Scripts/export-store-lan.sh" "$Out"
echo "serve-store-lan: http://0.0.0.0:$Port/  (Ctrl+C stop)"
echo "hint: store repo <this-host-LAN-IP>:$Port"
cd "$Out"
exec python3 - "$Port" <<'PY'
import os
import socket
import sys
from http.server import SimpleHTTPRequestHandler, HTTPServer

class Handler(SimpleHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def do_GET(self):
        path = self.translate_path(self.path)
        if os.path.isdir(path):
            self.send_error(404, "Directory listing disabled")
            return
        try:
            with open(path, "rb") as f:
                body = f.read()
        except OSError:
            self.send_error(404, "File not found")
            return
        ctype = self.guess_type(path)
        headers = (
            "HTTP/1.0 200 OK\r\n"
            "Server: ToyStoreLan/1.1\r\n"
            f"Content-Type: {ctype}\r\n"
            f"Content-Length: {len(body)}\r\n"
            "Connection: close\r\n"
            "\r\n"
        ).encode("ascii")
        try:
            self.connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        except OSError:
            pass
        # 整包一次写；若仍大，内核会按 MSS 切片（比应用层两次 write 干净）
        self.wfile.write(headers + body)
        self.wfile.flush()
        self.log_request(200, len(body))

    def log_message(self, fmt, *args):
        sys.stderr.write("%s - - [%s] %s\n" % (self.address_string(), self.log_date_time_string(), fmt % args))

class Server(HTTPServer):
    allow_reuse_address = True

port = int(sys.argv[1])
httpd = Server(("0.0.0.0", port), Handler)
print(f"Serving ToyStoreLan/1.1 on 0.0.0.0 port {port} ...", flush=True)
try:
    httpd.serve_forever()
except KeyboardInterrupt:
    print("\nKeyboard interrupt received, exiting.")
    httpd.server_close()
PY

#!/usr/bin/env python3
"""
Internal TLS forwarder for SIX.
Listens on 127.0.0.1:18443 and tunnels HTTPS connections via CONNECT.
Automatically terminates when parent process dies.
"""

import sys, os, socket, ssl, threading, signal

# Terminate if parent dies
try:
    import ctypes
    libc = ctypes.CDLL("libc.so.6")
    PR_SET_PDEATHSIG = 1
    libc.prctl(PR_SET_PDEATHSIG, signal.SIGKILL)
except Exception:
    pass

PORT = 18443

def handle_client(c):
    tls_s = None
    try:
        c.settimeout(30)
        req = b''
        while b'\r\n\r\n' not in req:
            chunk = c.recv(1024)
            if not chunk:
                return
            req += chunk
        
        # Parse: CONNECT host:port HTTP/1.x
        first_line = req.decode('latin1', errors='ignore').split('\r\n')[0]
        parts = first_line.split(' ')
        if len(parts) < 2:
            return
        target = parts[1]
        if ':' in target:
            host, port_str = target.split(':', 1)
            port = int(port_str)
        else:
            host = target
            port = 443

        ctx = ssl.create_default_context()
        raw_s = socket.create_connection((host, port), timeout=15)
        tls_s = ctx.wrap_socket(raw_s, server_hostname=host)
        tls_s.settimeout(30)

        c.sendall(b'HTTP/1.0 200 Connection established\r\n\r\n')

        def forward(src, dst):
            try:
                while True:
                    data = src.recv(4096)
                    if not data:
                        break
                    dst.sendall(data)
            except Exception:
                pass
            try:
                dst.shutdown(socket.SHUT_WR)
            except Exception:
                pass

        t1 = threading.Thread(target=forward, args=(c, tls_s), daemon=True)
        t2 = threading.Thread(target=forward, args=(tls_s, c), daemon=True)
        t1.start()
        t2.start()
        t1.join()
        t2.join()
    except Exception:
        pass
    finally:
        try:
            c.close()
        except Exception:
            pass
        if tls_s:
            try:
                tls_s.close()
            except Exception:
                pass

def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        srv.bind(('127.0.0.1', PORT))
    except OSError:
        # Already running
        sys.exit(0)
    srv.listen(32)
    while True:
        try:
            conn, _ = srv.accept()
            threading.Thread(target=handle_client, args=(conn,), daemon=True).start()
        except KeyboardInterrupt:
            break
        except Exception:
            break

if __name__ == '__main__':
    main()

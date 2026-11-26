#!/usr/bin/env python3
#
# Usage: python3 http-relay.py ADDRESS reset
#        python3 http-relay.py ADDRESS UPSTREAM CERT KEY ?notify?
#
# Does for test/http.test what Tcl cannot.  With "reset", listens on
# ADDRESS and resets each connection it accepts.  Otherwise it accepts
# TLS connections on ADDRESS with the certificate CERT and its key KEY,
# relays each request to the HTTP server on port UPSTREAM of 127.0.0.1,
# and sends the reply back.  It ends the TLS session with close_notify
# only when "notify" is given.  It prints "listening PORT" when ready,
# and "SNI: NAME" for the TLS server name each client sends.

import re
import socket
import ssl
import struct
import sys
import threading


# Returns the HTTP request that CLIENT sends, with its body.
def read_request(client):
    data = b""
    while b"\r\n\r\n" not in data:
        part = client.recv(65536)
        if not part:
            return data
        data += part
    header = data.split(b"\r\n\r\n", 1)[0]
    match = re.search(rb"(?im)^content-length:\s*(\d+)", header)
    length = len(header) + 4 + (int(match.group(1)) if match else 0)
    while len(data) < length:
        part = client.recv(65536)
        if not part:
            break
        data += part
    return data


# Serves one TLS connection: sends the request to the server on port
# UPSTREAM and the whole reply back, then ends the TLS session.
def relay(context, client, upstream, notify):
    try:
        client = context.wrap_socket(client, server_side=True)
        request = read_request(client)
        reply = b""
        with socket.create_connection(("127.0.0.1", upstream)) as server:
            server.sendall(request)
            while part := server.recv(65536):
                reply += part
        client.sendall(reply)
        if notify:
            client = client.unwrap()
        else:
            client.shutdown(socket.SHUT_RDWR)
    except OSError:
        pass
    client.close()


# Resets each connection that LISTENER accepts.  Closing a socket whose
# linger time is zero sends a reset.
def reset_connections(listener):
    while True:
        client, _ = listener.accept()
        client.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                          struct.pack("ii", 1, 0))
        client.close()


def print_server_name(tls_socket, name, context):
    print("SNI:", name, flush=True)


# Relays each TLS connection that LISTENER accepts, in a thread of its
# own.
def relay_connections(listener, upstream, cert, key, notify):
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)
    context.sni_callback = print_server_name
    while True:
        client, _ = listener.accept()
        thread = threading.Thread(
            target=relay, args=(context, client, upstream, notify),
            daemon=True)
        thread.start()


def main():
    address = sys.argv[1]
    family = socket.AF_INET6 if ":" in address else socket.AF_INET
    listener = socket.socket(family)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind((address, 0))
    listener.listen(8)
    print("listening", listener.getsockname()[1], flush=True)
    if sys.argv[2] == "reset":
        reset_connections(listener)
    else:
        upstream = int(sys.argv[2])
        cert, key = sys.argv[3:5]
        notify = sys.argv[5:] == ["notify"]
        relay_connections(listener, upstream, cert, key, notify)


main()

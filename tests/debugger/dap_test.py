import json
import os
import re
import socket
import subprocess
import sys


def send(sock, seq, command, arguments=None):
    body = json.dumps({
        "seq": seq,
        "type": "request",
        "command": command,
        "arguments": arguments or {},
    }).encode()
    sock.sendall(f"Content-Length: {len(body)}\r\n\r\n".encode() + body)


def receive(sock):
    header = b""
    while b"\r\n\r\n" not in header:
        header += sock.recv(1)
    length = int(re.search(br"Content-Length:\s*(\d+)", header).group(1))
    body = b""
    while len(body) < length:
        body += sock.recv(length - len(body))
    return json.loads(body)


oak, program = map(os.path.abspath, sys.argv[1:3])
breakpoint_source = program.replace(os.sep, "/")
process = subprocess.Popen(
    [oak, "--debug", "--debug-port", "0", program],
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True,
)
ready = process.stderr.readline().strip()
assert ready.startswith("OAK_DAP_PORT="), ready

with socket.create_connection(("127.0.0.1", int(ready.split("=")[1]))) as sock:
    send(sock, 1, "initialize")
    assert receive(sock)["command"] == "initialize"
    assert receive(sock)["event"] == "initialized"
    send(sock, 2, "launch", {"stopOnEntry": True})
    assert receive(sock)["command"] == "launch"
    send(sock, 3, "setBreakpoints", {
        "source": {"path": breakpoint_source},
        "breakpoints": [{"line": 2}, {"line": 4}],
    })
    breakpoints = receive(sock)["body"]["breakpoints"]
    assert breakpoints[0]["verified"]
    assert not breakpoints[1]["verified"]
    send(sock, 4, "configurationDone")
    assert receive(sock)["command"] == "configurationDone"
    stopped = receive(sock)
    assert stopped["event"] == "stopped"
    assert stopped["body"]["reason"] == "entry"

    send(sock, 5, "stackTrace", {"threadId": 1})
    stack = receive(sock)
    assert stack["body"]["stackFrames"]
    send(sock, 6, "scopes", {"frameId": 0})
    scopes = receive(sock)
    ref = scopes["body"]["scopes"][0]["variablesReference"]
    send(sock, 7, "variables", {"variablesReference": ref})
    assert "variables" in receive(sock)["body"]

    send(sock, 8, "continue", {"threadId": 1})
    assert receive(sock)["command"] == "continue"
    stopped = receive(sock)
    assert stopped["event"] == "stopped"
    assert stopped["body"]["reason"] == "breakpoint"
    send(sock, 9, "continue", {"threadId": 1})
    assert receive(sock)["command"] == "continue"
    assert receive(sock)["event"] == "exited"
    assert receive(sock)["event"] == "terminated"

assert process.wait(timeout=5) == 0

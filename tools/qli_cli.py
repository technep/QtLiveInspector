#!/usr/bin/env python3
"""qli_cli.py — minimal command-line client for the QtLiveInspector probe.

Speaks the newline-delimited JSON protocol directly (no Qt required), which
makes it useful for smoke tests, CI and remote targets without a GUI.

Usage:
    qli_cli.py [--host H] [--port P] [--token T] <command> [args]

Commands:
    hello                        show target info
    tree [--depth N]             print the object tree (indented)
    props TARGET                 print properties of an object
    set TARGET NAME VALUE        write a scalar property
    find QUERY                   search objectName/class for QUERY
    locator TARGET               print Squish locator for an object
    highlight TARGET [--seconds S]   highlight an object briefly
    click TARGET                 click a button/item (synthesized mouse event
                                 for items, animateClick() for widget buttons)
    invoke TARGET METHOD [ARGS...]   call any invokable method/slot or emit
                                 a signal (e.g. clicked) with JSON arguments
    pick                         arm the picker, wait for a pick event
    watch TARGET [--seconds S]   print propertyChanged events

TARGET is either --node <id> (from tree/find) or --name <objectName>
(objectName must exist and be unique, otherwise the probe reports the
conflicting node ids).
"""
import argparse
import json
import socket
import sys
import time

DEFAULT_PORT = 17017


class Client:
    def __init__(self, host, port, token=None, timeout=10.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.buf = b""
        self.next_id = 1
        self.token = token

    def _read_msg(self):
        while b"\n" not in self.buf:
            chunk = self.sock.recv(1 << 16)
            if not chunk:
                raise ConnectionError("probe closed the connection")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return json.loads(line)

    def command(self, cmd, **args):
        rid = self.next_id
        self.next_id += 1
        msg = {"id": rid, "cmd": cmd, **args}
        if self.token:
            msg["token"] = self.token
        self.sock.sendall(json.dumps(msg).encode() + b"\n")
        while True:
            res = self._read_msg()
            if res.get("type") == "event":
                print("event:", json.dumps(res), file=sys.stderr)
                continue
            if res.get("id") == rid:
                return res

    def wait_event(self, name, timeout=None):
        self.sock.settimeout(timeout)
        try:
            while True:
                msg = self._read_msg()
                if msg.get("type") == "event" and msg.get("event") == name:
                    return msg
        except socket.timeout:
            return None


def fmt_node(node):
    name = node.get("name") or ""
    cls = node.get("className") or "?"
    return f"{name} ({cls})" if name else cls


def print_tree(node, depth, max_depth):
    if max_depth is not None and depth > max_depth:
        return
    print("  " * depth + f"[{node.get('id')}] " + fmt_node(node))
    for child in node.get("children", []):
        print_tree(child, depth + 1, max_depth)


def walk(node):
    yield node
    for child in node.get("children", []):
        yield from walk(child)


def add_target(p):
    """Commands that address an object accept either --node or --name."""
    p.add_argument("--node", type=int, default=None,
                   help="object id (from tree/find)")
    p.add_argument("--name", "--object-name", dest="object_name", default=None,
                   help="objectName of the target object (must be unique)")


def target_args(args):
    if getattr(args, "object_name", None):
        return {"objectName": args.object_name}
    if getattr(args, "node", None) is not None:
        return {"node": args.node}
    sys.exit("error: provide --node or --name")


def main():
    ap = argparse.ArgumentParser(description="QtLiveInspector probe CLI client")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--token", default=None)
    sub = ap.add_subparsers(dest="command", required=True)

    sub.add_parser("hello")
    p = sub.add_parser("tree")
    p.add_argument("--depth", type=int, default=None)
    p = sub.add_parser("props")
    add_target(p)
    p = sub.add_parser("set")
    add_target(p)
    p.add_argument("name", help="property name")
    p.add_argument("value")
    p = sub.add_parser("find")
    p.add_argument("query")
    p = sub.add_parser("locator")
    add_target(p)
    p = sub.add_parser("highlight")
    add_target(p)
    p.add_argument("--seconds", type=float, default=3.0)
    sub.add_parser("pick")
    p = sub.add_parser("click")
    add_target(p)
    p = sub.add_parser("invoke")
    add_target(p)
    p.add_argument("method", help="method/slot/signal name, e.g. clicked")
    p.add_argument("args", nargs="*", default=[],
                   help="arguments as JSON scalars (e.g. 42 true '\"text\"')")
    p = sub.add_parser("watch")
    add_target(p)
    p.add_argument("--seconds", type=float, default=10.0)

    args = ap.parse_args()
    cli = Client(args.host, args.port, args.token)

    def check(res):
        if not res.get("ok"):
            sys.exit(f"error: {res.get('error')}")
        return res

    if args.command == "hello":
        print(json.dumps(check(cli.command("hello")), indent=2))

    elif args.command == "tree":
        root = check(cli.command("tree"))["root"]
        print_tree(root, 0, args.depth)

    elif args.command == "props":
        props = check(cli.command("properties", **target_args(args)))["properties"]
        for p in props:
            flags = "".join([
                " W" if p.get("writable") else "",
                " N" if p.get("notify") else "",
                " C" if p.get("constant") else "",
                " D" if p.get("dynamic") else "",
            ])
            print(f"{p['name']:32s} {p['type']:24s} "
                  f"{json.dumps(p.get('value'))}{flags}")

    elif args.command == "set":
        try:
            value = json.loads(args.value)
        except json.JSONDecodeError:
            value = args.value
        res = check(cli.command("setProperty", name=args.name, value=value,
                                **target_args(args)))
        print(json.dumps(res.get("value")))

    elif args.command == "find":
        root = check(cli.command("tree"))["root"]
        q = args.query.lower()
        for n in walk(root):
            if q in (n.get("name") or "").lower() or q in (n.get("className") or "").lower():
                print(f"[{n.get('id')}] {fmt_node(n)}  {n.get('address', '')}")

    elif args.command == "locator":
        print(check(cli.command("locator", **target_args(args)))["locator"])

    elif args.command == "highlight":
        check(cli.command("highlight", **target_args(args)))
        time.sleep(args.seconds)
        check(cli.command("clearHighlight"))

    elif args.command == "pick":
        check(cli.command("pickStart"))
        print("picker armed — click in the target application (Ctrl-C to abort)")
        ev = cli.wait_event("picked")
        if ev:
            print(f"picked node {ev.get('node')}, path {ev.get('path')}")

    elif args.command == "click":
        check(cli.command("click", **target_args(args)))
        print(f"clicked {args.object_name or 'node ' + str(args.node)}")

    elif args.command == "invoke":
        parsed = []
        for a in args.args:
            try:
                parsed.append(json.loads(a))
            except json.JSONDecodeError:
                parsed.append(a)
        check(cli.command("invoke", method=args.method, args=parsed,
                          **target_args(args)))
        print(f"invoked {args.method} on {args.object_name or 'node ' + str(args.node)}")

    elif args.command == "watch":
        res = check(cli.command("watch", **target_args(args)))
        print(f"watching {res.get('watching')} notify signals for "
              f"{args.seconds}s (Ctrl-C to abort)")
        end = time.time() + args.seconds
        while time.time() < end:
            ev = cli.wait_event("propertyChanged", timeout=max(end - time.time(), 0.1))
            if ev is None:
                break
            print(f"{ev.get('property')} = {json.dumps(ev.get('value'))}")
        cli.command("unwatch")


if __name__ == "__main__":
    main()

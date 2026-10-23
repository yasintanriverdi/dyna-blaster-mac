#!/usr/bin/env python3
"""Minimal MCP stdio client for REA, so one Ghidra session can serve many calls.

Usage: rea_session.py OUTDIR CALLS.json
  CALLS.json = [{"name": "tool", "args": {...}, "out": "file.json"}, ...]
"""
import json, os, subprocess, sys

GHIDRA = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ghidra_12.1.4_PUBLIC')


class Rea:
    def __init__(self):
        env = dict(os.environ, GHIDRA_INSTALL_DIR=GHIDRA)
        self.p = subprocess.Popen(['rea', '--mcp'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, env=env, text=True, bufsize=1)
        self.id = 0
        self.rpc('initialize', {'protocolVersion': '2024-11-05', 'capabilities': {},
                                'clientInfo': {'name': 'dyna-re', 'version': '1'}})
        self.send({'jsonrpc': '2.0', 'method': 'notifications/initialized'})

    def send(self, msg):
        self.p.stdin.write(json.dumps(msg) + '\n'); self.p.stdin.flush()

    def rpc(self, method, params):
        self.id += 1
        self.send({'jsonrpc': '2.0', 'id': self.id, 'method': method, 'params': params})
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError('rea exited')
            m = json.loads(line)
            if m.get('id') == self.id:
                return m

    def call(self, name, args):
        r = self.rpc('tools/call', {'name': name, 'arguments': args})
        if 'error' in r:
            return {'error': r['error']}
        res = r['result']
        texts = [c.get('text', '') for c in res.get('content', [])]
        try:
            return json.loads(texts[0]) if len(texts) == 1 else texts
        except Exception:
            return texts


if __name__ == '__main__':
    outdir, calls = sys.argv[1], json.load(open(sys.argv[2]))
    os.makedirs(outdir, exist_ok=True)
    r = Rea()
    for c in calls:
        res = r.call(c['name'], c.get('args', {}))
        with open(os.path.join(outdir, c['out']), 'w') as f:
            json.dump(res, f, indent=1)
        print(c['name'], '->', c['out'], len(json.dumps(res)), flush=True)

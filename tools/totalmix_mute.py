"""Mute or unmute the Mac's playback channel (Software Playback 1/2) in TotalMix FX over OSC,
so nothing jumps in level while SoundSource (which owns the software volume) restarts.
Hardware inputs are not touched. Needs TotalMix's OSC remote enabled (port 7001, replies to 9001).

usage: totalmix_mute.py status|on|off      exit code 0 when the requested state is confirmed"""
import socket, struct, sys, time

HOST, SEND, RECV = '127.0.0.1', 7001, 9001


def pad(b):
    return b + b'\0' * (4 - len(b) % 4)


def msg(addr, val):
    return pad(addr.encode()) + pad(b',f') + struct.pack('>f', val)


def parse(d):
    addr = d[:d.index(b'\0')].decode()
    i = (len(addr) // 4 + 1) * 4
    tags = d[i:d.index(b'\0', i)].decode()
    i += (len(tags) // 4 + 1) * 4
    vals = []
    for t in tags[1:]:
        if t == 'f':
            vals.append(struct.unpack('>f', d[i:i + 4])[0]); i += 4
        elif t == 's':
            e = d.index(b'\0', i); vals.append(d[i:e].decode('latin1')); i = (e // 4 + 1) * 4
        elif t == 'i':
            vals.append(struct.unpack('>i', d[i:i + 4])[0]); i += 4
    return addr, vals


def messages(d):
    if d.startswith(b'#bundle'):
        i = 16
        while i < len(d):
            n = struct.unpack('>i', d[i:i + 4])[0]
            yield from messages(d[i + 4:i + 4 + n])
            i += 4 + n
    else:
        yield d


class TotalMix:
    def __init__(self):
        self.rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.rx.bind((HOST, RECV))
        self.rx.settimeout(0.2)
        self.tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.state = {}

    def send(self, addr, val):
        self.tx.sendto(msg(addr, val), (HOST, SEND))
        time.sleep(0.15)

    def drain(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            try:
                d = self.rx.recv(65535)
            except socket.timeout:
                continue
            for m in messages(d):
                try:
                    a, v = parse(m)
                    self.state[a] = v[0] if len(v) == 1 else v
                except Exception:
                    pass

    def select_playback_1_2(self):
        self.state.clear()
        self.send('/2/busPlayback', 1.0)
        self.send('/setBankStart', 0.0)
        self.drain(1.5)
        return self.state.get('/2/trackname'), self.state.get('/2/mute')


def main():
    want = sys.argv[1] if len(sys.argv) > 1 else 'status'
    tm = TotalMix()
    name, muted = tm.select_playback_1_2()
    if muted is None:
        sys.exit('no reply from TotalMix OSC (is OSC enabled?)')
    print(f'playback {name}: {"muted" if muted == 1.0 else "unmuted"}')
    if want == 'status':
        return
    target = 1.0 if want == 'on' else 0.0
    if muted != target:
        tm.send('/2/mute', 1.0)  # toggle
        name, muted = tm.select_playback_1_2()
        print(f'playback {name}: {"muted" if muted == 1.0 else "unmuted"}')
    sys.exit(0 if muted == target else 1)


if __name__ == '__main__':
    main()

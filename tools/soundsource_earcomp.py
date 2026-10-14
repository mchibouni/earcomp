"""Point SoundSource's saved EarComp effect(s) at the installed EarComp build and optionally
apply one of its presets. Run only while SoundSource is not running (tools/stage.sh does that).

usage: soundsource_earcomp.py [preset-number]
Keeps a backup of the model file next to it (Models.v2.plist.earcomp-backup)."""
import pathlib, plistlib, shutil, struct, sys

MODEL = pathlib.Path.home() / 'Library/Application Support/SoundSource/Models.v2.plist'
TYPE, SUBTYPE, MANUFACTURER = 'aufx', 'Ercp', 'EarC'
UID = f'{TYPE}.{MANUFACTURER}.{SUBTYPE}'

# Mirrors kPresets / kPresetValues in src/EarCompAU.cpp: (name, intensity, variant, right treble, hiss guard)
PRESETS = [
    ('Mimi match', 100, 0, 0, -100),
    ('Speakers: Mimi match, hiss guard', 100, 0, 0, -60),
    ('Right ear +6 dB', 100, 0, 6, -100),
    ('Headphones: right ear +6 dB, hiss guard', 100, 0, 6, -60),
    ('Right ear +10 dB', 100, 0, 10, -100),
    ('Headphones: right ear +10 dB, hiss guard', 100, 0, 10, -60),
]


def fourcc(s):
    return struct.unpack('>I', s.encode())[0]


def param_blob(values):
    # SoundSource's auSettings data: scope, element, count, then (parameter id, float value) pairs
    out = struct.pack('>3I', 0, 0, len(values))
    for pid, v in enumerate(values):
        out += struct.pack('>If', pid, v)
    return out


def earcomp_entries(o):
    if isinstance(o, dict):
        node = o.get('nodeArchive', {}).get('graphNode', {})
        if node.get('auDescriptorUID', '').startswith(TYPE + '.') and node['auDescriptorUID'].endswith('.' + SUBTYPE):
            yield o
        for v in o.values():
            yield from earcomp_entries(v)
    elif isinstance(o, list):
        for v in o:
            yield from earcomp_entries(v)


def main():
    preset = int(sys.argv[1]) if len(sys.argv) > 1 else None
    data = plistlib.loads(MODEL.read_bytes())
    entries = list(earcomp_entries(data))
    if not entries:
        sys.exit('no EarComp effect in SoundSource yet: add it once with Add Effect -> EarComp -> EarComp')
    shutil.copy2(MODEL, MODEL.with_name(MODEL.name + '.earcomp-backup'))
    for e in entries:
        node = e['nodeArchive']['graphNode']
        old = node['auDescriptorUID']
        e['nodeArchive']['descriptorUID'] = UID
        node['auDescriptorUID'] = UID
        node['hostServiceLabel'] = node['hostServiceLabel'].replace(old, UID)
        st = node['auSettings']
        st['type'], st['subtype'], st['manufacturer'] = fourcc(TYPE), fourcc(SUBTYPE), fourcc(MANUFACTURER)
        if preset is not None:
            name, *values = PRESETS[preset]
            st['name'] = name
            st['data'] = param_blob([float(v) for v in values])
        print(f'{old} -> {UID}' + (f', preset "{st["name"]}"' if preset is not None else ''))
    MODEL.write_bytes(plistlib.dumps(data, fmt=plistlib.FMT_XML))


if __name__ == '__main__':
    main()

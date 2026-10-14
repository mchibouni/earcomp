#!/bin/sh
# Build EarComp, install it, and make SoundSource load the new build on its output.
# SoundSource only picks up a new plugin binary when it reloads the effect, so this quits it,
# points its saved EarComp effect at the installed build (optionally applying a preset),
# relaunches it and reads the effect chain back through Hammerspoon.
# While SoundSource is down its software volume is gone, so the Mac's playback channel is muted
# in TotalMix (OSC) for the duration and unmuted on exit, even on errors.
#
# usage: tools/stage.sh [preset-number]
#   0 Mimi match                          3 Headphones: right ear +6 dB, hiss guard
#   1 Speakers: Mimi match, hiss guard    4 Right ear +10 dB
#   2 Right ear +6 dB                     5 Headphones: right ear +10 dB, hiss guard
# Needs: the EarComp effect added once by hand in SoundSource, TotalMix OSC, and Hammerspoon.
set -eu
cd "$(dirname "$0")/.."

make -s

relaunch_and_unmute() {
    # never unmute without SoundSource running: it owns the software volume
    if ! pgrep -x SoundSource >/dev/null; then
        env -i /usr/bin/open -g -b com.rogueamoeba.soundsource
        sleep 4
    fi
    python3 -I tools/totalmix_mute.py off
}

python3 -I tools/totalmix_mute.py on || { echo "could not mute playback in TotalMix, not touching SoundSource"; exit 1; }
trap relaunch_and_unmute EXIT
# watchdog: if anything below stalls, restore SoundSource and sound after 60 s regardless
( sleep 60; relaunch_and_unmute ) >/dev/null 2>&1 &
watchdog=$!

# quit SoundSource before swapping the plugin, otherwise it alerts that the component vanished.
# It cancels scripted quit requests, so send SIGTERM; it saves its chain as you edit it anyway.
pkill -TERM -x SoundSource || true
i=0
while pgrep -x SoundSource >/dev/null; do
    i=$((i + 1)); [ $i -gt 50 ] && { echo "SoundSource did not quit"; exit 1; }
    sleep 0.2
done

# SoundSource hosts plugins in RemoteAUHost processes that can outlive it: stop the one running
# EarComp, or the old binary stays loaded
for pid in $(pgrep -f RemoteAUHost); do
    if lsof -p "$pid" 2>/dev/null | grep -q 'EarComp.component/Contents/MacOS/EarComp'; then
        kill -TERM "$pid" && echo "stopped plugin host $pid"
    fi
done

DEST="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$DEST"
rm -rf "$DEST/EarComp.component"
cp -R build/EarComp.component "$DEST/"
echo "installed $DEST/EarComp.component"

python3 -I tools/soundsource_earcomp.py "$@"

env -i /usr/bin/open -g -b com.rogueamoeba.soundsource
sleep 7

out=$(mktemp)
# hs -c can hang now and then: run it in the background and give up after a few seconds
( hs -c "return dofile(\"$PWD/tools/soundsource-check.lua\")(\"$out\")" </dev/null >/dev/null 2>&1 & hp=$!; sleep 5; kill $hp 2>/dev/null ) || true
echo "SoundSource output effects (name | enabled | preset):"
cat "$out"
rm -f "$out"
kill $watchdog 2>/dev/null || true

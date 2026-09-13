#!/bin/sh
# Turn an exported animation GIF into a pet_frame_t table.
#
#   ./decode.sh happy.gif happy           # prints the C table
#   ./decode.sh happy.gif happy --check   # ... plus the render check
#   ./decode.sh --all ../FaceAnimations   # every GIF in a folder, checked
#
# Reading a GIF directly only ever yields its first frame, so ffmpeg splits it.
# The frames are rotated counter-clockwise back to the watch's native layout,
# where the geometry in decode.py knows where every cell sits.
#
# -fps_mode passthrough matters: without it ffmpeg resamples the GIF to a
# constant rate and silently duplicates frames -- our 16-frame files came out
# as 48 -- which stretches every hold in the decoded table.
#
# On the PC neither shell has the whole toolchain -- ffmpeg is installed on the
# Windows side and python is in WSL -- so run this from WSL and it will reach
# across for ffmpeg.exe. On the Mac everything is native.
set -e

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if command -v ffmpeg >/dev/null 2>&1; then
    FFMPEG=ffmpeg; FFPROBE=ffprobe
    winpath() { printf '%s' "$1"; }
    DIR=$(mktemp -d)
elif command -v ffmpeg.exe >/dev/null 2>&1; then
    # Windows ffmpeg from WSL: it needs Windows paths, and it cannot see /tmp,
    # so the scratch directory has to live on a drive both sides share.
    FFMPEG=ffmpeg.exe; FFPROBE=ffprobe.exe
    winpath() { wslpath -w "$1"; }
    DIR=$(mktemp -d -p "$HERE")
else
    echo "$0: needs ffmpeg (or ffmpeg.exe reachable from WSL)" >&2
    exit 1
fi
trap 'rm -rf "$DIR"' EXIT

# What the file actually holds, which is what passthrough gives back.
nframes() {
    $FFPROBE -v error -select_streams v:0 -count_frames \
             -show_entries stream=nb_read_frames -of csv=p=0 "$(winpath "$1")" | tr -d '\r'
}

# transpose=2 is 90 degrees counter-clockwise, undoing how the art is drawn.
one() {
    gif="$1"; name="$2"; shift 2
    frames=$(nframes "$gif")
    echo "// $(basename "$gif"): $frames frames" >&2
    $FFMPEG -v error -y -i "$(winpath "$gif")" -fps_mode passthrough \
            -vf transpose=2 -pix_fmt gray -f rawvideo "$(winpath "$DIR/native.gray")"
    python3 "$HERE/decode.py" "$DIR/native.gray" "$frames" "$name" "$@"
}

# Strip the export's common prefix and lower-case what is left, so
# CasioPet_Animations-Play-1.gif becomes play_1.
autoname() {
    basename "$1" .gif | sed 's/^.*Animations-//' | tr 'A-Z-' 'a-z_'
}

if [ "$1" = "--all" ]; then
    ART="${2:-$HERE/../FaceAnimations}"
    [ -d "$ART" ] || { echo "$0: no such directory: $ART" >&2; exit 1; }
    for f in "$ART"/*.gif; do
        [ -e "$f" ] || { echo "$0: no GIFs in $ART" >&2; exit 1; }
        one "$f" "$(autoname "$f")" --check
        echo
    done
    exit 0
fi

GIF="$1"
[ -n "$GIF" ] || { echo "usage: $0 <anim.gif> [name] [--check]  |  $0 --all [dir]" >&2; exit 1; }
NAME="${2:-$(autoname "$GIF")}"
case "$NAME" in --*) NAME=$(autoname "$GIF") ;; esac
CHECK=""
for a in "$@"; do [ "$a" = "--check" ] && CHECK="--check"; done

one "$GIF" "$NAME" $CHECK

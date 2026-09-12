#!/bin/sh
# Turn an exported animation GIF into a pet_frame_t table.
#
#   ./decode.sh happy.gif happy           # prints the C table
#   ./decode.sh happy.gif happy --check   # ... and the tied-segment report
#
# Reading a GIF directly only ever yields its first frame, so ffmpeg splits it.
# The frames are rotated counter-clockwise back to the watch's native layout,
# where the calibration in decode.py knows where every cell sits.
#
# On the PC neither shell has the whole toolchain -- ffmpeg is installed on the
# Windows side, the compiler and python are in WSL -- so run this from WSL and
# it will reach across for ffmpeg.exe. On the Mac everything is native.
set -e

GIF="$1"
[ -n "$GIF" ] || { echo "usage: $0 <anim.gif> [name] [--check]" >&2; exit 1; }
NAME="${2:-anim}"
case "$NAME" in --*) NAME="anim" ;; esac
CHECK=""
for a in "$@"; do [ "$a" = "--check" ] && CHECK="--check"; done

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

FRAMES=$($FFPROBE -v error -select_streams v:0 -count_frames \
         -show_entries stream=nb_read_frames -of csv=p=0 "$(winpath "$GIF")" | tr -d '\r')
echo "// $GIF: $FRAMES frames" >&2

# transpose=2 is 90 degrees counter-clockwise, undoing how the art is drawn.
$FFMPEG -v error -i "$(winpath "$GIF")" -vf transpose=2 \
        -pix_fmt gray -f rawvideo "$(winpath "$DIR/native.gray")"

cc -O2 -o "$DIR/segmap" "$HERE/segmap.c"
"$DIR/segmap" "$DIR/native.gray" "$FRAMES" > "$DIR/segs.txt" 2>/dev/null

python3 "$HERE/decode.py" "$DIR/segs.txt" "$DIR/native.gray" "$FRAMES" "$NAME" $CHECK

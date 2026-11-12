#!/usr/bin/env bash
set -euo pipefail

# Run relative to the script so inputs work from any calling directory.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

# Keep the largest detected picture area to avoid cropping into dark scenes.
# Remove baked-in letterboxing, then stretch the active picture to the LCD.
detect_crop() {
  local log line crop="" area best_area=0
  log=$(ffmpeg -hide_banner "$@" -vf "cropdetect=24:2:0" -an -f null - 2>&1) || {
    printf '%s\n' "$log" >&2
    return 1
  }
  while IFS= read -r line; do
    if [[ $line =~ crop=([0-9]+):([0-9]+):([0-9]+):([0-9]+) ]]; then
      area=$((BASH_REMATCH[1] * BASH_REMATCH[2]))
      if ((area > best_area)); then
        best_area=$area
        crop="${BASH_REMATCH[0]}"
      fi
    fi
  done <<< "$log"
  if [[ -z $crop ]]; then
    printf 'Could not detect the active picture area. Check the input.\n' >&2
    return 1
  fi
  printf '%s\n' "$crop"
}

intro_crop=$(detect_crop -loop 1 -framerate 10 -t 1 -i "$SCRIPT_DIR/crysis_meme.jpg")
# This source changes letterboxing between scenes. Full-frame shots and overlays
# defeat a single automatic crop. Remove the observed ~260-pixel bars from both
# edges of the 2160-line source, preserving its full width, then stretch to LCD.
# Override for another source, e.g. VIDEO_CROP=crop=iw:ih:0:0 bash makemjpg.sh.
video_crop=${VIDEO_CROP:-crop=iw:ih-520:0:260}
printf 'Meme: %s\nVideo: %s\n' "$intro_crop" "$video_crop"

ffmpeg \
  -loop 1 -framerate 10 -t 3 -i "$SCRIPT_DIR/crysis_meme.jpg" \
  -ss 7 -i "$SCRIPT_DIR/source.mp4" \
  -filter_complex \
  "[0:v]${intro_crop},scale=320:240,setsar=1,fps=10,format=yuvj420p,setpts=PTS-STARTPTS[intro]; \
   [1:v]${video_crop},scale=320:240,setsar=1,fps=10,format=yuvj420p,setpts=PTS-STARTPTS[video]; \
   [intro][video]concat=n=2:v=1:a=0[out]" \
  -map "[out]" -an \
  -c:v mjpeg -q:v 5 -f mjpeg "$SCRIPT_DIR/VIDEO.MJP"

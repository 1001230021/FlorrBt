"""逐帧测量参考视频的面板色块；只输出分析数据，不复制素材到客户端。"""
import argparse
import csv
import json
import subprocess
from pathlib import Path
import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument("video", type=Path)
parser.add_argument("--output", type=Path, default=Path("artifacts/ui-motion"))
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
timestamps = json.loads(subprocess.check_output(["ffprobe", "-v", "error", "-select_streams", "v:0",
    "-show_entries", "frame=best_effort_timestamp_time", "-of", "json", str(args.video)]))["frames"]
colors = {"inventory": (89, 158, 219), "craft": (218, 155, 88),
          "talents": (214, 85, 88), "gallery": (219, 214, 74), "settings": (170, 170, 170)}
process = subprocess.Popen(["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", str(args.video),
    "-map", "0:v:0", "-an", "-vf", "crop=780:884:0:144,scale=195:221", "-fps_mode", "passthrough",
    "-f", "rawvideo", "-pix_fmt", "rgb24", "-"], stdout=subprocess.PIPE)
rows = []; index = 0
while True:
    raw = process.stdout.read(195 * 221 * 3)
    if not raw: break
    if len(raw) != 195 * 221 * 3: raise RuntimeError("Partial decoded frame")
    frame = np.frombuffer(raw, dtype=np.uint8).reshape(221, 195, 3).astype(np.int16)
    for name, color in colors.items():
        mask = np.max(np.abs(frame - color), axis=2) <= 9
        # 排除快捷按钮；记录实色区域，抗锯齿/文字/卡片不影响外轮廓。
        mask[:16] = False
        ys, xs = np.where(mask)
        if len(xs) < 200: continue
        rows.append(dict(frame=index, seconds=float(timestamps[index]["best_effort_timestamp_time"]), panel=name, pixels=int(len(xs)),
            x=int(xs.min())*4, y=int(ys.min())*4, right=(int(xs.max())+1)*4, bottom=(int(ys.max())+1)*4))
    index += 1
if process.wait() != 0: raise RuntimeError("Video decode failed")
with (args.output/"panel-frames.csv").open("w", newline="", encoding="utf8") as out:
    writer = csv.DictWriter(out, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
segments = []
for name in colors:
    samples = [r for r in rows if r['panel'] == name]
    group = []
    for row in samples:
        if group and row['frame'] > group[-1]['frame'] + 3:
            if len(group) > 12: segments.append(dict(panel=name,start=group[0]['seconds'],end=group[-1]['seconds'],
                opening=group[:24],closing=group[-24:]))
            group = []
        group.append(row)
    if len(group) > 12: segments.append(dict(panel=name,start=group[0]['seconds'],end=group[-1]['seconds'],opening=group[:24],closing=group[-24:]))
(args.output/"panel-segments.json").write_text(json.dumps(dict(frames=index, clock="source PTS", segments=segments),indent=2),encoding="utf8")
print(f"Analyzed {index} frames using source presentation timestamps.")
for s in segments: print(s['panel'],s['start'],s['end'])

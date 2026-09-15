import glob
import os

import cv2
import numpy as np

# Template: buttons + header strip above the OLED in frame 1 (native 776x606)
TPL_X, TPL_Y, TPL_W, TPL_H = 240, 110, 300, 90
# Crop window relative to the template's top-left, at scale 1.0
CROP_DX, CROP_DY, CROP_W, CROP_H = -45, -35, 390, 500
SCALES = np.arange(0.80, 1.30, 0.02)

frames = sorted(glob.glob(os.environ["WORK"] + "/frames/*.png"))
first = cv2.imread(frames[0])
template = first[TPL_Y : TPL_Y + TPL_H, TPL_X : TPL_X + TPL_W]
scaled_templates = [
    (s, cv2.resize(template, None, fx=s, fy=s, interpolation=cv2.INTER_AREA)) for s in SCALES
]

track = []
for path in frames:
    img = cv2.imread(path)
    best = None
    for s, tpl in scaled_templates:
        result = cv2.matchTemplate(img, tpl, cv2.TM_CCOEFF_NORMED)
        _, score, _, (tx, ty) = cv2.minMaxLoc(result)
        if best is None or score > best[3]:
            best = (tx, ty, s, score)
    track.append(best)

# Smooth translation and scale so matcher jitter doesn't show
def smooth(values):
    kernel = np.ones(7) / 7
    return np.convolve(np.pad(np.array(values, dtype=float), 3, mode="edge"), kernel, mode="valid")

xs = smooth([t[0] for t in track])
ys = smooth([t[1] for t in track])
ss = smooth([t[2] for t in track])

for i, path in enumerate(frames):
    img = cv2.imread(path)
    s = ss[i]
    x0 = xs[i] + CROP_DX * s
    y0 = ys[i] + CROP_DY * s
    w = CROP_W * s
    h = CROP_H * s
    assert 0 <= x0 and x0 + w <= img.shape[1], (i, x0, w)
    assert 0 <= y0 and y0 + h <= img.shape[0], (i, y0, h)
    # Sub-pixel crop + rescale to the fixed output size in one warp
    matrix = np.array([[1 / s, 0, -x0 / s], [0, 1 / s, -y0 / s]], dtype=np.float32)
    crop = cv2.warpAffine(img, matrix, (CROP_W, CROP_H), flags=cv2.INTER_AREA)
    cv2.imwrite(f"{os.environ['WORK']}/out/{i + 1:04d}.png", crop)

scores = [t[3] for t in track]
print(
    f"frames={len(frames)} x={xs.min():.0f}..{xs.max():.0f} y={ys.min():.0f}..{ys.max():.0f} "
    f"scale={ss.min():.2f}..{ss.max():.2f} score_min={min(scores):.2f}"
)

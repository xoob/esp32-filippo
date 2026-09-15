import glob
import os

import cv2
import numpy as np

# Template: buttons + header strip above the OLED in frame 1 (native 776x606)
TPL_X, TPL_Y, TPL_W, TPL_H = 240, 110, 300, 90
# Crop window relative to the template's top-left, at scale 1.0
CROP_DX, CROP_DY, CROP_W, CROP_H = -45, -35, 390, 500
SCALES = np.arange(0.88, 1.08, 0.01)

work = os.environ["WORK"]
frames = sorted(glob.glob(work + "/frames/*.png"))
first = cv2.imread(frames[0], cv2.IMREAD_GRAYSCALE)
template = first[TPL_Y : TPL_Y + TPL_H, TPL_X : TPL_X + TPL_W]
scaled_templates = [
    cv2.resize(template, None, fx=s, fy=s, interpolation=cv2.INTER_AREA) for s in SCALES
]


def parabolic_peak(left, peak, right):
    """Sub-sample offset of the true maximum given three samples around a discrete peak."""
    denom = left - 2 * peak + right
    return 0.0 if denom == 0 else 0.5 * (left - right) / denom


def locate(gray):
    """Sub-pixel template position and scale in one frame. Each frame is matched
    against the reference frame, not its predecessor, so there is no drift."""
    results = [cv2.matchTemplate(gray, tpl, cv2.TM_CCOEFF_NORMED) for tpl in scaled_templates]
    peaks = [cv2.minMaxLoc(r) for r in results]
    k = int(np.argmax([p[1] for p in peaks]))
    assert 0 < k < len(SCALES) - 1, f"scale {SCALES[k]:.2f} hit the search boundary"
    score = peaks[k][1]
    tx, ty = peaks[k][3]
    r = results[k]
    assert 0 < tx < r.shape[1] - 1 and 0 < ty < r.shape[0] - 1, (tx, ty)
    dx = parabolic_peak(r[ty, tx - 1], r[ty, tx], r[ty, tx + 1])
    dy = parabolic_peak(r[ty - 1, tx], r[ty, tx], r[ty + 1, tx])
    ds = parabolic_peak(peaks[k - 1][1], score, peaks[k + 1][1])
    scale_step = SCALES[1] - SCALES[0]
    return tx + dx, ty + dy, SCALES[k] + ds * scale_step, score


track = [locate(cv2.imread(path, cv2.IMREAD_GRAYSCALE)) for path in frames]

for i, path in enumerate(frames):
    img = cv2.imread(path)
    x, y, s, _ = track[i]
    x0 = x + CROP_DX * s
    y0 = y + CROP_DY * s
    assert 0 <= x0 and x0 + CROP_W * s <= img.shape[1], (i, x0)
    assert 0 <= y0 and y0 + CROP_H * s <= img.shape[0], (i, y0)
    # Sub-pixel crop + rescale to the fixed output size in one warp
    matrix = np.array([[1 / s, 0, -x0 / s], [0, 1 / s, -y0 / s]], dtype=np.float32)
    crop = cv2.warpAffine(img, matrix, (CROP_W, CROP_H), flags=cv2.INTER_CUBIC)
    cv2.imwrite(f"{work}/out/{i + 1:04d}.png", crop)

xs, ys, ss, scores = (np.array(v) for v in zip(*track))
print(
    f"frames={len(frames)} x={xs.min():.1f}..{xs.max():.1f} y={ys.min():.1f}..{ys.max():.1f} "
    f"scale={ss.min():.3f}..{ss.max():.3f} score_min={scores.min():.2f}"
)

# Self-check: the template must sit at the same spot in every output frame
residual = []
for i in range(len(frames)):
    out = cv2.imread(f"{work}/out/{i + 1:04d}.png", cv2.IMREAD_GRAYSCALE)
    r = cv2.matchTemplate(out, template, cv2.TM_CCOEFF_NORMED)
    _, _, _, (tx, ty) = cv2.minMaxLoc(r)
    dx = parabolic_peak(r[ty, tx - 1], r[ty, tx], r[ty, tx + 1])
    dy = parabolic_peak(r[ty - 1, tx], r[ty, tx], r[ty + 1, tx])
    residual.append((tx + dx, ty + dy))
residual = np.array(residual) - residual[0]
print(f"residual px: max={np.abs(residual).max():.2f} std={residual.std(axis=0).round(2)}")

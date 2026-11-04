"""Rebuild the teaching SVGs embedded by build_learning_guide.py (matplotlib)."""
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

OUT = Path(__file__).resolve().parents[1] / "docs/figures"
OUT.mkdir(exist_ok=True)
plt.rcParams.update({"font.family": "Microsoft YaHei", "font.size": 10,
                     "svg.fonttype": "path", "svg.hashsalt": "medvessel-guide-v1", "axes.unicode_minus": False})


def save(fig, name):
    destination = OUT / name
    fig.savefig(destination, format="svg", bbox_inches="tight", metadata={"Date": None})
    destination.write_text("\n".join(line.rstrip() for line in destination.read_text(encoding="utf-8").splitlines()) + "\n", encoding="utf-8")
    plt.close(fig)


x = np.linspace(-3.5, 3.5, 701)
e = np.exp(-x*x/2)
fig, axes = plt.subplots(3, 1, figsize=(8.4, 7), sharex=True, layout="constrained")
curves = [1-e, x*e, (1-x*x)*e]
names = ["原始灰度 I(x) = 1 − exp(−x²/2)", "一阶导数 I′(x) = x exp(−x²/2)",
         "二阶导数 I″(x) = (1 − x²) exp(−x²/2)"]
for ax, y, name in zip(axes, curves, names):
    ax.plot(x, y, color="#176e59", lw=2.3)
    ax.axhline(0, color="#82928b", lw=.7)
    ax.axvline(0, color="#4d5d54", ls="--", lw=.8)
    ax.set_title(name, loc="left", fontsize=11)
    ax.set_xlim(-3.5, 3.5)
    ax.grid(alpha=.12)
    ax.spines[["top", "right"]].set_visible(False)
axes[0].annotate("暗线中心：局部极小值", xy=(0, 0), xytext=(.6, .24),
                 arrowprops={"arrowstyle": "->", "color": "#61746e"})
axes[1].fill_between(x, curves[1], 0, where=x<0, color="#d88838", alpha=.18)
axes[1].fill_between(x, curves[1], 0, where=x>0, color="#176e59", alpha=.15)
axes[1].text(-2.3, -.38, "左侧为负")
axes[1].text(1.7, .34, "右侧为正")
axes[1].annotate("中心为 0", xy=(0, 0), xytext=(.4, -.43), arrowprops={"arrowstyle": "->"})
axes[2].scatter([-1, 0, 1], [0, 1, 0], color="#b36b25", zorder=4)
axes[2].annotate("中心为正：I″(0)=1", xy=(0, 1), xytext=(.6, .65), arrowprops={"arrowstyle": "->"})
for value in [-1, 1]:
    axes[2].annotate(f"x={value} 过零", xy=(value, 0), xytext=(value*1.6, -.5),
                     arrowprops={"arrowstyle": "->"}, ha="center")
axes[2].set_xlabel("横穿暗线的相对位置 x（同一横坐标；归一化教学剖面）")
save(fig, "enhancement-derivatives.svg")

fig, ax = plt.subplots(figsize=(8.4, 3.5), layout="constrained")
t = np.linspace(-3.2, 3.2, 301)
ax.plot(t, .65*np.sin(.9*t), color="#171d19", lw=20, solid_capstyle="round")
for pos in [-2, 0, 2]:
    center = np.array([pos, .65*np.sin(.9*pos)])
    tangent = np.array([1., .585*np.cos(.9*pos)])
    tangent /= np.linalg.norm(tangent)
    normal = np.array([-tangent[1], tangent[0]])
    ax.scatter(*center, color="white", s=14, zorder=4)
    for vector, color in [(tangent, "#007c8c"), (normal, "#c16a20")]:
        ax.annotate("", xy=center+.85*vector, xytext=center-.85*vector,
                    arrowprops={"arrowstyle": "<->", "color": color, "lw": 2.3})
ax.plot([], [], color="#007c8c", lw=2, label=r"$v_1$：局部切线，$\lambda_1\approx0$")
ax.plot([], [], color="#c16a20", lw=2, label=r"$v_2$：局部法线，$\lambda_2>0$")
ax.legend(loc="upper center", ncol=2, frameon=False)
ax.text(0, -1.8, "白背景上的暗色弯管：沿线灰度近似不变，横穿管腔先变暗再变亮", ha="center")
ax.set(xlim=(-3.6, 3.6), ylim=(-2.05, 1.85), aspect="equal")
ax.axis("off")
save(fig, "enhancement-eigenvectors.svg")

fig, ax = plt.subplots(figsize=(8.4, 3.4), layout="constrained")
sigma = np.linspace(.05, 3.6, 450)
for width, color, label in [(.35, "#176e59", "较细的理想二维亮线"), (1.2, "#b56a25", "较粗的理想二维亮线")]:
    # Magnitude of the scale-normalized transverse Hessian eigenvalue at a
    # Gaussian ridge's centre after smoothing; not a measured vessel radius.
    response = width*sigma**2/(width**2+sigma**2)**1.5
    response /= response.max()
    ax.plot(sigma, response, color=color, lw=2.2, label=label)
    best = np.sqrt(2)*width
    ax.axvline(best, color=color, ls=":", alpha=.6)
ax.set(xlabel="Gaussian 尺度 σ（与教学剖面宽度使用同一单位）",
       ylabel="归一化二阶响应幅值", xlim=(0, 3.6), ylim=(0, 1.12))
ax.legend(frameon=False, loc="lower right")
ax.spines[["top", "right"]].set_visible(False)
ax.grid(alpha=.15)
save(fig, "enhancement-scales.svg")
print("Built 3 enhancement teaching SVGs")

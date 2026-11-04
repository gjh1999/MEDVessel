"""Export presentation figures from existing MEDVessel outputs; no segmentation is changed."""
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import font_manager
from matplotlib.lines import Line2D
import numpy as np

from plot_reference import read


def main():
    root = Path(__file__).resolve().parents[1]
    output = root / 'artifacts/ppt-pipeline'
    output.mkdir(parents=True, exist_ok=True)
    font_manager.fontManager.addfont('C:/Windows/Fonts/msyh.ttc')
    plt.rcParams.update({'font.family': 'Microsoft YaHei', 'axes.unicode_minus': False,
                         'font.size': 15, 'savefig.facecolor': 'white'})
    values = {name: read(root / 'build/imagecas-x-plots' / name)[0]
              for name in ['source', 'vesselness', 'mask', 'skeleton', 'reference']}
    meta = read(root / 'build/imagecas-x-plots/source')[1]
    roi = json.loads((root / 'build/imagecas-x-case1/roi.json').read_text())
    metrics = json.loads((root / 'build/imagecas-x-case1/result/metrics.json').read_text())
    z = roi['seed_voxel'][2]
    sx, sy, _ = meta['spacing_mm']
    height, width = values['source'].shape[1:]
    extent = (0, width*sx, height*sy, 0)
    files = []

    def image(ax, data, cmap='gray', low=0, high=1):
        ax.imshow(data, cmap=cmap, vmin=low, vmax=high, interpolation='nearest', extent=extent)
        ax.set_aspect('equal')
        ax.set_axis_off()

    def save(fig, name):
        path = output / name
        fig.savefig(path, dpi=200)
        plt.close(fig)
        files.append(name)

    for view in ['mip', 'slice']:
        planes = {name: data.max(axis=0) if view == 'mip' else data[z]
                  for name, data in values.items()}
        label = 'Z 方向最大强度投影 / 二值结果取并集' if view == 'mip' else f'同一 Z 切片 · ROI 索引 {z}'
        titles = ['01  原始 CTA', '02  Frangi 增强', '03  血管分割 Mask', '04  骨架叠加']

        def draw_stage(ax, stage):
            if stage == 0:
                image(ax, planes['source'], low=-100, high=600)
            elif stage == 1:
                image(ax, planes['vesselness'], cmap='magma')
            elif stage == 2:
                image(ax, planes['mask'] > 0)
            else:
                image(ax, planes['source'], low=-100, high=600)
                overlay = np.zeros((height, width, 4))
                overlay[planes['mask'] > 0] = [1, .48, .12, .30]
                ax.imshow(overlay, extent=extent, interpolation='nearest')
                yy, xx = np.where(planes['skeleton'] > 0)
                ax.scatter((xx+.5)*sx, (yy+.5)*sy, c='#00dbe8', s=6, linewidths=0)

        fig, axes = plt.subplots(1, 4, figsize=(16, 9))
        fig.subplots_adjust(left=.035, right=.965, top=.77, bottom=.20, wspace=.13)
        fig.text(.04, .91, '血管分析的中间过程', fontsize=30, weight='bold', color='#173c42')
        fig.text(.04, .85, f'ImageCAS 病例 1  |  {label}', fontsize=16, color='#52666b')
        for i, ax in enumerate(axes):
            draw_stage(ax, i)
            ax.set_title(titles[i], fontsize=17, pad=18)
        fig.text(.04, .14, '原始灰度 → 管状结构响应 → 连通区域分割 → 拓扑细化骨架', fontsize=20, color='#173c42')
        fig.text(.04, .075, '参考标注辅助的局部开发 ROI；非完整冠脉、非独立测试。骨架为体素级结果。', fontsize=12, color='#65777b')
        save(fig, f'pipeline_{view}_16x9.png')

        for i, name in enumerate(['source', 'vesselness', 'mask', 'skeleton_overlay']):
            fig = plt.figure(figsize=(8, 8))
            ax = fig.add_axes([0, 0, 1, 1])
            draw_stage(ax, i)
            save(fig, f'{i+1:02d}_{name}_{view}.png')

        fig, axes = plt.subplots(1, 3, figsize=(16, 9))
        fig.subplots_adjust(left=.04, right=.96, top=.78, bottom=.22, wspace=.12)
        fig.text(.04, .91, '分割结果与参考标注对照', fontsize=30, weight='bold', color='#173c42')
        fig.text(.04, .84, f'局部三维 ROI：Dice = {metrics["dice"]:.4f}   |   clDice = {metrics["cldice"]:.4f}   |   HD95 = {metrics["hd95_mm"]:.2f} mm', fontsize=18)
        image(axes[0], planes['source'], low=-100, high=600)
        image(axes[1], planes['mask'] > 0)
        image(axes[2], planes['source'], low=-100, high=600)
        for name, color in [('mask', '#ff8a24'), ('reference', '#00cddd')]:
            plane = planes[name] > 0
            # Use voxel-center coordinates so contours align with imshow pixel centers.
            axes[2].contour((np.arange(width)+.5)*sx, (np.arange(height)+.5)*sy,
                            plane, levels=[.5], colors=[color], linewidths=1.6)
        for ax, title in zip(axes, ['原始 CTA', '预测 Mask', '预测 / 参考轮廓']):
            ax.set_title(title, fontsize=19, pad=18)
        axes[2].legend(handles=[Line2D([0], [0], color='#ff8a24', label='MEDVessel'),
                               Line2D([0], [0], color='#00cddd', label='ImageCAS-X v2')],
                       loc='lower right', fontsize=11)
        fig.text(.04, .15, label, fontsize=17, color='#173c42')
        fig.text(.04, .075, '指标来自整个局部三维 ROI，不能解释为当前单张切片的得分。参考辅助 ROI / 种子，非独立测试。', fontsize=12, color='#65777b')
        save(fig, f'comparison_{view}_16x9.png')

    manifest = {'source_case': 'ImageCAS case 1', 'reference': 'ImageCAS-X v2, record 23562647',
                'scope': 'reference-guided local development ROI; not held-out',
                'source_roi': roi['source_roi_voxel'], 'slice_z_roi': z,
                'ct_window_width': 700, 'ct_window_level': 250,
                'vesselness_display_range': [0, 1], 'metrics': metrics,
                'files': files, 'note': 'Existing outputs only; no algorithm rerun or image synthesis.'}
    (output/'manifest.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
    (output/'README.md').write_text('''# PPT 配图说明

优先使用 pipeline_mip_16x9.png 讲总体流程，pipeline_slice_16x9.png 讲同一切片的变化；comparison_slice_16x9.png 展示分割与参考差异。16:9 图片为 3200×1800，单阶段无标题图为 1600×1600。

原始灰度使用窗宽 700、窗位 250；Frangi 固定显示范围 0～1。投影为 Z 方向最大值（Mask/骨架等价于并集），切片使用 ROI 内 z=40。投影会叠加不同深度，不能据此判断三维连通性。坐标轴为原网格方向，不将其标称为标准解剖轴位。

骨架叠加图：橙色为预测 Mask，青色为体素骨架；点尺寸为可视化设置，不代表血管直径。对照图：橙线为预测，青线为 ImageCAS-X v2 参考。

建议 PPT 图注：ImageCAS 病例 1，参考标注辅助的局部开发 ROI（109×109×81）；Frangi → 区域生长 → 骨架提取。局部三维 Dice=0.8162，非独立测试结果。

数据来源：ImageCAS v3（CC BY-NC 4.0）；ImageCAS-X v2（Zenodo 23562647，CC BY 4.0）。原始文件不改动。具体运行参数见 build/imagecas-x-case1/result/run.json。

重现：先用 medvessel_export 将骨架导出到 build/imagecas-x-plots/skeleton，再运行 tools/plot_ppt_pipeline.py（需要 NumPy、Matplotlib 和 Windows 微软雅黑字体）。
''', encoding='utf-8')
    print(f'Exported {len(files)} PNG figures to {output}', flush=True)


if __name__ == '__main__':
    main()

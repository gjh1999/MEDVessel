"""Publication-style diagnostic plots from saved, independently checked measurements."""
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import font_manager
import numpy as np

ROOT = Path(__file__).resolve().parents[1]


def main():
    audit = json.loads((ROOT/'build/imagecas-multicase/diagnostics.json').read_text())
    output = ROOT/'docs/figures/coronary-report'
    output.mkdir(parents=True, exist_ok=True)
    font_manager.fontManager.addfont('C:/Windows/Fonts/msyh.ttc')
    plt.rcParams.update({'font.family':'Microsoft YaHei', 'font.size':12, 'axes.unicode_minus':False,
                         'axes.spines.top':False, 'axes.spines.right':False, 'svg.fonttype':'path'})
    rows = audit['cases']

    def save(fig, name):
        fig.savefig(output/f'{name}.svg', bbox_inches='tight')
        fig.savefig(output/f'{name}.png', dpi=200, bbox_inches='tight')
        plt.close(fig)

    fig, axes = plt.subplots(1, 2, figsize=(13, 4.4), constrained_layout=True)
    x = np.arange(len(rows))
    for key, label, color in [('dice','Dice','#225e6b'), ('precision','精确率','#d07939'), ('recall','召回率','#5a957e')]:
        axes[0].plot(x, [r['baseline'][key] for r in rows], marker='o', label=label, color=color)
    axes[0].set_xticks(x, [r['case_id'] for r in rows])
    axes[0].set_ylim(0,1.05)
    axes[0].set_xlabel('病例编号（1 为原开发病例）')
    axes[0].set_title('低 Dice 与低精确率相伴，召回率仍高')
    axes[0].legend(ncol=3, loc='lower left', fontsize=10)
    axes[0].grid(axis='y', alpha=.15)
    ratios = [r['baseline']['prediction_voxels']/r['baseline']['reference_voxels'] for r in rows]
    axes[1].bar(x, ratios, color=['#a9bac0' if r['case_id']=='1' else '#d07939' for r in rows])
    axes[1].axhline(1,color='#225e6b',linestyle='--',linewidth=1)
    axes[1].set_xticks(x, [r['case_id'] for r in rows])
    axes[1].set_xlabel('病例编号')
    axes[1].set_title('预测体积 / 参考体积：最差病例约 7.34 倍')
    for i,value in enumerate(ratios):
        axes[1].text(i,value+.09,f'{value:.2f}',ha='center',fontsize=9)
    axes[1].set_ylim(0,8.4)
    save(fig,'precision-recall')

    fig, ax = plt.subplots(figsize=(11,3.7), constrained_layout=True)
    categories = [('retained','正确保留','#478c74'), ('response_only_rejected','仅响应不足','#dba75c'),
                  ('both_rejected','两条件均不满足','#a8709d'), ('intensity_only_rejected','仅强度不符','#6b7f9a'),
                  ('eligible_but_unreachable','满足条件但未连到种子','#db6c5c')]
    new = [r for r in rows if r['case_id']!='1']
    left = np.zeros(len(new))
    for key,label,color in categories:
        values = np.array([r['reference_partitions_fractions'][key] for r in new])*100
        ax.barh(np.arange(len(new)), values, left=left, color=color, label=label)
        left += values
    ax.set_yticks(np.arange(len(new)), [r['case_id'] for r in new])
    ax.invert_yaxis()
    ax.set_xlabel('参考血管体素占比（每例互斥分解，总计 100%）')
    ax.set_title('这张图只解释漏分来源，不包含假阳性；漏分不是当前主要矛盾')
    ax.legend(ncol=3,loc='upper center',bbox_to_anchor=(.5,-.17),frameon=False,fontsize=10)
    save(fig,'gate-partitions')

    variants = [('baseline','原配置'), ('remove_intensity_gate','移除强度门限'),
                ('lower_response_gate_004','低阈值改为 0.04'), ('all_candidates_no_seed_constraint','所有候选直接纳入')]
    means = audit['additional_macro_means']
    fig,ax=plt.subplots(figsize=(11,4), constrained_layout=True)
    x=np.arange(len(variants));width=.24
    for shift,(key,label,color) in enumerate([('dice','Dice','#225e6b'),('precision','精确率','#d07939'),('recall','召回率','#5a957e')]):
        vals=[means[v][key] for v,_ in variants]
        bars=ax.bar(x+(shift-1)*width, vals, width, label=label,color=color)
        ax.bar_label(bars,labels=[f'{v:.3f}' for v in vals],padding=3,fontsize=10)
    ax.set_xticks(x,[label for _,label in variants]);ax.set_ylim(0,1.14)
    ax.set_title('已保存响应图上的事后诊断回放；不是经过验证的改进方案')
    ax.legend(ncol=3,loc='upper left',frameon=False)
    save(fig,'counterfactuals')
    print(f'Diagnostic figures exported to {output}')


if __name__=='__main__':
    main()

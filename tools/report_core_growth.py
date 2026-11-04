"""Create an offline, before/after development report for the optional C++ core-growth mode."""
import base64
import csv
from html import escape
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import font_manager
from matplotlib.lines import Line2D
import numpy as np

from plot_reference import read

ROOT=Path(__file__).resolve().parents[1]


def main():
    directory=ROOT/'build/segmentation-optimized'
    summary=json.loads((directory/'summary.json').read_text())
    baseline=json.loads((ROOT/'build/imagecas-multicase/summary.json').read_text())
    baseline_map={r['case_id']:r for r in baseline['evaluated_cases']}
    figures=ROOT/'docs/figures/core-growth';figures.mkdir(exist_ok=True)
    font_manager.fontManager.addfont('C:/Windows/Fonts/msyh.ttc')
    plt.rcParams.update({'font.family':'Microsoft YaHei','axes.unicode_minus':False,'font.size':11,'svg.fonttype':'path'})
    cases=[]

    def image(ax,values,extent,cmap='gray',vmin=-100,vmax=600):
        ax.imshow(values,extent=extent,cmap=cmap,vmin=vmin,vmax=vmax,interpolation='nearest');ax.set_axis_off()

    for row in summary['cases']:
        case=row['case_id'];source_dir=ROOT/f'build/imagecas-multicase/case-{case}'
        original,meta=read(source_dir/'plots/source');old=read(source_dir/'plots/mask')[0]>0
        reference=read(source_dir/'plots/reference')[0]>0
        new=read(directory/f'case-{case}/plots/mask')[0]>0
        core=read(directory/f'case-{case}/plots/core')[0]>0
        v=read(source_dir/'plots/vesselness')[0]
        z=json.loads((source_dir/'roi.json').read_text())['seed_voxel'][2]
        height,width=original.shape[1:];sx,sy,_=meta['spacing_mm'];extent=(0,width*sx,height*sy,0)
        xx=(np.arange(width)+.5)*sx;yy=(np.arange(height)+.5)*sy
        fig,axes=plt.subplots(2,3,figsize=(13,9),constrained_layout=True)
        for r,select in enumerate([None,z]):
            project=lambda data:data.max(axis=0) if select is None else data[select]
            ct=project(original)
            for col,mask in enumerate([None,old,new]):
                image(axes[r,col],ct,extent)
                if mask is not None:
                    axes[r,col].contour(xx,yy,project(mask),levels=[.5],colors=['#e78929' if col==1 else '#33a268'],linewidths=1.2)
                    axes[r,col].contour(xx,yy,project(reference),levels=[.5],colors=['#00bbd7'],linewidths=1)
            for ax,title in zip(axes[r],['原始 CTA','原配置 / 参考','核心补边 / 参考']):
                ax.set_title(title+(' · Z 投影' if select is None else f' · 切片 {z}'))
        axes[0,1].legend(handles=[Line2D([0],[0],color='#e78929',label='原配置'),Line2D([0],[0],color='#00bbd7',label='参考')],fontsize=9,loc='lower right')
        axes[0,2].legend(handles=[Line2D([0],[0],color='#33a268',label='核心补边'),Line2D([0],[0],color='#00bbd7',label='参考')],fontsize=9,loc='lower right')
        fig.suptitle(f'病例 {case} · Dice {row["baseline_dice"]:.4f} → {row["optimized_dice"]:.4f}\n参考辅助局部开发 ROI；统一参数，非独立测试',fontsize=16)
        path=figures/f'case-{case}.png';fig.savefig(path,dpi=160);plt.close(fig)
        cases.append({'case_id':case,'image':'data:image/png;base64,'+base64.b64encode(path.read_bytes()).decode(),**row})
        if case=='111':
            fig,axes=plt.subplots(1,4,figsize=(16,5),constrained_layout=True)
            for ax,data,title,cmap in zip(axes,[original.max(0),v.max(0),core.max(0),new.max(0)],
                    ['原始 CTA','Frangi 响应','严格核心','0.5 mm 范围内补边'],['gray','magma','gray','gray']):
                image(ax,data,extent,cmap,-100 if cmap=='gray' and title=='原始 CTA' else 0,600 if title=='原始 CTA' else 1)
                ax.set_title(title,fontsize=17)
            fig.suptitle('核心补边的两阶段过程 · 病例 111 的 Z 投影\n核心固定不更新；只向其邻近区域补回低响应体素',fontsize=18)
            fig.savefig(figures/'stages-case-111.png',dpi=200);fig.savefig(figures/'stages-case-111.svg');plt.close(fig)

    rows=summary['cases'];new_rows=[r for r in rows if r['case_id']!='1']
    mean=lambda key:float(np.mean([r[key] for r in new_rows]))
    old_cldice=float(np.mean([baseline_map[r['case_id']]['cldice'] for r in new_rows]))
    old_hd95=float(np.mean([baseline_map[r['case_id']]['hd95_mm'] for r in new_rows]))
    fig,ax=plt.subplots(figsize=(12,4),constrained_layout=True)
    x=np.arange(len(rows));width=.36
    ax.bar(x-width/2,[r['baseline_dice'] for r in rows],width,color='#d68a45',label='原配置')
    ax.bar(x+width/2,[r['optimized_dice'] for r in rows],width,color='#397e78',label='核心补边')
    ax.set_xticks(x,[r['case_id'] for r in rows]);ax.set_ylim(0,1);ax.set_ylabel('Dice');ax.set_xlabel('病例编号；1 为原开发病例')
    ax.legend(frameon=False);ax.set_title('所有病例用同一组参数；11/11 例 Dice 提高，但并非所有指标都提高')
    fig.savefig(figures/'dice-comparison.png',dpi=180);fig.savefig(figures/'dice-comparison.svg');plt.close(fig)
    fields=['case_id','baseline_dice','optimized_dice','dice_change','precision','recall','cldice','hd95_mm','core_threshold','python_cpp_voxelwise_match']
    with (directory/'comparison.csv').open('w',newline='',encoding='utf-8-sig') as f:
        writer=csv.DictWriter(f,fieldnames=fields,extrasaction='ignore');writer.writeheader();writer.writerows(rows)
    table=[]
    for r in rows:
        old=baseline_map[r['case_id']]
        cells=[r['case_id'],f'{r["baseline_dice"]:.4f}',f'{r["optimized_dice"]:.4f}',f'{r["dice_change"]:+.4f}',
               f'{old["cldice"]:.4f} → {r["cldice"]:.4f}',f'{old["hd95_mm"]:.2f} → {r["hd95_mm"]:.2f}']
        table.append('<tr>'+''.join('<td>'+escape(c)+'</td>' for c in cells)+'</tr>')
    embed=lambda name:'data:image/png;base64,'+base64.b64encode((figures/name).read_bytes()).decode()
    first=next(c for c in cases if c['case_id']=='111')
    page='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>分割优化开发实验 · MEDVessel</title>
<style>body{font:16px/1.9 "Microsoft YaHei",sans-serif;background:#f2f6f6;color:#263e45;margin:0}main{max-width:1150px;margin:35px auto;padding:0 22px}header{background:#183e47;color:white;padding:30px;border-radius:15px}h1{font-size:32px}h2{color:#174951;margin:0 0 20px}h3{margin:24px 0 10px}section{background:white;padding:27px;border:1px solid #d5e2e3;border-radius:13px;margin:22px 0}a{color:#236c70}img{max-width:100%;height:auto}table{border-collapse:collapse;width:100%;font-size:14px}td,th{border-bottom:1px solid #dce6e6;padding:10px;text-align:left;white-space:nowrap}th{background:#eaf2f2}.scroll{overflow-x:auto}.callout{padding:16px;background:#fff3e6;border-left:4px solid #b87839;margin:18px 0}.metrics{display:grid;grid-template-columns:repeat(3,1fr);gap:15px}.metrics div{background:#edf5f4;border-radius:9px;padding:16px}.metrics b{font-size:27px;display:block;color:#235a60}.small{font-size:13px;color:#64777c}pre{white-space:pre-wrap;padding:18px;background:#edf3f4;font:13px/1.8 Consolas,monospace}select{padding:9px;font:inherit;max-width:100%}code{font-family:Consolas,monospace}figcaption{font-size:13px;color:#64777c}li{margin:9px 0}@media(max-width:650px){section{padding:18px}.metrics{grid-template-columns:1fr}h1{font-size:25px}main{padding:0 13px}}@media print{body{background:white}section{border:0}header{background:white;color:#183e47}}</style><main>
<header><small>MEDVESSEL / DEVELOPMENT EXPERIMENT · 2026-11-04</small><h1>减少泄漏，再补回血管边界</h1><p>可选的“种子自适应核心＋固定物理距离补边”模式，已接入 C++ CLI 与 Qt。所有原结果保留，默认关闭新模式。</p></header>
<section><h2>这轮得到什么</h2><div class="metrics"><div><b>0.5100 → 0.7307</b>新增 10 例平均 Dice</div><div><b>0.5124 → 0.7931</b>新增 10 例平均 clDice</div><div><b>11 / 11</b>开发病例 Dice 提高</div></div><p>平均精确率 0.3805 → 0.6322，召回率 0.9186 → 0.8876；原开发病例 Dice 0.8162 → 0.8238。</p><div class="callout"><strong>这是回顾性开发优化，不是独立验证。</strong>本轮比较了 64 组阈值 / 种子条件和 96 组核心 / 距离配置，共 160 组。选择时已看这 11 例标注和分数，且 ROI、种子原本由参考辅助。未逐例使用不同参数，但选择本身仍有偏差，不能声称泛化已解决。</div><figure><img src="''' +embed('dice-comparison.png')+'''" alt="逐例 Dice 改进"><figcaption>均值不含原开发病例 1；每例都用 low=0.08、核心响应比例 0.35、补边 0.5 mm、26 邻域。</figcaption></figure></section>
<section><h2>方法：严格核心固定，弱响应只能在附近补边</h2><ol><li>对有效且不重复的种子，读取增强响应中位数。核心阈值取 <code>max(low, 0.35 × 种子响应中位数)</code>，无需读取标签。</li><li>在原强度约束下，从种子提取满足核心阈值的连通区域。低于核心阈值的种子不在第二阶段独立发起生长。</li><li>计算每个体素中心到原始核心的欧氏距离，使用真实 spacing，保留 0.5 mm 范围。<strong>核心保持固定，不随新接纳体素更新距离</strong>，避免沿弱桥反复向外推进。</li><li>只在此范围内按原 low、强度和连通性补回边界；被范围覆盖但不连通的孤岛不会自动填上。</li></ol><p>这不是完整 Level Set，也没有实现解剖模型；它是在既有 Frangi 上增加可解释的传播限制。核心仍可能漏进错误区域，远离核心的低响应真分支也可能被截断。</p><figure><img src="'''+embed('stages-case-111.png')+'''" alt="原图、Frangi、严格核心与最终 Mask"><figcaption>病例 111 的中间过程。显示用最大投影；算法在三维网格上计算距离与传播。</figcaption></figure></section>
<section><h2>逐例结果与代价</h2><div class="scroll"><table><thead><tr><th>病例</th><th>原 Dice</th><th>新 Dice</th><th>变化</th><th>clDice（原 → 新）</th><th>HD95 mm（原 → 新）</th></tr></thead><tbody>'''+''.join(table)+'''</tbody></table></div><p>新增病例平均 HD95 为 '''+f'{old_hd95:.2f} → {mean("hd95_mm"):.2f}'+''' mm，但病例 11 从 11.23 恶化到 24.14 mm，病例 109 从 3.35 到 3.95 mm。病例 11 召回率从 0.9025 降到 0.6976，说明较远的弱响应分支仍会丢失；不能只看 Dice 改善。</p><label for="case">切换实际对照图：</label><select id="case">'''+''.join(f'<option value="{c["case_id"]}"'+(' selected' if c['case_id']=='111' else '')+f'>病例 {c["case_id"]}</option>' for c in cases)+'''</select><figure><img id="comparison" src="'''+first['image']+'''" alt="病例 111 优化前后对照"><figcaption>橙：原配置；绿：核心补边；青：参考。上排投影，下排种子切片。全页图片和数据均内嵌，可离线阅读。</figcaption></figure></section>
<section><h2>如何使用</h2><p>Qt 的高级参数新增“核心补边范围 mm”和“种子核心响应比例”。范围为 0 时沿用旧算法；启用时须有手动种子或 NMS 种子。开发配置可从 <code>build/segmentation-optimized/case-&lt;编号&gt;/gui-config.json</code> 载入，包含同一份参数与对应输入 / 种子。</p><pre>medvessel_cli source.nii.gz new_result_directory \
  --scales 0.5,1,1.5,2 --c 100 --low 0.08 --high 0.2 \
  --seed x,y,z --min-intensity 100 --max-intensity 900 \
  --connectivity 26 --min-component 20 --prune-mm 1 \
  --growth-band-mm 0.5 --seed-core-ratio 0.35</pre><p>有效核心阈值记录在 run.json 的 effective_core_threshold；另导出 core.nii.gz 供检查。核心比例为 0 时使用 high 作为核心阈值。自动双阈值模式不默认启用本实验模式。</p></section>
<section><h2>验证与下一步</h2><p>11 例均实际运行 C++ CLI、评价器和导出程序。C++ 核心及最终 Mask 与独立 SciPy 回放逐体素一致，重新计算 Dice 与评价器一致；合成测试覆盖毫米距离、各向异性、固定核心不滚动、重复种子、无效参数、空核心与隔离弱种子。Qt 测试覆盖参数保存 / 回填、真实进程和 core.nii.gz 结果读回。</p><p>下一步优先研究“怎样保留远端弱分支而不重新引入心腔 / 邻近结构泄漏”，再用未查看病例验证；当前不把这个开发结果视作全冠脉自动分割成绩。</p><ul><li>原结果：build/imagecas-multicase</li><li>新结果与配置：build/segmentation-optimized</li><li>探索记录：build/segmentation-exploration/sweep.json、bounded-sweep.json</li><li>复现：tools/explore_segmentation.py、explore_bounded_growth.py、verify_core_growth_batch.py、report_core_growth.py</li><li>核心实现：include/segmentation/SeededCoreGrowth.hpp</li></ul><p><a href="traditional_coronary_report.html">返回传统方法与泛化诊断报告</a></p></section></main>
<script type="application/json" id="case-data">'''+json.dumps(cases,ensure_ascii=False).replace('</', '<' + chr(92) + '/')+'''</script><script>const cases=JSON.parse(document.getElementById('case-data').textContent);document.getElementById('case').addEventListener('change',e=>{const c=cases.find(c=>c.case_id===e.target.value);const img=document.getElementById('comparison');img.src=c.image;img.alt=`病例 ${c.case_id} 优化前后对照`;});</script></html>'''
    page = page.replace('<main>', '<main><section><h2>统一评价入口</h2><p>本页保留优化探索时的历史 C++ HD95 和 clDice 口径。论文式主表使用 Dice、HD95、ASSD 和 Lee clDice，已对相同的 22 个 mask 重算，请看 <a href="evaluation_and_industry.html">统一评价报告与工业落地调研</a>；不要混用两个版本的分数。</p></section>')
    (ROOT/'docs/segmentation_optimization.html').write_text(page,encoding='utf-8')
    print('Built docs/segmentation_optimization.html; 11 before/after figures and CSV')


if __name__=='__main__':main()

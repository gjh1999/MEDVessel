"""Re-evaluate saved development masks using the declared paper metric profile."""
import json
from pathlib import Path
from statistics import mean, stdev
from evaluate_standard import evaluate_exports

ROOT = Path(__file__).resolve().parents[1]


def main():
    summary = json.loads((ROOT / 'build/segmentation-optimized/summary.json').read_text())
    rows = []
    for case in summary['cases']:
        cid = case['case_id']
        reference = ROOT / f'build/imagecas-multicase/case-{cid}/plots/reference'
        for variant, folder in [('baseline', 'imagecas-multicase'), ('core_growth', 'segmentation-optimized')]:
            directory = ROOT / f'build/{folder}/case-{cid}'
            result = evaluate_exports(directory / 'plots/mask', reference)
            (directory / 'result/metrics_standard.json').write_text(json.dumps(result, indent=2, allow_nan=False)+'\n')
            rows.append({'case_id': cid, 'variant': variant, **result['metrics']})
        print('Evaluated', cid, flush=True)
    aggregates = {}
    for variant in ('baseline', 'core_growth'):
        group = [r for r in rows if r['variant'] == variant and r['case_id'] != '1']
        if any(r[key] is None for r in group for key in ('hd95_mm', 'assd_mm')):
            raise ValueError('Empty-mask distance failures require explicit reporting before aggregation')
        aggregates[variant] = {key: {'mean': mean(r[key] for r in group), 'sample_std': stdev(r[key] for r in group)}
                               for key in ('dice', 'hd95_mm', 'assd_mm', 'cldice', 'precision', 'recall')}
    output = {'profile': result['profile'], 'versions': result['versions'],
              'scope': '11 reference-assisted development ROIs; aggregates are the 10 added cases, excluding initial case 1. Not held-out validation.',
              'case_count': len(summary['cases']), 'evaluations': len(rows),
              'undefined_distance_count': sum(r['hd95_mm'] is None for r in rows),
              'cases': rows, 'additional_case_aggregates': aggregates}
    (ROOT / 'build/segmentation-optimized/standard-summary.json').write_text(json.dumps(output, indent=2)+'\n')
    table = ''.join('<tr><td>'+label+'</td>'+''.join(f'<td>{aggregates[v][key]["mean"]:.4f} ± {aggregates[v][key]["sample_std"]:.4f}</td>' for v in ('baseline','core_growth'))+'</tr>'
                    for key,label in [('dice','Dice ↑'),('hd95_mm','HD95 / mm ↓'),('assd_mm','ASSD / mm ↓'),('cldice','clDice ↑')])
    cases = ''.join(f'<tr><td>{r["case_id"]}</td><td>{r["variant"]}</td><td>{r["dice"]:.4f}</td><td>{r["hd95_mm"]:.3f}</td><td>{r["assd_mm"]:.3f}</td><td>{r["cldice"]:.4f}</td></tr>' for r in rows)
    content = (ROOT / 'docs/evaluation_and_industry_body.html').read_text(encoding='utf-8')
    page = '''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>3D 血管分割：评价指标与工业落地</title><style>body{max-width:1050px;margin:40px auto;padding:0 20px;font:16px/1.9 system-ui;color:#183e49;background:#f6fafb}section{background:white;padding:24px;margin:22px 0;border:1px solid #d6e4e8;border-radius:12px}table{border-collapse:collapse;width:100%;font-size:14px}td,th{padding:10px;border-bottom:1px solid #d6e4e8;text-align:left}a{color:#067a85}pre{white-space:pre-wrap;overflow-wrap:anywhere}div.scroll{overflow:auto}</style><h1>3D 血管分割：评价指标与工业落地</h1>'''+content+'''<section><h2>相同口径重算的开发结果</h2><p>新增 10 例逐病例平均 ± 样本标准差；原始病例 1 单列。所有病例均参与了开发，ROI 和种子借助参考标注准备；不是独立测试集，也不是全冠脉自动分割成绩。优化参数是在这些病例上探索后选择的。</p><table><tr><th>指标</th><th>原配置</th><th>核心补边</th></tr>'''+table+'''</table><p>下面两组均用新评估器重算，计算口径变化不算作算法提升。病例 11 等远端漏分问题仍需结合逐例距离与召回检查。</p><div class="scroll"><table><tr><th>病例</th><th>配置</th><th>Dice</th><th>HD95 mm</th><th>ASSD mm</th><th>clDice</th></tr>'''+cases+'''</table></div><h3>复现与版本</h3><pre>python tools/evaluate_standard.py prediction_export_prefix reference_export_prefix metrics_standard.json
python tools/report_standard_metrics.py</pre><p>输入是 medvessel_export 导出的 JSON/raw 前缀；检查尺寸、间距、原点和方向，不隐式重采样。依赖 NumPy、SciPy、scikit-image。</p><pre>'''+json.dumps(result['versions'],indent=2)+'''</pre><p>逐例文件：build/{imagecas-multicase,segmentation-optimized}/case-编号/result/metrics_standard.json；汇总：build/segmentation-optimized/standard-summary.json。</p></section><p><a href="segmentation_optimization.html">优化过程与图像对照</a> · <a href="traditional_coronary_report.html">传统方法报告</a></p></html>'''
    (ROOT / 'docs/evaluation_and_industry.html').write_text(page, encoding='utf-8')


if __name__ == '__main__':
    main()

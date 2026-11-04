"""Build a browsable report from existing fixed-protocol batch measurements."""
import html
import json
from pathlib import Path
import statistics


def main():
    root = Path(__file__).resolve().parents[1]
    directory = root/'build/imagecas-multicase'
    summary = json.loads((directory/'summary.json').read_text())
    rows = summary['evaluated_cases']
    new = [r for r in rows if not r['previously_used_for_development']]
    scores = [r['dice'] for r in new if r['status']=='ok']
    heading = '固定参数多病例局部分割测试'
    note = ('参考标注辅助 ROI 和种子；每例取种子周围半宽 20 mm 的局部区域。'
            '新增病例使用原病例参数，不逐例调参。不是全冠脉自动分割或独立盲测；'
            '病例按归档顺序选取，不能视为随机代表性样本。')
    stats = (f'已评估 {len(rows)} 例；新增 {len(new)} 例，其中成功计算指标 {len(scores)} 例。'
             f'新增病例 Dice 均值 {statistics.mean(scores):.4f}，中位数 {statistics.median(scores):.4f}，'
             f'范围 {min(scores):.4f}～{max(scores):.4f}。') if scores else '新增病例尚无可用指标。'
    table = []
    markdown = [f'# {heading}', '', note, '', stats, '',
                '| 病例 | 范围 | Dice | clDice | HD95 (mm) | 状态 |', '| --- | --- | --- | --- | --- | --- |']
    gallery = []
    for r in rows:
        case = r['case_id']
        scope = '原开发病例' if r['previously_used_for_development'] else '新增病例'
        metrics = [f'{r[key]:.4f}' if key in r else '—' for key in ['dice','cldice','hd95_mm']]
        markdown.append('| '+' | '.join([case, scope, *metrics, r['status']])+' |')
        cells = [case, scope, *metrics, r['status']]
        table.append('<tr>'+''.join('<td>'+html.escape(x)+'</td>' for x in cells)+'</tr>')
        picture = directory/f'case-{case}/comparison.png'
        if picture.exists():
            gallery.append(f'<article><h2>病例 {case} · {scope} · Dice {metrics[0]}</h2>'
                           f'<a href="case-{case}/comparison.png"><img src="case-{case}/comparison.png" loading="lazy"></a></article>')
    markdown.extend(['', '失败病例同样保留在表中，均值仅针对有有效指标的新增病例；原开发病例不进入新增病例均值。',
                     '', '协议见 protocol.json；逐例参数和输出在 case-<编号>/result；下载及 SHA256 清单见 datasets/imagecas-multicase/manifest.json。'])
    (directory/'report.md').write_text('\n'.join(markdown)+'\n', encoding='utf-8')
    page = ('<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>'+heading+'</title>'
            '<style>body{font:16px/1.7 sans-serif;max-width:1200px;margin:40px auto;color:#173c42;background:#f5f8fa;padding:20px}'
            'table{border-collapse:collapse;width:100%;background:white}td,th{border:1px solid #cddadd;padding:10px;text-align:left}'
            'article{padding:20px;background:white;margin-top:25px;border-radius:12px}img{width:100%;height:auto}</style>'
            f'<h1>{heading}</h1><p>{note}</p><p><b>{stats}</b></p>'
            '<p><a href="summary.csv">下载逐例 CSV</a> · <a href="protocol.json">固定测试协议</a></p>'
            '<table><tr><th>病例</th><th>范围</th><th>Dice</th><th>clDice</th><th>HD95 (mm)</th><th>状态</th></tr>'
            +''.join(table)+'</table>'+''.join(gallery)+'</html>')
    (directory/'index.html').write_text(page, encoding='utf-8')
    print(stats)


if __name__ == '__main__':
    main()

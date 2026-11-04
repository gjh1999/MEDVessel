"""Render the experiment handbook as an offline HTML page using the project's renderer."""
from html import escape
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from build_learning_guide import markdown


def main():
    body = markdown((HERE / 'README.md').read_text(encoding='utf-8'), 'nnunet')
    diagram = '''<div class="flow"><span>原始 CTA</span><b>→</b><span>通道 0：CT<br>通道 1：Frangi 响应</span><b>→</b><span>nnU-Net 3D<br>联合预处理与分割</span><b>→</b><span>Mask<br>评价 / Qt 显示</span></div>'''
    validation = HERE / 'validation_result.json'
    evidence = '<p>尚未生成验证记录。</p>'
    if validation.exists():
        evidence = '<pre>' + escape(json.dumps(json.loads(validation.read_text()), ensure_ascii=False, indent=2)) + '</pre>'
    page = '''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>CTA + Frangi 双通道 nnU-Net 实验</title><style>body{margin:0;background:#f2f7f9;color:#193c49;font:16px/1.9 system-ui,"Microsoft YaHei"}main{max-width:1100px;margin:auto;padding:30px 22px 70px}h1{font-size:32px}h3{margin-top:48px;border-bottom:2px solid #c9e1e6;padding-bottom:12px}a{color:#087b86;overflow-wrap:anywhere}pre{background:#16313d;color:#e7f2f4;padding:20px;border-radius:10px;overflow:auto;font-size:13px}code{font-family:Consolas,monospace}p code,td code{overflow-wrap:anywhere}table{border-collapse:collapse;width:100%;background:white;font-size:14px}td,th{border:1px solid #d5e4e9;padding:12px;text-align:left;min-width:100px}.table-scroll{overflow:auto}.flow{display:flex;gap:12px;align-items:center;flex-wrap:wrap;padding:24px;background:#def0ef;border-radius:12px}.flow span{background:white;padding:14px;border-radius:8px}.status{padding:16px 22px;background:#fff2cf;border-left:4px solid #c78a11}@media print{pre{white-space:pre-wrap}body{background:white}}</style><main><h1>CTA + Frangi 双通道 nnU-Net</h1><p class="status">当前阶段：数据与网络前向验证。没有启动训练，没有深度模型分割成绩。</p>'''+diagram+body+'<h3>本机验证记录</h3>'+evidence+'</main></html>'
    (HERE / 'index.html').write_text(page, encoding='utf-8')
    (ROOT / 'docs/nnunet_hybrid.html').write_text(page, encoding='utf-8')
    print('Built', HERE / 'index.html')


if __name__ == '__main__':
    main()

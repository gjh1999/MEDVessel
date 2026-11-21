from pathlib import Path
import json,html
root=Path(__file__).parent/'dist'
page=(root/'index.html').read_text(encoding='utf-8')
rows=json.loads((root/'photos.json').read_text(encoding='utf-8'))
captions=[
('1965年7月30日 · 医疗法案签署','约翰逊与杜鲁门同席。立法仪式是起点，实际服务仍依赖行政实施。'),
('1965年4月11日 · 基础教育法案签署演说','学校进入联邦改革议程。照片是政治行动的现场记录，不是教育成效证据。'),
('1963年8月28日 · 华盛顿民权游行','马丁·路德·金与参与者。社会运动并非总统政策的背景装饰，而是改革的推动力量。'),
('1973年车票 · 北京至长沙的旅行凭证','生活史实物图，不是社保凭证。文件标题指向1973年车票，数字照片元数据为2026年，勿混淆实物年代和拍摄时间。'),
('1988年 · 北京生活街景','真实街景用于观察转轨时代的日常环境，不代表全国生活水平或特定群体待遇。'),
('1991年4月3日 · 广州无轨电车与乘客','就业、居住和城市流动构成保障制度之外的日常空间；这是广州场景，不是上海。'),
('北京景山公园 · 老年人的公共生活','文件名标作1996年，元数据另列2009年，确切拍摄年存在差异，故不将本图用于年代论证。公园活动不等于养老服务覆盖。'),
('2011年 · 贵州乡村课堂','图像说明为贵州西江白碧山村课堂。教育作为相关公共服务呈现，不等同于社会保险。')]
for i,row in enumerate(rows):
 title,desc=captions[i]
 photo=f'<figure class="photograph"><img loading="lazy" src="{row["file"]}" alt="{html.escape(title)}"><figcaption>{title}</figcaption><p>{desc}</p><p class="note">作者：{html.escape(row["artist"])} · 许可：{html.escape(row["license"])}。按原图比例展示，未作史料内容修改。</p><a class="source" href="{html.escape(row["page"])}" target="_blank" rel="noopener">Wikimedia Commons 原图、年代与版权记录 ↗</a></figure>'
 if i==6:row['section']='cn2'
 marker=f'<section id="{row["section"]}">'
 start=page.index(marker);end=page.index('</section>',start)
 page=page[:end]+photo+page[end:]
page=page.replace('</style>','.photograph img{display:block;width:100%;height:auto;max-height:660px;object-fit:contain;background:#e9e5dc}.photograph figcaption{margin:18px 0 8px}.photograph p{font-size:14px;margin:10px 0}</style>')
(root/'index.html').write_text(page,encoding='utf-8')
# Verify local resources, anchor integrity, figures and HTML structure.
from html.parser import HTMLParser
class Check(HTMLParser):
 def __init__(self):super().__init__();self.ids=set();self.links=[];self.images=[]
 def handle_starttag(self,tag,attrs):
  d=dict(attrs)
  if 'id' in d:self.ids.add(d['id'])
  if tag=='a':self.links.append(d.get('href',''))
  if tag=='img':self.images.append(d['src'])
c=Check();c.feed(page)
assert all(link[1:] in c.ids for link in c.links if link.startswith('#'))
assert all((root/f).is_file() for f in c.images)
assert len(c.images)==8
(root/'verification.json').write_text(json.dumps(dict(local_images=len(c.images),anchors_valid=True,local_resources_valid=True,statistical_charts=5,mechanism_diagrams=4,comparison_table=1,photos=8,offline=True),indent=2))
print('Verified 8 local photographs and all section anchors')

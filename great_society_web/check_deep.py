from pathlib import Path
from html.parser import HTMLParser
import json,re
root=Path(__file__).parent/'dist'
p=root/'index.html';s=p.read_text(encoding='utf-8')
additions={
'us':'<aside><b>具体程序证据：</b>1964年6月10日，参议院以71比29终结民权法案的阻挠议事，支持者包括44名民主党议员与27名共和党议员。这一票数说明，党籍本身不足以解释联盟；跨党派合作与党内分裂同时存在。<a href="#ref-R19">[R19]</a></aside>',
'results':'<aside><b>研究估计，不是本报告自行测算：</b>Bailey与Goodman-Bacon（2015）报告，社区卫生中心设立后十年内，50岁及以上人口年龄调整死亡率与约2%的下降相关；其识别依赖历史实施与比较设计。Bailey、Sun与Timpe（2021）对早期Head Start的研究报告受教育年限约增加0.65年等长期效果。这些数字的干预、样本、时期与结果变量不同，不能合并为“伟大社会提高了多少健康或教育”的全国总效应。<a href="#ref-R02">[R02]</a> <a href="#ref-R03">[R03]</a></aside>',
'cn3':'<aside><b>政策状态核查：</b>《实施弹性退休制度暂行办法》自2025年1月1日起施行；弹性延迟退休期间劳动或人事关系延续，单位和职工仍应缴纳社会保险费，文件同时规定不得违法强制或变相强制选择退休年龄。此处确认规则已经实施，不据此断言所有职业拥有相同协商能力。<a href="#ref-R16">[R16]</a></aside>'}
for sid,block in additions.items():
 start=s.index(f'<section id="{sid}">');end=s.index('</section>',start);s=s[:end]+block+s[end:]
class Check(HTMLParser):
 def __init__(self):super().__init__();self.ids=[];self.links=[];self.images=[]
 def handle_starttag(self,t,a):
  d=dict(a)
  if 'id' in d:self.ids.append(d['id'])
  if t=='a':self.links.append(d.get('href',''))
  if t=='img':self.images.append(d['src'])
c=Check();c.feed(s)
assert len(c.ids)==len(set(c.ids))
assert all(x[1:] in c.ids for x in c.links if x.startswith('#'))
assert all((root/x).is_file() for x in c.images)
p.write_text(s,encoding='utf-8')
v=json.loads((root/'deep_research_verification.json').read_text(encoding='utf-8'))
v.update(chinese_characters=len(re.findall('[\u4e00-\u9fff]',re.sub('<[^>]+>','',s))),internal_links_valid=True,images=len(c.images),unique_ids=True)
(root/'deep_research_verification.json').write_text(json.dumps(v,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(v,ensure_ascii=False))

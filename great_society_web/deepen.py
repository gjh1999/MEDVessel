"""Add researched thematic chapters to the preserved first edition."""
from pathlib import Path
from html import escape
import json,re,shutil,datetime
root=Path(__file__).parent
out=root/'dist'
backup=root/'archive'/'overview_20261110'
if not backup.exists():shutil.copytree(out,backup)
page=(out/'index.html').read_text(encoding='utf-8')
if 'deep-research-edition' in page:raise RuntimeError('Rebuild base before applying deep edition again')
refs=[
('R01','Finkelstein & McKnight (2008), What Did Medicare Do? Journal of Public Economics 92:1644–1668','https://economics.mit.edu/sites/default/files/2022-08/What%20Did%20Medicare%20Do%20The%20Initial%20Impact%20of%20Medicar.pdf','准实验研究：Medicare初期死亡率与个人医疗支出；不代表全部长期健康收益。'),
('R02','Bailey & Goodman-Bacon (2015), The War on Poverty’s Experiment in Public Medicine, AER 105(3):1067–1104','https://pubs.aeaweb.org/doi/10.1257/aer.20120070','社区卫生中心分期设立与较年长群体死亡率；效果依赖历史研究场景。'),
('R03','Bailey, Sun & Timpe (2021), Prep School for Poor Kids, AER 111(12):3963–4001','https://www.nber.org/papers/w28268','使用项目推出与年龄资格研究长期结果；工作论文入口含正式发表信息。'),
('R04','Head Start项目评估与历史资料：美国国会研究处2014年报告','https://www.everycrsreport.com/files/20140102_RL30952_2711d1b6b4310c14d32ca3a123017417933a94cc.html','包含全国影响研究及三年级追踪；与早期长期队列的对象和替代服务不同。'),
('R05','CMS: Medicare与Medicaid项目历史','https://www.cms.gov/about-cms/who-we-are/history','核实原始Part A/B、最初Medicaid资格与后来扩展。'),
('R06','CMS: Thirty Years of Medicare—Impact on the Covered Population','https://www.cms.gov/research-statistics-data-and-systems/research/healthcarefinancingreview/downloads/cms1191479dl.pdf','医疗筹资历史，注意数据年份。'),
('R07','The political economy of healthcare reform in China: negotiating public and private (2013)','https://pmc.ncbi.nlm.nih.gov/articles/PMC3776089/','开放学术研究，解释筹资与公私关系；并非所有论点均为独立因果识别。'),
('R08','Research in health policy making in China: out-of-pocket payments in Healthy China 2030 (2018)','https://pmc.ncbi.nlm.nih.gov/articles/PMC5797981/','卫生费用筹资结构与政策证据使用；宏观比例不是患者报销比例。'),
('R09','Betcherman & Blunch: The Limited Job Prospects of Displaced Workers—Evidence from Two Cities in China, World Bank','https://documents1.worldbank.org/curated/en/380541468024657837/pdf/378890CHA0Job0prospects0SP0061301PUBLIC1.pdf','两城市研究，不能把样本结果直接外推全国。'),
('R10','Wang Zheng (2003): Gender, Employment and Women’s Resistance, Chinese Society: Change, Conflict and Resistance','https://sites.lsa.umich.edu/wangzheng/wp-content/uploads/sites/948/2021/09/2003WangZ_Gender-Employment-and-Womens-Resistance.pdf','性别与就业重组的学术分析；具体年份与群体范围需保留。'),
('R11','国务院：建立城镇职工基本医疗保险制度的决定，国发〔1998〕44号','https://www.nhsa.gov.cn/art/1998/12/14/art_53_1190.html','原始制度文本；历史指导费率不是当前地方办理指南。'),
('R12','财政部：财政社会保障工作大事记（1994—2007年）','https://www.mof.gov.cn/zhengwuxinxi/caijingshidian/zgcjb/200812/t20081222_101677.htm','养老、低保、下岗与失业保险政策节点。'),
('R13','中华人民共和国社会保险法（2010年通过、2011年施行，页面含修订说明）','https://www.mct.gov.cn/preview/whhlyqyzcxxfw/zhgl/202105/t20210513_924441.html','法律文本与生效日期；修订版不得无说明倒用于原始时点。'),
('R14','国务院：建立统一的城乡居民基本养老保险制度，国发〔2014〕8号','https://www.ndrc.gov.cn/xwdt/ztzl/xxczhjs/ghzc/201605/t20160505_971901_ext.html','文件年份2014，所链接存档页面发布于2016；区别文件与网页日期。'),
('R15','国务院：整合城乡居民基本医疗保险制度，2016','https://www.nhsa.gov.cn/art/2016/1/3/art_37_1171.html','居民制度整合，并非职工居民全部制度合并。'),
('R16','人社部等：实施弹性退休制度暂行办法，2025年1月1日起施行','https://www.mohrss.gov.cn/wap/zc/zcwj/202501/t20250101_533701.html','核实已实施规则；其实际效果仍需后续评估。'),
('R17','World Bank & 国务院发展研究中心：China 2030','https://www.worldbank.org/content/dam/Worldbank/document/China-2030-complete.pdf','社会保护、人口与增长背景；预测属于成稿时假设，不能作已实现事实。'),
('R18','IMF Working Paper 26/27: Population Aging and Pension Reforms in China (2026)','https://www.imf.org/-/media/files/publications/wp/2026/english/wpiea2026027-source-pdf.pdf','近期养老研究；工作论文作者观点与条件预测，不视为确定未来。'),
('R19','美国参议院：1964年民权法的终结辩论与最终通过','https://www.senate.gov/legislative/landmark-legislation/civil-rights-act-of-1964/cloture-and-final-passage-of-the-civil-rights-act-of-1964-essay.htm','跨党派联盟、委员会与阻挠议事的具体程序史。'),
('R20','美国国务院历史办公室：1961—1968外交史专题','https://history.state.gov/milestones/1961-1968','越战升级与1968年背景；不把外交年表本身当作国内预算挤出的因果证明。')]
byid={r[0]:r for r in refs}
sectionrefs={
'question':['R01','R09','R17'],'us':['R05','R19'],'policy':['R05','R06','R02','R03','R04'],
'results':['R01','R02','R03','R04','R20'],'cn1':['R07','R17'],'cn2':['R07','R08','R09','R10','R11','R12'],
'cn3':['R11','R13','R14','R15','R16','R18'],'pressure':['R08','R17'],'compare':['R05','R11','R14','R15','R16','R17'],'conclusion':['R01','R02','R03','R09','R17']}
for filename in ('depth_us.json','depth_china.json','depth_comparison.json'):
 for chapter in json.loads((root/filename).read_text(encoding='utf-8')):
  sid=chapter['section'];block='<article class="deepchapter"><h3>'+escape(chapter['title'])+'</h3>'
  block+=''.join('<p>'+escape(p)+'</p>' for p in chapter['paragraphs'])
  block+='<div class="evidence"><strong>证据与解释边界：</strong>本节机制比较属于作者综合解释；具体文件和研究结论请对照下列资料。 '
  block+=' · '.join(f'<a href="#ref-{k}">[{k}]</a>' for k in sectionrefs[sid])+'</div></article>'
  start=page.index(f'<section id="{sid}">');end=page.index('</section>',start)
  page=page[:end]+block+page[end:]
evidence='''<h3>项目评估证据表：结论为什么有边界？</h3><div class="tablewrap"><table><thead><tr><th>对象</th><th>比较策略</th><th>主要启示</th><th>不能推断的内容</th></tr></thead><tbody><tr><td>Medicare初期</td><td>改革前保险差异、年龄与地区等历史比较</td><td>财务保护与短期死亡率可能表现不同</td><td>不能认定无任何长期健康作用</td></tr><tr><td>社区卫生中心</td><td>历史分期设立与地区追踪</td><td>基层服务与保险具有互补性</td><td>不能外推每个当代中心相同效果</td></tr><tr><td>Head Start</td><td>推出时间、资格边界与长期行政数据；另有较新全国实验</td><td>长期教育与近期测试是不同结果</td><td>不同队列的估计不宜直接当成矛盾或平均</td></tr><tr><td>中国下岗再就业</td><td>两城市调查研究</td><td>再就业受到个人与市场共同约束</td><td>不能当作全国每类工人的因果效应</td></tr></tbody></table></div>'''
start=page.index('<section id="results">');end=page.index('</section>',start);page=page[:end]+evidence+page[end:]
refhtml='<div class="deepchapter"><h3>深度版参考文献与逐节注释索引</h3><p>以下是新增研究文献。R编号对应每个专题末尾的注释；原始法律、统计及照片来源仍保留在原图和各节旁。参考文献列入不意味着本文接受其中每一项解释。</p><ol class="bibliography">'
for key,title,url,note in refs:refhtml+=f'<li id="ref-{key}"><b>[{key}]</b> <a href="{url}" target="_blank" rel="noopener">{escape(title)}</a><p>{escape(note)}</p></li>'
refhtml+='</ol></div>'
start=page.index('<section id="sources">');end=page.index('</section>',start);page=page[:end]+refhtml+page[end:]
page=page.replace('<body>','<body class="deep-research-edition">').replace('历史与制度比较</span>','深度研究版</span>')
page=page.replace('</style>','.deepchapter{margin:36px 0;padding-top:15px;border-top:1px solid #d8dbd0}.deepchapter h3{font-size:25px}.evidence{font-size:13px;color:#4e6865;padding:14px 18px;background:#e8eee7}.bibliography li{padding:12px 0}.bibliography p{font-size:13px;margin:4px 0}</style>')
page=page.replace('本报告没有足够统一资料计算整套伟大社会的净收益','本报告没有足够统一资料计算整套伟大社会的净收益')
(out/'index.html').write_text(page,encoding='utf-8')
plain=re.sub('<[^>]+>','',page);chars=len(re.findall('[\u4e00-\u9fff]',plain))
report=dict(edition='deep research',chinese_characters=chars,new_thematic_chapters=19,additional_references=len(refs),backup=str(backup),visual_qa='not completed: browser host unavailable, native approval timeout',not_systematic_review=True)
(out/'deep_research_verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))

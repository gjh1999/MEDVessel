"""Build a standalone offline report from reviewed sources and local diagnostic measurements."""
import base64
from html import escape
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def link(url, title):
    return f'<a href="{escape(url,quote=True)}">{escape(title)}</a>'


def card(kind, number, title, badges, paper, body, implementation, metrics, study, urls):
    return (f'<article class="method" data-kind="{kind}" id="method-{number}">'
        +''.join(f'<span class="badge">{escape(x)}</span>' for x in badges)
        +f'<h3>{number:02d} · {escape(title)}</h3><p class="paper-title">{paper}</p>'
        +f'<p>{body}</p><h4>源码从哪里读</h4><p>{implementation}</p>'
        +f'<h4>公开指标怎么解释</h4><p>{metrics}</p><h4>读完应掌握什么</h4><p>{study}</p>'
        +'<p>'+ ' · '.join(link(url,text) for url,text in urls)+'</p></article>')


def main():
    diagnostics = json.loads((ROOT/'build/imagecas-multicase/diagnostics.json').read_text())
    means = diagnostics['additional_macro_means']['baseline']
    page = (ROOT/'tools/coronary_report.template.html').read_text(encoding='utf-8')
    cards = []
    cards.append(card('cta',1,'VMTK：Fast Marching 初始化＋梯度驱动 Level Set',
        ['三维 CTA','半自动管腔分割','优先学习'],
        'Antiga 等，An image-based modeling framework for patient-specific computational hemodynamics，2008；官方另有冠脉 RCA 教程。',
        '输入原始 CTA、局部增强和用户端点，得到管腔标签与表面，再提取中心线。增强主要用于初始化；后续演化使用原图梯度与曲率约束。适合学习增强之后如何控制边界和分支范围。',
        '<code>SlicerExtension-VMTK/LevelSetSegmentation/LevelSetSegmentation.py</code> 是界面与逻辑入口；随后读 VMTK 的 <code>vmtkScripts</code> 与底层 C++。先追初始化，再追演化，最后看中心线；不是把 Python 界面代码误认成全部算法。',
        '官方冠脉教程给出单个 RCA 样例的中心线 OV=0.911、OF=0.837、OT=0.911、AI≈0.307 mm。<strong>这些不是管腔 Dice，也不是多病例平均。</strong>',
        '比较“增强门限直接生长”与“初始化后做边界演化”。记录端点、停止点和交互时间；交互式成绩与全自动成绩分开报告。',
        [('https://github.com/vmtk/vmtk','VMTK GitHub'),
         ('https://github.com/vmtk/SlicerExtension-VMTK','Slicer 扩展'),
         ('https://github.com/vmtk/SlicerExtension-VMTK/blob/master/Docs/CoronaryArteryCenterlineExtraction.md','官方冠脉教程与指标'),
         ('https://doi.org/10.1007/s11517-008-0420-1','论文')]))
    cards.append(card('cta',2,'CATE：冠脉分割到曲折度分析的完整应用',
        ['三维 CCTA','半自动工具','VMTK 同家族'],
        'Ferrari 等，CATE (Coronary Artery Tortuosity Evaluator): A Semi-Automatic Tool for Quantitative Assessment of Coronary Artery Tortuosity from CT Angiography，MeMeA 2024。',
        '输入 CCTA，依次执行 Frangi 预处理、Level Set 管腔分割与中心线 / 曲折度分析。它依赖 SlicerVMTK，<strong>不是独立于 VMTK 的新分割算法</strong>，但与我们正在建设的桌面流程非常接近。',
        '按 <code>Preprocessing → ArterySegmentation → TortuosityEvaluator</code> 三个目录阅读。观察同一影像、种子、表面和中心线怎样跨模块传递，再比较本项目的 MainWindow / JobConfig / 查看器职责。',
        '已核验的 README 没有给出可直接比较的冠脉体积分割 Dice；本轮论文全文受访问限制，<strong>不补写一个未核实的准确率</strong>。曲折度工具的验证也不能自动转成分割准确率。',
        '学习工具链、交互与几何分析怎样组织。先依赖官方扩展跑通同一支冠脉，再理解它增加的曲折度测量，而不是重复算作另一种分割家族。',
        [('https://github.com/mife-git/SlicerCATE','作者 GitHub'),
         ('https://ieeexplore.ieee.org/document/10596810','论文页面')]))
    cards.append(card('component',3,'Frangi / ThinVesselSegmentation：局部 Hessian 特征',
        ['三维增强组件','含 NMS 示例','不是完整冠脉基准'],
        'Frangi 等，Multiscale vessel enhancement filtering，MICCAI 1998。仓库另关联 Thin Structure Estimation with Curvature Regularization，ICCV 2015。',
        'Vesselness 模块将多尺度 Hessian 的特征值转成管状响应；NMS 示例提取局部中心位置。整个仓库还包含模型拟合等模块；只实现 Frangi＋区域生长，不能叫做完整复现其曲率正则化方法。',
        '先读 <code>Vesselness</code> 的示例与特征值分解，再看 <code>ModelFitting</code> 的数据项和成对平滑项。对照本项目 <code>Frangi.hpp</code> 的尺度、符号与强度参数，尤其留意参数单位和响应并非概率。',
        '仓库 README 未公布可直接对应当前 ImageCAS-X 管腔任务的 Dice。关联论文的其他任务指标不能挪给本项目；我们的成绩只属于自己的组合实现。',
        '真正理解“局部像管状”为什么不足以表示“属于冠脉”；掌握特征响应与分割决策的接口。这个入口最适合对照现有 C++ 算法读代码。',
        [('https://github.com/yzhong52/ThinVesselSegmentation','GitHub'),
         ('https://doi.org/10.1007/BFb0056195','Frangi 论文'),
         ('https://arxiv.org/abs/1506.04654','曲率正则化关联论文')]))
    cards.append(card('component',4,'RORPO：形态学路径响应替代局部 Hessian',
        ['三维管状增强','半全局路径特征','增强组件'],
        'Merveille、Talbot、Najman、Passat，Tubular Structure Filtering by Ranking Orientation Responses of Path Operators，ECCV 2014。',
        '利用不同方向的形态学路径响应及排序区分细长结构与其他结构。相对局部导数，它提供更长范围的形状信息；输出仍然是管状特征，后面仍需分割决策。不能直接宣称它能自动给出完整冠脉 Mask。',
        '按 <code>PO_3D → RPO → RORPO → RORPO_multiscale</code> 读 <code>libRORPO</code>；示例在 <code>RORPO_multiscale_Usage</code>。仓库明确要求各向同性体素，当前数据需先重采样并保留世界坐标；路径长度也不能直接当成毫米 sigma。',
        '官方项目确认论文与实现对应，但本轮未核实到可与我们同条件比较的冠脉 Dice。项目页及代码的可用性不等于本机已经复现论文实验。',
        '作为替代增强模块做公平消融：固定病例和下游流程，在开发集分别校准响应阈值；不同响应标度不能机械共用 0.08。',
        [('https://github.com/path-openings/RORPO','GitHub'),
         ('https://path-openings.github.io/RORPO/','作者项目页与用法'),
         ('https://hal.archives-ouvertes.fr/hal-00990014','论文归档（可能受访问保护）')]))
    cards.append(card('xca',5,'VCO：冠脉序列的对应关系与 MRF 优化',
        ['二维 X 光序列','传统图模型','不是三维 CTA 管腔分割'],
        'Shin 等，Extraction of Coronary Vessels in Fluoroscopic X-Ray Sequences Using Vessel Correspondence Optimization，MICCAI 2016；2022 年补充 arXiv 版本。',
        '给定源帧血管中心线，在下一帧生成分层对应候选，用 MRF 一元项和成对项保持外观与结构一致，再用 Fast Marching 补充新显影分支。源帧结构需人工或其他算法提供。',
        '按 <code>SampleCodeForVCO.m → VesselCorrespondenceOptimization.m → Point2PointMatching.m / ComputeCosts2 → GrowVesselUsingFastMarching.m</code> 阅读。README 明确依赖 MATLAB、VLFeat、MRF 与 Fast Marching 的 MEX 编译，不能假设克隆后即运行。',
        '论文 Exp1：5 位患者、18 段序列，共 599 对帧；使用真实源帧结构，中心线点在 2 像素内匹配算正确，Precision=0.905、Recall=0.841、F=0.872。<strong>这是带距离容差的中心线提取，不是三维管腔 Dice。</strong>',
        '理解“用结构约束局部检测”的思路，尤其是一元数据项与成对平滑项。若目标只做三维 CTA，可作为扩展阅读，不应优先移植整个二维时序工程。',
        [('https://github.com/kmu-ee-ailab/vco_v1.0_matlab','作者 GitHub'),
         ('https://arxiv.org/pdf/2207.13837','论文全文与表 1')]))
    page = page.replace('<!-- METHOD_CARDS -->','\n'.join(cards))
    for marker,key in [('MEAN_DICE','dice'),('MEAN_PRECISION','precision'),('MEAN_RECALL','recall')]:
        page = page.replace(f'<!-- {marker} -->',f'{means[key]:.4f}')
    table=[];options=[];case_data=[]
    for row in diagnostics['cases']:
        case=row['case_id'];metric=row['baseline'];ratio=metric['prediction_voxels']/metric['reference_voxels']
        note='原开发病例' if case=='1' else ('明显过分割' if metric['precision']<.4 else '仍有参考外预测')
        cells=[case]+[f'{metric[k]:.4f}' for k in ['dice','precision','recall']]+[f'{ratio:.2f} 倍',note]
        table.append('<tr>'+''.join('<td>'+escape(c)+'</td>' for c in cells)+'</tr>')
        options.append(f'<option value="{case}"'+(' selected' if case=='111' else '')+f'>病例 {case} · Dice {metric["dice"]:.4f}</option>')
        picture=ROOT/f'build/imagecas-multicase/case-{case}/comparison.png'
        image='data:image/png;base64,'+base64.b64encode(picture.read_bytes()).decode()
        case_data.append({'case_id':case,'baseline':metric,'image':image})
    page=page.replace('<!-- CASE_TABLE -->','\n'.join(table)).replace('<!-- CASE_OPTIONS -->',''.join(options))
    page=page.replace('<!-- DEFAULT_CASE_IMAGE -->',next(c['image'] for c in case_data if c['case_id']=='111'))
    for marker,name in [('FIG_PRECISION','precision-recall'),('FIG_PARTITIONS','gate-partitions'),('FIG_COUNTERFACTUALS','counterfactuals')]:
        svg=(ROOT/f'docs/figures/coronary-report/{name}.svg').read_text(encoding='utf-8')
        svg=svg[svg.index('<svg'):]
        # Namespace IDs for inline Matplotlib SVGs.
        import re
        for identifier in set(re.findall(r'\bid="([^"]+)"',svg)):
            svg=svg.replace(f'id="{identifier}"',f'id="{name}-{identifier}"')
            svg=svg.replace(f'="#{identifier}"',f'="#{name}-{identifier}"')
            svg=svg.replace(f'url(#{identifier})',f'url(#{name}-{identifier})')
        svg=svg.replace('<svg ', '<svg role="img" aria-label="'+escape(name)+'" style="max-width:100%;height:auto" ',1)
        page=page.replace(f'<!-- {marker} -->',svg)
    rows=[]
    for key,title,note in [
        ('baseline','原配置','响应≥0.08、强度范围、单种子可达'),
        ('remove_intensity_gate','移除强度门限','目标召回几乎不变，假阳性进一步增加'),
        ('lower_response_gate_004','低阈值改为 0.04','召回略增，精确率与 Dice 更低'),
        ('all_candidates_no_seed_constraint','所有候选直接纳入','保留原门限，去掉种子可达约束；候选里的误检很多')]:
        metric=diagnostics['additional_macro_means'][key]
        values=[title]+[f'{metric[k]:.4f}' for k in ['dice','precision','recall']]+[note]
        rows.append('<tr>'+''.join('<td>'+escape(v)+'</td>' for v in values)+'</tr>')
    page=page.replace('<!-- ABLATION_TABLE -->',''.join(rows))
    sources=[
        ('VMTK 原论文','https://doi.org/10.1007/s11517-008-0420-1','Antiga 等，2008。图像分割、中心线与几何建模框架。'),
        ('VMTK 冠脉官方教程','https://github.com/vmtk/SlicerExtension-VMTK/blob/master/Docs/CoronaryArteryCenterlineExtraction.md','已核对流程与 Accuracy of this workflow 段。OV、OF、OT、AI 属于单支中心线评价。'),
        ('VMTK 分割源码','https://github.com/vmtk/SlicerExtension-VMTK/blob/master/LevelSetSegmentation/LevelSetSegmentation.py','已核对界面与逻辑入口；本轮未构建外部工程。'),
        ('CATE 作者仓库','https://github.com/mife-git/SlicerCATE','README 将仓库映射到 MeMeA 2024 论文，给出三个应用模块；IEEE 全文页面受验证限制，未确认论文分割 Dice。'),
        ('Frangi 原论文','https://doi.org/10.1007/BFb0056195','MICCAI 1998，管状结构增强；不是某个数据集固定准确率的保证。'),
        ('ThinVesselSegmentation','https://github.com/yzhong52/ThinVesselSegmentation','README 核对 Vesselness / NMS / ModelFitting；没有从中找到可对应当前任务的 Dice。'),
        ('RORPO 官方项目','https://path-openings.github.io/RORPO/','确认 ECCV 2014 对应实现与各向同性要求；HAL 全文受访问保护，不填未核验数字。'),
        ('RORPO GitHub','https://github.com/path-openings/RORPO','公开增强组件的目录、接口与约束；未在本机运行。'),
        ('VCO 作者仓库','https://github.com/kmu-ee-ailab/vco_v1.0_matlab','README 经 GitHub API 核对；本地核对副本位于 build/research-sources/vco-readme.md。'),
        ('VCO 论文全文','https://arxiv.org/pdf/2207.13837','核对第 5～6 页实验与表 1：599 对帧、给定真实源中心线、2 像素匹配容差。'),
        ('统计区域生长论文（补充读物）','https://pmc.ncbi.nlm.nih.gov/articles/PMC5107877/','Tian 等，2016；未核实论文配套 GitHub，不列为可运行代码推荐。'),
        ('ImageCAS-X 官方基准','https://kitbransby.github.io/ImageCAS-X/','核对表 1 的 nnU-Net、CAS-Net 与 TotalSegmentator 结果；不与本地 ROI 分数直接排名。'),
        ('ImageCAS-X 实验代码','https://github.com/kitbransby/ImageCAS-X','核对统一配置、评估入口和 Included Methods；子模型为基准实现集合。'),
        ('nnU-Net 论文与官方实现','https://www.nature.com/articles/s41592-020-01008-z','Nature Methods 2021；官方代码见 https://github.com/MIC-DKFZ/nnUNet。'),
    ]
    page=page.replace('<!-- SOURCE_LIST -->','<ol class="sources">'+''.join(
        '<li>'+link(url,title)+'<br><span class="small">'+escape(note)+'</span></li>' for title,url,note in sources)+'</ol>')
    data={'cases':case_data,'means':diagnostics['additional_macro_means']}
    page=page.replace('<!-- REPORT_DATA -->',json.dumps(data,ensure_ascii=False).replace('</', '<' + chr(92) + '/'))
    if re.search(r'<!-- [A-Z_]+ -->', page):
        raise ValueError('Unfilled report template marker')
    destination=ROOT/'docs/traditional_coronary_report.html'
    destination.write_text(page,encoding='utf-8')
    (ROOT/'docs/figures/coronary-report/sources.json').write_text(json.dumps(
        {'checked_date':'2026-11-04','sources':[dict(title=t,url=u,note=n) for t,u,n in sources]},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(f'Built {destination}: {len(page.encode("utf-8")):,} bytes; {len(cards)} method entries; {len(case_data)} cases')


if __name__=='__main__':
    main()

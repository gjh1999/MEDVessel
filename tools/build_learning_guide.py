"""Build the offline overview, algorithm lessons, and separate Qt handbook."""
import base64
from html import escape
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
DOCS = [
    ("gui.md", "ref-gui", "Qt 桌面工作台", "打开使用、ROI 与种子、构建、任务状态与当前边界"),
    ("build_windows.md", "ref-build", "构建与依赖", "安装、完整构建、核心构建与工具链问题"),
    ("user_guide.md", "ref-use", "运行与参数", "所有程序、CLI 默认值、分割模式和排错"),
    ("output_formats.md", "ref-output", "输出字段", "每种文件、CSV 字段、单位、空值和 JSON 语义"),
    ("architecture.md", "ref-code", "架构与接口", "模块职责、数据契约和 C++ 接口定位"),
    ("algorithms.md", "ref-math", "算法实现约定", "数值校正、边界条件、拓扑与指标定义"),
    ("reference_data.md", "ref-data", "数据与实验复现", "数据来源、版本、校验、脚本和完整实验记录"),
    ("validation.md", "ref-checks", "验证证据", "测试覆盖、实验结果、失败案例和验收条件"),
    ("progress.md", "ref-progress", "进度与剩余工作", "与原始开发计划逐阶段对照"),
]
LINKS = {name: "#" + anchor for name, anchor, *_ in DOCS}
LINKS["README.md"] = "#learn-start"
LINKS["medical_vessel_analysis_project.html"] = "#roadmap"
LINKS["qt_development.md"] = "#learn-qt"


def href(url):
    name = url.split("#", 1)[0]
    return LINKS.get(name, url)


def inline(text):
    tokens = []

    def token(value):
        tokens.append(value)
        return f"\x00{len(tokens)-1}\x00"

    text = re.sub(r"`([^`]+)`", lambda m: token("<code>" + escape(m[1]) + "</code>"), text)
    text = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", lambda m: token('<a href="' + escape(href(m[2]), quote=True) + '">' + escape(m[1]) + "</a>"), text)
    text = escape(text)
    text = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", text)
    text = re.sub(r"(?<!\*)\*([^*]+)\*(?!\*)", r"<em>\1</em>", text)
    return re.sub("\x00(\\d+)\x00", lambda m: tokens[int(m[1])], text)


def markdown(text, anchor):
    lines = text.splitlines()
    result = []
    index = 0
    heading = 0
    while index < len(lines):
        line = lines[index]
        if not line.strip():
            index += 1
            continue
        graphic = re.fullmatch(r'!\[([^\]]+)\]\(figures/([^/)]+\.svg)\)', line.strip())
        if graphic:
            result.append('<div class="diagram-scroll">' + inline_svg(graphic[2], graphic[1], graphic[1] + '；窄屏可横向滑动查看。') + '</div>')
            index += 1
            continue
        if line.startswith("```"):
            language = line[3:].strip()
            index += 1
            code = []
            while index < len(lines) and not lines[index].startswith("```"):
                code.append(lines[index])
                index += 1
            if index == len(lines):
                raise ValueError("Unclosed code fence")
            index += 1
            if language == "mermaid":
                result.append('<p class="note">调用关系已绘制在本页的<a href="#overview-flow">总体流程图</a>中。</p>')
            else:
                result.append('<pre data-language="' + escape(language, quote=True) + '"><code>' + escape("\n".join(code)) + "</code></pre>")
            continue
        match = re.match(r"^(#{1,6})\s+(.+)$", line)
        if match:
            index += 1
            if len(match[1]) == 1:
                continue  # The surrounding reference accordion supplies its title.
            heading += 1
            level = min(len(match[1]) + 1, 6)
            result.append(f'<h{level} id="{anchor}-h{heading}">' + inline(match[2]) + f"</h{level}>")
            continue
        if line.startswith("|"):
            rows = []
            while index < len(lines) and lines[index].startswith("|"):
                cells = [cell.strip() for cell in lines[index].strip().strip("|").split("|")]
                if not all(re.fullmatch(r":?-{2,}:?", cell) for cell in cells):
                    rows.append(cells)
                index += 1
            result.append('<div class="table-scroll"><table><thead><tr>' + "".join("<th>" + inline(cell) + "</th>" for cell in rows[0]) + "</tr></thead><tbody>")
            for row in rows[1:]:
                result.append("<tr>" + "".join("<td>" + inline(cell) + "</td>" for cell in row) + "</tr>")
            result.append("</tbody></table></div>")
            continue
        match = re.match(r"^(?:([-*]) |(\d+)\. )(.+)$", line)
        if match:
            ordered = bool(match[2])
            tag = "ol" if ordered else "ul"
            result.append("<" + tag + ">")
            while index < len(lines):
                item = re.match(r"^(?:([-*]) |(\d+)\. )(.+)$", lines[index])
                if not item or bool(item[2]) != ordered:
                    break
                result.append("<li>" + inline(item[3]) + "</li>")
                index += 1
            result.append("</" + tag + ">")
            continue
        paragraph = []
        while index < len(lines) and lines[index].strip() and not re.match(r"^(?:#{1,6} |```|\||[-*] |\d+\. )", lines[index]):
            paragraph.append(lines[index])
            index += 1
        result.append("<p>" + inline(" ".join(paragraph)) + "</p>")
    return "\n".join(result)


def fold_sections(body):
    """Keep existing deep links while making each generated lesson opt-in."""
    parts = re.split(r'(<h3 id="[^"]+">.*?</h3>)', body)
    result = [parts[0]]
    for index in range(1, len(parts), 2):
        heading = re.fullmatch(r'<h3 id="([^"]+)">(.*?)</h3>', parts[index])
        result.append(f'<details class="lesson-detail" id="{heading[1]}"><summary>{heading[2]}</summary>'
                      f'<div class="reference-body">{parts[index+1]}</div></details>')
    return "\n".join(result)


def inline_svg(filename, alt, caption):
    # Drop XML/DOCTYPE wrappers; all plots stay vector and self-contained offline.
    svg = (ROOT / "docs/figures" / filename).read_text(encoding="utf-8")
    svg = svg[svg.index("<svg"):]
    if not re.search(r'<svg\b[^>]*\brole=', svg):
        svg = svg.replace("<svg ", '<svg role="img" ', 1)
    if not re.search(r'<title\b', svg):
        svg = re.sub(r'(<svg\b[^>]*>)', lambda m: m[1] + '<title>' + escape(alt) + '</title>', svg, count=1)
    # Matplotlib uses document-global IDs; namespace each embedded figure.
    namespace = filename.replace(".svg", "") + "-"
    for identifier in set(re.findall(r'\bid="([^"]+)"', svg)):
        svg = svg.replace(f'id="{identifier}"', f'id="{namespace}{identifier}"')
        svg = svg.replace(f'="#{identifier}"', f'="#{namespace}{identifier}"')
        svg = svg.replace(f'url(#{identifier})', f'url(#{namespace}{identifier})')
    return '<figure class="teaching-figure">' + svg + '<figcaption>' + escape(caption) + '</figcaption></figure>'


def main():
    template = (ROOT / "tools/learning_guide.template.html").read_text(encoding="utf-8")
    enhancement = (ROOT / "tools/enhancement_lesson.html").read_text(encoding="utf-8")
    plots = [
        ("DERIVATIVES", "enhancement-derivatives.svg", "同一横坐标下的灰度、一阶和二阶导数曲线", "归一化教学剖面：暗线中心一阶为零、二阶为正；不是实际病例强度。"),
        ("EIGENVECTORS", "enhancement-eigenvectors.svg", "黑色弯管上的局部切线和法线特征向量", "二维理想局部示意：蓝绿色为切线 v₁，橙色为法线 v₂。箭头表示无正负定向的轴，不代表血流方向。"),
        ("SCALES", "enhancement-scales.svg", "细线和粗线的尺度归一化二阶响应随 sigma 的变化", "二维理想 Gaussian 亮线中心的 σ² 归一化横向二阶导幅值，各曲线按自身最大值归一化；不是项目三维 Frangi 实测分数，也不是直径标定。"),
    ]
    for marker, filename, alt, caption in plots:
        enhancement = enhancement.replace(f"<!-- ENHANCEMENT_{marker} -->", inline_svg(filename, alt, caption))
    template = template.replace("<!-- ENHANCEMENT_LEARNING_CONTENT -->", enhancement)
    template = template.replace("<!-- PIPELINE_LEARNING_CONTENT -->", (ROOT / "tools/pipeline_lessons.html").read_text(encoding="utf-8"))
    reference = []
    for name, anchor, title, description in DOCS:
        body = markdown((ROOT / "docs" / name).read_text(encoding="utf-8"), anchor)
        entry = f'<details class="reference" id="{anchor}"><summary><span>{title}</span><small>{description}</small></summary><div class="reference-body">{body}</div></details>'
        if name == "gui.md":
            template = template.replace("<!-- QT_REFERENCE_CONTENT -->", entry)
        else:
            reference.append(entry)
    template = template.replace("<!-- REFERENCE_CONTENT -->", "\n".join(reference))
    lesson_source = (ROOT / "docs/qt_development.md").read_text(encoding="utf-8")
    lesson = fold_sections(markdown(lesson_source, "qt"))
    lesson_headings = re.findall(r'^## (.+)$', lesson_source, re.MULTILINE)
    toc = '<details class="aside-detail"><summary>查看 Qt 章节目录 · 点击条目自动展开</summary><div class="grid">'
    toc += ''.join(f'<a href="#qt-h{i}">' + escape(title) + '</a>' for i, title in enumerate(lesson_headings, 1))
    toc += '</div></details>'
    template = template.replace("<!-- QT_LEARNING_CONTENT -->", toc + lesson)
    code_source = (ROOT / "docs/code_walkthrough.md").read_text(encoding="utf-8")
    def excerpt(match):
        path, start, end = match.group(1).split("|")
        source = (ROOT / path).read_text(encoding="utf-8")
        begin = source.index(start)
        finish = source.index(end, begin)
        line = source[:begin].count("\n") + 1
        return f"来源：[{path}，第 {line} 行](../{path})，连续源码节选。\n\n```cpp\n{source[begin:finish].rstrip()}\n```"
    code_source = re.sub(r"<!-- SOURCE: (.+?) -->", excerpt, code_source)
    def code_blocks(source, prefix):
        parts = re.split(r'(<h3 id="[^"]+">.*?</h3>)', markdown(source, prefix))
        return {index//2+1: fold_sections(re.sub(r'(<h3 id="[^"]+">)(?:\d+\.\s*)?', r'\1核心代码 · ', parts[index])+parts[index+1]) for index in range(1,len(parts),2)}
    blocks = code_blocks(code_source, "code")
    for marker, indices in [("VOLUME", [1,2]), ("ENHANCE", [3,4]), ("SEGMENT", [5]), ("EVALUATE", [6])]:
        template = template.replace(f"<!-- CODE_{marker} -->", "".join(blocks[i] for i in indices))
    pipeline_source = (ROOT / "docs/pipeline_code.md").read_text(encoding="utf-8")
    pipeline_source = re.sub(r"<!-- SOURCE: (.+?) -->", excerpt, pipeline_source)
    blocks = code_blocks(pipeline_source, "pipeline-code")
    template = template.replace("<!-- CODE_SKELETON -->", blocks[1]+blocks[2])
    template = template.replace("<!-- CODE_GEOMETRY -->", blocks[3])
    qt_figure = ROOT / "build/gui-real-preview.png"
    if qt_figure.exists():
        qt_src = "data:image/png;base64," + base64.b64encode(qt_figure.read_bytes()).decode("ascii")
        qt_content = '<figure><img src="' + qt_src + '" alt="Qt 工作台实际窗口：左侧参数、中间血管投影、右侧结果摘要与底部日志" loading="lazy"><figcaption>当前 Qt 6 工作台，打开 ImageCAS 病例 1 的参考辅助局部开发结果。中间是二维投影，不是三维渲染；左侧参数用于提交新任务，不会因打开结果而自动恢复为该实验参数。</figcaption></figure>'
    else:
        qt_content = '<p class="note">未嵌入窗口截图。下面的结构图、示例与讲解仍可完整阅读。</p>'
    three_d_figure = ROOT / "build/gui-3d-real-preview.png"
    if three_d_figure.exists():
        three_d_src = "data:image/png;base64," + base64.b64encode(three_d_figure.read_bytes()).decode("ascii")
        qt_content += '<figure><img src="' + three_d_src + '" alt="VTK 三维血管表面、青绿色中心线、橙色选中分支与 RAS 方向角标" loading="lazy"><figcaption>真实 Windows OpenGL 渲染的 ImageCAS 局部开发结果。半透明网格来自 Mask，橙色为选中分支；三维展示不会改变分割、测量或评价结果。</figcaption></figure>'
    result_slice_figure = ROOT / "build/gui-result-slices-real.png"
    if result_slice_figure.exists():
        result_slice_src = "data:image/png;base64," + base64.b64encode(result_slice_figure.read_bytes()).decode("ascii")
        qt_content += '<figure><img src="' + result_slice_src + '" alt="三维分支对应的三方向结果切片，黄色十字定位、橙色 Mask 叠加" loading="lazy"><figcaption>结果定位页：按运行记录读取底图和 ROI，三维世界坐标转换为结果网格索引，黄色十字标出目标；此页不会改动左侧分析配置。</figcaption></figure>'
    qt_content = re.sub(r'<figure>.*?</figure>', lambda m: '<details class="aside-detail"><summary>查看截图 · ' + re.search(r'alt="([^"]+)"', m[0])[1] + '</summary><div>' + m[0] + '</div></details>', qt_content, flags=re.DOTALL)
    template = template.replace("<!-- QT_FIGURE -->", qt_content)
    figure = ROOT / "build/imagecas-x-case1/result/comparison.png"
    if figure.exists():
        src = "data:image/png;base64," + base64.b64encode(figure.read_bytes()).decode("ascii")
        content = '<figure><a href="' + src + '" target="_blank" rel="noopener"><img src="' + src + '" alt="真实冠脉局部样例：原图、Frangi 响应与预测和参考标注的对照；包含最大投影与单切片" loading="lazy"></a><figcaption>ImageCAS 病例 1，ImageCAS-X v2 标注。橙色为预测，青色为参考；ROI 与种子使用参考标签辅助选取。点击可查看大图。图像已嵌入本文件，移动网页后仍可查看。</figcaption></figure>'
    else:
        content = '<p class="note">当前构建未嵌入对照图。按本页复现步骤生成 PNG 后，再运行文档生成器即可嵌入；文字与流程图不受影响。</p>'
    template = template.replace("<!-- CASE_FIGURE -->", content)
    destination = ROOT / "docs/medvessel_learning_guide.html"
    destination.write_text(template, encoding="utf-8")
    print(f"Built {destination.name}: {destination.stat().st_size:,} bytes; {len(DOCS)} reference chapters; embedded figure={figure.exists()}")


if __name__ == "__main__":
    main()

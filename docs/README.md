# MEDVessel 文档入口

**推荐入口：[项目学习网页](medvessel_learning_guide.html)。** 单文件手册分为同级的「总体概览」「深入学习」「Qt 桌面开发」三页。算法页按影像、增强、分割、骨架与图、测量、实验六章阅读；代码放在对应章节，细节默认折叠。原始文档保留为维护来源。

维护网页：修改源文档或 `tools/learning_guide.template.html` 后，在仓库根目录运行 `python tools/build_learning_guide.py`。生成时会自动嵌入本机已有的真实样例对照图。

Gaussian → Hessian → Frangi 的折叠课程维护在 `tools/enhancement_lesson.html`。教学图为 `docs/figures` 中的 SVG，生成网页无需联网或绘图库；修改图示时运行 `python tools/plot_enhancement_guide.py`（需要 Matplotlib、NumPy），再生成网页。代码精读与 Qt 细节默认折叠，章节链接会自动展开对应条目。

分割到实验的课程维护在 `tools/pipeline_lessons.html`；`docs/code_walkthrough.md` 与 `docs/pipeline_code.md` 中的源码片段在生成时分配到各章，不再集中为单独的代码章节。Qt 开发说明和桌面操作查阅放在独立 Qt 页。

当前已跑通算法主流程：读取 → ROI → Gaussian 导数 / Hessian → Jacobi → Frangi → 分割 → EDT → 细化 → 分支图 → 几何报告。
已验证合成数据和一个真实冠脉病例的参考辅助局部区域；这不等于全计划完成或全图自动分割已经验证。

| 文档 | 内容 |
| --- | --- |
| [Qt 桌面工作台](gui.md) | 启动、界面操作、ROI/种子、配置、任务记录、打包与测试 |
| [Qt 开发讲解](qt_development.md) | 从零理解 Widgets、事件循环、信号槽、参数链、QProcess、日志、状态、对象生命周期及修改练习 |
| [原始网页方案](medical_vessel_analysis_project.html) | 项目目标、数学背景、阶段计划；新增当前实现状态入口 |
| [使用手册](user_guide.md) | 程序用途、完整参数、输入要求、运行和故障排查 |
| [Windows 构建](build_windows.md) | 本机工具链、依赖准备、配置、构建和测试 |
| [架构与接口](architecture.md) | 模块职责、调用关系、C++ 接口与坐标契约 |
| [输出格式](output_formats.md) | NIfTI、CSV、JSON、预览图的字段及单位 |
| [算法约定](algorithms.md) | 数值实现、边界条件、拓扑及评价定义 |
| [数据与复现](reference_data.md) | 数据来源、版本、校验、脚本、真实样例与失败案例 |
| [验证记录](validation.md) | 测试范围、结果、结果解释和后续验收条件 |
| [进度与限制](progress.md) | 与原开发计划逐阶段对照、未完成工作 |

建议先按构建文档运行合成示例，再按数据文档复现冠脉样例，最后结合输出格式阅读报告。
所有文档中的测试数与实验数值都是当前记录，不会在构建时自动更新。

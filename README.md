# MEDVessel

**桌面版：** 双击 `build/desktop/medvessel_gui.exe`，或运行 `./tools/start_gui.ps1`。点击「载入合成示例」→「开始分析」体验全流程。使用和构建见 [Qt 桌面工作台](docs/gui.md)。

想学习 Qt 怎么写：打开 [学习网页的 Qt 开发章节](docs/medvessel_learning_guide.html#learn-qt)，或阅读 [Qt 开发讲解源码](docs/qt_development.md)。

**第一次了解项目，请打开 [项目学习网页](docs/medvessel_learning_guide.html)。** 默认阅读「总体概览」；需要理解原理、代码和复现方法时，再切换「深入学习」。完整参数与实验记录也已收录在同一网页中，可离线阅读。

按照 [开发方案](docs/medical_vessel_analysis_project.html) 实现 C++17 三维医学血管分析系统。
核心算法自行实现，先验证单线程正确性，再推进性能和可视化。

完整文档从 [文档入口](docs/README.md) 开始：

新增专题：[传统冠脉分割开源路线与泛化诊断](docs/traditional_coronary_report.html)，整理论文与 GitHub，并基于新增十例的精确率、召回率及门限回放解释失败原因。

| 需要做什么 | 文档 |
| --- | --- |
| 构建与准备依赖 | [Windows 构建](docs/build_windows.md) |
| 运行、查参数、排错 | [使用手册](docs/user_guide.md) |
| 理解代码与接口 | [架构与接口](docs/architecture.md)、[算法约定](docs/algorithms.md) |
| 阅读输出文件 | [输出格式](docs/output_formats.md) |
| 复现实验与理解结果 | [数据与复现](docs/reference_data.md)、[验证记录](docs/validation.md) |
| 查看完成程度与剩余任务 | [进度记录](docs/progress.md) |

当前已跑通 NIfTI → 多尺度 Frangi → 分割 → EDT → 细化 → 分支图 → CSV/NIfTI 报告的单线程算法基线。
可选启用 DICOM CT 输入、OpenMP 和 GoogleTest。
坐标方向矩阵按行存储，支持旋转和反射；体素按 x 最快、其次 y、最后 z 排列。

构建与测试（需要 CMake 和 C++17 编译器）：

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

开发进度见 [进度记录](docs/progress.md)。

构建后运行 `build/medvessel_demo`（Windows 为 `.exe`；多配置生成器位于对应配置目录），执行无数据依赖的合成圆柱滤波与重采样演示。

Gaussian 使用毫米 sigma、截断归一化可分离核和边界复制。
重采样显式指定输出几何与尺寸，源体素中心范围外使用常量填充值。
降采样前需按目标分辨率进行抗混叠滤波；标签数据不可使用三线性插值。

## NIfTI 与完整 CLI

从 [官方 nifti_clib](https://github.com/NIFTI-Imaging/nifti_clib) 获取源码，并准备 zlib。
格式库只负责影像读写，核心算法使用本项目实现。

```sh
cmake -S . -B build -DMEDVESSEL_NIFTI_SOURCE_DIR=/path/to/nifti_clib
cmake --build build
ctest --test-dir build --output-on-failure
build/medvessel_phantom build/phantom
build/medvessel_cli build/phantom/phantom.nii.gz build/results
```

CLI 可选参数：`--scales 0.5,1,2`、`--alpha 0.5`、`--beta 0.5`、`--c 100`、`--dark`、
`--low 0.1`、`--high 0.3`、`--min-intensity 100`、`--max-intensity 800`、`--connectivity 6`、`--seed 16,16,8`。
种子可重复指定；无种子时使用双阈值连通传播。默认参数仅为合成验证起点。
`--roi x,y,z,w,h,d` 在物理坐标不变的前提下裁剪；种子索引相对于裁剪后的处理网格。
还可选 `--nms-seeds --nms-step 1`、`--min-component 100`、`--prune-mm 2`。
`--min-hu/--max-hu` 是灰度约束的别名，只有确认输入确为 HU 时才按 HU 解读。

输出包括 `vesselness.nii.gz`、`best_scale_mm.nii.gz`、`mask.nii.gz`、`skeleton.nii.gz`、
`radius_field_mm.nii.gz`、`branches.csv`、`centerline.csv`、`sections.csv`、`branch_angles.csv`、
`nodes.csv`、`preview.svg` 和 `run.json`。
坐标为 RAS 毫米，直径为 EDT 近似；真实 CTA 目前只验证了参考辅助局部样例，复杂骨架和独立多病例表现仍需验证。

本机 Windows 构建命令见 [本机构建说明](docs/build_windows.md)。

文档参考仓库的数据已下载，来源与 VMTK 样例实验见 [参考数据记录](docs/reference_data.md)。
已下载并运行 11 例 ImageCAS CTA 与 ImageCAS-X v2 配对标注（共 2766 张 Z 切片）。固定参数的新增 10 例参考辅助局部 ROI 平均 Dice 约 0.5100；原开发病例约 0.8162，不代表总体性能。逐例结果、图片与复现见 [多病例记录](docs/multicase.md)。
本机可执行 `./tools/run_imagecas_sample.ps1 -PlotPython C:/Users/1/anaconda3/python.exe` 复现并生成对照图。
`medvessel_evaluate` 可计算 Dice、HD95、clDice、中心线距离和连通分量数。
`medvessel_export` 导出 float32 原始数据及几何 JSON，供独立绘图工具使用。
# 最新实验与评价

- [CTA + Frangi 双通道 nnU-Net 实验文档](docs/nnunet_hybrid.html)：独立目录 `experiments/nnunet_frangi`，本阶段只验证数据与前向，不训练；含 RTX 5080 换机步骤和病例规模建议。

- [3D 血管主流指标、统一重算与工业落地](docs/evaluation_and_industry.html)：主指标 Dice / HD95，补充 ASSD / clDice。
- [核心补边优化及 11 例图像对照](docs/segmentation_optimization.html)：新增 10 例开发 ROI 的平均 Dice 从 0.5100 提高到 0.7307；参数使用这些病例探索，尚非独立测试结论。
- 统一评估入口：`tools/evaluate_standard.py`，汇总：`tools/report_standard_metrics.py`。旧 C++ 指标保留作历史记录，主表读取 `metrics_standard.json`。

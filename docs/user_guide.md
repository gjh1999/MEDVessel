# 使用手册

桌面界面已提供 Qt 6 工作台，可选影像、设参数、启动/停止与查看结果；首次使用请看 [桌面工作台](gui.md)。以下保留 CLI 用法。

## 当前交付范围

这是 C++17 研究型命令行 Demo。核心算法自行实现，NIfTI/DCMTK 负责格式解析；Python 辅助脚本负责数据获取、格式转换和绘图。
已经验证合成直管、Y 型、弯曲管、带噪斜管，以及实际 ImageCAS 病例 1 的局部冠脉区域。
尚无 Qt/VTK 界面、全图自动冠脉定位、独立多病例评估或临床测量验证。

以下 PowerShell 命令在项目根目录执行，假设已按 [构建说明](build_windows.md) 生成 `build/cmake`。
多配置生成器的可执行文件可能位于 `Release/`；下文采用已验证的 Ninja Release 构建。

## 选择程序

| 程序 | 用途 | 构建条件 |
| --- | --- | --- |
| medvessel_demo | 内存中的圆柱滤波与重采样演示 | 核心构建即可 |
| medvessel_benchmark | 两尺度 Frangi 计时，启用 OpenMP 时比较 1/4 线程结果 | 核心构建即可 |
| medvessel_cli | 主处理流程与报告 | 启用 NIfTI；目录输入另需 DCMTK |
| medvessel_phantom | 写出合成影像和参考 Mask | 启用 NIfTI |
| medvessel_evaluate | 比较预测与参考，写评价 JSON | 启用 NIfTI |
| medvessel_prepare_case | 从参考标签辅助选择局部开发 ROI 和种子 | 启用 NIfTI |
| medvessel_export | 导出 float32 原始体数据及几何 JSON | 启用 NIfTI |

传参不足返回码为 2，运行异常为 1，成功为 0。没有统一的 `--help`/`--version` 选项；不带参数运行带参程序可查看其用法。

## 合成数据全流程

```powershell
& ./build/cmake/medvessel_phantom.exe build/demo/input straight
& ./build/cmake/medvessel_cli.exe build/demo/input/phantom.nii.gz build/demo/output
& ./build/cmake/medvessel_evaluate.exe build/demo/output/mask.nii.gz `
  build/demo/input/reference_mask.nii.gz build/demo/output/metrics.json
```

场景可改为 `y`、`curved`、`noisy`。参考 Mask 是生成器给出的几何真值。
打开输出目录的 `preview.svg` 查看投影；它会选择物理范围最大的两个图像轴，并保持物理纵横比。
投影会叠加不同深度的结构，不能单凭投影判断三维连通性。

## 真实冠脉样例复现

```powershell
& ./tools/run_imagecas_sample.ps1 -PlotPython C:/Users/1/anaconda3/python.exe
```

脚本校验/获取 ImageCAS v3 病例 1 和 ImageCAS-X v2 标签，准备 ROI、运行主程序、评估，并可绘制 PNG。
不需要绘图时省略 `-PlotPython`；图像获取与主算法不依赖 NumPy/Matplotlib。
结果位于 `build/imagecas-x-case1/result`。

**该脚本使用参考标签选择 ROI 和种子。** 这属于开发样例，不是全图自动推理或独立测试。
`roi.json` 和 `experiment.json` 保存这一评估范围；不要只拷贝 `metrics.json` 而丢弃这些说明。
网络读取有范围限制和缓存，首次运行仍需联网；源文件与派生结果分别保留。
详细来源、校验值、许可和范围见 [数据与复现](reference_data.md)。

## 主程序与参数

```text
medvessel_cli input.nii[.gz] output_directory [options]
medvessel_cli dicom_directory output_directory [options]
```

| 参数 | 默认值 | 作用与范围 |
| --- | --- | --- |
| --scales | 0.5,1,2 | 逗号分隔的正 sigma，单位 mm，按给定顺序遍历 |
| --alpha | 0.5 | Frangi 管/片状比参数，正数 |
| --beta | 0.5 | Frangi blob 抑制参数，正数 |
| --c | 100 | 结构强度参数，正数；与输入灰度尺度有关 |
| --dark | 关闭 | 切换到暗管；默认增强亮管 |
| --low | 0.1 | 生长候选点的 vesselness 下限 |
| --high | 0.3 | 双阈值或 NMS 自动种子的下限 |
| --min-intensity | 无下限 | 原图灰度下限 |
| --max-intensity | 无上限 | 原图灰度上限 |
| --min-hu / --max-hu | 同上 | 灰度参数的别名；只有输入已确认为 HU 时才按 HU 理解 |
| --connectivity | 26 | 生长和连通域过滤使用 6 或 26 邻接 |
| --roi | 不裁剪 | `x,y,z,width,height,depth`，非空且在输入范围内 |
| --seed | 无 | `x,y,z`，可重复指定，使用 **裁剪后的零基体素索引** |
| --nms-seeds | 关闭 | 没有手工种子时，在垂直管方向的 NMS 图上选种子 |
| --nms-step | 1 | NMS 比较距离，正数，单位 mm |
| --min-component | 0 | 删除少于此体素数的前景连通域；0 表示关闭 |
| --prune-mm | 0 | 删除短于此长度的终末骨架支；0 表示关闭 |

要求 `0 ≤ low ≤ high ≤ 1`，灰度下限不能大于上限。数值必须有效；没有自动参数搜索。

分割模式按以下规则选择：

1. 有手工种子：从这些点生长；`--high` 不限制手工种子的初始化，种子仍需满足 low 和灰度约束。
2. 无手工种子且启用 NMS：从大于 0 且达到 high 的 NMS 点生长；无候选种子时输出空 Mask。
3. 两者都没有：将所有达到 high 的点作为双阈值传播种子。

NMS 只筛选种子，实际生长使用完整响应图，不把 NMS 线直接当成管腔 Mask。
种子在范围内但不满足候选条件时会被忽略，越界种子会报错。
改变连通性可能影响细小分支和斜向连接；`--connectivity` 不改变细化固定的前景 26/背景 6 约定。
短枝修剪可能删除真实小分支，因此默认关闭。

例如，处理裁剪后的局部影像：

```powershell
$env:OMP_NUM_THREADS='4'
& ./build/cmake/medvessel_cli.exe build/imagecas-x-case1/source.nii.gz build/manual-run `
  --scales 0.5,1,1.5,2 --c 100 --low 0.08 --high 0.2 --seed 54,54,40 `
  --min-intensity 100 --max-intensity 900 --min-component 20 --prune-mm 1
```

这些参数来自病例 1 的开发样例，不能直接当作其他病例的最优参数。
重复使用输出目录会覆盖同名派生结果；建议不同病例/参数使用不同目录。
输出名与输入影像冲突时程序拒绝写入。运行失败可能留下部分派生文件，不能把目录存在当作运行成功。

## 输入约定

NIfTI：支持 `.nii` / `.nii.gz`、标量空间三维，额外维度长度不得大于 1。
支持 uint8/int8/uint16/int16/uint32/int32/float32/float64，按 slope/intercept 转换为 float。
需要有效 sform 或 qform，sform 优先；m、mm、μm 转为 mm。
未知单位、无有效变换、剪切网格、不支持的像素类型或非有限灰度会被拒绝。
它不会自动把任意 NIfTI 灰度解释为 HU。

DICOM：一个目录中一个 SeriesInstanceUID，经典单帧 CT、未封装压缩的 16-bit MONOCHROME2 像素。
按 ImagePositionPatient 在切片法向上的投影排序，不依赖文件名；PixelSpacing 的行/列顺序转换为 x/y spacing。
保留每片的 rescale 信息并转换为 HU，LPS 坐标转换为内部 RAS。
拒绝混合序列/尺寸/方向、重复或不均匀位置、gantry tilt、增强多帧 CT 及不支持的压缩像素。
目前只通过生成的 DICOM 测试验证，尚未完成真实厂商序列兼容性评估。

## 评价与辅助工具

```powershell
& ./build/cmake/medvessel_evaluate.exe prediction.nii.gz reference.nii.gz metrics.json
```

默认将正值作为前景，要求相同网格。`--reference-levelset-negative` 将参考负值作为内部；
`--align-reference` 显式将参考最近邻采样到预测网格，适合 ROI 输出，评价范围随预测网格改变。
这两个选项可以同时使用。命令不会推断参考是否完整、是否为人工金标准。

```powershell
& ./build/cmake/medvessel_prepare_case.exe source.nii.gz label.nii.gz build/local-case 20
& ./build/cmake/medvessel_export.exe volume.nii.gz build/export/volume --roi 0,0,0,32,32,32
```

前者的 20 是 ROI 半宽 mm，默认也是 20；使用标签 EDT 最大有限值选择参考辅助种子。
后者写出 `.raw` 和 `.json`，NumPy 读取形状为 `(depth,height,width)`。

## 常见问题

| 现象 | 检查与处理 |
| --- | --- |
| 找不到 CLI | 只做了核心构建；启用 NIfTI 源码路径后重新配置 |
| DICOM support was not enabled | 构建时没有启用 DCMTK |
| NIfTI physical units are unspecified | 核对元数据；仅在有明确配对几何证据时使用转换工具的 `--geometry-from`，保留原件 |
| 网格不一致 | 检查尺寸、spacing、origin、direction；确需重采样参考时显式使用 align 选项 |
| Mask 为空 | 检查亮暗管模式、尺度、c、阈值、灰度约束、种子及 ROI 后索引 |
| 区域泄漏 | 检查种子范围、ROI、low 与灰度门控；用三维切片/对照图判断，记录失败案例 |
| 半径场未输出 | 全前景无体积内背景，EDT 无穷；相应旧半径文件会被移除 |
| CSV 有空字段 | 几何量未定义或截面被截断；结合 status 检查，不能按 0 解释 |
| 大体积慢或内存较高 | 使用合理 ROI；OpenMP 只加速卷积；目前尚无完整大体积峰值内存基准 |
| 下载超时 | 重跑获取脚本，复用分块缓存；CRC/SHA 不匹配时先排查缓存/文件，不能跳过校验 |

字段定义见 [输出格式](output_formats.md)，算法限制见 [进度记录](progress.md)。

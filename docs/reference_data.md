# 参考仓库的数据与实验

本地数据位于 `datasets/`（不提交版本管理）。已获取下列公开示例，无需用户自行提供影像。

已提供带 SHA256 校验的获取脚本和完整复现脚本：

```powershell
& ./tools/fetch_reference_data.ps1
& ./tools/run_reference.ps1 -PlotPython C:/Users/1/anaconda3/python.exe
```

复现脚本要求先完成 C++ 构建，并在 `.venv` 安装转换依赖。
`-PlotPython` 指向有 NumPy/Matplotlib 的 Python；省略时仍生成 NIfTI、CSV、JSON 和 SVG。

## 已下载

| 来源 | 文件 | 大小 | 用途 |
| --- | --- | --- | --- |
| [ThinVesselSegmentation/data](https://github.com/yzhong52/ThinVesselSegmentation/tree/master/data) | data15.data | 3,512,944 字节 | 原作者示例，134×113×116，大端 int16 |
| 同上 | data15.data.readme.txt | 82 字节 | 尺寸、类型、端序说明 |
| [VMTK 测试数据](https://github.com/vmtk/vmtk-test-data/tree/7f5abc07a33e1037a106fa4e0ac6c320adb1baa1/input) | aorta.mha | 3,399,202 字节 | 带几何元数据的主动脉影像，已接入流程 |
| 同上 | aorta-centerline.vtp | 14,614 字节 | 原仓库中心线；用于定位演示种子 |
| 同上 | aorta-final-levelset.mha | 本地已保存 | 原仓库算法分割参考，负值表示内部 |

VMTK 数据锁定到其主仓库测试子模块使用的提交 `7f5abc07a33e1037a106fa4e0ac6c320adb1baa1`。

SHA256：

- data15.data：`f50f771c585f3761b5880009a77ad9d1795bd9082420a51b58dbb4a04934e4f2`
- aorta.mha：`435c368b6ce04011b94bceb9f9a62110d49934417a96b73aafec57aba41cfca0`
- aorta-final-levelset.mha：`bf376aa5b4f72b68630e14266fdd060cfdb148ff1532f056fd11fadea42d1961`

data15 的说明不包含 spacing、origin 或 direction，尚未用于毫米测量。
VMTK 示例的采集与标注属性不能直接等同于冠状动脉 CTA 的完整人工标注数据集。

## VMTK 示例运行

`aorta.mha` 为 157×393×34，spacing 约 0.878906/0.878906/1.50009 mm。
使用 SimpleITK **仅做格式转换**，不调用其增强、分割、骨架等算法；核心流程仍是本项目 C++ 实现。
转换记录包含源文件与目标文件 SHA256、方向、位置、spacing 和工具版本。

```powershell
& ./.venv/Scripts/python.exe tools/convert_reference.py datasets/vmtk/aorta.mha datasets/vmtk/aorta.nii.gz
& ./.venv/Scripts/python.exe tools/convert_reference.py datasets/vmtk/aorta-final-levelset.mha datasets/vmtk/aorta-final-levelset.nii.gz
$env:OMP_NUM_THREADS='4'
& ./build/cmake/medvessel_cli.exe datasets/vmtk/aorta.nii.gz build/reference-results/vmtk-aorta-roi `
  --roi 40,55,0,80,150,34 --scales 1,2,3,4,5 --c 200 --low 0.2 --high 0.3 `
  --seed 49,32,19 --min-intensity 1600 --max-intensity 2400 --min-component 100 --prune-mm 3
& ./build/cmake/medvessel_evaluate.exe build/reference-results/vmtk-aorta-roi/mask.nii.gz `
  datasets/vmtk/aorta-final-levelset.nii.gz build/reference-results/vmtk-aorta-roi/agreement.json `
  --reference-levelset-negative --align-reference
```

种子为裁剪后索引；原图索引 89,87,19，来自仓库中心线上的点。
参考中心线采用与转换后 NIfTI 一致的 RAS 坐标，不能直接当作 SimpleITK 的 LPS 坐标使用。
此示例灰度不假定为 HU；使用通用 intensity 参数。

## 当前结果与失败案例

| 配置 | 评估范围 | 与 VMTK 算法结果的 Dice | clDice | HD95 (mm) |
| --- | --- | --- | --- | --- |
| 初始无种子，尺度 1/2/3 | 完整体积 | 0.277674 | 0.084461 | 144.378 |
| 种子，尺度 1/2/3/4/5 | 完整体积 | 0.304314 | 0.325733 | 149.587 |
| 上述显式 ROI 与灰度约束 | 80×150×34 ROI | 0.698610 | 0.588727 | 22.8516 |

各配置参数和评估范围不同，不能把表格解读为同一测试集上的单因素提升。
第三组在本机约 2 秒完成，输出 28 条分支，仍有多余分支和边界偏差。
对照图显示，原仓库参考分割只覆盖部分血管区段，当前预测还延伸到参考端点之外。
这些指标表示和该算法参考的**一致性**，不是相对于人工金标准的准确率，参数也在同一样例上调试过。
没有将演示样例计入独立测试集。

图像：`build/reference-results/vmtk-aorta-roi/comparison.png`；SVG：同目录 `preview.svg`。
PNG 使用 NumPy/Matplotlib 对 C++ 导出的数据作图，不改变算法输出。

## 完整冠脉数据集入口

[ImageCAS-X 仓库](https://github.com/kitbransby/ImageCAS-X) 提供数据结构与评测说明，数据托管于 Zenodo。
截至本次核查，方案中的 v1 已有 [v2 记录](https://zenodo.org/records/23562647)，数据包约 934.1 MB。
官方 v2 说明修正了中心线起点、表面生成和部分中心线错误。
作者在 [论文 Data Records](https://arxiv.org/html/2608.30404v1#S3) 明确说明：ImageCAS-X 不重新分发原始 CTA，原图需从
[原始 ImageCAS 的 Kaggle 数据页](https://www.kaggle.com/datasets/xiaoweixumedicalai/imagecas) 另行获取。
[原始 ImageCAS 仓库](https://github.com/XiaoweiXu/ImageCAS-A-Large-Scale-Dataset-and-Benchmark-for-Coronary-Artery-Segmentation-based-on-CT)
提供官方划分表和获取说明，本体不直接放在 GitHub。
已进一步按范围提取第 1 例原图与配对标签，具体见下节；未下载完整数据包，未把代码许可视作影像数据许可。
后续完整冠脉实验仍需固定数据版本并保留官方划分。

## 已接入的实际冠脉病例

原始 ImageCAS 当前版本 3 的公开清单总计 89,085,952,489 字节；API 声明许可为 CC BY-NC 4.0。
对应 ImageCAS-X v2 记录的 API 声明为 CC BY 4.0，二者分别记录，不混用代码许可。

通过有上限的 HTTP 范围读取和 ZIP 流校验，已取得：

| 文件 | 本地位置 | 字节数 | SHA256 |
| --- | --- | --- | --- |
| 原始 CTA | datasets/imagecas-sample/1.img.nii.gz | 95,770,776 | c020b1244ac6c841924e27c3f5c328e6cf1eefb70bc14d3b725959ed99b656ff |
| 原始标注 | datasets/imagecas-sample/1.label.nii.gz | 2,590,448 | afd992bf8855ebc42dcec721bd7695878bdc137efc8a9f965a7937ed85f159c7 |
| ImageCAS-X v2 标注 | datasets/imagecas-x-sample/1.coronary.nii.gz | 85,387 | 48fc4523a9e5cc704b2a453ab7771023d7ba2d9c3ad8096f4259bbec0f1b73b8 |

原图包只消费了约 96.47 MB 的分卷前缀；网络失败重试与预取会有少量额外传输。
ImageCAS-X 的单个标签只读取约 0.87 MB，未下载 934 MB 全包。脚本有范围上限，不会默默拉取整个数据集。
内层成员完成 CRC 校验后才提交；有分块缓存、SHA256 manifest 与离线损坏测试。

复现（已构建 C++ 程序；绘图 Python 需 NumPy/Matplotlib）：

```powershell
& ./tools/run_imagecas_sample.ps1 -PlotPython C:/Users/1/anaconda3/python.exe
```

首个病例尺寸为 512×512×275，spacing 0.376953125/0.376953125/0.5 mm。
原始标注的 xyzt_units 为 0，但数值几何与声明 mm 的原图完全一致。严格 C++ 读取器保持拒绝未知单位。
若使用原始标注，先运行以下**显式**元数据修复，只写派生文件：

```powershell
& ./.venv/Scripts/python.exe tools/convert_reference.py datasets/imagecas-sample/1.label.nii.gz `
  build/imagecas-prepared/label-mm.nii.gz --geometry-from datasets/imagecas-sample/1.img.nii.gz
```

修复工具要求 donor 明确声明 mm，且尺寸、spacing、origin、direction 已匹配；否则拒绝推断配准。
ImageCAS-X v2 的同病例标注声明了 mm，可直接和原图配对，主复现脚本不需要上述修复。

`medvessel_prepare_case` 用参考标签的 EDT 选择内部种子，生成半宽 20 mm 的开发区域。
主实验 ROI 为原图索引 `[198,229,130,109,109,81]`，局部种子 `[54,54,40]`。
这是参考辅助的局部调试流程，不是全图自动定位或独立测试。

参数：scales 0.5/1/1.5/2 mm，c=100，low=0.08，high=0.2，灰度范围 100–900，最小连通域 20，短枝阈值 1 mm。
当前 Dice=0.816186、clDice=0.875168、HD95=2.73998 mm、合并双向平均中心线距离=0.77204 mm。
输出仍有 ostium 端部过分割、部分细支漏分与多余骨架分支，不能外推到其他病例。

结果目录 `build/imagecas-x-case1/result` 包含 NIfTI、几何 CSV、参数、评估 JSON、SVG 与 `comparison.png`。
`roi.json` 与 `experiment.json` 明确标记参考辅助的开发评估范围。

[VESSEL12 记录](https://zenodo.org/records/8055066) 则有肺 CT、肺 Mask 和带参考注释的示例扫描；肺 Mask 不是血管金标准。

# Qt 后续开发与当前检出状态

更新：2026-11-03。

## 当前工作

本次检出的两个历史提交均未包含 `SliceViewer.hpp/.cpp` 和
`ThreeDViewer.hpp/.cpp`，但 CMake、主窗口和 GUI 测试已引用这些文件。
因此旧文档中的构建成功及测试数量属于旧环境记录，不能作为当前检出的验证结果。
本次按已有接口和测试补齐模块，并接入结果切片到三维的反向定位。

本机环境放在被 Git 忽略的 `build/deps/msys64`，NIfTI 源码位于
`build/deps/nifti_clib-master`。构建脚本会优先发现这个工具目录，也可通过
`-QtPrefix` 和 `-NiftiSource` 显式指定其他兼容环境：

```powershell
# Qt + VTK 构建和完整测试
./tools/build_gui.ps1 -EnableVtk -Test
# 启动工作台
./tools/start_gui.ps1
# 生成可运行目录 build/desktop
./tools/package_gui.ps1
```

省略 `-EnableVtk` 可构建不含三维模块的 Qt 工作台。此脚本默认不启用 DCMTK，
因此本机这套配置支持 NIfTI，DICOM 读取需另按构建文档配置 DCMTK。

反向定位入口为结果切片页的“定位三维”模式。单击后，按当前结果网格的
spacing、origin 和 direction 转换为 RAS 毫米坐标，在三维窗口显示黄色标记并
平移相机焦点。此操作不更新种子、ROI 或分支选择，也不重新执行分析。
三维数据尚未加载时显示提示。输入切片不连接三维定位，以免不同影像串用坐标。

## 本轮验证结果

- 环境：Windows、UCRT64 GCC 16.2.0、CMake 4.4.5、Qt 6.12.0、VTK 9.6.2。
- 独立核心构建：10 项 CTest 全部通过。
- Qt 无 VTK 配置：26 项 CTest 全部通过，GUI 日志 14 项通过（含初始化和清理）。
- Qt + VTK 配置：26 项 CTest 全部通过，GUI 日志 15 项通过（含初始化和清理）。
- 真实 Windows OpenGL：15 项 GUI 检查全部通过，包括实际鼠标拾取。
- `build/desktop` 已生成；PATH 仅保留 Windows 目录、清除 Qt 插件环境变量后，
  随包 CLI 可运行合成数据，GUI 可载入三维结果并截图，退出码均为 0。
- 已检查 `build/gui-three-d.png` 和 `build/gui-result-slices.png`。
  三维使用斜向初始视角，表面关闭标量着色，中心线保留分支高亮颜色。

本轮使用合成数据与测试夹具，没有重新下载或评估真实冠脉病例。
当前配置未启用 DCMTK，所以测试数为 26，不包含旧环境的 DICOM 测试。
尚未在另一台干净电脑或多种 DPI 下验收。

## 按顺序推进

1. 已完成：建立本机 Qt 构建环境，完成无 VTK 的 Qt 构建与测试。
2. 已完成：启用 VTK，验证网格变换、分支表双向联动和三维/切片双向定位。
3. 已完成本机检查：实际 Windows OpenGL 三维显示、鼠标拾取和截图；其他机器仍待验收。
4. 已完成：标准解剖平面按需重采样，保留原网格；灰度线性、Mask 最近邻、原图种子和世界坐标定位均已验证。ROI 框选与边框仍在原网格提供。
5. 已完成合成全尺寸显示基准和二维缓存；真实 CTA 的磁盘加载、全分析流程及更大体积内存仍待验证。
6. 已完成本机 Qt 缩放因子 1、1.5、2 和打包复验；不同实际显示器及另一台干净电脑仍待验收。

## 标准平面与显示性能补全记录

Qt + VTK 与不含 VTK 的配置各 26 项 CTest 通过，GUI 分别为 17 和 16 项通过（含初始化、清理）。新增解析斜采集、轴交换、Mask 最近邻、黑色角区拒绝选点、原图种子换算和极端网格回退测试。Windows OpenGL 在 `QT_SCALE_FACTOR=1/1.5/2` 三档下各 17 项通过；这些是 Qt 缩放因子，不代表切换了系统实际显示器 DPI。

512×512×275、spacing 0.4/0.4/0.6 mm、绕 z 轴旋转 30° 的合成灰度与二值 Mask，原始浮点数组合计约 550 MiB。一次本机 Release/offscreen 显示基准：

| 项目 | 结果 |
| --- | --- |
| setVolume、全范围扫描和原网格三平面 | 70.2 ms |
| 切到标准模式并生成三幅平面 | 56.4 ms |
| 连续 20 次标准轴位切层 | 12.7～16.2 ms/次 |
| 标准模式前 / 后工作集 | 584.6 / 586.6 MiB |
| 本次进程峰值工作集 | 588.5 MiB |

这里只测 CPU 采样和 Qt 场景更新，不含磁盘 I/O、真实屏幕呈现或主分析流水线，也不是实际 CTA 吞吐量承诺。原始结果在 `build/cmake/slice-benchmark.json`，标准平面截图在 `build/gui-anatomical-1.png` 等文件。

复现基准（已有构建环境，在当前 PowerShell 临时设置环境）：

```powershell
$qt = (Resolve-Path build/deps/msys64/ucrt64).Path
$env:PATH = "$qt/bin;" + $env:PATH
$env:QT_PLUGIN_PATH = "$qt/share/qt6/plugins"
& "$qt/bin/cmake.exe" -S . -B build/cmake -DMEDVESSEL_BUILD_GUI_BENCHMARK=ON
& "$qt/bin/cmake.exe" --build build/cmake --target medvessel_slice_benchmark --parallel 4
./build/cmake/medvessel_slice_benchmark.exe -platform offscreen
```

当前没有纳入本轮的内容：独立多病例算法评估、压缩/增强 DICOM、亚体素中心线。
这些仍按 [整体进度](progress.md) 跟踪。

# Qt 6 桌面工作台

想学习实现，请读 [Qt 开发讲解](qt_development.md)，按“总体关系 → 点击分析调用链 → 进程与状态 → 结果显示 → 修改练习”阅读。本文侧重操作与构建；开发讲解已整合进单文件学习网页的「07 · Qt 工作台怎么写」。

## 先打开使用

本机已生成可直接运行的目录：`build/desktop`。双击其中的 `medvessel_gui.exe`。
保留同目录的 DLL、platforms、docs、samples 等文件，不能只移动一个 EXE。

首次体验：点击「载入合成示例」→「开始分析」。完成后，中间出现原图、响应、分割和中心线投影；右侧「分支表」显示物理长度和平均 EDT 直径。
「打开结果」可直接选择现有的 `build/imagecas-x-case1/result` 查看真实冠脉开发样例；该样例的 ROI 和种子由参考标签辅助选取。

开发构建可从仓库根目录启动：

```powershell
& ./tools/start_gui.ps1
# 直接打开已有结果
& ./tools/start_gui.ps1 -Result build/imagecas-x-case1/result
```

## 当前能做什么

| 操作 | 行为 |
| --- | --- |
| 选择输入 | NIfTI `.nii/.nii.gz` 或单一经典 DICOM CT 序列目录；格式限制与 CLI 一致 |
| 设置参数 | 尺度、双阈值、灰度、ROI、种子；高级区包含 Frangi、NMS、邻接、连通域、短枝和线程数 |
| 保存/载入配置 | 独立 GUI JSON，记录路径与所有界面参数，不等同于 CLI 的 run.json |
| 开始分析 | QProcess 异步调用 medvessel_cli；每次在结果根目录创建新的 run-时间-随机后缀目录 |
| 查看进度 | 当前处理阶段、耗时和日志；没有伪造完成百分比 |
| 停止 | 先请求终止，1.5 秒后仍未退出则结束子进程；不支持断点续跑 |
| 查看结果 | 缩放/平移四幅 SVG 投影、摘要、可排序分支表、导出分支 CSV、打开结果文件夹 |
| 重新打开 | 校验 run.json、CSV 行数和 SVG；GUI 任务必须为 completed 状态 |

当前支持原网格三方向切片、窗宽窗位、二值 Mask 叠加、鼠标点选种子和框选 ROI；启用 VTK 的构建另有三维血管表面、中心线及分支表联动。ROI 与种子支持鼠标选取和手工填写。
投影会把不同深度结构叠在一起，不能用它单独判断三维连通性。

## ROI 和种子怎么填

ROI 是原图零基体素索引 `x,y,z,width,height,depth`。留空表示全图。
种子是 `x,y,z`，多个点用英文分号分隔。
默认勾选「种子使用原图索引」，GUI 发送给 CLI 前减去 ROI 起点，并检查点是否在 ROI 中。
例如 ROI `40,55,0,100,100,50`、原图种子 `89,87,19`，实际 CLI 种子为 `49,32,19`。
取消勾选后，种子直接按裁剪后网格的索引解释；没有 ROI 时两种方式一致。
源影像的实际尺寸及几何由算法读入时继续校验。

ImageCAS 开发 ROI 已在 `build/imagecas-x-case1/source.nii.gz` 中裁好；重跑该输入时不要再填写原图 ROI。
使用尺度 `0.5,1,1.5,2`、low `0.08`、high `0.2`、种子 `54,54,40`、灰度 `100` 到 `900`、最小连通域 `20`、短枝 `1 mm`。
GUI 只运行主分析，不会自动读参考标签、评估 Dice 或复制原实验的评估指标。

## 构建

使用 Qt 6.5 或以上，模块为 Widgets、SvgWidgets、Concurrent；启用测试还需 Qt Test。
本机安装 Qt 6.11.1 UCRT64，与现有 GCC 16.1 工具链匹配；Anaconda 的 Qt 5/MSVC 套件不用于这个构建。
完整算法依赖与初次 CMake 配置仍见 [Windows 构建](build_windows.md)。已配置的本机构建增加：

```powershell
$env:PATH = 'C:/msys64/ucrt64/bin;' + $env:PATH
& ./.venv/Scripts/cmake.exe -S . -B build/cmake `
  -DMEDVESSEL_BUILD_GUI=ON -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64
& ./.venv/Scripts/cmake.exe --build build/cmake --parallel 4
$env:QT_PLUGIN_PATH = 'C:/msys64/ucrt64/share/qt6/plugins'
& ./.venv/Scripts/ctest.exe --test-dir build/cmake --output-on-failure
```

`MEDVESSEL_BUILD_GUI` 默认 OFF，命令行与核心构建无需 Qt。GUI 也可单独构建，再在高级区选择外部 CLI 路径。
Windows 上不要混用 Qt 与不同 ABI 的编译器。启动脚本为当前进程设置 Qt DLL 和插件路径，并恢复调用者环境。

生成本机可运行目录：

```powershell
& ./tools/package_gui.ps1
```

脚本复制 GUI/CLI，调用 windeployqt，补齐 UCRT64 的递归 DLL 依赖，并放入学习手册与合成示例。
它生成本地可运行目录，不是安装器。对外分发前需按所用 Qt 与其他第三方库许可证准备相应材料。
Windows CLI 启用进程 UTF-8 manifest，支持现代 Windows 的中文路径；已在本机 Windows 11 验证，不要求修改系统区域设置。
依据见 [Microsoft 的 UTF-8 进程代码页说明](https://learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page)。

## 任务文件与失败处理

每次任务额外写出 `gui_run.json` 和 `process.log`。`gui_run.json` 包含 schema_version=1、配置快照、实际参数数组、UTC 起止时间、退出码与状态。
状态为 running、completed、failed、canceled。只有子进程正常返回 0 且必要输出校验通过，才记录 completed。
CLI 输出中的 `MEDVESSEL_STAGE` 加制表符和阶段代码用于通知界面，不改变算法参数或输出数值。
失败/中止保留日志及部分文件，GUI 拒绝把这类目录作为完成结果打开。已有 CLI 结果没有 GUI 状态时仍可查看，并标明来源。
如果应用或系统被强制结束，可能留下 running 状态；这表示没有完成确认，不支持恢复或自动认定成功。
任务运行时参数及结果切换锁定；关闭窗口前需要先停止任务。

Qt 官方机制参考：[QProcess](https://doc.qt.io/qt-6/qprocess.html)、[Windows 部署](https://doc.qt.io/qt-6/windows-deployment.html)。

## 代码入口与验证

| 文件 | 职责 |
| --- | --- |
| app/gui/main.cpp | 应用、命令行入口、已有结果打开与截图 |
| app/gui/MainWindow.cpp | 参数面板、投影、表格、配置与结果交互 |
| app/gui/JobConfig.cpp | 配置序列化、ROI 种子换算、复用核心参数校验 |
| app/gui/AnalysisJob.cpp | 异步进程、日志、阶段解析、取消、完成状态与输出检查 |
| tests/gui_tests.cpp | 真实 CLI 集成、中文及含空格路径、ROI、配置往返、无效输入、取消与启动失败 |

完整配置当前 27 个 CTest 项通过，其中启用 VTK 的 gui_workbench 内含 13 项行为检查，另有初始化和清理。
Qt 逐项结果在 `build/cmake/gui-test-results.txt`。测试以 offscreen 平台运行；不替代真人交互与不同 DPI 的验证。
本机打包目录还通过独立运行检查：PATH 仅保留 Windows 系统目录，清除 Qt 插件环境变量后，GUI 可打开真实冠脉结果并生成窗口 PNG，随包 CLI 可处理随包合成影像。该检查确认不依赖开发环境的 PATH，尚未在另一台干净电脑验收。

可生成窗口截图并退出，供本机检查布局和打包结果：

```powershell
& ./build/desktop/medvessel_gui.exe -platform offscreen `
  --result C:/Users/1/PycharmProjects/MEDVessel/build/imagecas-x-case1/result `
  --capture C:/Users/1/PycharmProjects/MEDVessel/build/gui-real-preview.png
```

后续主要工作：标准解剖平面重采样、大体积性能验证，以及切片选点反向定位三维。

## VTK 三维血管与分支联动

打开有效的分析结果目录，切换到“三维血管”，点击“载入三维结果”。底层在后台读取 `mask.nii.gz` 和 `centerline.csv`，构建 Mask 的 0.5 等值面和中心线。无需重新运行算法，也不依赖原图仍在原路径。显示使用 RAS 毫米坐标，方向角标表示正向 R、A、S。表面是离散 Mask 的显示网格，边界补零闭合，不代表额外验证过的真实管壁。

左键拖动旋转、滚轮缩放；“重置视角”恢复全景。表面可隐藏或调整不透明度；中心线默认青绿色，选中分支为橙色。右侧“分支表”选中一行会高亮并将视角中心平移到对应分支，保留当前缩放和旋转；在三维图上单击中心线可回选表格。按实际分支 ID 联动，排序后仍正确；单击表面不选分支。

换结果目录或启动新任务会清除旧三维对象及结果定位页；载入失败显示原因，不沿用旧模型。结果必须包含合法二值 Mask、与 run.json 相符的网格、有限中心线坐标和连续点 ID。三维几何与“结果定位”页使用同一结果坐标系，独立于“浏览输入切片”的底图；尚未接入原始灰度体绘制。

### 从三维分支定位到切片

先在三维图单击中心线，或在分支表选中一行，然后点击三维页的“定位到结果切片”。页面自动切换到“结果定位”，三方向滑块跳到对应位置，黄色十字标出目标。表格选中使用该分支中心线序列中间的采样点，三维单击使用实际拾取位置；定位页已加载后，后续选择会同步更新十字，点击按钮可再次切回查看。

定位页按 run.json 的 input_path 读取当次输入，并按 roi_voxel 裁剪，再验证与结果 Mask 的尺寸、间距、原点和方向一致。若原图已移动、不能读取或网格不符，则明确提示“仅显示结果 Mask（无原始灰度）”，仍可查看分割坐标。不会拿当前参数面板中另一幅影像作为底图。

目标使用 RAS 毫米坐标，通过结果网格的 worldToIndex 转换并取最近体素。ROI 的偏移已经包含在裁剪后的 origin 中，不能再手工加减 ROI 起点。越界目标会提示错误，不会强行跳到边缘。页内索引属于结果网格，不一定等于全原图索引；定位页只读，不修改当前输入、种子或 ROI。窗宽窗位及叠加透明度仍可调整。

可用 `--result <目录> --result-slices --capture <图片.png>` 自动载入首个分支并生成定位截图；截图会等待底图和 Mask 完成加载。

VTK 是可选编译功能；本机安装 MSYS2 UCRT64 VTK 9.5.2，与 Qt 6 使用同一工具链。完整构建增加：

```powershell
& ./.venv/Scripts/cmake.exe -S . -B build/cmake -DMEDVESSEL_BUILD_GUI=ON -DMEDVESSEL_ENABLE_VTK=ON
& ./.venv/Scripts/cmake.exe --build build/cmake --parallel 4
```

需要 VTK 的 GUISupportQt、RenderingOpenGL2、FiltersCore 等模块，以及已有 NIfTI 依赖。默认 MEDVESSEL_ENABLE_VTK=OFF，CLI 和无 VTK 的 Qt 构建不受影响。实际显示需要可用的 OpenGL；offscreen/minimal 平台只检查几何和联动，不创建三维 OpenGL 控件。真实 Windows 渲染可用 `--result <目录> --three-d --capture <图片.png>` 检查，程序等待三维加载完成后截图并退出。

## 鼠标选取种子与三维 ROI

先选择输入并点击“浏览输入切片”，保持“种子使用原图索引”勾选。在切片页选择“点选种子”，左键单击血管位置，坐标自动追加到左侧种子框；重复坐标不重复追加。青色圆圈只在种子所在切片显示，可在不同切片追加多个点。“清除种子”清空参数和标记；也可直接编辑左侧文本。

选择“框选 ROI”，按住左键拖出矩形。首次框选覆盖当前平面的两个轴，第三轴使用全范围。例如在 XY 框出 x/y 范围后，可到 XZ 再框选来缩小 z 范围；此时 y 范围沿用已有 ROI。每次框选替换所见两轴范围，另一轴沿用现有合法 ROI。绿色边框仅在与 ROI 相交的切片显示。

ROI 使用包含起止体素的整数范围：尺寸为最大索引减最小索引加 1，单体素框尺寸为 1。必须从图像内开始拖动，拖出图像边缘时终点截到边缘。拖动中切层会取消本次框选。“清除 ROI”恢复全图处理。缩小 ROI 不自动删除已有种子，开始分析时仍由 JobConfig 校验种子是否位于 ROI 内。

鼠标选取对应当前输入文件的零基索引，不是毫米坐标，也不会直接使用裁剪后的局部索引。取消“种子使用原图索引”后禁用鼠标选取，避免混用坐标；已有文本不会自动转换。分析运行期间禁止修改种子和 ROI。切换输入路径后需要重新载入切片，选取只写入与当前输入相符的底图参数。

## 三方向切片浏览

选择 NIfTI 文件或受支持的 DICOM 序列目录，点击左侧“浏览输入切片”。中间的“三方向切片”页显示 XY、XZ、YZ 三幅图，通过各自滑块逐层浏览，索引从 0 开始。显示比例使用真实 spacing，窗口变化时自动适应。

文件在后台读取；重新加载先清除旧图，失败会显示原因。修改输入路径后旧切片禁用，需要重新点击浏览。切片展示按钮点击时的输入文件，不自动跟随“打开结果”，也不应用参数中的 ROI。

方向标签按实际 RAS 方向矩阵计算，标出屏幕左、右、上、下方向，斜向轴显示组合字母；每幅图显示切片中心 RAS 毫米坐标。XY/XZ/YZ 通常对应轴位/冠状位/矢状位，但本版本不对斜采集或轴交换数据重采样到标准解剖平面，应以实际方向标签为准。

初始灰度使用全体有限值范围映射，常量体显示黑色。可修改窗宽窗位，或点击“全范围”复位。“CT 血管”为窗宽 700、窗位 250 的显示预设，仅在输入为 HU 时具有相应 CT 含义，不修改算法参数。暂不支持取消加载。GUI 读取能力取决于构建时 NIfTI/DCMTK 依赖，缺少模块时会明确提示；GUI-only 构建仍可使用外部 CLI 与结果投影。DICOM 格式限制与 CLI 一致。

点击“载入 Mask…”选择结果目录内的 `mask.nii.gz`，橙色显示前景，可切换显示、调整叠加透明度或清除。Mask 必须为有限 0/1 数据，尺寸、spacing、origin、direction 必须与底图一致；几何比较只容许格式往返带来的微小误差。概率图或不匹配网格会被拒绝，不进行隐式重采样。ROI 分割结果必须配对同一裁剪底图，不能直接叠到全图。重新加载底图会清除旧 Mask；Mask 读取或校验失败也不会保留旧叠加。

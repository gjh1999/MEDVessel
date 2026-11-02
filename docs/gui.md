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

当前没有三方向切片、鼠标框选 ROI、点选种子、Mask 切片叠加或 VTK 三维视图。ROI 与种子目前以文本索引填写。
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

使用 Qt 6.5 或以上，模块为 Widgets、SvgWidgets；启用测试还需 Qt Test。
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

完整配置当前 27 个 CTest 项通过，其中 gui_workbench 内含 8 项行为检查，另有初始化和清理。
Qt 逐项结果在 `build/cmake/gui-test-results.txt`。测试以 offscreen 平台运行；不替代真人交互与不同 DPI 的验证。
本机打包目录还通过独立运行检查：PATH 仅保留 Windows 系统目录，清除 Qt 插件环境变量后，GUI 可打开真实冠脉结果并生成窗口 PNG，随包 CLI 可处理随包合成影像。该检查确认不依赖开发环境的 PATH，尚未在另一台干净电脑验收。

可生成窗口截图并退出，供本机检查布局和打包结果：

```powershell
& ./build/desktop/medvessel_gui.exe -platform offscreen `
  --result C:/Users/1/PycharmProjects/MEDVessel/build/imagecas-x-case1/result `
  --capture C:/Users/1/PycharmProjects/MEDVessel/build/gui-real-preview.png
```

后续顺序：三方向切片与方向标签 → 窗宽窗位/Mask 叠加 → 点选种子/框选 ROI → VTK 三维分支联动。

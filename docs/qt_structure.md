# Qt 项目结构与源码阅读路线

## 1. 先确定这个程序的任务

MEDVessel 是一个血管影像后处理桌面原型。用户提供 CTA 等三维影像，选定处理区域和参数，运行血管提取，再通过切片、三维视图和分支表复核结果，最后导出分割、中心线及几何指标。

这条需求对应三个交付结果：能运行的分析任务、能检查的可视化结果、能追溯的文件记录。Qt 负责把这些步骤组织为一个工作台；Frangi、分割和中心线计算由独立 C++ 算法程序完成。当前项目定位和验证情况见 [进度记录](progress.md)，实际医院部署不属于已有验证记录。

阅读代码时，先找“谁接收操作、谁组织任务、谁执行计算、谁保存结果”，再学习具体控件。已有整体结构图继续用于理解算法流程；本章只解释实现的职责分工。

## 2. 整个项目按五组职责阅读

下面的“五层”是一种源码阅读方法。当前仓库没有五个独立服务，也没有为每层创建抽象基类。数据与格式层是界面和算法共同使用的基础，不能把它理解成所有请求都必须依次经过的五级调用链。

| 职责层 | 主要位置 | 接收什么，交付什么 |
| --- | --- | --- |
| 交互与展示 | `app/gui/MainWindow.*`、`SliceViewer.*`、`ThreeDViewer.*` | 接收按钮、表单、鼠标操作，展示图像、表格和日志 |
| 任务配置与控制 | `app/gui/JobConfig.*`、`AnalysisJob.*` | 接收界面参数，形成任务配置，启动 CLI，报告状态与结果目录 |
| 分析流程编排 | `app/pipeline.cpp` | 接收命令行参数，依次组织增强、分割、骨架、测量和写出 |
| 核心算法 | `include/filters`、`segmentation`、`skeleton`、`geometry` | 接收体数据或路径，返回响应、Mask、中心线图和指标 |
| 数据与格式基础 | `include/core`、`include/io`、`src/io` | 提供体素存储、坐标变换、裁剪、格式读写、参数和报告支持 |

例如，`SliceViewer` 为了显示切片，会直接读取影像并使用 `Volume3D`，无需先经过 `AnalysisJob`。`pipeline.cpp` 调用核心算法时，也会直接使用影像读写接口。这里的分层强调职责，实际依赖以源码为准。

`main.cpp`、CMake、测试和脚本分别承担启动、构建、验证和运行支持，不额外计作业务层。

## 3. Qt 内部由三组对象协作

Qt 部分可以继续分成“主窗口组织交互、任务对象管理分析、查看器展示数据”三组。`JobConfig` 是它们传递的一份参数值，不是窗口，也不是运行中的任务。

| 文件或类 | 具体职责 | 阅读时重点找的方法 |
| --- | --- | --- |
| `app/gui/main.cpp` | 创建 QApplication 和主窗口，处理启动参数，进入事件循环 | `main()`、`app.exec()` |
| `MainWindow` | 创建页面，读取和回填参数，连接对象，打开结果 | 构造函数、`config()`、`setConfig()`、`startAnalysis()`、`openResult()` |
| `JobConfig` | 保存路径和算法参数，换算 ROI 种子，生成 CLI 参数，保存和载入 JSON | `arguments()`、`toJson()`、`fromJson()` |
| `AnalysisJob` | 启动和取消子进程，解析阶段日志，记录 completed/failed/canceled | `start()`、`consume()`、`cancel()`、`complete()` |
| `SliceViewer` | 显示三方向切片、窗宽窗位、Mask、种子、ROI 和世界坐标定位 | `load()`、`setVolume()`、`sliceImage()`、`eventFilter()`、`focusWorld()` |
| `ThreeDViewer` | 构建与显示血管表面、中心线，处理分支选择和三维定位 | `prepare()`、`load()`、`selectBranch()`、`focusWorld()` |

`MainWindow` 在界面中创建两个 `SliceViewer` 实例：`inputSlices` 用于输入浏览和参数选取，`resultSlices` 用于只读结果定位。它们复用同一个类，但数据来源和交互目的不同。结果页不会把选点写回新任务的种子或 ROI。

这套实现没有使用 `.ui` 文件或 QML，控件和布局直接写在 C++ 中。它也没有完整实现标准 MVC/MVVM：分支表使用了 Qt 的 Model/View 控件，但整个应用仍以 `MainWindow` 连接和协调对象。理解现有代码时，不必强行寻找并不存在的 Controller 或 ViewModel 目录。

## 4. 目录、类和运行程序是三个不同概念

```text
MEDVessel/
  app/
    gui/                 桌面入口、窗口、任务对象、查看器
    pipeline.cpp         主分析 CLI 的入口
    phantom.cpp          合成数据程序
    evaluate.cpp         预测与参考结果的评价程序
  include/
    core/                Volume3D、物理坐标、裁剪和重采样
    filters/             Gaussian、Hessian、Frangi
    segmentation/        分割与连通域
    skeleton/            距离场、细化与分支图
    geometry/            几何测量和评价
    io/                  读写声明、参数与报告接口
  src/io/                NIfTI、DICOM 格式适配的实现
  tests/                 核心算法、格式、CLI、GUI 测试
  tools/                 构建、启动、打包、实验和文档脚本
  docs/                  操作、实现、学习与验证文档
  build/                 本机依赖、构建结果和运行产物
```

目录树表示代码放在哪里，不表示一个目录就会变成一个 EXE。主要构建目标如下：

| CMake 目标 | 产物或作用 |
| --- | --- |
| `medvessel` | INTERFACE 库目标，共享头文件目录和编译要求；核心算法主要在头文件中 |
| `medvessel_io` | NIfTI 格式库，启用相应依赖时构建 |
| `medvessel_dicom` | 可选 DICOM 格式库 |
| `medvessel_desktop` | Qt 桌面实现的静态库，供 GUI 和 GUI 测试复用 |
| `medvessel_gui` | 用户打开的桌面 EXE |
| `medvessel_cli` | GUI 启动的独立分析 EXE，也可以单独运行 |
| `gui_tests` | 链接桌面实现、验证界面和真实 CLI 协作的测试程序 |

`.hpp` 通常放类声明和接口，`.cpp` 放实现；核心算法的许多 `.hpp` 同时包含实现。这是本项目的组织方式，不能仅凭文件后缀判断有没有算法代码。

## 5. 沿一次分析任务串起各层

先在 `MainWindow` 构造函数中找到开始按钮的 `connect`。按钮点击后，`startAnalysis()` 调用 `config()`，把当前表单值装进 `JobConfig`。

接下来读 `AnalysisJob::start()`。它检查任务条件，创建独立结果目录，通过 `JobConfig::arguments()` 取得参数数组，再用 `QProcess` 启动 `medvessel_cli`。此时 GUI 进程继续处理事件，算法在另一个进程中运行。

然后切换到 `app/pipeline.cpp`。这里将输入读成体数据，按参数裁剪 ROI，调用增强、分割、距离场、细化、分支图和几何分析，最后将结果写成 NIfTI、CSV、JSON、SVG 文件。它负责执行顺序，各个算法的细节在 `include` 中。

CLI 运行时输出阶段和日志，`AnalysisJob::consume()` 接收并解析，再通过信号更新主窗口。程序结束后，`complete()` 确认退出状态和输出文件；成功信号触发 `MainWindow::openResult()`，界面才读取本次结果。创建了目录或启动了进程，都不等于任务已经完成。

| 交接位置 | 传递的内容 |
| --- | --- |
| 表单 → `JobConfig` | 路径、阈值、尺度、ROI、种子等参数值 |
| `JobConfig` → CLI | `QStringList` 参数数组和子进程环境 |
| CLI → `AnalysisJob` | 标准输出、标准错误和退出状态 |
| CLI → 结果目录 | 影像、中心线、分支表、参数记录等文件 |
| `AnalysisJob` → `MainWindow` | 运行状态、阶段、日志、成功结果目录 |
| 结果目录 → 查看器 | 可读取的体数据、表面和中心线路径 |

## 6. 分清进程、线程与信号

应用通常有一个 GUI 主线程。控件创建、按钮响应和界面更新在这里完成。`AnalysisJob` 对象也在 GUI 线程，异步接收 `QProcess` 的通知。

主分析由另一个 CLI 进程执行；`QProcess` 是 GUI 侧的进程控制对象。切片影像读取与三维几何准备则使用 `QtConcurrent::run()` 和 `QFutureWatcher`，属于 GUI 进程内的后台任务。后台任务返回数据，界面对象负责显示；OpenGL 渲染不放到这些读取任务中执行。

信号用于通知对象发生了什么，本身不等于新建线程。例如三维选择发出世界坐标，主窗口连接到结果切片的 `focusWorld()`；切片反向定位也通过主窗口连接到三维查看器。阅读这些 `connect`，就能找到跨页面行为的入口。

## 7. 先读七个位置，再展开算法

1. `app/gui/main.cpp`：确认应用和主窗口从哪里开始，最后在哪里进入事件循环。
2. `MainWindow.hpp`：先看成员和公开方法，建立对象清单；暂时跳过样式和控件布局。
3. `JobConfig.hpp`：了解一次分析到底需要哪些参数。
4. `MainWindow.cpp`：只追 `config()`、`startAnalysis()` 和对应的信号连接。
5. `AnalysisJob.cpp`：顺着启动、输出解析、结束确认读一遍。
6. `app/pipeline.cpp`：确认算法调用顺序和输出文件；需要理解某一步时再进入对应头文件。
7. 回到 `openResult()`，再分别读 `SliceViewer` 和 `ThreeDViewer` 的载入、显示与选择代码。

第一遍读完，应能自己回答“点击开始后，参数怎样到达算法，结果怎样返回窗口”。第二遍再学习 Widgets 布局、对象生命周期和每个查看器的绘制细节。后面的 21 节实现讲解按此基础继续展开。

## 8. 根据修改目标定位文件

| 修改目标 | 优先阅读 | 同时检查 |
| --- | --- | --- |
| 标签、布局、按钮位置 | `MainWindow` 构造函数 | 控件 objectName 与相关 GUI 测试 |
| 新参数或参数默认值 | `JobConfig` 和主窗口参数读写 | CLI 参数解析、JSON 往返；本轮仅学习，不新增参数 |
| 取消、失败或日志行为 | `AnalysisJob` | 子进程退出与 `gui_run.json` 状态 |
| 切片方向、叠加或鼠标坐标 | `SliceViewer` | spacing、direction、屏幕纵轴翻转和原图索引 |
| 三维外观、选中或相机 | `ThreeDViewer` | 世界坐标、分支 ID、OpenGL 渲染 |
| 多个页面之间的联动 | `MainWindow` 中的 `connect` | 输入页和结果页的数据是否属于同一网格 |
| 分割结果或几何指标 | 对应核心算法与 `pipeline.cpp` | 算法测试和输出字段约定 |

这个项目的 `MainWindow.cpp` 同时承担较多布局和协调工作，尚未进一步拆成多个页面类。当前先按方法和职责阅读即可；文档中的分层描述用于解释已有实现，不代表本轮进行了代码重构。

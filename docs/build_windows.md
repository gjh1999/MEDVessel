# 本机 Windows 构建

在项目根目录的 PowerShell 执行。编译器使用已存在的 MSYS2 UCRT64 GCC，CMake/Ninja 已安装到 `.venv`。

## 从干净目录准备依赖

需要 C++17 编译器；NIfTI 格式层还需要 C 编译器和 zlib。
本机使用 `C:/msys64/ucrt64/bin/g++.exe`、`gcc.exe` 及同一 UCRT64 环境的 zlib，不能混用其他运行时的预编译库。
若只运行核心算法演示，不需要 NIfTI、DCMTK、GoogleTest 或 Python；使用已安装的 CMake 即可。

本机可选 Python 工具环境按以下方式准备（已有 `.venv` 时不必重新创建）：

```powershell
py -3.10 -m venv .venv
& ./.venv/Scripts/python.exe -m pip install cmake==4.4.4 ninja==1.13.2
& ./.venv/Scripts/python.exe -m pip install -r tools/requirements.txt
```

`tools/requirements.txt` 的 SimpleITK 只用于格式转换；当前 ImageCAS-X 主复现流程的数据下载使用 Python 标准库。
PNG 绘图另需 NumPy/Matplotlib。本机已有 Anaconda NumPy 2.2.6、Matplotlib 3.10.0，复现脚本通过 `-PlotPython` 指定该解释器。
其他机器可以用自己的绘图环境，或省略 PNG，只查看主程序输出的 SVG。

下载并解压已验证的源码依赖：

```powershell
New-Item -ItemType Directory -Force build/deps | Out-Null
Invoke-WebRequest -UseBasicParsing https://codeload.github.com/NIFTI-Imaging/nifti_clib/zip/refs/heads/master -OutFile build/deps/nifti_clib.zip
Invoke-WebRequest -UseBasicParsing https://codeload.github.com/google/googletest/zip/refs/tags/v1.17.0 -OutFile build/deps/googletest.zip
Invoke-WebRequest -UseBasicParsing https://codeload.github.com/DCMTK/dcmtk/zip/refs/tags/DCMTK-3.6.9 -OutFile build/deps/dcmtk.zip
Get-FileHash build/deps/nifti_clib.zip,build/deps/googletest.zip,build/deps/dcmtk.zip -Algorithm SHA256
# 先核对下表，再解压：
Expand-Archive -LiteralPath build/deps/nifti_clib.zip -DestinationPath build/deps
Expand-Archive -LiteralPath build/deps/googletest.zip -DestinationPath build/deps
Expand-Archive -LiteralPath build/deps/dcmtk.zip -DestinationPath build/deps
```

| 归档 | 已验证 SHA256 |
| --- | --- |
| nifti_clib.zip | 7f9ab2043f895972d8db0402f423ee79de9e8777feb80f7d0a76a1fe26cc370f |
| googletest.zip | 40d4ec942217dcc84a9ebe2a68584ada7d4a33a8ee958755763278ea1c5e18ff |
| dcmtk.zip | 8485ea88f81fe177280cba745fdcaf5273295cff708975bae941232969ecd8c9 |

NIfTI 的 master 地址不是不可变版本：如果校验值不同，应保留新版本信息并重新验证，不能声称复现了本次环境。
GoogleTest/DCMTK 的源码是可选的；不启用相应功能时无需下载。

## 本机完整构建

下面包含已验证的本机绝对路径；项目换位置时，相应修改源码路径与 Ninja 路径。

```powershell
& ./.venv/Scripts/cmake.exe -S . -B build/cmake -G Ninja `
  -DCMAKE_MAKE_PROGRAM=C:/Users/1/PycharmProjects/MEDVessel/.venv/Scripts/ninja.exe `
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe `
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe `
  -DCMAKE_BUILD_TYPE=Release `
  -DMEDVESSEL_ENABLE_OPENMP=ON `
  -DMEDVESSEL_GOOGLETEST_SOURCE_DIR=C:/Users/1/PycharmProjects/MEDVessel/build/deps/googletest-1.17.0 `
  -DMEDVESSEL_DCMTK_SOURCE_DIR=C:/Users/1/PycharmProjects/MEDVessel/build/deps/dcmtk-DCMTK-3.6.9 `
  -DHAVE__SET_OUTPUT_FORMAT=0 `
  -DMEDVESSEL_NIFTI_SOURCE_DIR=C:/Users/1/PycharmProjects/MEDVessel/build/deps/nifti_clib-master `
  -DZLIB_INCLUDE_DIR=C:/msys64/ucrt64/include `
  -DZLIB_LIBRARY=C:/msys64/ucrt64/lib/libz.a
& ./.venv/Scripts/cmake.exe --build build/cmake
& ./.venv/Scripts/ctest.exe --test-dir build/cmake --output-on-failure
& ./build/cmake/medvessel_phantom.exe build/phantom
& ./build/cmake/medvessel_cli.exe build/phantom/phantom.nii.gz build/results
```

源码归档来自官方仓库：
`https://codeload.github.com/NIFTI-Imaging/nifti_clib/zip/refs/heads/master`。
本次下载 SHA256：`7f9ab2043f895972d8db0402f423ee79de9e8777feb80f7d0a76a1fe26cc370f`。
`master` 会变化，复现需使用同一归档，或记录新版本并重新验证。
归档和源码位于 `build/deps`，没有加入版本管理。

## 核心构建与配置项

不启用格式层的独立构建：

```powershell
& ./.venv/Scripts/cmake.exe -S . -B build/core-only -G Ninja `
  -DCMAKE_MAKE_PROGRAM=C:/Users/1/PycharmProjects/MEDVessel/.venv/Scripts/ninja.exe `
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe -DCMAKE_BUILD_TYPE=Release
& ./.venv/Scripts/cmake.exe --build build/core-only
& ./.venv/Scripts/ctest.exe --test-dir build/core-only --output-on-failure
& ./build/core-only/medvessel_demo.exe
```

使用新的 build 目录区分配置，避免误用旧 CMakeCache 中启用的依赖。

| CMake 项 | 默认 | 用途 |
| --- | --- | --- |
| BUILD_TESTING | ON | CTest 测试目标；缺少框架时仍有自包含 C++ 测试入口 |
| MEDVESSEL_NIFTI_SOURCE_DIR | 空 | 启用 NIfTI 格式层和完整 CLI |
| MEDVESSEL_DCMTK_SOURCE_DIR | 空 | 启用经典 DICOM CT 读取；主 CLI 同时依赖 NIfTI 输出层 |
| MEDVESSEL_GOOGLETEST_SOURCE_DIR | 空 | 从源码构建测试框架 |
| MEDVESSEL_USE_SYSTEM_GOOGLETEST | OFF | 显式使用与当前编译器兼容的系统 GTest |
| MEDVESSEL_ENABLE_OPENMP | OFF | 并行卷积切片 |

发现 Python 3.10+ 时还会注册离线下载完整性测试，不会在 CTest 中访问网络。

## 工具链注意事项

GoogleTest 固定为 v1.17.0，DCMTK 固定为 DCMTK-3.6.9，均从各自官方 GitHub 源码归档获取。
本机 GCC 16/UCRT 环境下，DCMTK 探测出 `_set_output_format`，但对应 `_TWO_DIGIT_EXPONENT` 常量不可用；
上述 `HAVE__SET_OUTPUT_FORMAT=0` 只关闭该过时格式化分支，避免更改上游源码。
不同工具链应重新验证此项。

GoogleTest 默认从显式源码路径构建，不自动链接 Anaconda 等目录中的其他编译器版本。
需要使用兼容的系统包时可显式开启 `MEDVESSEL_USE_SYSTEM_GOOGLETEST`。
OpenMP 默认关闭；开启后可用 `$env:OMP_NUM_THREADS='4'` 控制 CLI 的线程上限。
`medvessel_benchmark` 会比较 1/4 线程结果是否逐体素一致。

Python 仅用于可选格式转换与画图；转换依赖见 `tools/requirements.txt`。
本机绘图调用已有的 Anaconda NumPy/Matplotlib，未向 Anaconda 环境安装依赖。

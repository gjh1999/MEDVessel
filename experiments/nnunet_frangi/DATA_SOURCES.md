# 数据来源与恢复

项目使用 ImageCAS 原始 CTA 与 ImageCAS-X v2 冠脉标签。GitHub 提供项目说明；体数据通过数据托管平台下载。

- ImageCAS 官方仓库：https://github.com/XiaoweiXu/ImageCAS-A-Large-Scale-Dataset-and-Benchmark-for-Coronary-Artery-Segmentation-based-on-CT
- ImageCAS-X 官方仓库：https://github.com/kitbransby/ImageCAS-X
- 当前脚本使用的 CTA 镜像：https://www.kaggle.com/datasets/xiaoweixumedicalai/imagecas
- ImageCAS-X v2 标签：https://zenodo.org/records/23562647

在项目根目录运行：

```powershell
./.venv/Scripts/python.exe tools/fetch_imagecas_batch.py
./.venv/Scripts/python.exe experiments/nnunet_frangi/prepare_dataset.py --mode full-volume --check-only
```

下载脚本按归档顺序选取前 11 个具有配对 ImageCAS-X 标签的病例，输出到 `datasets/imagecas-multicase`。它不是整个 ImageCAS 数据集，也没有基于模型成绩筛选病例。脚本对归档成员进行 CRC 校验，并记录源图和标签 SHA256；只有完成 11 对数据后，manifest 的 `complete` 才为 true。CTA 范围请求缓存保存在 `build/dataset-metadata/imagecas-chunks`，中断后重跑可复用缓存。

这批病例属于项目已有开发队列，不构成独立测试集。下载成功不代表已经完成完整 CTA 增强、训练或分割准确率验证。后续步骤见 [README](README.md)。

扩充至 100 对病例使用独立目录，避免改变正在运行的 11 例训练输入：

```powershell
./.venv/Scripts/python.exe tools/fetch_imagecas_batch.py --target-cases 100 --output datasets/imagecas-100
```

100 例包含先前的开发病例，不能整体视为独立测试集。按当前已下载病例估算，源 CTA 与标签约 8–10 GiB；双通道 raw 数据约 18–25 GiB。考虑源文件、范围请求缓存、预处理和权重，建议预留 80–120 GiB。此为当前数据样本外推，最终大小取决于体数据尺寸、压缩率及 nnU-Net 规划。

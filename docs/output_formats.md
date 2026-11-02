# 输出格式与字段

## 体数据与预览

主程序写出的 NIfTI 均为 float32，采用处理后网格的 RAS/mm 几何。
Mask/Skeleton 文件虽然存为 float，数值仍为 0 或 1。输入经过 ROI 后，输出尺寸与 origin 对应裁剪区域。

| 文件 | 值/单位 | 解释 |
| --- | --- | --- |
| vesselness.nii.gz | 无量纲，通常 0–1 | 各尺度的最大 Frangi 响应 |
| best_scale_mm.nii.gz | mm | 最大响应对应 sigma；无响应为 0 |
| mask.nii.gz | 0/1 | 生长及可选连通域过滤后的管腔候选 |
| skeleton.nii.gz | 0/1 | 细化及可选修剪后的骨架 |
| radius_field_mm.nii.gz | mm | EDT 距离场，不只是在中心线上取样的半径列表 |
| preview.svg | 投影图 | 原图、响应、Mask 和分支；选择物理范围最大的两个图像轴 |

全前景 Mask 没有体积内背景，EDT 为无穷，此时不写半径场并移除该名称的旧派生文件。
CSV 中的 EDT 直径也可能为空。EDT 距离指体素中心之间的距离，不能直接视为连续边界的精确管腔半径。
`comparison.png` 由独立绘图脚本生成，不是主 CLI 的固定输出；源数据获取与格式转换记录也由辅助脚本写出。

## CSV

文件第一行为字段名，ID 从 0 开始。空字段表示未定义/不可用，不等于 0。

### branches.csv

| 字段 | 单位 | 说明 |
| --- | --- | --- |
| branch_id | 整数 | 当前图中的分支序号 |
| start_node_id / end_node_id | 整数 | 两端节点 ID；相同 ID 可表示自环 |
| length_mm | mm | 当前体素路径的物理折线长度，未补充分叉簇内部长度 |
| mean_edt_diameter_mm | mm | 路径点 `2×EDT` 的算术平均；非有限时为空 |

当前没有 parent_id、稳定解剖命名、最小/最大直径字段。不能从 start/end 推断血流方向或父子关系。

### centerline.csv / nodes.csv

`centerline.csv`：`branch_id,point_id,x_ras_mm,y_ras_mm,z_ras_mm`。
point_id 是该分支的路径顺序，坐标为物理位置，不是体素索引。

`nodes.csv`：`node_id,x_ras_mm,y_ras_mm,z_ras_mm,degree`。
分叉簇的代表位置为簇内点的平均世界坐标，可能不等于分支边界路径的端点。
degree 是图中相邻分支数，自环在邻接表中计两次。

### sections.csv

| 字段 | 单位 | 说明 |
| --- | --- | --- |
| branch_id | 整数 | 分支序号 |
| midpoint_area_mm2 | mm² | 路径中间序号点处的截面积，未按弧长重新定位中点 |
| midpoint_equivalent_diameter_mm | mm | `2√(area/π)`，与 EDT 直径是不同估计 |
| mean_curvature_per_mm | 1/mm | 2 次邻居平滑后，有限的三点曲率值的算术平均 |
| status | 字符串 | ok 或 undefined_or_truncated |

截面切向量由中间点前后相邻路径点估计，采样步长为最小 spacing 的一半。
只统计包含中心点的平面 4 连通前景，避免把相邻血管一起计入。
采样不足、切向量退化、半径未定义或窗口截断时，截面字段为空；曲率字段仍可能有效。

### branch_angles.csv

`node_id,branch_a,branch_b,outward_angle_degrees,fit_window_mm`。
在同一节点对不同分支两两计算出射方向夹角，拟合窗口当前固定为 3 mm；退化方向的角度为空。
该值不是自动确定父分支后的父子夹角，窗口参数也尚无 CLI 选项。

## run.json

主 CLI 的运行参数与摘要；`schema_version` 当前为 1。

| 字段组 | 内容 |
| --- | --- |
| 输入 | input_path、input_kind、input_bytes；DICOM 目录的 input_bytes 为 null |
| 网格 | coordinate_system=RAS_mm、roi_voxel、dimensions、spacing_mm；无裁剪时 roi_voxel 为 null |
| 增强 | scales_mm、alpha、beta、c、bright_vessels |
| 分割 | low_threshold、high_threshold、min_intensity、max_intensity、seeds_voxel、connectivity |
| 后处理 | nms_seeds、nms_step_mm、minimum_component_voxels、prune_length_mm |
| 摘要 | max_threads、nodes、branches、elapsed_seconds |

没有灰度上下限时相应字段为 null，意为不限制，而不是 0。
seeds_voxel 保存实际使用的种子，NMS 模式下可能是自动生成的列表。
elapsed_seconds 从读入之前计时到报告/元数据写入之前附近，包含大部分处理与输出开销，不是精确的纯内核计时。
max_threads 是 OpenMP 线程上限，不能解读为每个阶段实际使用的线程数。
此文件不包含输入 SHA256、完整 direction/origin 或代码提交号；复现须同时保留输入、转换/下载 manifest、NIfTI 头、代码版本与实验记录。
运行失败可能留下旧/部分文件，应以退出码和本次日志为准。

## metrics.json

文件名由评价命令的第三个参数指定，主 CLI 不自动生成评价文件。

| 字段 | 单位/定义 |
| --- | --- |
| prediction_path / reference_path | 参与比较的输入路径 |
| reference_negative_inside | 是否将参考负值当作内部 |
| reference_aligned_to_prediction | 是否显式最近邻采样参考到预测网格 |
| dice / cldice | 0–1；clDice 使用本实现从两个 Mask 重新提取的骨架 |
| hd95_mm | 两个有向最近边界距离的 95% 最近秩百分位的最大值 |
| symmetric_mean_centerline_distance_mm | 两个方向全部样本距离的合并平均，单位 mm |
| prediction_components / reference_components | Mask 的 26 连通域数，**不是图分支数** |

无穷距离以 JSON null 表示。双空 Mask 的 Dice/clDice 为 1、HD95 为 0；单侧为空的距离不可用。
评价定义及空集行为见 [算法约定](algorithms.md)。

## 辅助记录与二进制导出

- `roi.json`：prepare_case 的原图裁剪索引、局部种子、参考 EDT 半径及 reference_guided_development_only 标记。
- `experiment.json`：冠脉复现脚本的数据版本与参考辅助评估范围。
- `manifest.json`：获取脚本保存的数据来源、CRC/SHA256、版本或传输信息；不同数据源的字段不完全相同。
- `*.conversion.json`：格式转换器版本、源/目标 SHA256、LPS 几何以及显式 donor（若使用）。

`medvessel_export input prefix` 生成 `prefix.raw` 和 `prefix.json`：

```python
import json
from pathlib import Path
import numpy as np

metadata_path = Path("build/export/volume.json")
metadata = json.loads(metadata_path.read_text())
shape = tuple(reversed(metadata["dimensions_xyz"]))
volume = np.fromfile(metadata_path.parent / metadata["raw_file"], dtype="<f4").reshape(shape)
```

raw 为小端 float32、x 最快；JSON 声明 dtype、文件名、x/y/z 尺寸、spacing_mm、origin_ras_mm 和按行 direction。
不能仅凭 raw 数组的形状恢复患者物理坐标。

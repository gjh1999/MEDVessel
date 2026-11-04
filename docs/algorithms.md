# 算法约定

## Gaussian / Hessian / Frangi

Gaussian sigma 单位为 mm，按每个轴 spacing 换算采样；核截断于 3 sigma，边界复制。
导数核为相关形式：一阶正一阶矩，二阶零均值并校正二阶矩，从而复现内部线性/二次多项式导数。
这是有限采样的校正导数核；sigma 远小于 spacing 时不可当作高精度连续导数近似。
Hessian 顺序为 xx、xy、xz、yy、yz、zz；默认使用 sigma² 尺度归一化。
矩阵使用图像轴正交基，特征值不受正交方向变换影响。

Jacobi 先按最大绝对元素缩放，每次消去最大非对角项；特征值按绝对值升序，向量同步重排。
Frangi 使用三维管状响应，alpha/beta/c 显式配置；c 与输入强度和导数尺度相关。
多尺度保留最大响应及 sigma，零响应的 BestScale 为 0。

## EDT / 细化 / 图

EDT 沿三个轴计算一维抛物线下包络，以轴间距平方为距离权重；搜索域是体积内背景体素中心。
细化以 EDT 为优先级队列顺序检查，删除后重新入队邻居，逐点重新判断 simple point 并保护端点。
simple point 使用前景 N26 中单一 26 连通分量、背景 N18 中与中心 6 邻接的单一 6 连通分量。
顺序删除不能直接改成批量并行删除。
局部判据参考 [Lohou / Bertrand 的拓扑定义](https://doi.org/10.1016/j.dam.2005.02.030)。
本实现是基础顺序侵蚀，不声称复现该论文完整 thinning 调度。

图压缩聚类相邻 degree>2 体素，沿 degree=2 路径追踪；闭环以一个合成节点和自环表达。
长度来自路径物理坐标；直径快速估计为 2×EDT。
截面使用垂直切向量平面、最近邻 Mask 采样与显式像素面积；窗口边缘有前景时拒绝截断结果。
曲率使用显式平滑次数和三点外接圆近似，端点/退化位置为 NaN。

## 评价与范围

HD95 定义为两个有向最近边界距离的 95% 最近秩百分位中的较大值；边界按 6 邻域提取，单位为 mm。
中心线距离为两个方向所有样本的合并平均，样本多的方向权重更大；与两个方向均值再平均的定义不同。
clDice 使用本项目细化产生的骨架；不能取代分支图拓扑检查。
空 Mask 对的 Dice/clDice 为 1、HD95 为 0；单侧为空时 HD95 无穷，JSON 用 null 表示不可用数值。
显式 `--align-reference` 使用最近邻将参考数据映射到预测网格；不启用时拒绝不同网格。

截面只统计包含中心点的 4 连通平面区域，以排除邻近血管；截面采样步长为最小 spacing 的一半。
CLI 的曲率报告使用 2 次邻居平滑，角度使用 3 mm 局部方向拟合，并输出所有出射分支两两夹角，未指定父分支。
短枝修剪默认关闭，因为它会删除真实或伪影终末支；启用后仍保留完整孤立分支和环路。

## 算法来源与成绩归属

核心路径是自行实现的 Gaussian / Hessian / Frangi、阈值区域生长、EDT、拓扑细化与分支图。开发方案列出的 [ThinVesselSegmentation](https://github.com/yzhong52/ThinVesselSegmentation) 是三维 vesselness / NMS 的阅读参考，其 README 指向 Frangi 等人的 1998 年多尺度血管增强论文。没有完整复现该仓库的模型拟合与曲率正则化，也没有在同一测试协议下运行原仓库。因此当前分数仅属于 MEDVessel 实现，不能称作原仓库复现成绩。

原仓库 README 未公布可直接比较的 ImageCAS 冠脉分割 Dice。它链接的 ICCV 2015 论文《Thin Structure Estimation with Curvature Regularization》包含其他任务的评价，不能把边缘检测的 F 值当成冠脉体积分割 Dice。VMTK 样例中的 level-set 是算法参考输出，比较值表示一致性，也不是本项目的人工金标准成绩。
## 可选的种子核心与固定距离补边

`SeededCoreGrowth.hpp` 在手动或 NMS 种子模式下提供可选分割：核心阈值为 `max(low, seedCoreRatio × 有效唯一种子的响应中位数)`；比例为 0 时使用 high。先在严格阈值下得到种子连通核心，再用物理间距计算距该核心的欧氏距离，限定在固定 growthBandMm 范围内以 low 补边。只有严格核心保留的种子可启动补边，距离域不会随着生长更新。范围为 0 时保留原算法。

开发配置 `--growth-band-mm 0.5 --seed-core-ratio 0.35` 在 11 个既有开发 ROI 上提高 Dice，但部分远端分支漏分加重；不自动设为默认。详见 [优化报告](segmentation_optimization.html)。

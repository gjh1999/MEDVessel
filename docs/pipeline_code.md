# 骨架和测量的核心代码

## 骨架：删除前检查 simple point

<!-- SOURCE: include/skeleton/Thinning.hpp|inline bool isSimplePoint26(|// Sequential distance-priority -->

先确认中心体素属于前景，再取 3×3×3 邻域。两个连通分量检查分别针对前景与背景，只有都满足条件才是 simple point。实际细化循环还保护端点，按 EDT 优先级顺序处理，删除后将邻居重新入队。

## 图结构：节点与分支分别存什么

<!-- SOURCE: include/skeleton/CenterlineGraph.hpp|struct VesselNode {|// Compress degree-two chains -->

节点保存物理位置和相邻分支 ID；分支保存两个端点、体素序列、有序世界坐标和长度。图是这两类对象的集合，不再只是一幅 0/1 骨架图。构图时把度数为 2 的长链压缩为一条边，相邻分叉点先合并为节点。

## 测量：沿物理路径累计长度

<!-- SOURCE: include/geometry/BranchAnalysis.hpp|inline double polylineLengthMm(|// Fit a local direction -->

输入已经是 RAS 毫米坐标，因此逐段相减、求欧氏范数、相加就得到毫米长度。先检查坐标有限，避免无效值传播。这里不能传原始体素索引代替物理坐标。

<!-- SOURCE: app/pipeline.cpp|        auto distance=medvessel::distanceTransform3D(mask);|        medvessel::writePreviewSvg -->

这段连续调用串起 EDT、细化、可选短枝修剪、构图和几何报告。thinning3D 内部也会计算用于排序的距离场；不是调用一个已经实现的 2D 滤波器逐层堆叠。writeGeometryReports 将图、Mask 和距离场合起来计算截面、角度和曲率。

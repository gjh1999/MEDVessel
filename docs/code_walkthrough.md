# 关键代码精读

## 1. 三维数组怎样存进一段内存

<!-- SOURCE: include/core/Volume3D.hpp|    std::size_t offset(|    std::array<std::size_t, 3> dimensions_; -->

先确定排列顺序：x 最快变化，其次 y，最后 z。先用 z×height+y 找到行，再乘 width 跳过前面的行，最后加 x。例如尺寸 4×3×2，位置 (2,1,1) 的偏移为 (1×3+1)×4+2=18。偏移从 0 开始。

`std::size_t` 表示大小与索引；成员函数末尾的 `const` 表示查询不会修改对象。先检查三个坐标，再计算偏移，避免越界。外层 `at()` 返回 `T&` 时可以赋值，返回 `const T&` 时只读。模板参数 T 让同一个容器既能保存 float 灰度，也能保存 uint8_t Mask。内存偏移不是毫米距离。

## 2. 坐标变换：从公式推导循环

<!-- SOURCE: include/core/Volume3D.hpp|    Point3D indexToWorld(|    Point3D worldToIndex( -->

公式是 world = origin + direction × (index × spacing)。先复制 origin 作为起点，内层循环计算方向矩阵第 i 行与缩放后索引的点积。`direction[i*3+j]` 对应按行存储的 3×3 矩阵元素。index 使用 double，因此也能表示半体素位置。

`const Point3D&` 是只读引用，函数返回新的坐标；`validate()` 检查几何合法性。写路径长度时，先把点转换到物理坐标，再求相邻点距离。例如 spacing 为 (0.5,0.5,1)，沿 z 相邻中心的距离是 1 mm，沿 x 则是 0.5 mm，不能统一按格子数计算。

## 3. Hessian：用表格描述六种导数

<!-- SOURCE: include/filters/Hessian.hpp|    const int orders[6][3]|    return result; -->

Hessian 对称，只保存 xx、xy、xz、yy、yz、zz 六项。`orders` 每行指定三轴导数阶数：`{2,0,0}` 表示 x 二阶导、y/z 平滑；`{1,1,0}` 表示 x/y 各一阶导、z 平滑。用配置表驱动循环，比复制六份卷积过程更容易核对。

`auto filtered=input` 复制输入，然后逐轴进行一维相关运算，上一步输出成为下一步输入。这利用 Gaussian 可分离性，避免直接遍历三维核。核函数接收毫米 sigma 和当前轴 spacing，内部换算采样尺度；各轴不能盲目共用体素尺度。

最后乘 sigma² 是二阶导数的尺度归一化，为多尺度比较做准备。节选前面还有参数检查和六个输出体的创建，片段不能独立编译。六个分量与临时体数据也意味着实际内存开销，不能只看输入文件大小。

## 4. Frangi：判断形状，再组合响应

<!-- SOURCE: include/filters/Frangi.hpp|inline double frangiResponse(|inline VesselnessResult frangi3D( -->

前置约定是特征值已按绝对值从小到大排列。a、b、c 用绝对值构造形状比例；亮管/暗管筛选仍看原始符号。亮管的两个较大特征值应为负，不满足则提前返回 0。b 或 c 为零时也提前返回，防止除零。

ra=b/c 用于区分板状与管状结构；rb=a/sqrt(b*c) 检查轴向变化是否足够小；strength 抑制几乎没有结构的区域。三个因子相乘，要求形状和强度共同满足条件。局部 c 是特征值绝对值，`options.c` 才是强度参数，读代码时要区分。

`-std::expm1(-t)` 等价于 `1-exp(-t)`，在 t 很小时能减少相近浮点数相减的精度损失。数学表达式转代码时，也需要考虑数值稳定性。

<!-- SOURCE: include/filters/Frangi.hpp|                float response=|    return result; -->

外层循环逐个处理尺度，内层逐体素计算特征值。新响应严格更大时，响应图和最佳尺度图一起更新，保证对应关系。同分保留先前尺度；全零响应的最佳尺度保持初始化的 0，不能解释为测出了零毫米血管。最佳尺度也不是直接测得的血管半径。本节第二段节选包含外层循环的结束括号，需要结合完整函数阅读。

## 5. 区域生长：先定义合法点，再用队列传播

<!-- SOURCE: include/segmentation/RegionGrowing.hpp|    auto eligible=|    while(!pending.empty()) -->

先用局部 lambda 统一候选规则：响应和灰度必须有限、响应达到 lowThreshold、灰度位于允许区间。`[&]` 按引用访问当前函数里的数据与参数；这个 lambda 只在本次函数调用中使用，不保存到外部。

队列保存接下来需要访问的点。种子先检查范围，再判断合法性；入队之前立刻把 Mask 标为 1，因此重复种子不会重复入队。后续循环取出队首、遍历邻居，把尚未标记的合法邻居也标记并入队，直到队列为空。这是广度优先遍历，不是每次只挑响应最大的点。

显式种子只要求满足低阈值和灰度条件。双阈值模式先把达到 highThreshold 的点收集成种子，再复用同一生长函数。因此 highThreshold 负责起点，lowThreshold 负责允许传播的区域；两者不是简单地把中间响应全部保留。

<!-- SOURCE: include/segmentation/RegionGrowing.hpp|            int steps=|            if(inside -->

dx/dy/dz 来自 -1 到 1 的邻域循环。steps=0 是自己；6 邻接只保留 steps=1 的面邻居，26 邻接还允许棱和角邻居。索引是无符号数，0 减 1 会下溢，所以先检查边缘，再计算 q。边界检查的顺序也是算法正确性的一部分。

## 6. Dice：先检查比较对象，再实现公式

<!-- SOURCE: include/segmentation/RegionGrowing.hpp|inline double dice(|} // namespace medvessel -->

先确认两个 Mask 的尺寸、spacing、origin、direction 一致，否则同一数组位置未必代表同一物理位置。遍历时将非零值统一视为前景，统计 A、B 和交集，最后返回 2×交集/(A+B)。两个 Mask 都为空时，本项目约定返回 1，避免 0/0。

`aa&&bb` 是布尔交集，累加时转成 0 或 1。double 计数器也让最后除法按浮点进行。几何不匹配时，应检查配准与重采样，不能直接删掉校验。Dice 只衡量重叠，不能单独判断细支断裂或边界偏差，还要结合后文其他指标。

## 7. 用小例子检查自己是否真正理解

先手算存储偏移，再设置非等距 spacing 检查坐标转换。构造一个强种子经弱响应连接另一端的案例，观察双阈值如何传播；让两个点只在角上相接，对比 6/26 邻接。最后用完全相同与完全不相交的非空 Mask，核对 Dice 为 1 和 0。

这些练习分别验证存储、几何、连通与指标定义。完整流程从 `app/pipeline.cpp` 继续阅读；后面的 Qt 专章则解释按钮、JobConfig、QProcess、日志和结果显示怎样连接。这里的源码片段由网页生成器直接提取，保留连续代码及来源行号；完整源码才是可编译实现。

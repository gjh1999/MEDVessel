# PPT 配图说明

优先使用 pipeline_mip_16x9.png 讲总体流程，pipeline_slice_16x9.png 讲同一切片的变化；comparison_slice_16x9.png 展示分割与参考差异。16:9 图片为 3200×1800，单阶段无标题图为 1600×1600。

原始灰度使用窗宽 700、窗位 250；Frangi 固定显示范围 0～1。投影为 Z 方向最大值（Mask/骨架等价于并集），切片使用 ROI 内 z=40。投影会叠加不同深度，不能据此判断三维连通性。坐标轴为原网格方向，不将其标称为标准解剖轴位。

骨架叠加图：橙色为预测 Mask，青色为体素骨架；点尺寸为可视化设置，不代表血管直径。对照图：橙线为预测，青线为 ImageCAS-X v2 参考。

建议 PPT 图注：ImageCAS 病例 1，参考标注辅助的局部开发 ROI（109×109×81）；Frangi → 区域生长 → 骨架提取。局部三维 Dice=0.8162，非独立测试结果。

数据来源：ImageCAS v3（CC BY-NC 4.0）；ImageCAS-X v2（Zenodo 23562647，CC BY 4.0）。原始文件不改动。具体运行参数见 build/imagecas-x-case1/result/run.json。

重现：先用 medvessel_export 将骨架导出到 build/imagecas-x-plots/skeleton，再运行 tools/plot_ppt_pipeline.py（需要 NumPy、Matplotlib 和 Windows 微软雅黑字体）。

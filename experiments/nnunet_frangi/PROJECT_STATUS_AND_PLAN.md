# MEDVessel 冠脉分割实验：现状与后续计划

更新时间：2026-11-07 19:01（北京时间）。

## 当前状态：已按用户要求暂停（2026-11-08）

最新用户指令为暂停。已先停止预处理消融队列，再停止新模型比较及评分进程树；进程复查未发现相关Python任务。模型、数据、预测、日志全部保留。comparison_status.json与preprocessing-ablation/status.json已记录paused及暂停前状态，并设置停止标记。暂停时正在评分best/plain；其余三组统一验证Dice：best/TTA 0.85587304、final/plain 0.85118203、final/TTA 0.85372490。纯CT与双通道配对训练尚未开始。恢复时需显式启动队列；当前两个新脚本尚未实现运行中轮询停止标记，不可仅删除标记便视为已恢复，也不可依赖标记终止运行进程。恢复比较会复用匹配manifest的预测并重新评分。

以下为暂停前执行记录。

用户已确认预处理优化计划并授权执行，详见 `PREPROCESSING_PLAN.md`。`preprocessing_ablation.py` 队列等待当前四组模型比较完成后，按相同划分、固定445轮预算分别从头训练纯CT与双通道，随后再决定灰度窗口和重采样实验。纯CT准备检查通过：85例开发数据、无Frangi通道、原空间规划与冻结划分；不复制保留测试影像与标签。队列状态见 `work/preprocessing-ablation/status.json`。

最新进展：低学习率续训200轮已完成，累计445轮；nnU-Net内置15例全体积验证Dice为0.85372995。原基线四组验证完成，最佳为best+TTA，Dice0.77798431。用户授权继续，已新增 `compare_finetune.py`：串行运行新模型final/best × TTA/plain四组验证，输出统一Dice/precision/recall/clDice/HD95/ASSD及低分病例、各组连通域体积阈值研究；仅使用验证集，原始预测不做后处理覆盖。后台进度见 `work/accuracy-finetune-01/comparison_status.json`，结果见 `comparison_results.json`。后处理候选仍需进一步验证连通性，尚不设为默认。

用户已授权按下午计划继续执行，无需中途询问。已完成官方配置初步核对，见 `OFFICIAL_CONFIG_AUDIT.md`；已恢复基线守护与低学习率实验队列。基线验证完成后自动开始独立续训，保留原模型。`finish_experiment.py` 默认只完成验证对比，不再自动评估保留测试集。定时提醒保持关闭。检查点预检通过，14项流程单元测试通过。结果仍需等待后台任务完成，尚不能报告精度提升。

以下暂停记录为恢复前的历史交接信息；停止标记与进程状态应以当前 status/log 为准。

## 历史状态：按用户要求暂停

用户要求先记录项目现状与计划，暂停本项目，之后再继续。
已向基线守护、精度优化队列和本机通知程序写入停止标记。
本次暂停不删除模型、预测、数据、日志，也不继续训练、评估或下载。
工作区存在本次及此前尚未提交的改动；本文不代表这些改动已经提交 Git。

## 目标与实验范围

- 用户明确优先提高分割精度，不要求优先优化推理速度。
- 曾提出完整 CT 平均 Dice ≥90% 作为研究目标，用户尚未确认具体验收阈值。
- 验收不能只看平均 Dice，还需要逐例表现、召回率、精确率、clDice、表面距离及血管连通性。
- 当前是内部研究实验，不能将结果表述为临床可用性证明。

## 数据、模型与训练

- 原始 CTA：ImageCAS；标签：ImageCAS-X v2，冠脉分支标签合并为二分类前景。
- 已准备 100 例：70 例训练、15 例验证、15 例保留测试。
- 训练与验证划分文件和 holdout 文件一致，三组病例 ID 无交叉；患者身份独立性尚未额外核实。
- 之前用于开发的 11 个病例被排除在保留测试集之外，但可出现在训练或验证中。
- 模型：nnU-Net v2，3d_fullres，fold 0，CT＋Frangi 双通道，batch size 2。
- patch size：[96, 160, 160]；启用混合精度。
- 基线训练于 2026-11-07 17:18 完成 245 轮（日志最后 Epoch 244）。
- 早停：EMA pseudo Dice 连续 50 轮没有累计至少 0.002 的提升。
- 结束时学习率约 0.00777445，初始值为 0.01。过早停止、未充分低学习率收敛是待验证假设，不能当作已证明的唯一原因。

基线目录：`work/full-real-100/`。
模型目录：`work/full-real-100/nnUNet_results/Dataset511_CoronaryFrangi/nnUNetTrainer__nnUNetPlans__3d_fullres/fold_0/`。
已保留 `checkpoint_final.pth`、`checkpoint_best.pth` 等检查点。

## 已确认的验证结果

均为 15 例完整 CT 的验证结果；不是保留测试结果。

| 配置 | 平均 Dice |
|---|---:|
| final，关闭翻转增强 | 0.7593419 |
| final，开启翻转增强 TTA | 0.7706602 |
| best，关闭翻转增强 | 0.7579966 |
| best，开启 TTA | 暂停时尚未完成 |

nnU-Net 自带全量验证汇总 Dice 为 0.7706606，与 final TTA 基本一致。
不要把训练日志的约 0.86 EMA pseudo Dice 当作完整 CT 的最终指标。

final plain 的其他指标：

| 指标 | 均值 |
|---|---:|
| Precision | 0.6677819 |
| Recall | 0.9041556 |
| clDice | 0.7518215 |
| HD95 | 42.4864 mm |
| ASSD | 5.7375 mm |

已完成验证的病例 Dice 标准差约 0.115（final plain），存在明显病例间差异。
低分病例包括 imagecas_2（0.5034）、imagecas_124（0.5454）、imagecas_114（0.6538）。
低精确率、高召回率提示明显过分割；错误解剖区域仍需可视化确认。

## 已做的排查与修复

1. **数据供给瓶颈**：2/4/6 个增强进程实测约 1.98/3.40/3.92 batch/s。
   改为训练增强 6 进程、验证增强 3 进程后，每轮从约 113–115 秒降为 51–56 秒；
   短时 GPU 采样为 97–99%，不是完整运行期的平均利用率。
2. **预测单位元数据丢失**：导出 NIfTI 的空间单位变成 unknown，严格评分拒绝读取。
   `predict.py` 现会核对源 CT 的尺寸、仿射矩阵和毫米单位后补回元数据；
   已修复现有 final plain 的 15 个预测，验证首例体素未改变；合成检查确认网格不匹配仍会被拒绝。
3. **重试流程**：评估目录已存在会导致失败，且旧流程重复跑全量验证。
   已支持匹配 manifest 的预测续跑，复用完成的基线验证；新推理须记录模型、检查点和输入配置。
4. **检查点选择**：续训时可以选择最新保存的 best/latest 检查点，避免总是回退到较旧的定期检查点。
5. **碎片过滤研究**：`tune_components.py` 在验证集比较物理体积阈值，不修改原始掩膜。
   过滤小于 100 mm³ 的连通域后，final plain 平均 Dice 从 0.7593 提至 0.8172，
   但 Recall 从 0.9042 降至 0.8798。尚未采用为默认后处理，也没有据此宣称精度达标。
   imagecas_2 过滤后仍只有约 0.5264，说明大块误分割不能靠碎片清理解决。

## 暂停时的后台任务

- 基线评估暂停前阶段：`predict_checkpoint_best_tta`。
- 已有 final plain、final TTA、best plain 的评分文件。
- 尚未生成最终选定配置和保留测试指标；恢复时应重新核实文件状态。
- 低学习率实验队列 `work/accuracy-finetune-01/` 原处于等待基线评估完成状态，**尚未开始续训**。
- 15 分钟本机通知已按用户要求停止，不应在恢复训练时自动重新开启。

停止标记：

- `work/full-real-100/STOP_TRAINING`
- `work/full-real-100/STOP_REPORTING`
- `work/accuracy-finetune-01/STOP`

注意：`training_status.json`、`evaluation_status.json` 中的最后阶段可能是中断前的阶段。
以本文的用户暂停状态、停止标记、guardian/queue 的 stopped 状态及实际进程为准。

## 官方论文与开源配置：下一步先对齐

参考：

- 官方仓库：https://github.com/kitbransby/ImageCAS-X
- 论文：https://arxiv.org/html/2608.30404v1
- 基准：https://kitbransby.github.io/ImageCAS-X/

官方公开训练、预处理、增强、后处理和评分框架；但 nnU-Net 在 README 中列为 External，
`configs/nnunet.json` 仅声明方法名称，不包含完整 nnU-Net plans/trainer 复现参数。
论文附录列出 nnU-Net batch 2、初始 LR 0.01、SGD、Dice＋CE、深监督等设置。
仍需核对预训练权重配套 plans、trainer 版本、实际训练轮数、采样及后处理。
不能将本项目称为已经严格复现论文 nnU-Net。

官方 ImageCAS-X 为 560/80/160 的训练/验证/测试划分，nnU-Net 测试 Dice 89.8%；
我们只有 70 例训练，且验证集与官方测试集不同，不能直接作公平排名。

## 恢复后的优先计划

1. **先核对官方复现配置**：记录仓库 commit、版本、plans、trainer、损失、采样、归一化、训练预算、增强、选模、后处理及评分定义。
   区分论文明确设置、代码实际设置和仍然未知的部分。
2. **完成基线验证对比**：续跑 best TTA 并评分。保留现有预测与结果，核验 manifest；不要覆盖基线。
3. **逐例定位错误**：重点查看低分病例，区分大块假阳性、远端分支缺失、管径偏差、断裂和标签/配准异常。
   CT＋Frangi 是否有益尚无对照证据。
4. **低学习率对照实验**：已实现 `accuracy_finetune.py`，与基线隔离存储。
   从 final 权重和优化器状态继续，起始 LR 0.001，200 轮多项式衰减；
   新阶段早停 patience 80、min_delta 0.001；每 10 轮保存，增强 6 进程。
   仅改变收敛策略，原模型、数据划分、损失、batch 等保持一致。
   已通过检查点有限值、epoch/历史一致性、LR 起点/衰减/续跑偏移等预检；尚未验证实际训练改善。
   待官方配置核对完成后，再决定按现方案启动还是修订方案。
5. **纯 CT 消融与数据扩充**：同划分、同训练预算比较单通道/双通道，再考虑扩大训练数据；不能挪用现有验证或测试样本。
6. **联合选模**：在验证集对比权重、TTA、概率阈值及后处理，同时检查召回与连通性。
   后处理调参结果仍是验证结果，不能作为无偏测试成绩。
7. **最后验收**：方案冻结后评估保留测试集；不根据测试成绩反复调参。
   旧基线流水线会自动运行保留测试；恢复前应先审查并根据上述计划调整测试入口。

## 之后如何恢复（现在不执行）

1. 读取本文及各 status/log，确认没有遗留训练或评估进程。
2. 先完成官方配置核对，审查自动测试入口与低学习率实验方案。
3. 明确恢复基线评估时才移除 `STOP_TRAINING`，再启动 `training_guardian.py`。
4. 明确恢复精度续训时才移除 `accuracy-finetune-01/STOP`，再启动
   `accuracy_finetune.py --execute-training`；其队列默认等待基线守护 completed。
5. 保留 `STOP_REPORTING`，除非用户再次要求本机定时通知。

关键文档与结果：

- `ACCURACY_PLAN.md`：早期精度计划，以本交接文档为最新优先级。
- `TRAINING_PERFORMANCE.md`：性能调优记录。
- `work/full-real-100/accuracy_diagnosis.json`：诊断摘要。
- `work/full-real-100/component_tuning.json`：碎片过滤候选逐例结果。
- `work/full-real-100/metrics_checkpoint_*.json`：完整指标。
- `work/full-real-100/training.log`、`guardian_child.log`、各阶段 `.log`：运行与故障记录。

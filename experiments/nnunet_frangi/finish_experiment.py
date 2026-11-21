"""Wait for the authorized baseline, tune inference on validation, test once and report."""
import argparse
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
WORK = HERE / 'work/full-real-100'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evaluate-holdout', action='store_true',
                        help='Only use after the complete experiment configuration is frozen')
    args = parser.parse_args()
    status = WORK / 'evaluation_status.json'
    def record(stage, **kwargs):
        status.write_text(json.dumps({'stage': stage, **kwargs}, indent=2))
    def run(name, script, args):
        record(name)
        with (WORK / f'{name}.log').open('w', encoding='utf-8') as log:
            subprocess.run([sys.executable, str(HERE / script), *map(str, args)],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
    lock = WORK / 'EVALUATION_LOCK'
    lock.open('x').close()
    try:
        record('waiting_for_training')
        while True:
            state = json.loads((WORK / 'pipeline_status.json').read_text())
            if state['stage'] == 'failed':
                raise RuntimeError(f'Baseline pipeline failed: {state}')
            if state['stage'] == 'completed':
                break
            time.sleep(30)
        dataset = WORK / 'nnUNet_raw/Dataset511_CoronaryFrangi'
        split = json.loads((dataset / 'holdout.json').read_text())
        model = WORK / 'nnUNet_results' / dataset.name / 'nnUNetTrainer__nnUNetPlans__3d_fullres'
        images = WORK / 'validation_images'
        images.mkdir(exist_ok=True)
        for cid in split['val']:
            for channel in (0, 1):
                name = f'{cid}_{channel:04d}.nii.gz'
                shutil.copy2(dataset / 'imagesTr' / name, images / name)
        val_list = WORK / 'validation_cases.json'
        val_list.write_text(json.dumps(split['val']))
        candidates = []
        for checkpoint in ('checkpoint_final.pth', 'checkpoint_best.pth'):
            for mirror in (False, True):
                tag = checkpoint.removesuffix('.pth') + ('_tta' if mirror else '_plain')
                output = WORK / f'validation_{tag}'
                run(f'predict_{tag}', 'predict.py', [model, images, output, '--work', WORK,
                    '--checkpoint', checkpoint, '--resume'] + (['--mirroring'] if mirror else []))
                metrics = WORK / f'metrics_{tag}.json'
                run(f'score_{tag}', 'evaluate_predictions.py', [output, dataset / 'labelsTr', metrics,
                    '--case-list', val_list])
                score = json.loads(metrics.read_text())['aggregates']['dice']['mean_defined']
                candidates.append({'checkpoint': checkpoint, 'mirroring': mirror, 'validation_dice': score})
        winner = max(candidates, key=lambda c: c['validation_dice'])
        (WORK / 'selected_parameters.json').write_text(json.dumps({'candidates': candidates, 'selected': winner}, indent=2))
        if not args.evaluate_holdout:
            record('completed', scope='baseline_validation_only', selected=winner,
                   holdout_evaluated=False)
            return
        test_list = WORK / 'test_cases.json'
        test_list.write_text(json.dumps(split['test']))
        output = WORK / 'test_predictions'
        run('test_predict', 'predict.py', [model, dataset / 'imagesTs', output, '--work', WORK,
            '--checkpoint', winner['checkpoint'], '--resume'] + (['--mirroring'] if winner['mirroring'] else []))
        metrics = WORK / 'test_metrics.json'
        run('test_score', 'evaluate_predictions.py', [output, dataset / 'labelsTs', metrics, '--case-list', test_list])
        results = json.loads(metrics.read_text())
        report = ['# CTA + Frangi 双通道 nnU-Net 实验报告', '',
            '续训早停规则：验证 EMA pseudo Dice 连续 50 轮累计提升不足 0.002 时结束；保持原学习率计划，保留最佳及结束时权重。',
            '早停状态：' + ((model / 'fold_0/early_stopping.json').read_text() if (model / 'fold_0/early_stopping.json').exists() else '未记录；不可据此认定触发早停。'), '',
            '100 例 ImageCAS CTA，ImageCAS-X v2 标签；70 例训练、15 例验证、15 例内部保留测试。',
            '测试病例未参与 nnU-Net 指纹提取或训练；先前 11 例开发病例排除在测试集之外。',
            '病例 ID 对应患者的独立性尚未核实；结果不代表外部泛化或临床有效性。', '',
            '## 验证集调参', '',
            '比较最佳/最终 checkpoint 与是否启用翻转增强；仅按验证集平均 Dice 选择。未进行训练超参数搜索。', '',
            '```json', json.dumps(candidates, ensure_ascii=False, indent=2), '```', '',
            '选定配置：' + json.dumps(winner, ensure_ascii=False), '', '## 保留测试结果', '',
            '| 指标 | 均值 | 样本标准差 | 有定义病例数 | 未定义病例数 |',
            '|---|---:|---:|---:|---:|']
        for name, values in results['aggregates'].items():
            report.append(f"| {name} | {values['mean_defined']} | {values['sample_std_defined']} | {values['defined_cases']} | {values['undefined_cases']} |")
        report.extend(['', '完整逐例结果见 test_metrics.json；数据划分见 raw 数据目录 holdout.json。',
            '缺少单通道 CTA 消融对照，不能将本次结果解释为 Frangi 带来的提升。'])
        (WORK / 'experiment_report.md').write_text('\n'.join(report), encoding='utf-8')
        record('completed', report=str(WORK / 'experiment_report.md'))
    except Exception as error:
        record('failed', error=str(error))
        raise
    finally:
        lock.unlink()


if __name__ == '__main__':
    main()

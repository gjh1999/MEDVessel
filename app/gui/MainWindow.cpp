#include "MainWindow.hpp"
#include <QtWidgets>
#include <QSvgRenderer>
#include <QGraphicsSvgItem>
#include <QJsonArray>
#include <memory>

#ifndef MEDVESSEL_SOURCE_DIR
#define MEDVESSEL_SOURCE_DIR "."
#endif
namespace medvessel::desktop {
namespace {
QString sourceRoot(){
    const auto local=QCoreApplication::applicationDirPath();
    if(QFileInfo::exists(local+"/docs/medvessel_learning_guide.html"))return local;
    return QString::fromUtf8(MEDVESSEL_SOURCE_DIR);
}
QString cliPath(){
    QString name="medvessel_cli";
#ifdef Q_OS_WIN
    name+=".exe";
#endif
    for(const auto& dir:{QCoreApplication::applicationDirPath(),sourceRoot()+"/build/cmake"}) {
        auto path=QDir(dir).filePath(name);if(QFileInfo::exists(path))return QDir::cleanPath(path);
    }
    return {};
}
QLabel* text(const QString& s){auto* label=new QLabel(s);label->setWordWrap(true);label->setTextFormat(Qt::PlainText);return label;}
QString arrayText(const QJsonValue& value){QStringList s;for(auto v:value.toArray())s<<QString::number(v.toDouble(),'g',6);return s.join(" × ");}
}
MainWindow::MainWindow(QWidget* parent):QMainWindow(parent),job_(this) {
    setFont(QFont("Microsoft YaHei UI",10));
    setWindowTitle("MEDVessel · 血管分析工作台");resize(1480,940);setMinimumSize(1100,720);
    setStyleSheet(R"(
        QMainWindow,QWidget#workspace{background:#f2f5f3;color:#193a34;}
        QToolBar{background:#fff;border:0;border-bottom:1px solid #d7e2dc;padding:10px;spacing:8px;}
        QGroupBox{font-weight:600;border:1px solid #d6e2dc;border-radius:8px;margin-top:15px;padding-top:16px;background:#fff;}
        QGroupBox::title{subcontrol-origin:margin;left:12px;padding:0 5px;color:#275e50;}
        QLineEdit,QDoubleSpinBox,QSpinBox,QComboBox{background:#fff;border:1px solid #c8d8d0;border-radius:5px;padding:5px;color:#173b32;}
        QPushButton{background:#fff;border:1px solid #c5d6cc;border-radius:5px;padding:7px 12px;color:#244c40;}
        QPushButton:hover{background:#e8f1eb;}QPushButton:disabled{color:#87968e;background:#edf1ee;border-color:#dde5df;}
        QPushButton#run{background:#1b604e;color:white;border:0;font-weight:600;}
        QLabel#title{font-size:23px;font-weight:700;}QLabel#muted{color:#627a6f;}
        QTableView{background:white;alternate-background-color:#f4f8f5;border:1px solid #d7e2dc;gridline-color:#e8eee9;selection-background-color:#cee7da;selection-color:#12392b;}
        QHeaderView::section{background:#eaf1ed;padding:7px;border:0;border-bottom:1px solid #d7e2dc;color:#305647;}
        QPlainTextEdit{background:#172d28;color:#d5e8dc;border:0;border-radius:5px;font-family:Consolas;font-size:12px;}
        QProgressBar{border:0;background:#dfe9e2;border-radius:3px;max-height:7px;}QProgressBar::chunk{background:#368369;}
        QSplitter::handle{background:#e1e9e3;}QTabWidget::pane{border:1px solid #d7e2dc;background:white;}
        QTabBar::tab{padding:9px 15px;background:#e4ece6;color:#4d695b;}QTabBar::tab:selected{background:white;color:#194e3c;}
    )");
    auto* toolbar=addToolBar("工作台");toolbar->setMovable(false);
    auto* brand=text("MEDVessel");brand->setObjectName("title");toolbar->addWidget(brand);toolbar->addSeparator();
    auto* open=new QPushButton("打开结果");open->setObjectName("openResult");toolbar->addWidget(open);
    demo_=new QPushButton("载入合成示例");toolbar->addWidget(demo_);
    loadConfig_=new QPushButton("载入配置");toolbar->addWidget(loadConfig_);
    auto* save=new QPushButton("保存配置");toolbar->addWidget(save);
    auto* spacer=new QWidget;spacer->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);toolbar->addWidget(spacer);
    auto* help=new QPushButton("学习手册");toolbar->addWidget(help);
    run_=new QPushButton("开始分析");run_->setObjectName("run");toolbar->addWidget(run_);
    stop_=new QPushButton("停止");stop_->setEnabled(false);toolbar->addWidget(stop_);
    auto* workspace=new QWidget;workspace->setObjectName("workspace");setCentralWidget(workspace);
    auto* root=new QVBoxLayout(workspace);root->setContentsMargins(16,12,16,12);
    auto* header=new QHBoxLayout;auto* title=text("影像到血管结构");title->setWordWrap(false);title->setObjectName("title");header->addWidget(title);
    auto* subtitle=text("配置 → 分析 → 查看结果");subtitle->setWordWrap(false);subtitle->setObjectName("muted");header->addWidget(subtitle);header->addStretch();
    root->addLayout(header);
    auto* split=new QSplitter(Qt::Horizontal);root->addWidget(split,1);
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setMinimumWidth(300);scroll->setMaximumWidth(420);
    parameters_=new QWidget;auto* left=new QVBoxLayout(parameters_);left->setContentsMargins(0,0,8,0);scroll->setWidget(parameters_);split->addWidget(scroll);
    auto group=[&](const QString& title){auto* g=new QGroupBox(title);auto* form=new QFormLayout(g);form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);form->setLabelAlignment(Qt::AlignLeft);left->addWidget(g);return form;};
    auto edit=[&](QFormLayout* f,const QString& key,const QString& label,const QString& hint){auto* e=new QLineEdit;e->setObjectName(key);e->setPlaceholderText(hint);e->setToolTip(hint);edits_[key]=e;f->addRow(label,e);return e;};
    auto number=[&](QFormLayout* f,const QString& key,const QString& label,double min,double max,int decimals){auto* e=new QDoubleSpinBox;e->setObjectName(key);e->setRange(min,max);e->setDecimals(decimals);e->setSingleStep(decimals>1?.05:1);doubles_[key]=e;f->addRow(label,e);};
    auto integer=[&](QFormLayout* f,const QString& key,const QString& label,int min,int max){auto* e=new QSpinBox;e->setObjectName(key);e->setRange(min,max);ints_[key]=e;f->addRow(label,e);};
    auto check=[&](QFormLayout* f,const QString& key,const QString& label){auto* e=new QCheckBox(label);e->setObjectName(key);checks_[key]=e;f->addRow(e);};
    auto* inputForm=group("01 · 输入与保存");edit(inputForm,"input","影像","NIfTI 文件 / 单一 DICOM 序列目录");
    auto* picks=new QWidget;auto* picksLayout=new QHBoxLayout(picks);picksLayout->setContentsMargins(0,0,0,0);
    auto* pickFile=new QPushButton("NIfTI 文件");auto* pickDicom=new QPushButton("DICOM 目录");picksLayout->addWidget(pickFile);picksLayout->addWidget(pickDicom);inputForm->addRow(picks);
    edit(inputForm,"output","结果根目录","每次分析自动创建独立子目录");auto* pickOutput=new QPushButton("选择保存位置…");inputForm->addRow(pickOutput);
    auto* common=group("02 · 常用参数");edit(common,"scales","尺度 mm","如 0.5,1,2；多个尺度用英文逗号分隔");
    number(common,"low","候选阈值 low",0,1,3);number(common,"high","种子阈值 high",0,1,3);
    edit(common,"minIntensity","灰度下限","留空为不限制；输入需确认是否为 HU");edit(common,"maxIntensity","灰度上限","留空为不限制");
    auto* region=group("03 · ROI 与种子（可选）");edit(region,"roi","裁剪区域","x,y,z,width,height,depth；原图零基索引");
    edit(region,"seeds","种子点","x,y,z；多个点用分号分隔");check(region,"sourceSeeds","种子使用原图索引（自动换算 ROI）");
    auto* roiNote=text("留空处理全图。未指定种子时使用双阈值生长；手工种子仍须满足 low 与灰度约束。");roiNote->setObjectName("muted");region->addRow(roiNote);
    auto* advanced=new QGroupBox("高级参数 · 点击展开");advanced->setCheckable(true);advanced->setChecked(false);auto* advancedLayout=new QVBoxLayout(advanced);
    auto* advancedBody=new QWidget;auto* advancedForm=new QFormLayout(advancedBody);advancedForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);advancedLayout->addWidget(advancedBody);advancedBody->hide();
    connect(advanced,&QGroupBox::toggled,advancedBody,&QWidget::setVisible);left->addWidget(advanced);
    edit(advancedForm,"executable","算法程序","medvessel_cli.exe 完整路径");auto* pickCli=new QPushButton("选择算法程序…");advancedForm->addRow(pickCli);
    number(advancedForm,"alpha","alpha",.0001,100000,4);number(advancedForm,"beta","beta",.0001,100000,4);number(advancedForm,"c","c",.0001,1000000,4);
    check(advancedForm,"dark","增强暗管（默认亮管）");check(advancedForm,"nms","使用 NMS 自动种子（无手工种子时）");
    number(advancedForm,"nmsStep","NMS 距离 mm",.0001,1000,4);integer(advancedForm,"minComponent","最小连通域 / 体素",0,1000000000);
    number(advancedForm,"prune","短枝修剪 mm",0,10000,3);integer(advancedForm,"threads","线程数",1,256);
    connectivity_=new QComboBox;connectivity_->addItems({"26","6"});advancedForm->addRow("生长邻接",connectivity_);
    auto* reset=new QPushButton("恢复默认参数");left->addWidget(reset);left->addStretch();
    auto* center=new QWidget;auto* centerLayout=new QVBoxLayout(center);centerLayout->setContentsMargins(10,0,10,0);split->addWidget(center);
    auto* centerHeader=new QHBoxLayout;centerHeader->addWidget(text("结果投影"));centerHeader->addStretch();
    auto* fit=new QPushButton("适应窗口");auto* zoomIn=new QPushButton("＋");auto* zoomOut=new QPushButton("－");centerHeader->addWidget(fit);centerHeader->addWidget(zoomOut);centerHeader->addWidget(zoomIn);centerLayout->addLayout(centerHeader);
    scene_=new QGraphicsScene(this);view_=new QGraphicsView(scene_);view_->setObjectName("preview");view_->setBackgroundBrush(QColor("#101b2c"));view_->setRenderHint(QPainter::Antialiasing);view_->setDragMode(QGraphicsView::ScrollHandDrag);centerLayout->addWidget(view_,1);
    previewHint_=text("打开已有结果，或载入示例后开始分析。完成后展示原图、增强、分割和中心线投影。");previewHint_->setObjectName("muted");centerLayout->addWidget(previewHint_);
    auto* warning=text("投影会叠加不同深度的结构，不能单凭投影判断三维连通性。当前版本提供投影预览；三方向切片与 3D 显示尚未接入。");warning->setObjectName("muted");centerLayout->addWidget(warning);
    auto* right=new QTabWidget;right->setMinimumWidth(300);split->addWidget(right);split->setSizes({330,710,400});split->setStretchFactor(1,1);
    auto* info=new QWidget;auto* infoLayout=new QVBoxLayout(info);summary_=text("还没有分析结果\n\n可先载入合成示例，体验完整流程。");summary_->setObjectName("summary");summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);infoLayout->addWidget(summary_);
    resultPath_=text("");resultPath_->setTextInteractionFlags(Qt::TextSelectableByMouse);infoLayout->addWidget(resultPath_);infoLayout->addStretch();
    folder_=new QPushButton("打开结果文件夹");folder_->setEnabled(false);export_=new QPushButton("导出分支 CSV…");export_->setEnabled(false);infoLayout->addWidget(folder_);infoLayout->addWidget(export_);right->addTab(info,"实验摘要");
    table_=new QTableView;table_->setObjectName("branches");model_=new QStandardItemModel(this);table_->setModel(model_);table_->setAlternatingRowColors(true);table_->setSortingEnabled(true);table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);right->addTab(table_,"分支表");
    auto* logGroup=new QGroupBox("运行日志");auto* logLayout=new QVBoxLayout(logGroup);logs_=new QPlainTextEdit;logs_->setReadOnly(true);logs_->setMaximumBlockCount(1500);logs_->setMaximumHeight(115);logLayout->addWidget(logs_);root->addWidget(logGroup);
    auto* status=new QHBoxLayout;stage_=text("就绪");stage_->setObjectName("stage");elapsed_=text("");status->addWidget(stage_);status->addStretch();status->addWidget(elapsed_);root->addLayout(status);progress_=new QProgressBar;progress_->setRange(0,1);progress_->setValue(0);progress_->setTextVisible(false);root->addWidget(progress_);
    JobConfig defaults;defaults.executable=cliPath();
    defaults.outputRoot=QFileInfo::exists(sourceRoot()+"/CMakeLists.txt")?sourceRoot()+"/build/gui-results":QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/MEDVessel/results";
    setConfig(defaults);
    connect(pickFile,&QPushButton::clicked,this,[this]{auto p=QFileDialog::getOpenFileName(this,"选择 NIfTI 影像",{},"NIfTI (*.nii *.nii.gz)");if(!p.isEmpty())edits_["input"]->setText(p);});
    connect(pickDicom,&QPushButton::clicked,this,[this]{auto p=QFileDialog::getExistingDirectory(this,"选择单一 DICOM 序列目录");if(!p.isEmpty())edits_["input"]->setText(p);});
    connect(pickOutput,&QPushButton::clicked,this,[this]{auto p=QFileDialog::getExistingDirectory(this,"选择结果根目录",edits_["output"]->text());if(!p.isEmpty())edits_["output"]->setText(p);});
    connect(pickCli,&QPushButton::clicked,this,[this]{auto p=QFileDialog::getOpenFileName(this,"选择 medvessel_cli",QCoreApplication::applicationDirPath());if(!p.isEmpty())edits_["executable"]->setText(p);});
    connect(run_,&QPushButton::clicked,this,[this]{QString e;if(!startAnalysis(&e))notify(e);});connect(stop_,&QPushButton::clicked,&job_,&AnalysisJob::cancel);
    connect(open,&QPushButton::clicked,this,[this]{auto p=QFileDialog::getExistingDirectory(this,"选择包含 run.json 的结果目录",sourceRoot()+"/build");if(!p.isEmpty()){QString e;if(!openResult(p,&e))notify(e);}});
    connect(fit,&QPushButton::clicked,this,&MainWindow::fitPreview);connect(zoomIn,&QPushButton::clicked,this,[this]{view_->scale(1.25,1.25);});connect(zoomOut,&QPushButton::clicked,this,[this]{view_->scale(.8,.8);});
    connect(folder_,&QPushButton::clicked,this,[this]{QDesktopServices::openUrl(QUrl::fromLocalFile(result_));});
    connect(export_,&QPushButton::clicked,this,[this]{auto p=QFileDialog::getSaveFileName(this,"导出分支表",QDir(result_).filePath("branches-export.csv"),"CSV (*.csv)");if(p.isEmpty())return;QFile f(QDir(result_).filePath("branches.csv"));QSaveFile out(p);if(!f.open(QIODevice::ReadOnly)||!out.open(QIODevice::WriteOnly)){notify("无法读取或保存 CSV");return;}auto b=f.readAll();if(out.write(b)!=b.size()||!out.commit())notify("保存 CSV 失败");});
    connect(save,&QPushButton::clicked,this,[this]{auto p=QFileDialog::getSaveFileName(this,"保存配置",{},"JSON (*.json)");if(p.isEmpty())return;QString e;if(!writeJson(p,config().toJson(),&e))notify(e);else appendLog("配置已保存："+p);});
    connect(loadConfig_,&QPushButton::clicked,this,[this]{auto p=QFileDialog::getOpenFileName(this,"载入 GUI 配置",{},"JSON (*.json)");if(p.isEmpty())return;try{setConfig(JobConfig::fromJson(readJson(p)));appendLog("已载入配置："+p);}catch(const std::exception& e){notify(QString::fromUtf8(e.what()));}});
    connect(reset,&QPushButton::clicked,this,[this]{auto c=config();JobConfig d;d.input=c.input;d.outputRoot=c.outputRoot;d.executable=c.executable;setConfig(d);});
    connect(demo_,&QPushButton::clicked,this,&MainWindow::loadDemo);
    connect(help,&QPushButton::clicked,this,[this]{auto p=sourceRoot()+"/docs/medvessel_learning_guide.html";if(!QFileInfo::exists(p))notify("未找到学习手册，请查看项目 docs 目录");else QDesktopServices::openUrl(QUrl::fromLocalFile(p));});
    timer_.setInterval(250);connect(&timer_,&QTimer::timeout,this,[this]{elapsed_->setText(QString("已耗时 %1 秒").arg(clock_.elapsed()/1000.0,0,'f',1));});
    connect(&job_,&AnalysisJob::runningChanged,this,[this,open](bool running){parameters_->setEnabled(!running);run_->setEnabled(!running);stop_->setEnabled(running);demo_->setEnabled(!running);loadConfig_->setEnabled(!running);open->setEnabled(!running);progress_->setRange(0,running?0:1);if(running){clock_.start();timer_.start();}else timer_.stop();});
    connect(&job_,&AnalysisJob::stageChanged,stage_,&QLabel::setText);connect(&job_,&AnalysisJob::logMessage,this,&MainWindow::appendLog);
    connect(&job_,&AnalysisJob::succeeded,this,[this](const QString& dir){QString e;if(openResult(dir,&e)){stage_->setText("分析完成");progress_->setValue(1);}else notify(e);});
    connect(&job_,&AnalysisJob::failed,this,[this](const QString& e){stage_->setText("分析失败："+e);appendLog(e);progress_->setValue(0);});
    connect(&job_,&AnalysisJob::canceled,this,[this]{stage_->setText("已中止 · 本次目录保留日志，不作为完成结果");progress_->setValue(0);});
}
JobConfig MainWindow::config() const {
    JobConfig c;c.input=edits_["input"]->text().trimmed();c.outputRoot=edits_["output"]->text().trimmed();c.executable=edits_["executable"]->text().trimmed();
    c.scales=edits_["scales"]->text();c.roi=edits_["roi"]->text();c.seeds=edits_["seeds"]->text();c.minIntensity=edits_["minIntensity"]->text();c.maxIntensity=edits_["maxIntensity"]->text();
    c.alpha=doubles_["alpha"]->value();c.beta=doubles_["beta"]->value();c.c=doubles_["c"]->value();c.low=doubles_["low"]->value();c.high=doubles_["high"]->value();c.nmsStep=doubles_["nmsStep"]->value();c.prune=doubles_["prune"]->value();
    c.threads=ints_["threads"]->value();c.minComponent=ints_["minComponent"]->value();c.dark=checks_["dark"]->isChecked();c.nms=checks_["nms"]->isChecked();c.seedsInSource=checks_["sourceSeeds"]->isChecked();c.connectivity=connectivity_->currentText().toInt();return c;
}
void MainWindow::setConfig(const JobConfig& c) {
    edits_["input"]->setText(c.input);edits_["output"]->setText(c.outputRoot);edits_["executable"]->setText(c.executable);edits_["scales"]->setText(c.scales);edits_["roi"]->setText(c.roi);edits_["seeds"]->setText(c.seeds);edits_["minIntensity"]->setText(c.minIntensity);edits_["maxIntensity"]->setText(c.maxIntensity);
    doubles_["alpha"]->setValue(c.alpha);doubles_["beta"]->setValue(c.beta);doubles_["c"]->setValue(c.c);doubles_["low"]->setValue(c.low);doubles_["high"]->setValue(c.high);doubles_["nmsStep"]->setValue(c.nmsStep);doubles_["prune"]->setValue(c.prune);
    ints_["threads"]->setValue(c.threads);ints_["minComponent"]->setValue(c.minComponent);checks_["dark"]->setChecked(c.dark);checks_["nms"]->setChecked(c.nms);checks_["sourceSeeds"]->setChecked(c.seedsInSource);connectivity_->setCurrentText(QString::number(c.connectivity));
}
bool MainWindow::startAnalysis(QString* error) {
    auto c=config();
    if(c.input.isEmpty()||c.executable.isEmpty()||c.outputRoot.isEmpty()){if(error)*error="请填写输入影像、算法程序与结果根目录";return false;}
    c.input=QFileInfo(c.input).absoluteFilePath();c.executable=QFileInfo(c.executable).absoluteFilePath();c.outputRoot=QDir(c.outputRoot).absolutePath();
    // Validate before clearing the last successfully opened result.
    if(!job_.start(c,error))return false;
    result_.clear();model_->clear();summary_->setText("正在分析…\n\n结果保存到：\n"+job_.outputDirectory());resultPath_->clear();folder_->setEnabled(false);export_->setEnabled(false);scene_->clear();delete renderer_;renderer_=nullptr;logs_->clear();appendLog("新任务："+job_.outputDirectory());previewHint_->setText("算法正在后台运行，完成后自动载入结果。");return true;
}
bool MainWindow::openResult(const QString& directory,QString* error) {
    if(job_.running()){if(error)*error="请先等待或停止当前任务";return false;}
    try {
        QDir dir(directory);QString status="CLI 结果（无 GUI 任务状态记录）";
        if(QFileInfo::exists(dir.filePath("gui_run.json"))) {
            auto record=readJson(dir.filePath("gui_run.json"));auto state=record["status"].toString();
            if(state!="completed")throw std::runtime_error(("该任务未完成，状态："+state+"。请查看 process.log。").toStdString());
            status="GUI 任务已完成";
        }
        auto run=readJson(dir.filePath("run.json"));
        if(run["schema_version"].toInt()!=1||run["dimensions"].toArray().size()!=3||!run["branches"].isDouble())throw std::runtime_error("run.json 缺少必要字段");
        QFile csv(dir.filePath("branches.csv"));if(!csv.open(QIODevice::ReadOnly))throw std::runtime_error("无法读取 branches.csv");
        auto lines=QString::fromUtf8(csv.readAll()).split('\n',Qt::SkipEmptyParts);
        if(lines.isEmpty()||lines.front().trimmed()!="branch_id,start_node_id,end_node_id,length_mm,mean_edt_diameter_mm")throw std::runtime_error("分支表格式不支持");
        QList<QStringList> rows;for(int i=1;i<lines.size();++i){auto cells=lines[i].trimmed().split(',');if(cells.size()!=5)throw std::runtime_error("分支表行格式错误");rows.append(cells);}
        if(rows.size()!=run["branches"].toInt())throw std::runtime_error("分支表数量与运行记录不一致");
        auto nextRenderer=std::make_unique<QSvgRenderer>(dir.filePath("preview.svg"));if(!nextRenderer->isValid())throw std::runtime_error("preview.svg 缺失或无效");
        QString details=status+"\n\n输入："+run["input_path"].toString()+"\n\n网格："+arrayText(run["dimensions"])+"\n间距 mm："+arrayText(run["spacing_mm"])+
            QString("\n\n分支：%1    节点：%2\n处理耗时：%3 秒").arg(run["branches"].toInt()).arg(run["nodes"].toInt()).arg(run["elapsed_seconds"].toDouble(),0,'f',2)+
            "\n\n长度是中心线路径的物理长度；EDT 直径是离散估计，空值表示未定义。";
        if(QFileInfo::exists(dir.filePath("metrics.json")))details+="\n\n该目录包含评估记录，请结合 experiment.json 和参考标签来源解释。GUI 不会自动计算评估指标。";
        scene_->clear();delete renderer_;renderer_=nextRenderer.release();renderer_->setParent(this);auto* item=new QGraphicsSvgItem;item->setSharedRenderer(renderer_);scene_->addItem(item);scene_->setSceneRect(item->boundingRect());fitPreview();
        table_->setSortingEnabled(false);model_->clear();model_->setHorizontalHeaderLabels({"分支 ID","起点","终点","长度 mm","平均 EDT 直径 mm"});
        for(const auto& cells:rows){QList<QStandardItem*> items;for(const auto& cell:cells){auto* itemCell=new QStandardItem;bool ok=false;double v=cell.toDouble(&ok);if(ok)itemCell->setData(v,Qt::DisplayRole);else itemCell->setText(cell.isEmpty()?"—":cell);items<<itemCell;}model_->appendRow(items);}
        table_->setSortingEnabled(true);table_->sortByColumn(0,Qt::AscendingOrder);table_->resizeColumnsToContents();
        result_=dir.absolutePath();summary_->setText(details);resultPath_->setText("结果目录：\n"+result_);folder_->setEnabled(true);export_->setEnabled(true);previewHint_->setText("左至右：原图强度、Frangi 响应、分割 Mask、中心线分支。拖动平移，使用 ＋ / － 缩放。");stage_->setText("已载入结果");appendLog("已打开："+result_);return true;
    }catch(const std::exception& e){if(error)*error=QString::fromUtf8(e.what());return false;}
}
void MainWindow::fitPreview(){if(!scene_->items().isEmpty())view_->fitInView(scene_->sceneRect(),Qt::KeepAspectRatio);}
void MainWindow::notify(const QString& message){QMessageBox::warning(this,"MEDVessel",message);}
void MainWindow::appendLog(const QString& message){logs_->appendPlainText(message);}
void MainWindow::loadDemo() {
    auto file=sourceRoot()+"/samples/phantom.nii.gz";
    if(!QFileInfo::exists(file))file=sourceRoot()+"/build/cmake/integration/input/phantom.nii.gz";
    if(!QFileInfo::exists(file)){notify("未找到合成示例。请先运行 medvessel_phantom build/cmake/integration/input，或自行选择输入影像。");return;}
    auto current=config();JobConfig d;d.input=file;d.executable=current.executable;d.outputRoot=current.outputRoot;setConfig(d);stage_->setText("已载入合成示例 · 点击开始分析");
}
void MainWindow::closeEvent(QCloseEvent* event) {
    if(job_.running()){notify("请先停止当前分析，等待任务结束后再关闭窗口。");event->ignore();return;}
    QSettings settings;settings.setValue("geometry",saveGeometry());event->accept();
}
}

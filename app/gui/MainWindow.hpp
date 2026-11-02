#pragma once
#include "AnalysisJob.hpp"
#include <QMainWindow>
#include <QElapsedTimer>
#include <QMap>

class QLineEdit;class QDoubleSpinBox;class QSpinBox;class QCheckBox;
class QComboBox;class QLabel;class QPushButton;class QPlainTextEdit;
class QTableView;class QStandardItemModel;class QProgressBar;class QGraphicsView;
class QGraphicsScene;class QSvgRenderer;
namespace medvessel::desktop {
class MainWindow:public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent=nullptr);
    JobConfig config() const;
    void setConfig(const JobConfig& config);
    bool startAnalysis(QString* error=nullptr);
    bool openResult(const QString& directory,QString* error=nullptr);
    AnalysisJob* job(){return &job_;}
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    AnalysisJob job_;
    QMap<QString,QLineEdit*> edits_;
    QMap<QString,QDoubleSpinBox*> doubles_;
    QMap<QString,QSpinBox*> ints_;
    QMap<QString,QCheckBox*> checks_;
    QComboBox* connectivity_;
    QWidget* parameters_;
    QLabel *stage_,*elapsed_,*summary_,*resultPath_,*previewHint_;
    QPushButton *run_,*stop_,*folder_,*export_,*demo_,*loadConfig_;
    QPlainTextEdit* logs_;
    QProgressBar* progress_;
    QTableView* table_;
    QStandardItemModel* model_;
    QGraphicsView* view_;
    QGraphicsScene* scene_;
    QSvgRenderer* renderer_=nullptr;
    QString result_;
    QElapsedTimer clock_;
    QTimer timer_;
    void fitPreview();
    void notify(const QString& message);
    void appendLog(const QString& message);
    void loadDemo();
};
}

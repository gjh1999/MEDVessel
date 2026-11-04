#pragma once
#include <QWidget>
#include "core/Volume3D.hpp"
#include <optional>
#include <QFutureWatcher>
#include <QMap>
#include <vtkSmartPointer.h>
#include <vtkPolyData.h>
#include <memory>
class QLabel;class QVTKOpenGLNativeWidget;class QPushButton;
class vtkRenderer;class vtkGenericOpenGLRenderWindow;class vtkActor;
class vtkOrientationMarkerWidget;
namespace medvessel::desktop {
struct ThreeDScene {
    vtkSmartPointer<vtkPolyData> surface;
    QMap<int,vtkSmartPointer<vtkPolyData>> branches;
    QString error;
};
class ThreeDViewer:public QWidget {
    Q_OBJECT
public:
    explicit ThreeDViewer(QWidget* parent=nullptr);
    ~ThreeDViewer() override;
    static ThreeDScene prepare(const QString& directory);
    void setResult(const QString& directory);
    void load();
    void selectBranch(int id);
    bool focusWorld(const Point3D& world);
    std::optional<Point3D> focusedWorld() const {return focusWorld_;}
    int selectedBranch() const{return selected_;}
    int branchCount() const{return actors_.size();}
signals:
    void loaded(bool success);
    void branchPicked(int id);
    void branchLocationChanged(double x,double y,double z);
    void locationCleared();
    void locateSlicesRequested();
protected:
    bool eventFilter(QObject* object,QEvent* event) override;
    void showEvent(QShowEvent* event) override;
private:
    QString directory_;
    quint64 generation_=0;
    QFutureWatcher<ThreeDScene> watcher_;
    QVTKOpenGLNativeWidget* view_=nullptr;
    QLabel* status_;
    QPushButton* locate_;
    vtkSmartPointer<vtkRenderer> renderer_;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> window_;
    vtkSmartPointer<vtkActor> surface_;
    vtkSmartPointer<vtkOrientationMarkerWidget> orientation_;
    QMap<int,vtkSmartPointer<vtkActor>> actors_;
    int selected_=-1;
    std::optional<Point3D> focusWorld_;
    vtkSmartPointer<vtkActor> focusMarker_;
    QPoint press_;
    void render();
};
}

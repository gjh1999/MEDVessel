#pragma once
#include "core/Volume3D.hpp"
#include <QWidget>
#include <QMap>
#include <vtkSmartPointer.h>
#include <vtkPolyData.h>

class vtkActor;
class vtkRenderer;
class vtkOrientationMarkerWidget;
class QVTKOpenGLNativeWidget;
class QLabel;
class QPushButton;
namespace medvessel::desktop {
class ThreeDViewer : public QWidget {
    Q_OBJECT
public:
    struct Scene {
        vtkSmartPointer<vtkPolyData> surface;
        QMap<int,vtkSmartPointer<vtkPolyData>> branches;
        ImageGeometry geometry;
        std::array<std::size_t,3> dimensions{};
        QString error;
    };
    explicit ThreeDViewer(QWidget* parent=nullptr);
    static Scene prepare(const QString& directory);
    void setResult(const QString& directory);
    void load();
    void selectBranch(int id);
    bool focusWorld(const Point3D& world,QString* error=nullptr);
    int branchCount()const{return scene_.branches.size();}
    int selectedBranch()const{return selected_;}
signals:
    void loaded(bool success);
    void branchPicked(int id);
    void branchLocationChanged(double x,double y,double z);
    void locationCleared();
    void locateSlicesRequested();
protected:
    bool eventFilter(QObject* object,QEvent* event) override;
private:
    QString directory_;
    Scene scene_;
    quint64 generation_=0;
    int selected_=-1;
    QVTKOpenGLNativeWidget* gl_=nullptr;
    vtkSmartPointer<vtkRenderer> renderer_;
    vtkSmartPointer<vtkActor> surfaceActor_,targetActor_;
    vtkSmartPointer<vtkOrientationMarkerWidget> orientation_;
    QMap<int,vtkSmartPointer<vtkActor>> actors_;
    QLabel* status_;
    QPushButton* locate_;
    QPoint press_;
    void render();
    void moveFocus(const Point3D& point);
};
}

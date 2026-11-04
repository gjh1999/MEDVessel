#pragma once
#include "core/Volume3D.hpp"
#include "core/AnatomicalGrid.hpp"
#include <QWidget>
#include <QImage>
#include <memory>
#include <optional>
#include <tuple>

class QGraphicsView;
class QGraphicsScene;
class QSlider;
class QDoubleSpinBox;
class QCheckBox;
class QComboBox;
class QLabel;
namespace medvessel::desktop {
class SliceViewer : public QWidget {
    Q_OBJECT
public:
    using Volume = Volume3D<float>;
    explicit SliceViewer(QWidget* parent=nullptr);
    void setVolume(std::shared_ptr<Volume> volume,const QString& path);
    bool setMask(std::shared_ptr<Volume> mask,const QString& path,QString* error=nullptr);
    QImage sliceImage(int plane,int index) const;
    void load(const QString& path);
    void loadMask(const QString& path);
    void loadResult(const QString& directory);
    void clear();
    void setReadOnly();
    void setPickingEnabled(bool enabled);
    void setSelection(const QString& roi,const QString& seeds);
    bool focusWorld(const Point3D& world,QString* error=nullptr);
    void clearFocus();
signals:
    void loaded(bool success);
    void maskLoaded(bool success);
    void seedPicked(const QString& point);
    void roiPicked(const QString& box);
    void clearSeedsRequested();
    void clearRoiRequested();
    void worldPicked(double x,double y,double z);
protected:
    bool eventFilter(QObject* object,QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
private:
    std::shared_ptr<Volume> volume_,mask_;
    QGraphicsView* views_[3]{};
    QGraphicsScene* scenes_[3]{};
    QSlider* sliders_[3]{};
    QLabel* labels_[3]{};
    QDoubleSpinBox *width_,*level_;
    QSlider* opacity_;
    QCheckBox* showMask_;
    QComboBox *mode_,*orientation_;
    DisplayGrid display_;
    using ImageKey=std::tuple<quint64,quint64,int,int,double,double,int,bool>;
    mutable std::array<std::optional<ImageKey>,3> imageKeys_;
    mutable std::array<QImage,3> images_;
    QLabel* status_;
    bool picking_=true,readOnly_=false;
    quint64 generation_=0,maskGeneration_=0;
    QString roi_,seeds_;
    std::optional<Point3D> focus_;
    std::optional<std::array<int,3>> dragStart_;
    int dragPlane_=-1;
    void refresh();
    void fullRange();
    std::optional<std::array<int,3>> voxelAt(int plane,const QPoint& point,bool clamp) const;
    void request(const QString& path,bool result);
    void updateDisplay();
    std::optional<std::array<int,3>> sourceVoxel(const std::array<int,3>& displayIndex) const;
};
}

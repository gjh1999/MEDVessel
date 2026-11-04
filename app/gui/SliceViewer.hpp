#pragma once
#include "core/Volume3D.hpp"
#include <QWidget>
#include <QFutureWatcher>
#include <QImage>
#include <memory>
#include <optional>
class QSlider; class QLabel; class QGraphicsView; class QGraphicsScene;
class QDoubleSpinBox; class QCheckBox; class QPushButton;
class QComboBox; class QRubberBand;
namespace medvessel::desktop {
class SliceViewer : public QWidget {
    Q_OBJECT
public:
    explicit SliceViewer(QWidget* parent=nullptr);
    void load(const QString& path);
    void setVolume(std::shared_ptr<const Volume3D<float>> volume, const QString& name);
    QImage sliceImage(int plane, int index) const;
    bool setMask(std::shared_ptr<const Volume3D<float>> mask,const QString& name,QString* error=nullptr);
    void loadMask(const QString& path);
    void setSelection(const QString& roi,const QString& seeds);
    void setPickingEnabled(bool enabled);
    void setReadOnly();
    void clear();
    void loadResult(const QString& directory);
    bool focusWorld(const Point3D& world,QString* error=nullptr);
    void clearFocus();
signals:
    void worldPointPicked(double x,double y,double z);
    void locateThreeDRequested();
    void loaded(bool success);
    void maskLoaded(bool success);
    void seedPicked(const QString& sourceIndex);
    void roiPicked(const QString& sourceBox);
    void clearSeedsRequested();
    void clearRoiRequested();
protected:
    bool eventFilter(QObject* object,QEvent* event) override;
private:
    struct LoadResult {std::shared_ptr<const Volume3D<float>> volume; QString error; std::shared_ptr<const Volume3D<float>> mask;QString name;};
    QFutureWatcher<LoadResult> watcher_;
    std::shared_ptr<const Volume3D<float>> volume_;
    std::shared_ptr<const Volume3D<float>> mask_;
    QFutureWatcher<LoadResult> maskWatcher_;
    QDoubleSpinBox *width_,*level_;
    QCheckBox* overlay_;
    QSlider* opacity_;
    QPushButton* maskButton_;
    QLabel* maskStatus_;
    QWidget* pickControls_;
    QLabel* focusStatus_;
    std::optional<Point3D> focusWorld_;
    std::optional<std::array<std::size_t,3>> focusVoxel_;
    quint64 generation_=0;
    QString pendingResult_;
    QComboBox* pickMode_;
    QLabel* selectionHint_;
    QRubberBand* rubber_=nullptr;
    int dragPlane_=-1,dragSlice_=0;
    QPoint dragStart_;
    std::array<std::size_t,3> dragVoxel_{};
    std::optional<std::array<std::size_t,6>> roi_;
    std::vector<std::array<std::size_t,3>> seeds_;
    QString roiText_,seedsText_;
    bool pickingEnabled_=true;
    bool resultPicking_=false;
    QPushButton* locateThreeD_;
    QSlider* sliders_[3];
    QLabel* labels_[3];
    QGraphicsView* views_[3]{};
    QGraphicsScene* scenes_[3]{};
    QLabel* status_;
    double minimum_=0, maximum_=1;
    void render(int plane);
    void refresh();
    bool voxelAt(int plane,const QPoint& viewport,std::array<std::size_t,3>& voxel,bool clamp=false) const;
};
}

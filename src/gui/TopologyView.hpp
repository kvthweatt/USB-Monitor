// src/gui/TopologyView.hpp
#pragma once
#include <QGraphicsView>
#include <memory>

namespace usb_monitor {

class DeviceManager;
class UsbDevice;

class TopologyView : public QGraphicsView {
    Q_OBJECT

public:
    explicit TopologyView(QWidget* parent = nullptr);
    ~TopologyView();

    void setDeviceManager(DeviceManager* manager);

public slots:
    void refresh();
    void zoomIn();
    void zoomOut();
    void resetZoom();

signals:
    void deviceSelected(std::shared_ptr<UsbDevice> device);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private slots:
    void handleDeviceAdded(std::shared_ptr<UsbDevice> device);
    void handleDeviceRemoved(std::shared_ptr<UsbDevice> device);
    void updateLayout();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace usb_monitor

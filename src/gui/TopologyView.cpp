// src/gui/TopologyView.cpp
#include "TopologyView.hpp"
#include "DeviceManager.hpp"
#include "UsbDevice.hpp"
#include <QGraphicsScene>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsTextItem>
#include <QWheelEvent>
#include <QTimer>
#include <map>
#include <cmath>

namespace usb_monitor {

struct DeviceNode {
    QGraphicsEllipseItem* circle{nullptr};
    QGraphicsTextItem* label{nullptr};
    std::vector<QGraphicsLineItem*> connections;
    const UsbDevice* device{nullptr};
    double x{0}, y{0};
    double dx{0}, dy{0}; // For force-directed layout
};

class TopologyView::Private {
public:
    DeviceManager* manager{nullptr};
    QGraphicsScene* scene{nullptr};
    std::map<const UsbDevice*, DeviceNode> nodes;
    QPointF lastMousePos;
    bool isDragging{false};
    double zoomLevel{1.0};
    QTimer* layoutTimer{nullptr};
    
    void createDeviceNode(const std::shared_ptr<UsbDevice>& device) {
        if (!device) return;
        
        DeviceNode node;
        node.device = device.get();
        
        // Create visual elements
        node.circle = new QGraphicsEllipseItem(-20, -20, 40, 40);
        node.circle->setBrush(QBrush(Qt::white));
        node.circle->setPen(QPen(Qt::black));
        node.circle->setZValue(1);
        
        node.label = new QGraphicsTextItem(QString::fromStdString(device->description()));
        node.label->setDefaultTextColor(Qt::black);
        node.label->setZValue(2);
        
        scene->addItem(node.circle);
        scene->addItem(node.label);
        
        // Initialize position randomly within view bounds
        node.x = rand() % 400 - 200;
        node.y = rand() % 400 - 200;
        
        nodes[device.get()] = node;
        updateLayout();
    }
    
    void removeDeviceNode(const UsbDevice* device) {
        auto it = nodes.find(device);
        if (it != nodes.end()) {
            // Remove connections
            for (auto* line : it->second.connections) {
                scene->removeItem(line);
                delete line;
            }
            
            // Remove visual elements
            scene->removeItem(it->second.circle);
            scene->removeItem(it->second.label);
            delete it->second.circle;
            delete it->second.label;
            
            nodes.erase(it);
            updateLayout();
        }
    }
    
    void updateLayout() {
        if (nodes.empty()) return;
        
        // Implement force-directed layout
        const double k = 100.0;  // Spring constant
        const double c = 200.0;  // Repulsion constant
        const double damping = 0.9;
        
        // Calculate forces
        for (auto& [dev1, node1] : nodes) {
            node1.dx = 0;
            node1.dy = 0;
            
            // Repulsion between all nodes
            for (const auto& [dev2, node2] : nodes) {
                if (dev1 == dev2) continue;
                
                double dx = node1.x - node2.x;
                double dy = node1.y - node2.y;
                double dist = std::sqrt(dx * dx + dy * dy);
                if (dist < 1) dist = 1;
                
                // Coulomb-like repulsion
                double force = c / (dist * dist);
                node1.dx += (dx / dist) * force;
                node1.dy += (dy / dist) * force;
            }
            
            // Spring force for connected nodes
            for (const auto* conn : node1.connections) {
                // Find the connected node
                for (const auto& [dev2, node2] : nodes) {
                    bool isConnected = false;
                    for (const auto* conn2 : node2.connections) {
                        if (conn == conn2) {
                            isConnected = true;
                            break;
                        }
                    }
                    
                    if (isConnected) {
                        double dx = node1.x - node2.x;
                        double dy = node1.y - node2.y;
                        double dist = std::sqrt(dx * dx + dy * dy);
                        
                        // Hooke's law
                        node1.dx -= (dx / dist) * (dist / k);
                        node1.dy -= (dy / dist) * (dist / k);
                    }
                }
            }
        }
        
        // Update positions
        for (auto& [dev, node] : nodes) {
            // Apply damping and update position
            node.x += node.dx * damping;
            node.y += node.dy * damping;
            
            // Update visual elements
            node.circle->setPos(node.x, node.y);
            node.label->setPos(node.x + 25, node.y - 10);
            
            // Update connection lines
            for (auto* line : node.connections) {
                // Find the connected node
                for (const auto& [dev2, node2] : nodes) {
                    bool isConnected = false;
                    for (const auto* conn2 : node2.connections) {
                        if (line == conn2) {
                            line->setLine(node.x, node.y, node2.x, node2.y);
                            break;
                        }
                    }
                }
            }
        }
    }
};

TopologyView::TopologyView(QWidget* parent)
    : QGraphicsView(parent)
    , d(std::make_unique<Private>()) {
    
    setRenderHint(QPainter::Antialiasing);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setDragMode(QGraphicsView::ScrollHandDrag);
    
    // Create scene
    d->scene = new QGraphicsScene(this);
    d->scene->setBackgroundBrush(Qt::white);
    setScene(d->scene);
    
    // Create layout update timer
    d->layoutTimer = new QTimer(this);
    connect(d->layoutTimer, &QTimer::timeout, this, &TopologyView::updateLayout);
    d->layoutTimer->start(50); // Update every 50ms
}

TopologyView::~TopologyView() = default;

void TopologyView::setDeviceManager(DeviceManager* manager) {
    if (d->manager) {
        disconnect(d->manager, nullptr, this, nullptr);
    }
    
    d->manager = manager;
    
    if (manager) {
        connect(manager, &DeviceManager::deviceAdded,
                this, &TopologyView::handleDeviceAdded);
        connect(manager, &DeviceManager::deviceRemoved,
                this, &TopologyView::handleDeviceRemoved);
        
        // Load initial devices
        refresh();
    }
}

void TopologyView::refresh() {
    if (!d->manager) return;
    
    // Clear existing nodes
    for (const auto& [device, node] : d->nodes) {
        d->removeDeviceNode(device);
    }
    d->nodes.clear();
    
    // Create nodes for all devices
    for (const auto& device : d->manager->getConnectedDevices()) {
        d->createDeviceNode(device);
    }
    
    // Center the view
    d->scene->setSceneRect(d->scene->itemsBoundingRect());
    fitInView(d->scene->sceneRect(), Qt::KeepAspectRatio);
}

void TopologyView::handleDeviceAdded(std::shared_ptr<UsbDevice> device) {
    d->createDeviceNode(device);
}

void TopologyView::handleDeviceRemoved(std::shared_ptr<UsbDevice> device) {
    if (device) {
        d->removeDeviceNode(device.get());
    }
}

void TopologyView::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        // Zoom
        const ViewportAnchor anchor = transformationAnchor();
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        
        double scaleFactor = 1.15;
        if (event->angleDelta().y() < 0) {
            scaleFactor = 1.0 / scaleFactor;
        }
        
        scale(scaleFactor, scaleFactor);
        d->zoomLevel *= scaleFactor;
        
        setTransformationAnchor(anchor);
    } else {
        QGraphicsView::wheelEvent(event);
    }
}

void TopologyView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        d->lastMousePos = event->pos();
        d->isDragging = true;
    }
    QGraphicsView::mousePressEvent(event);
}

void TopologyView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        d->isDragging = false;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void TopologyView::mouseMoveEvent(QMouseEvent* event) {
    if (d->isDragging) {
        QPointF delta = event->pos() - d->lastMousePos;
        d->lastMousePos = event->pos();
        
        // Pan the view
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
    }
    QGraphicsView::mouseMoveEvent(event);
}

void TopologyView::zoomIn() {
    scale(1.2, 1.2);
    d->zoomLevel *= 1.2;
}

void TopologyView::zoomOut() {
    scale(1.0 / 1.2, 1.0 / 1.2);
    d->zoomLevel /= 1.2;
}

void TopologyView::resetZoom() {
    scale(1.0 / d->zoomLevel, 1.0 / d->zoomLevel);
    d->zoomLevel = 1.0;
    
    // Center the view
    fitInView(d->scene->sceneRect(), Qt::KeepAspectRatio);
}

void TopologyView::updateLayout() {
    d->updateLayout();
}

} // namespace usb_monitor

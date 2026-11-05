#pragma once

#include "disktool/core/treemap.h"
#include "disktool/core/tree_builder.h"

#include <QSize>
#include <QWidget>

#include <cstddef>
#include <vector>

/// Paints the direct subdirectories of one DirNode as a squarified treemap.
/// The widget does not own the node; the model or tab keeps it alive.
class TreemapWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit TreemapWidget(QWidget* parent = nullptr);

    void setCurrent(const disktool::core::DirNode* node);
    [[nodiscard]] const disktool::core::DirNode* current() const { return m_current; }

    /// Recomputes the treemap layout from the current node. Used while a scan
    /// is appending directories/sizes incrementally.
    void refresh();

    [[nodiscard]] QSize minimumSizeHint() const override;

signals:
    /// Index of the activated child in current()->children.
    void itemActivated(std::size_t index);
    /// Index of the hovered child in current()->children, or nothing on leave.
    void itemHovered(std::size_t index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool event(QEvent* event) override;

private:
    struct VisibleItem {
        disktool::core::TreemapItem layout;
        const disktool::core::DirNode* node = nullptr;
        std::size_t childIndex = 0;
    };

    void ensureLayout();
    void rebuildLayout();
    void drawMessage(QPainter& painter, const QString& text);
    [[nodiscard]] int itemAt(const QPointF& position) const;
    [[nodiscard]] QString tooltipFor(
        const disktool::core::DirNode& node) const;

    const disktool::core::DirNode* m_current = nullptr;
    const disktool::core::DirNode* m_layoutCurrent = nullptr;
    QSize m_layoutSize;
    std::vector<VisibleItem> m_visible;
    int m_lastHover = -1;
};

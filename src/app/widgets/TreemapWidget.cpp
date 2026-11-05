#include "widgets/TreemapWidget.h"

#include "FormatUtils.h"

#include <QColor>
#include <QEvent>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QString>
#include <QToolTip>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

QString pathToQString(const fs::path& path)
{
    const auto text = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(text.data()),
                             static_cast<qsizetype>(text.size()));
}

QString nameToQString(const std::string& name)
{
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

std::uint32_t hashName(const std::string& name)
{
    // FNV-1a keeps colours stable between runs and platforms.
    std::uint32_t hash = 2166136261u;
    for (const unsigned char byte : name) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

constexpr double kMinimumDrawnSize = 3.0;

}  // namespace

TreemapWidget::TreemapWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(100, 100);
    setMouseTracking(true);
    setAutoFillBackground(false);
}

void TreemapWidget::setCurrent(const disktool::core::DirNode* node)
{
    if (m_current == node) {
        return;
    }

    m_current = node;
    m_layoutCurrent = nullptr;
    m_lastHover = -1;
    unsetCursor();
    update();
}

QSize TreemapWidget::minimumSizeHint() const
{
    return QSize(100, 100);
}

void TreemapWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), QColor(245, 245, 245));

    ensureLayout();

    if (m_current == nullptr) {
        drawMessage(painter, tr("Выберите папку для просмотра"));
        return;
    }

    if (m_visible.empty()) {
        drawMessage(painter, tr("Нет данных для отображения"));
        return;
    }

    const QFontMetrics metrics(painter.font());
    painter.setRenderHint(QPainter::Antialiasing, false);

    for (const auto& item : m_visible) {
        const auto& rect = item.layout.rect;

        if (rect.w < kMinimumDrawnSize || rect.h < kMinimumDrawnSize) {
            continue;  // too small to be useful
        }

        const QRectF area(rect.x, rect.y, rect.w, rect.h);
        const QColor fill = QColor::fromHsv(
            static_cast<int>(hashName(item.node->name) % 360u), 180, 220);

        painter.fillRect(area, fill);
        painter.setPen(QColor(55, 55, 55));
        painter.drawRect(area);

        if (area.width() >= 60.0 && area.height() >= 20.0) {
            painter.setPen(QColor(20, 20, 20));
            const int availableWidth =
                std::max(0, static_cast<int>(area.width()) - 6);
            const QString name = nameToQString(item.node->name);
            const QString elided =
                metrics.elidedText(name, Qt::ElideRight, availableWidth);
            painter.drawText(area.adjusted(3, 2, -3, -2),
                             Qt::AlignLeft | Qt::AlignTop, elided);
        }
    }
}

void TreemapWidget::mouseMoveEvent(QMouseEvent* event)
{
    ensureLayout();

    const int hit = itemAt(event->position());
    if (hit != m_lastHover) {
        m_lastHover = hit;
        if (hit >= 0) {
            setCursor(Qt::PointingHandCursor);
            emit itemHovered(m_visible[static_cast<std::size_t>(hit)].childIndex);
        } else {
            unsetCursor();
        }
    }

    QWidget::mouseMoveEvent(event);
}

void TreemapWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || m_current == nullptr) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    ensureLayout();
    const int hit = itemAt(event->position());
    if (hit >= 0) {
        emit itemActivated(m_visible[static_cast<std::size_t>(hit)].childIndex);
        event->accept();
        return;
    }

    QWidget::mouseDoubleClickEvent(event);
}

void TreemapWidget::leaveEvent(QEvent* event)
{
    m_lastHover = -1;
    unsetCursor();
    QWidget::leaveEvent(event);
}

bool TreemapWidget::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip && m_current != nullptr) {
        auto* help = static_cast<QHelpEvent*>(event);
        ensureLayout();

        const int hit = itemAt(help->pos());
        if (hit >= 0) {
            QToolTip::showText(
                help->globalPos(),
                tooltipFor(*m_visible[static_cast<std::size_t>(hit)].node), this);
        } else {
            QToolTip::hideText();
        }

        event->accept();
        return true;
    }

    return QWidget::event(event);
}

void TreemapWidget::ensureLayout()
{
    if (m_layoutCurrent == m_current && m_layoutSize == size()) {
        return;
    }
    rebuildLayout();
}

void TreemapWidget::rebuildLayout()
{
    m_visible.clear();
    m_layoutCurrent = m_current;
    m_layoutSize = size();

    if (m_current == nullptr || m_current->children.empty()) {
        return;
    }

    std::vector<std::uintmax_t> sizes;
    std::vector<const disktool::core::DirNode*> nodes;
    std::vector<std::size_t> childIndices;
    sizes.reserve(m_current->children.size());
    nodes.reserve(m_current->children.size());
    childIndices.reserve(m_current->children.size());

    for (std::size_t index = 0; index < m_current->children.size(); ++index) {
        const auto& child = m_current->children[index];
        if (child->cumulativeSize == 0) {
            continue;
        }
        sizes.push_back(child->cumulativeSize);
        nodes.push_back(child.get());
        childIndices.push_back(index);
    }

    if (sizes.empty()) {
        return;
    }

    const disktool::core::Rect bounds{
        0.0, 0.0, static_cast<double>(width()), static_cast<double>(height())};
    const auto layout = disktool::core::squarifiedTreemap(bounds, sizes);

    m_visible.reserve(layout.size());
    for (const auto& item : layout) {
        m_visible.push_back(VisibleItem{
            item, nodes[item.index], childIndices[item.index]});
    }
}

void TreemapWidget::drawMessage(QPainter& painter, const QString& text)
{
    painter.setPen(QColor(110, 110, 110));
    painter.drawText(rect(), Qt::AlignCenter, text);
}

int TreemapWidget::itemAt(const QPointF& position) const
{
    for (std::size_t index = 0; index < m_visible.size(); ++index) {
        const auto& rect = m_visible[index].layout.rect;
        if (rect.w < kMinimumDrawnSize || rect.h < kMinimumDrawnSize) {
            continue;
        }
        if (position.x() >= rect.x && position.x() <= rect.x + rect.w
            && position.y() >= rect.y && position.y() <= rect.y + rect.h) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

QString TreemapWidget::tooltipFor(const disktool::core::DirNode& node) const
{
    double percent = 0.0;
    if (m_current != nullptr && m_current->cumulativeSize > 0) {
        percent = 100.0 * static_cast<double>(node.cumulativeSize)
            / static_cast<double>(m_current->cumulativeSize);
    }

    return tr("%1\n%2 (%3 %)")
        .arg(pathToQString(node.path),
             disktool::app::formatBytes(node.cumulativeSize),
             QString::number(percent, 'f', 1));
}

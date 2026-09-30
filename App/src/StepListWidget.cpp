#include "StepListWidget.h"
#include "PipelineCommands.h"
#include <QPainter>
#include <QStyleOptionViewItem>

// Extra roles stored in each item
static constexpr int RoleStatus   = Qt::UserRole;
static constexpr int RoleProgress = Qt::UserRole + 1; // "current/total" string

// ── StepDelegate ─────────────────────────────────────────────────────────────

StepDelegate::StepDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void StepDelegate::paint(QPainter* painter,
                         const QStyleOptionViewItem& option,
                         const QModelIndex& index) const
{
    painter->save();

    // Background
    if (option.state & QStyle::State_Selected)
        painter->fillRect(option.rect, option.palette.highlight());
    else
        painter->fillRect(option.rect, option.palette.base());

    const int iconSize = 14;
    const int margin   = 6;
    QRect iconRect(option.rect.left() + margin,
                   option.rect.top() + (option.rect.height() - iconSize) / 2,
                   iconSize, iconSize);

    auto status = static_cast<StepStatus>(index.data(RoleStatus).toInt());
    QColor color;
    QString symbol;

    switch (status) {
    case StepStatus::NOT_RUN:
        color  = QColor(160, 160, 160);
        symbol = QStringLiteral("○");
        break;
    case StepStatus::RUNNING:
        color  = QColor(255, 193, 7);
        symbol = QStringLiteral("⟳");
        break;
    case StepStatus::SUCCESS:
        color  = QColor(76, 175, 80);
        symbol = QStringLiteral("✓");
        break;
    case StepStatus::FAILED:
        color  = QColor(244, 67, 54);
        symbol = QStringLiteral("✗");
        break;
    }

    painter->setPen(color);
    QFont f = painter->font();
    f.setPointSize(10);
    painter->setFont(f);
    painter->drawText(iconRect, Qt::AlignCenter, symbol);

    // Step label
    QRect textRect = option.rect.adjusted(margin + iconSize + 4, 0, 0, 0);
    QColor textColor = (option.state & QStyle::State_Selected)
                       ? option.palette.highlightedText().color()
                       : option.palette.text().color();
    painter->setPen(textColor);
    QFont tf = painter->font();
    tf.setPointSize(9);
    painter->setFont(tf);

    QString label = index.data(Qt::DisplayRole).toString();
    QString progress = index.data(RoleProgress).toString();
    if (!progress.isEmpty())
        label += QStringLiteral("  ") + progress;

    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, label);
    painter->restore();
}

// ── StepListWidget ────────────────────────────────────────────────────────────

StepListWidget::StepListWidget(QWidget* parent) : QListWidget(parent)
{
    setItemDelegate(new StepDelegate(this));
    setFixedWidth(200);
    setSpacing(2);

    for (int i = 0; i < STEP_COUNT; ++i) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1. %2").arg(i + 1).arg(stepName(static_cast<StepIndex>(i))));
        item->setData(RoleStatus,   static_cast<int>(StepStatus::NOT_RUN));
        item->setData(RoleProgress, QString{});
        addItem(item);
    }
}

void StepListWidget::setStepStatus(int index, StepStatus status)
{
    if (index < 0 || index >= count()) return;
    item(index)->setData(RoleStatus, static_cast<int>(status));
    viewport()->update();
}

void StepListWidget::setStepProgress(int index, int current, int total)
{
    if (index < 0 || index >= count()) return;
    QString prog = total > 0
        ? QStringLiteral("(%1/%2)").arg(current).arg(total)
        : QString{};
    item(index)->setData(RoleProgress, prog);
    viewport()->update();
}

void StepListWidget::resetAll()
{
    for (int i = 0; i < count(); ++i) {
        item(i)->setData(RoleStatus,   static_cast<int>(StepStatus::NOT_RUN));
        item(i)->setData(RoleProgress, QString{});
    }
    viewport()->update();
}

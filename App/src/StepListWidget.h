#pragma once
#include <QListWidget>
#include <QStyledItemDelegate>

enum class StepStatus { NOT_RUN, RUNNING, SUCCESS, FAILED };

// A QListWidget that displays five pipeline steps with colored status icons.
// Each item stores its StepStatus in Qt::UserRole.
class StepListWidget : public QListWidget
{
    Q_OBJECT
public:
    explicit StepListWidget(QWidget* parent = nullptr);

    void setStepStatus(int index, StepStatus status);
    void setStepProgress(int index, int current, int total);
    void resetAll();
};

// Delegate that paints a colored circle/check/cross in the decoration area.
class StepDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit StepDelegate(QObject* parent = nullptr);
    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
};

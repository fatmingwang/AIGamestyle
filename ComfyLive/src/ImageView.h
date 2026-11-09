#pragma once
#include <QImage>
#include <QWidget>

// The picture, scaled to fit with its aspect kept, over a dark background, with a one-line caption at the bottom
// ("preview 7/20", "done in 4.2 s").
class ImageView : public QWidget
{
    Q_OBJECT
public:
    explicit ImageView(QWidget* parent = nullptr);
    void setImage(const QImage& image);
    void setCaption(const QString& caption);
    QString caption() const { return caption_; }
    const QImage& image() const { return image_; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage image_;
    QString caption_;
};

/********************************************************************************
** Form generated from reading UI file 'designerOAYGQF.ui'
**
** Created by: Qt User Interface Compiler version 6.6.3
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef DESIGNEROAYGQF_H
#define DESIGNEROAYGQF_H

#include "GUIResources.h"
#include "core/Bone2.h"


#include <qgraphicsview.h>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QToolButton>
#include "gui/prop/bezierCurveEditor.h"
#include "tool/tool_FFDPanel.h"
#include "util/Easing.h"
#include <QtConcurrent>

#include <QMessageBox>
#include <QPainter>
#include <QPen>

QT_BEGIN_NAMESPACE

// Easing preview: the canvas's value axis flattened to a vertical strip. A
// dot rides up/down with Y = the easing value, using the editor's fixed
// value viewport ([kMinValue, kMaxValue] over the full height) at the same
// row height as the canvas, so its vertical position mirrors the curve's
// height at the current progress. The track is the full height — the
// viewport — and the value is not clamped: the canvas's handles can swing
// ±kUnderOverRange past the 0..1 band, and the dot follows (values stay
// inside the viewport: the curve's y is within the convex hull of
// {0, y1, y2, 1} ⊂ [−0.5, 1.5]).
class EasingDot : public QWidget {
public:
    explicit EasingDot(const BezierCurveEditor* aEditor, QWidget* aParent = nullptr)
        : QWidget(aParent), mEditor(aEditor) {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        setFixedWidth(16);
    }
    void setValue(double aValue) {
        mValue = aValue;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const theme::Colors& c = theme::Colors::current();
        const int cx = width() / 2;
        // the track: the full viewport height (the 0..1 band is its middle
        // half); +0.5 snaps the 1px pen onto one column. The strip sits on
        // the dialog's base surface, not the recessed canvas, so it uses
        // the plain hairline token (recessedHairline would be invisible
        // here — dark col 8 == base col 8).
        p.setPen(QPen(c.hairline, 1.0));
        p.drawLine(cx + 0.5, 0, cx + 0.5, height());
        // the dot at Y = value — the editor's viewport applied to this
        // strip, so the dot rides the same rows the canvas's anchors and
        // axis lines do.
        const qreal y = mEditor ? mEditor->valueToY(mValue, height()) : height() / 2.0;
        p.setPen(Qt::NoPen);
        p.setBrush(c.accentSwatch);
        p.drawEllipse(QPointF(cx, y), 5.0, 5.0);
    }

private:
    const BezierCurveEditor* mEditor;
    double mValue = 0.0;
};

class Ui_splineWidget {
public:
    QGridLayout *gridLayout_2;
    QDoubleSpinBox *x1_spin;
    QDoubleSpinBox *x2_spin;
    QDoubleSpinBox *y1_spin;
    BezierCurveEditor* m_editor;
    util::Easing::CubicBezier* bezier;
    QToolButton *toolButton_2;
    QToolButton *toolButton;
    QDoubleSpinBox *y2_spin;
    QPushButton *cancel;
    QPushButton *apply;
    EasingDot* mEasingDot;
    float progress;
    QVector<QDoubleSpinBox*> spins;


    static void delay() // https://stackoverflow.com/questions/3752742/how-do-i-create-a-pause-wait-function-using-qt#11487434
    {
        QTime dieTime= QTime::currentTime().addSecs(1);
        while (QTime::currentTime() < dieTime) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            // So we don't blow up the CPU but also the user doesn't notice too much
            QThread::sleep(std::chrono::milliseconds(10));
        }
    }

    void setupUi(QDialog *splineWidget, const gui::GUIResources* guiRes, util::Easing::CubicBezier* cubicBezier) {
        bezier = cubicBezier;
        if (splineWidget->objectName().isEmpty())
            splineWidget->setObjectName("splineWidget");
        splineWidget->resize(600, 600);
        splineWidget->setMinimumHeight(250);
        splineWidget->setMinimumWidth(250);
        gridLayout_2 = new QGridLayout(splineWidget);
        gridLayout_2->setObjectName("gridLayout_2");
        x1_spin = new QDoubleSpinBox(splineWidget);
        x1_spin->setObjectName("x1_spin");

        gridLayout_2->addWidget(x1_spin, 2, 1, 1, 1);

        x2_spin = new QDoubleSpinBox(splineWidget);
        x2_spin->setObjectName("x2_spin");

        gridLayout_2->addWidget(x2_spin, 2, 3, 1, 1);

        y1_spin = new QDoubleSpinBox(splineWidget);
        y1_spin->setObjectName("y1_spin");

        gridLayout_2->addWidget(y1_spin, 2, 2, 1, 1);

        y2_spin = new QDoubleSpinBox(splineWidget);
        y2_spin->setObjectName("y2_spin");

        gridLayout_2->addWidget(y2_spin, 2, 4, 1, 1);

        spins = {x1_spin, y1_spin, x2_spin, y2_spin};
        m_editor = new BezierCurveEditor(splineWidget, cubicBezier, spins, &progress);
        m_editor->adjustSize();
        for (auto spin : spins) {
            // dot separator, matching the property fields (see prop_Items)
            spin->setLocale(QLocale::c());
            spin->setSingleStep(0.01);
            spin->setMaximum(2);
            spin->setMinimum(-2);
            QDoubleSpinBox::connect(spin, &QDoubleSpinBox::valueChanged, [=](double) {
                m_editor->blockSignals(true);
                progress = 0;
                *cubicBezier = {
                    static_cast<float>(spins[0]->value()),
                    static_cast<float>(spins[1]->value()),
                    static_cast<float>(spins[2]->value()),
                    static_cast<float>(spins[3]->value())
                };
                m_editor->bezier = cubicBezier;
                const int width = m_editor->width();
                const int height = m_editor->height();
                m_editor->m_points[1] = QPointF(
                    m_editor->valueToX(cubicBezier->x1, width),
                    m_editor->valueToY(cubicBezier->y1, height));
                m_editor->m_points[2] = QPointF(
                    m_editor->valueToX(cubicBezier->x2, width),
                    m_editor->valueToY(cubicBezier->y2, height));
                m_editor->blockSignals(false);
                m_editor->repaint();
            });
        }

        m_editor->setObjectName("splineChart");
        QSizePolicy sizePolicy(QSizePolicy::Policy::Minimum, QSizePolicy::Policy::MinimumExpanding);
        m_editor->setSizePolicy(sizePolicy);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);

        // Copy
        toolButton = new QToolButton(splineWidget);
        toolButton->setObjectName("toolButton");
        //Paste
        toolButton_2 = new QToolButton(splineWidget);
        toolButton_2->setObjectName("toolButton_2");
        QToolButton::connect(toolButton, &QToolButton::clicked, [=]() {
            QString str;
            str.append(QString::number(bezier->x1)).append(",");
            str.append(QString::number(bezier->y1)).append(",");
            str.append(QString::number(bezier->x2)).append(",");
            str.append(QString::number(bezier->y2));
            QClipboard *clip = QGuiApplication::clipboard();
            clip->setText(str);
            toolButton->setText("Success ✓");
            delay();
            toolButton->setText("Copy");
        });

        gridLayout_2->addWidget(toolButton_2, 2, 5, 1, 1);

        // The editor spans the full width; added after the paste button so
        // columnCount() includes its column (copy, x1, y1, x2, y2, paste).
        gridLayout_2->addWidget(m_editor, 0, 0, 1, gridLayout_2->columnCount());

        QToolButton::connect(toolButton_2, &QToolButton::clicked, [=]() {
            QClipboard *clip = QGuiApplication::clipboard();
            if (QStringList list = clip->text().split(","); !list.isEmpty() && list.size() == 4) {
                bezier->x1 = list[0].toFloat();
                bezier->y1 = list[1].toFloat();
                bezier->x2 = list[2].toFloat();
                bezier->y2 = list[3].toFloat();
                m_editor->blockSignals(true);
                m_editor->bezier = bezier;
                const int width = m_editor->width();
                const int height = m_editor->height();
                m_editor->m_points[1] = QPointF(
                    m_editor->valueToX(bezier->x1, width),
                    m_editor->valueToY(bezier->y1, height));
                m_editor->m_points[2] = QPointF(
                    m_editor->valueToX(bezier->x2, width),
                    m_editor->valueToY(bezier->y2, height));
                m_editor->blockSignals(false);
                m_editor->repaint();
                toolButton_2->setText("Success ✓");
                delay();
                toolButton_2->setText("Paste");
            }
            else {
                QMessageBox::information(splineWidget, "Error", "Invalid clipboard data");
            }
        });


        gridLayout_2->addWidget(toolButton, 2, 0, 1, 1);
        // Easing preview: the canvas's value axis as a vertical strip to the
        // right, with a dot at Y = value — the curve's height at the current
        // progress reads directly. The sweep ramps 0..1.25 (the extra head
        // room keeps the loop from stalling at the exact end); the tick
        // fires at 60Hz (16ms) with progressAccuracy keeping a ~2.8s loop.
        constexpr float progressAccuracy = 0.0072f;
        mEasingDot = new EasingDot(m_editor, splineWidget);

        gridLayout_2->addWidget(mEasingDot, 0, 6, 1, 1);

        auto *timer = new QTimer(splineWidget);
        QTimer::connect(timer, &QTimer::timeout, [=] {
            progress += progressAccuracy;
            // The addition is for extra time so the animation is smoother
            if (progress >= 1.25f) {
                progress = 0;
            }
            /*qDebug("---");
            qDebug() << progress;*/
            QEasingCurve easing;
            easing.setType(QEasingCurve::BezierSpline);
            easing.addCubicBezierSegment(
                {cubicBezier->x1, cubicBezier->y1},
                {cubicBezier->x2, cubicBezier->y2},
                {1.0, 1.0}
                );
            const double easingProgress = easing.valueForProgress(progress);
            /*qDebug("---");
            qDebug() << easingProgress;
            qDebug("---");*/
            mEasingDot->setValue(easingProgress);
        });
        timer->start(16);

        QTimer::connect(splineWidget, &QDialog::finished, [=]() {
            timer->stop();
            timer->deleteLater();
        });

        cancel = new QPushButton(splineWidget);
        cancel->setObjectName("cancel");

        cancel->connect(cancel, &QPushButton::clicked, [=]() {
            splineWidget->reject();
        });

        gridLayout_2->addWidget(cancel, 6, 0, 1, 3);

        apply = new QPushButton(splineWidget);
        apply->setObjectName("apply");

        apply->connect(apply, &QPushButton::clicked, [=]() {
            splineWidget->accept();
        });

        gridLayout_2->addWidget(apply, 6, 3, 1, 3);


        retranslateUi(splineWidget);

        QMetaObject::connectSlotsByName(splineWidget);
    } // setupUi

    void retranslateUi(QWidget *splineWidget)
    {
        splineWidget->setWindowTitle(QCoreApplication::translate("splineWidget", "Bezier editor", nullptr));
        toolButton_2->setText(QCoreApplication::translate("splineWidget", "Paste", nullptr));
        toolButton->setText(QCoreApplication::translate("splineWidget", "Copy", nullptr));
        cancel->setText(QCoreApplication::translate("splineWidget", "Cancel", nullptr));
        apply->setText(QCoreApplication::translate("splineWidget", "Apply", nullptr));
    } // retranslateUi

};

namespace Ui {
    class splineWidget: public Ui_splineWidget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // DESIGNEROAYGQF_H

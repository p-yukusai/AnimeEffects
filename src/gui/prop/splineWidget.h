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
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QToolButton>
#include "gui/prop/bezierCurveEditor.h"
#include "tool/tool_FFDPanel.h"
#include "util/Easing.h"
#include <QtConcurrent>

#include <QMessageBox>
#include <QProgressBar>

QT_BEGIN_NAMESPACE

class Ui_splineWidget {
public:
    QGridLayout *gridLayout_2;
    QDoubleSpinBox *x1_spin;
    QDoubleSpinBox *x2_spin;
    QDoubleSpinBox *y1_spin;
    QFrame *line;
    BezierCurveEditor* m_editor;
    util::Easing::CubicBezier* bezier;
    QToolButton *toolButton_2;
    QToolButton *toolButton;
    QDoubleSpinBox *y2_spin;
    QPushButton *cancel;
    QPushButton *apply;
    QProgressBar *progressBar;
    int progress;
    QVector<QDoubleSpinBox*> spins;


    static float denormalize (const float var, const int min, const int max) {
        return var * static_cast<float>(max - min) + static_cast<float>(min);
    }

    static float normalize (const float var, const int min, const int max) {
        return (var - static_cast<float>(min)) / static_cast<float>(max - min);
    }

    static float invert (const int min, const int max, const float value) {
        return static_cast<float>(max) - value + static_cast<float>(min);
    }

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
        splineWidget->resize(400, 400);
        splineWidget->setMinimumHeight(200);
        splineWidget->setMinimumWidth(200);
        gridLayout_2 = new QGridLayout(splineWidget);
        gridLayout_2->setObjectName("gridLayout_2");
        x1_spin = new QDoubleSpinBox(splineWidget);
        x1_spin->setObjectName("x1_spin");

        gridLayout_2->addWidget(x1_spin, 3, 1, 1, 1);

        x2_spin = new QDoubleSpinBox(splineWidget);
        x2_spin->setObjectName("x2_spin");

        gridLayout_2->addWidget(x2_spin, 3, 3, 1, 1);

        y1_spin = new QDoubleSpinBox(splineWidget);
        y1_spin->setObjectName("y1_spin");

        gridLayout_2->addWidget(y1_spin, 3, 2, 1, 1);

        line = new QFrame(splineWidget);
        line->setObjectName("line");
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);

        gridLayout_2->addWidget(line, 1, 0, 1, 6);


        y2_spin = new QDoubleSpinBox(splineWidget);
        y2_spin->setObjectName("y2_spin");

        gridLayout_2->addWidget(y2_spin, 3, 4, 1, 1);

        spins = {x1_spin, y1_spin, x2_spin, y2_spin};
        m_editor = new BezierCurveEditor(splineWidget, guiRes->mTheme.isDark(), cubicBezier, spins, &progress);
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
                const QPointF points1 = {
                    denormalize(cubicBezier->x1, 20, width - 20), 
                    denormalize(invert(0, 1, cubicBezier->y1), 20, height -20)};
                QPointF points2 = {
                    denormalize(cubicBezier->x2, 20, width - 20),
                    denormalize(invert(0, 1, cubicBezier->y2), 20, height - 20)};
                m_editor->m_points[1] = points1;
                m_editor->m_points[2] = points2;
                m_editor->blockSignals(false);
                m_editor->repaint();
            });
        }

        m_editor->setObjectName("splineChart");
        QSizePolicy sizePolicy(QSizePolicy::Policy::Minimum, QSizePolicy::Policy::MinimumExpanding);
        m_editor->setSizePolicy(sizePolicy);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);

        gridLayout_2->addWidget(m_editor, 0, 0, 1, 6);

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

        gridLayout_2->addWidget(toolButton_2, 3, 5, 1, 1);

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
                const QPointF points1 = {
                    denormalize(bezier->x1, 20, width - 20),
                    denormalize(invert(0, 1, bezier->y1), 20, height -20)};
                const QPointF points2 = {
                    denormalize(bezier->x2, 20, width - 20),
                    denormalize(invert(0, 1, bezier->y2), 20, height - 20)};
                m_editor->m_points[1] = points1;
                m_editor->m_points[2] = points2;
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


        gridLayout_2->addWidget(toolButton, 3, 0, 1, 1);

        // Fuck it, we ball. These numbers are picked for increased smoothness and also to make this easier to code lol
        constexpr float accuracy = 10000000.0f;
        constexpr float progressAccuracy = 1000.0f;
        progressBar = new QProgressBar(splineWidget);
        progressBar->setValue(0);
        progressBar->setMinimum(0.0);
        progressBar->setMaximum(static_cast<int>(accuracy));
        progressBar->setFormat("Preview");
        progressBar->setObjectName("progressBar");

        gridLayout_2->addWidget(progressBar, 6, 0, 1, 6);

        auto *timer = new QTimer(splineWidget);
        QTimer::connect(timer, &QTimer::timeout, [=] {
            progress++;
            // The addition is for extra time so the animation is smoother
            if (static_cast<float>(progress) >= progressAccuracy + 125) {
                progress = 0;
            }
            /*qDebug("---");
            qDebug() << progress;*/
            QEasingCurve easing;
            easing.setType(QEasingCurve::BezierSpline);
            easing.addCubicBezierSegment({
                cubicBezier->x1, cubicBezier->x2},
                {cubicBezier->y1, cubicBezier->y2},
                {1.0, 1.0}
                );
            const double easingProgress = easing.valueForProgress(static_cast<double>(progress) / progressAccuracy);
            /*qDebug() << easingProgress;
            qDebug("---");*/
            progressBar->setValue(static_cast<int>(easingProgress * accuracy));
        });
        timer->start(2);

        QTimer::connect(splineWidget, &QDialog::finished, [=]() {
            timer->stop();
            timer->deleteLater();
        });

        cancel = new QPushButton(splineWidget);
        cancel->setObjectName("cancel");

        cancel->connect(cancel, &QPushButton::clicked, [=]() {
            splineWidget->reject();
        });

        gridLayout_2->addWidget(cancel, 7, 0, 1, 3);

        apply = new QPushButton(splineWidget);
        apply->setObjectName("apply");

        apply->connect(apply, &QPushButton::clicked, [=]() {
            splineWidget->accept();
        });

        gridLayout_2->addWidget(apply, 7, 3, 1, 3);


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

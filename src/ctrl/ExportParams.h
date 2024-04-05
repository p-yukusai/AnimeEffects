#ifndef ANIMEEFFECTS_EXPORTPARAMS_H
#define ANIMEEFFECTS_EXPORTPARAMS_H

#include <QString>
#include <QList>
// For UI
#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QWidget>
// For exporter
#include <cmath>
#include <list>
#include <memory>
#include <QSize>
#include <QFileInfo>
#include <QProcess>
#include <QOpenGLFramebufferObject>
#include <utility>
#include "qdir.h"
#include "qmessagebox.h"
#include "gl/Global.h"
#include "gl/EasyTextureDrawer.h"
#include "core/Project.h"
#include "core/TimeInfo.h"
#include "core/ClippingFrame.h"
#include "core/DestinationTexturizer.h"
#include "gl/Util.h"
#include "qpushbutton.h"
#include "qregularexpression.h"
#include "qurl.h"
#include "util/SelectArgs.h"

#include <QBuffer>
#include <QDesktopServices>
// I was not meant to do OOP...

// Formats other than video and image are for internal use only, ***DO NOT*** use them on export
enum class exportTarget { video, image, pxFmt, aviEnc, mkvEnc, movEnc, mp4Enc, webmEnc };
enum targetRatio { oneToOne, keep, custom };
enum class availableVideoFormats { apng, avi, f4v, flv, gif, mkv, mov, mp2, mp4, ogv, swf, webm, webp };
enum class availableImageFormats { bmp, jpeg, jpg, png, ppm, xbm, xpm, tiff, webp };
enum class availableIntermediateFormats { png, bmp, jpg, jpeg, ppm };
enum class pixelFormats { Auto, yuv420p, yuva420p, rgb24, rgba, bgr24, bgra, gray };

enum class aviEncoders { Auto, mpeg2, mpeg4 };
enum class mkvEncoders { Auto, x264, x265, vp8, vp9, av1, ffv1, magicyuv, huffyuv, theora };
enum class movEncoders { Auto, libx264, h264, prores_ks, utvideo };
enum class mp4Encoders { Auto, mpeg4, h264 };
enum class webmEncoders { Auto, vp8, vp9 };

struct defaultEncoders {
    aviEncoders avi = aviEncoders::Auto;
    mkvEncoders mkv = mkvEncoders::Auto;
    movEncoders mov = movEncoders::Auto;
    mp4Encoders mp4 = mp4Encoders::Auto;
    webmEncoders webm = webmEncoders::Auto;
};

QStringList const videoFormats{
    "apng", "avi", "f4v", "flv", "gif", "mkv", "mov", "mp2", "mp4", "ogv", "swf", "webm", "webp"};
QStringList const imageFormats{"bmp", "jpeg", "jpg", "png", "ppm", "xbm", "xpm", "tiff", "webp"};
QStringList const intermediateImageFormats{"PNG", "BMP", "JPG", "JPEG", "PPM"};
QStringList const pxFormats{"Auto", "yuv420p", "yuva420p", "rgb24", "rgba", "bgr24", "bgra", "gray"};
QStringList const aviEnc{"Auto", "mpeg2", "mpeg4"};
QStringList const mkvEnc{"Auto", "x264", "x265", "vp8", "vp9", "av1", "ffv1", "magicyuv", "huffyuv", "theora"};
QStringList const movEnc{"Auto", "libx264", "h264", "prores_ks", "utvideo"};
QStringList const mp4Enc{"Auto", "mpeg4", "h264"};
QStringList const webmEnc{"Auto", "vp8", "vp9"};

struct frameExportRange {
    int firstFrame = 0;
    int lastFrame = 0;
};

struct GeneralParams {
    QString fileName = "New Project";
    QString exportName = "New Project Export";
    QDir exportDirectory = QDir();
    QDir exportFileName = QDir();
    QString osExportTarget = "";
    int nativeWidth = 0;
    int nativeHeight = 0;
    int exportWidth = 0;
    int exportHeight = 0;
    targetRatio aspectRatio = keep;
    int nativeFPS = 24;
    int fps = 24;
    int bitrate = 0;
    bool allowTransparency = true;
    bool forcePipe = false;
    bool useCustomParam = false;
    bool useIntermediate = true;
    bool usePost = true;
    bool useCustomPalette = false;
    QDir palettePath = QDir();
    QVector<frameExportRange> exportRange = QVector<frameExportRange>();
    QString customInterCommand = "";
    QString customPostCommand = "";
    QVector<int> framesToBeExported = QVector<int>();
    frameExportRange nativeFrameRange = frameExportRange();
    bool exportAllFrames = false;
    bool exportToLast = false;
    int imageExportQuality = -1;
};

struct VideoParams {
    availableVideoFormats format = availableVideoFormats::gif;
    availableIntermediateFormats intermediateFormat = availableIntermediateFormats::png;
    pixelFormats pixelFormat = pixelFormats::Auto;
    defaultEncoders encoders;
};

struct ImageParams {
    availableImageFormats format = availableImageFormats::png;
};

class exportParam {
public:
    exportParam();
    exportTarget exportType = exportTarget::video;
    GeneralParams generalParams;
    // One of these will be ignored on export //
    VideoParams videoParams;
    ImageParams imageParams;
    // They are available for ease of access ///
};

template<typename formatEnum>
QString getFormatAsString(exportTarget target, formatEnum formatIndex) {
    switch (target) {
    case exportTarget::video:
        return videoFormats[formatIndex];
    case exportTarget::image:
        return imageFormats[formatIndex];
    case exportTarget::pxFmt:
        return pxFormats[formatIndex];
    case exportTarget::aviEnc:
        return aviEnc[formatIndex];
    case exportTarget::mkvEnc:
        return mkvEnc[formatIndex];
    case exportTarget::movEnc:
        return movEnc[formatIndex];
    case exportTarget::mp4Enc:
        return mp4Enc[formatIndex];
    case exportTarget::webmEnc:
        return webmEnc[formatIndex];
    }
    return nullptr;
}
inline int getFormatAsInt(exportTarget target, const QString& format) {
    switch (target) {
    case exportTarget::video:
        return videoFormats.indexOf(format);
    case exportTarget::image:
        return imageFormats.indexOf(format);
    case exportTarget::pxFmt:
        return pxFormats.indexOf(format);
    case exportTarget::aviEnc:
        return aviEnc.indexOf(format);
    case exportTarget::mkvEnc:
        return mkvEnc.indexOf(format);
    case exportTarget::movEnc:
        return movEnc.indexOf(format);
    case exportTarget::mp4Enc:
        return mp4Enc.indexOf(format);
    case exportTarget::webmEnc:
        return webmEnc.indexOf(format);
    default:
        return 0;
    }
}

inline bool formatAllowsTransparency(const QString& format) {
    QStringList formatsWithTransparency{"apng", "gif", "png", "webp", "webm", "tiff"};
    return formatsWithTransparency.contains(format.toLower());
}
inline bool pixelFormatAllowsTransparency(const QString& format) {
    QStringList pxWithTransparency{"Auto", "yuva420p", "rgba", "bgra"};
    return pxWithTransparency.contains(format);
}


inline QString tr(const QString& str) { return QCoreApplication::translate("ExportParams", str.toStdString().c_str()); }

inline bool isExportParamValid(exportParam* exParam, QWidget* widget) {
    QStringList warnings;
    QStringList warningDetail;
    QStringList errors;
    QStringList errorDetail;
    // For convenience
    auto* params = &exParam->generalParams;
    if (params->osExportTarget.contains("&#32")) {
        errors.append(tr("Export name or location contains an invalid character (\"&#32\")"));
        errorDetail.append(tr("Export name or location has the invalid character, please rename it to proceed."));
    }
    if (params->allowTransparency && exParam->videoParams.intermediateFormat != availableIntermediateFormats::png){
        errors.append(tr("Transparent export does not have PNG intermediate"));
        errorDetail.append(tr("Export was set to allow transparency but the intermediate format does not support it, "
            "please change it to PNG."));
    }
    if (params->exportHeight == 0) {
        errors.append(tr("Export height is zero"));
        errorDetail.append(tr("Export height was set to zero, please increase the resolution."));
    }
    if (params->exportWidth == 0) {
        errors.append(tr("Export width is zero"));
        errorDetail.append(tr("Export width was set to zero, please increase the resolution."));
    }
    // Generic indivisable by two resolution error for all video targets,
    // as I don't wanna go through each format to check :P
    if (params->exportHeight % 2 != 0 && exParam->exportType == exportTarget::video) {
        errors.append(tr("Export height contains an odd number."));
        errorDetail.append(
            tr("Height is ") + QString::number(params->exportHeight) + tr(", please change it to ") +
            QString::number(params->exportHeight + 1) + tr(" or to ") + QString::number(params->exportHeight - 1) +
            tr(" to continue with the export, as animation exports do not support resultions that cannot be "
               "divided by two.")
        );
    }
    if (params->exportWidth % 2 != 0 && exParam->exportType == exportTarget::video) {
        errors.append(tr("Export width contains an odd number."));
        errorDetail.append(
            tr("Width is ") + QString::number(params->exportWidth) + tr(", please change it to ") +
            QString::number(params->exportWidth + 1) + tr(" or to ") + QString::number(params->exportWidth - 1) +
            tr(" to continue with the export, as animation exports do not support resultions that cannot be "
               "divided by two.")
        );
    }
    if (params->fps == 0) {
        errors.append(tr("Export FPS is equal to zero."));
        errorDetail.append(tr("Target FPS was set to zero, cannot proceed with a framerate lower than 1."));
    }
    double a = static_cast<double>(params->nativeHeight) / static_cast<double>(params->nativeWidth);
    double b = static_cast<double>(params->exportHeight) / static_cast<double>(params->exportWidth);
    double epsilon = std::numeric_limits<double>::epsilon();
    bool aspectRatioKindaEqual = fabs(a - b) <= (fabs(a) > fabs(b) ? fabs(b) : fabs(a)) * epsilon;
    if (params->aspectRatio == keep && !aspectRatioKindaEqual) {
        warnings.append(tr("Target aspect ratio is not met"));
        warningDetail.append(
            tr("The aspect ratio was set to keep but the aspect ratio of the export is very "
               "different to that of the original image, consider setting the aspect ratio to \"Custom\" or to change"
               " the resolution while having \"Keep aspect ratio\" selected.")
        );
    }
    if (!params->exportDirectory.exists()) {
        errors.append(tr("Target directory could not be found"));
        errorDetail.append(tr("The directory \"") + params->osExportTarget + tr("\" could not be found."));
    }
    QFileInfo exDir(params->exportDirectory.absolutePath());
    if (!exDir.isWritable()) {
        errors.append(tr("Target directory is protected"));
        errorDetail.append(
            tr("The directory \"") + params->osExportTarget +
            tr("\" could not be written into as it's protected or otherwise inaccessible, "
               "if you wish to save onto it please launch AnimeEffects as admin.")
        );
    }
    if (params->fps > 10 && params->nativeFPS < 10 && params->fps != 0) {
        warnings.append(tr("Target framerate is low"));
        warningDetail.append(tr(
            "The target framerate for exporting is generally considered very low for "
            "exporting animations and does not to seem intended as the project's fps is higher, consider raising it."
        ));
    }
    if (params->bitrate > 5 * 1000) { // Recommended for standard 720p encoding at 24 fps (5 Mbps)
        warnings.append(tr("Target bitrate is low"));
        warningDetail.append(
            tr("The target bitrate for encoding will generate a low quality export, we "
               "recommend letting FFmpeg decide for you unless you know what you're doing.")
        );
    }
    if (params->bitrate == -1) {
        warnings.append(tr("Target bitrate invalid"));
        warningDetail.append(
            tr("The target bitrate for encoding was invalid in some way, we have changed it to automatic so the"
               " export process may continue without issues should you decide to continue with the export.")
        );
        params->bitrate = 0;
    }

    QString format = exParam->exportType == exportTarget::video
        ? getFormatAsString(exportTarget::video, static_cast<int>(exParam->videoParams.format))
        : getFormatAsString(exportTarget::image, static_cast<int>(exParam->imageParams.format));
    int intermediateFormatToImage;
    switch (static_cast<int>(exParam->videoParams.intermediateFormat)) {
    case 0:
        intermediateFormatToImage = 3;
        break;
    case 1:
        intermediateFormatToImage = 0;
        break;
    case 2:
        intermediateFormatToImage = 2;
    case 3:
        intermediateFormatToImage = 1;
    case 4:
    default:
        intermediateFormatToImage = -1;
    }
    // Special case for PPM
    QString intermediateFormat =
        intermediateFormatToImage == -1 ? "ppm" : getFormatAsString(exportTarget::image, intermediateFormatToImage);
    QString pixelFormat = getFormatAsString(exportTarget::pxFmt, static_cast<int>(exParam->videoParams.pixelFormat));

    if (params->allowTransparency && !formatAllowsTransparency(format)) {
        warnings.append(tr("Export target does not support transparency"));
        warningDetail.append(
            tr("Export with transparency was marked but the selected format does not "
               "support an alpha layer, this means that if the export proceeds it will not have transparency.")
        );
    }
    if (params->allowTransparency && !pixelFormatAllowsTransparency(pixelFormat)) {
        warnings.append(tr("Pixel format does not support transparency"));
        warningDetail.append(
            tr("Export with transparency was marked but the selected pixel format does not "
               "support alpha channels, this means that if the export proceeds it will not have transparency.")
        );
    }
    if (pixelFormat != "auto" && pixelFormat != "Auto" && !params->allowTransparency &&
        pixelFormatAllowsTransparency(pixelFormat) && formatAllowsTransparency(format) &&
        (exParam->exportType != exportTarget::video || formatAllowsTransparency(intermediateFormat))) {
        warnings.append(tr("Forced pixel format with transparency"));
        warningDetail.append(
            tr("A pixel and target format that allow for alpha layers were selected but the option for allowing "
               "transparents export was not, please select it or change your format to one without the 'a' in it to "
               "avoid unintentional transparency.")
        );
    }
    if (exParam->exportType == exportTarget::video && params->allowTransparency &&
        !formatAllowsTransparency(intermediateFormat)) {
        warnings.append(tr("Intermediate export target does not support transparency"));
        warningDetail.append(
            tr("Export with transparency was marked but the selected intermediate format "
               "does not support an alpha layer, this means that if the export proceeds it will not have transparency.")
        );
    }
    if (params->useCustomPalette && params->palettePath.exists()) {
        if (!params->palettePath.isReadable()) {
            errors.append(tr("Selected palette is unreadable"));
            errorDetail.append(
                tr("The palette located at \"" + params->palettePath.absolutePath() +
                   tr("\" could not be read, please ensure it's a valid file."))
            );
        }
        QFileInfo customPalette(params->palettePath.absolutePath());
        if (!customPalette.suffix().contains(QRegularExpression("(png|\\.png)"))) {
            errors.append(tr("Selected palette format unsupported"));
            errorDetail.append(
                tr("The palette located at \"" + params->palettePath.absolutePath() +
                   tr("\" seems to not be a PNG file, please convert it into one."))
            );
        }
    } else if (!params->palettePath.exists()) {
        errors.append(tr("Selected palette does not exist"));
        errorDetail.append(
            tr("The palette referenced at \"" + params->palettePath.absolutePath() +
               tr("\" does not exist or is otherwise inaccessible."))
        );
    }
    if (params->useCustomParam) {
        if (params->customPostCommand.isEmpty() && params->customInterCommand.isEmpty()) {
            warnings.append(tr("Param field is empty"));
            warningDetail.append(
                tr("Export with custom params was selected but both command fields are empty, "
                   "the export process can continue without them should you choose to do so.")
            );
        } else if ((params->customPostCommand.isEmpty() && params->usePost) || (params->customInterCommand.isEmpty() && params->usePost)) {
            if (params->customPostCommand.isEmpty()) {
                warnings.append(
                    tr("Export with custom post parameter was selected but no command was "
                       "given.")
                );
                warningDetail.append(tr("Custom post parameter not found, proceeding without it."));
            } else {
                warnings.append(
                    tr("Export with custom intermediate parameter was selected but no command was "
                       "given.")
                );
                warningDetail.append(tr("Custom intermediate parameter not found, proceeding without it."));
            }
        }
        if (!params->useIntermediate && !params->usePost) {
            warnings.append(tr("Custom parameters disabled"));
            warningDetail.append(
                tr("Export with custom params was selected but both command types are disabled, "
                   "the export process can continue as is should you choose to do so.")
            );
        }
    }

    if (params->exportRange.isEmpty() ||
        (params->exportRange.size() == 1 && params->exportRange.first().firstFrame == 0 &&
         params->exportRange.first().lastFrame == 0) ||
        params->exportRange.first().firstFrame - params->exportRange.first().lastFrame >= 0) {
        errors.append(tr("Export range is empty"));
        errorDetail.append(
            tr("The export range is either empty, set to zero or lasts less than one frame,"
               " please select a range of one frame or more.")
        );
    }

    if (params->exportRange.size() > 1) {
        int x = 0;
        for (auto exportRange : params->exportRange) {
            if (exportRange.firstFrame > exportRange.lastFrame) {
                warnings.append(tr("Export range at index ") + QString::number(x) + tr(" is invalid, swapping."));
                int firstFrame = exportRange.lastFrame;
                int lastFrame = exportRange.firstFrame;
                params->exportRange[x].firstFrame = firstFrame;
                params->exportRange[x].lastFrame = lastFrame;
                warningDetail.append(tr("Swapped frames from range at index ") + QString::number(x) + tr("."));
            }
            if (exportRange.firstFrame == exportRange.lastFrame) {
                warnings.append(tr("Export range at index " + QString::number(x) + tr(" is the same, removing.")));
                params->exportRange.removeAt(x);
                x--; // To compensate for the deletion range
                warningDetail.append(tr("Removing zero frame range at ") + QString::number(x) + tr("."));
            }
            x++;
        }
    }
    // Get all frames that were selected for export
    for (auto range : params->exportRange) {
        int rangeLow = range.firstFrame;
        while (rangeLow < range.lastFrame + 1) {
            params->framesToBeExported.push_back(rangeLow);
            rangeLow++;
        }
    }
    // Hopefully remove duplicates
    std::sort(params->framesToBeExported.begin(), params->framesToBeExported.end());
    params->framesToBeExported.erase(
        std::unique(params->framesToBeExported.begin(), params->framesToBeExported.end()),
        params->framesToBeExported.end()
    );
    // Check if the user is exporting all frames
    int summedNative = params->nativeFrameRange.firstFrame + params->nativeFrameRange.lastFrame;
    int summedExport = params->framesToBeExported.first() + params->framesToBeExported.last();
    if (summedNative == summedExport) {
        params->exportAllFrames = true;
    }
    // If only one range check if it's a continuous export
    if (params->exportRange.size() == 1 && params->exportRange.first().firstFrame == 0 &&
        params->exportRange.first().lastFrame != params->nativeFrameRange.lastFrame) {
        params->exportToLast = true;
    }
    QSettings settings;
    QVariant customEncoderWarning = settings.value("export_custom_encoder_warning_shown");
    if (!customEncoderWarning.isValid()) {
        settings.setValue("export_custom_encoder_warning_shown", false);
        settings.sync();
    }
    bool customEncoderWarningShown = customEncoderWarning.toBool();
    auto encoders = exParam->videoParams.encoders;
    bool isUsingCustomEncoder = encoders.webm != webmEncoders::Auto || encoders.mp4 != mp4Encoders::Auto ||
        encoders.mkv != mkvEncoders::Auto || encoders.avi != aviEncoders::Auto || encoders.mov != movEncoders::Auto;
    if (!customEncoderWarningShown && isUsingCustomEncoder) {
        warnings.append(tr("Using custom encoder"));
        warningDetail.append(
            tr("\nThis warning will only be shown once.\n"
               "You're using a custom encoder for one of our supported formats, these might either just not work or may "
               "not support some of the features you may be expecting out of your export such as transparency, please "
               "learn about the features, advantages and disadvantages of your selected encoder before using it.")
        );
        settings.setValue("export_custom_encoder_warning_shown", true);
    }
    // Should be synced, but just in case I'll leave this here to easily revert it.
    // exParam->generalParams = *params;
    bool proceedToExport = true;
    bool shouldDisplayErrors = !errors.isEmpty() || !warnings.isEmpty();
    if (shouldDisplayErrors) {
        QMessageBox msg;
        bool error = !errors.isEmpty();
        msg.setIcon(error ? QMessageBox::Icon::Critical : QMessageBox::Warning);
        msg.setWindowTitle(
            error ? tr("Critical errors have been found, cannot continue exporting.")
                  : tr("Minor errors have been found, please review.")
        );
        QString conMsg = error ? tr("You cannot proceed with the export until the critical issues found have been "
                                    "resolved.")
                               : tr("You may ignore the errors and proceed should you choose to, click \"Ok\" "
                                    "to export anyway or \"Cancel\" if you wish to cancel the export.");
        QString textMsg = tr("Some issues have been found while exporting, you can review them down below:\n") +
            "-----\n" + tr("Critical errors: ") + QString::number(errors.size()) + tr(" | Warnings: ") +
            QString::number(warnings.size()) + "\n-----\n" + conMsg;
        msg.setText(textMsg);
        QString detMsg = "-----\n" + tr("Critical errors (" + QString::number(errors.size()) + "): \n");
        if (!errors.isEmpty()) {
            int x = 0;
            for (const auto& err : errors) {
                detMsg.append("-----\n" + tr("Error: ") + err + "\n");
                detMsg.append("-\n" + tr("Error detail: ") + errorDetail[x] + "\n");
                x++;
            }
        } else {
            detMsg.append("-----\n" + tr("No errors found.\n"));
        }

        detMsg.append("-----\n" + tr("Warnings (" + QString::number(warnings.size()) + "): \n"));
        if (!warnings.isEmpty()) {
            int x = 0;
            for (const auto& warn : warnings) {
                detMsg.append("-----\n" + tr("Warning: ") + warn + "\n");
                detMsg.append("-\n" + tr("Warning detail: ") + warningDetail[x] + "\n");
                x++;
            }
        } else {
            detMsg.append("-----\n" + tr("No warnings found.\n"));
        }
        msg.setDetailedText(detMsg);
        msg.setStandardButtons(error ? QMessageBox::Cancel : QMessageBox::Ok | QMessageBox::Cancel);
        msg.setParent(widget);
        msg.setWindowFlag(Qt::Window);
        int ret = msg.exec();
        if (ret == QMessageBox::Cancel) {
            proceedToExport = false;
        }
    }
    return proceedToExport;
}

inline exportParam::exportParam() { exportType = exportTarget::video; }

// Sorry for this

namespace exportUI {
class form {
public:
    QGridLayout* gridLayout = new QGridLayout();
    QLabel* loadingMessage = new QLabel();
    QLabel* frameCounter = new QLabel();
    QProgressBar* progressBar = new QProgressBar();
    QLabel* pixmapLabel = new QLabel();
    QPushButton* cancelButton = new QPushButton();

    void setupUi(QWidget* Form) const {
        loadingMessage->setTextFormat(Qt::RichText);
        frameCounter->setTextFormat(Qt::RichText);
        pixmapLabel->setAlignment(Qt::AlignHCenter);
        cancelButton->setStyleSheet("text-align:center;");
        if (Form->objectName().isEmpty())
            Form->setObjectName(QString::fromUtf8("Form"));
        Form->resize(500, 450);
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        loadingMessage->setObjectName(QString::fromUtf8("loadingMessage"));
        QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(loadingMessage->sizePolicy().hasHeightForWidth());
        loadingMessage->setSizePolicy(sizePolicy);

        gridLayout->addWidget(loadingMessage, 0, 0, 1, 1);

        cancelButton->setParent(Form);
        QSizePolicy sizePol1(QSizePolicy::Expanding, QSizePolicy::Maximum);
        sizePol1.setHorizontalStretch(0);
        sizePol1.setVerticalStretch(0);
        sizePol1.setHeightForWidth(frameCounter->sizePolicy().hasHeightForWidth());
        cancelButton->setSizePolicy(sizePol1);

        gridLayout->addWidget(cancelButton, 4, 0, 1, 1);


        frameCounter->setObjectName(QString::fromUtf8("frameCounter"));
        QSizePolicy sizePolicy1(QSizePolicy::Preferred, QSizePolicy::Maximum);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(frameCounter->sizePolicy().hasHeightForWidth());
        frameCounter->setSizePolicy(sizePolicy1);

        gridLayout->addWidget(frameCounter, 5, 0, 1, 1);

        progressBar->setObjectName(QString::fromUtf8("progressBar"));
        QSizePolicy sizePolicy2(QSizePolicy::Expanding, QSizePolicy::Fixed);
        sizePolicy2.setHorizontalStretch(0);
        sizePolicy2.setVerticalStretch(0);
        sizePolicy2.setHeightForWidth(progressBar->sizePolicy().hasHeightForWidth());
        progressBar->setSizePolicy(sizePolicy2);
        progressBar->setValue(0);

        gridLayout->addWidget(progressBar, 1, 0, 1, 1);

        pixmapLabel->setObjectName(QString::fromUtf8("pixmapLabel"));
        QSizePolicy sizePolicy3(QSizePolicy::Preferred, QSizePolicy::Preferred);
        sizePolicy3.setHorizontalStretch(0);
        sizePolicy3.setVerticalStretch(0);
        sizePolicy3.setHeightForWidth(pixmapLabel->sizePolicy().hasHeightForWidth());
        pixmapLabel->setSizePolicy(sizePolicy3);

        gridLayout->addWidget(pixmapLabel, 2, 0, 2, 3);
        Form->setLayout(gridLayout);

        retranslateUi(Form);

        QMetaObject::connectSlotsByName(Form);
    } // setupUi

    void retranslateUi(QWidget* Form) const {
        Form->setWindowTitle(QCoreApplication::translate("Exporting window", "Exporting", nullptr));
        loadingMessage->setText(QCoreApplication::translate(
            "Form", "<html><head/><body><p align=\"center\">Loading</p></body></html>", nullptr
        ));
        cancelButton->setText(QCoreApplication::translate("Form", "Cancel", nullptr));
        frameCounter->setText(QCoreApplication::translate(
            "Form",
            "<html><head/><body><p align=\"center\">Frame rendered x/y | Frame encoded x/y</p></body></html>",
            nullptr
        ));
        pixmapLabel->setText(QCoreApplication::translate(
            "Form", "<html><head/><body><p align=\"center\">Initializing...</p></body></html>", nullptr
        ));
    } // retranslateUi
};
inline bool overwrite(const QString& path) {
    QMessageBox msgBox;
    msgBox.setText(tr("File already exists."));
    msgBox.setInformativeText(tr("Do you want to overwrite the file") + path + tr("?"));
    msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Ok);
    int ret = msgBox.exec();
    return ret == QMessageBox::Ok;
}
} // namespace exportUI

namespace ffmpeg {
enum exportResult {
    ExportFailedToStart,
    ExportCrashed,
    ExportTimedout,
    ExportReadError,
    ExportWriteError,
    ExportUnknownError,
    ExportArgError,
    ExportSuccess,
    ExportCanceled,
    ExportOngoing,
};
inline QString resultToStr(const exportResult& result) {
    switch (result) {
    case ExportFailedToStart:
        return "FFmpeg failed to start";
    case ExportCrashed:
        return "FFmpeg crashed";
    case ExportTimedout:
        return "FFmpeg took too long to respond, most likely due to an argument error";
    case ExportReadError:
        return "Read error";
    case ExportWriteError:
        return "Unable to write to FFmpeg process or to the file's designated path.";
    case ExportUnknownError:
        return "Unknown error";
    case ExportArgError:
        return "Argument given to FFmpeg is invalid or otherwise erroneous";
    case ExportSuccess:
        return "Export was successful";
    case ExportCanceled:
        return "Export was cancelled";
    case ExportOngoing:
        return "Export is ongoing";
    default:
        return "Result type \"" + QString::number(result) + "\" is not valid";
    }
}
inline bool isExportFinished(const exportResult& result) {
    if (result == ExportOngoing) {
        return false;
    }
    return true;
}
inline bool export_errored(const exportResult& result) {
    switch (result) {
    case ExportFailedToStart:
    case ExportCrashed:
    case ExportTimedout:
    case ExportReadError:
    case ExportWriteError:
    case ExportUnknownError:
    case ExportArgError:
        return true;
    case ExportOngoing:
    case ExportSuccess:
    default:
        return false;
    }
}
// "Weird spacing it is" -CLion 2023
struct ffmpegObject {
    QString executable;
    QString argument;
    int tick = 0;
    int procWaitTick = 0;
    exportResult result = ExportOngoing;
    QString latestLog;
    QString latestWriteLog;
    QStringList log;
    QStringList errorLog; // Same as above
    QStringList writeLog;
};
inline bool isAuto(const QString& str) {
    if (str == "Auto" || str == "auto") {
        return true;
    }
    return false;
}
// Only animations get to build arguments so don't worry about image output
inline QString buildArgument(const exportParam& exParam, bool loopGif) {
    // Default
    QString argument = "-y -hwaccel auto -f image2pipe";
    // Framerate
    argument.append(" -framerate " + QString::number(exParam.generalParams.fps));
    // Codec
    switch (exParam.videoParams.format) {
    case availableVideoFormats::avi:
        if (isAuto(aviEnc[static_cast<int>(exParam.videoParams.encoders.avi)])) {
            break;
        }
        argument.append(" -c:v " + aviEnc[static_cast<int>(exParam.videoParams.encoders.avi)]);
        break;
    case availableVideoFormats::mkv:
        if (isAuto(mkvEnc[static_cast<int>(exParam.videoParams.encoders.mkv)])) {
            break;
        }
        argument.append(" -c:v " + mkvEnc[static_cast<int>(exParam.videoParams.encoders.mkv)]);
        break;
    case availableVideoFormats::mov:
        if (isAuto(movEnc[static_cast<int>(exParam.videoParams.encoders.mov)])) {
            break;
        }
        argument.append(" -c:v " + movEnc[static_cast<int>(exParam.videoParams.encoders.mov)]);
        break;
    case availableVideoFormats::mp4:
        if (isAuto(mp4Enc[static_cast<int>(exParam.videoParams.encoders.mp4)])) {
            break;
        }
        argument.append(" -c:v " + mp4Enc[static_cast<int>(exParam.videoParams.encoders.mp4)]);
        break;
    case availableVideoFormats::webm:
        if (isAuto(webmEnc[static_cast<int>(exParam.videoParams.encoders.webm)])) {
            break;
        }
        argument.append(" -c:v " + webmEnc[static_cast<int>(exParam.videoParams.encoders.webm)]);
        break;
    default:
        break; // We leave it to ffmpeg
    }
    // Input
    argument.append(" -i -");
    // Bitrate
    argument.append(" -b:v " + QString::number(exParam.generalParams.bitrate));
    // FPS
    argument.append(" -r " + QString::number(exParam.generalParams.fps));
    // Pixel format
    if (exParam.videoParams.format != availableVideoFormats::gif) {
        if (isAuto(pxFormats[static_cast<int>(exParam.videoParams.pixelFormat)])) {
            if (exParam.generalParams.allowTransparency) {
                if (formatAllowsTransparency(videoFormats[static_cast<int>(exParam.videoParams.format)])) {
                    argument.append(" -pix_fmt yuva420p");
                } else {
                    argument.append(" -pix_fmt yuv420p");
                }
            } else {
                argument.append(" -pix_fmt yuv420p");
            }
        } else {
            argument.append(" -pix_fmt " + pxFormats[static_cast<int>(exParam.videoParams.pixelFormat)]);
        }
    }
    // Format
    argument.append(" -f " + videoFormats[static_cast<int>(exParam.videoParams.format)]);

    // Threads
    argument.append(" -threads 0");

    // Custom palette. To be completely honest with you, I've no idea the difference between these.
    if (exParam.generalParams.useCustomPalette) {
        argument.append(" -i \"" + exParam.generalParams.palettePath.absolutePath() + "\"");
        if (exParam.videoParams.format == availableVideoFormats::gif && exParam.generalParams.allowTransparency) {
            argument.append(" -lavfi paletteuse=alpha_threshold=128 -gifflags -offsetting");
        } else {
            argument.append(" -filter_complex \"paletteuse\"");
        }
    } else if (exParam.videoParams.format == availableVideoFormats::gif) {
        if (exParam.generalParams.allowTransparency) {
            argument.append(" -lavfi split[v],palettegen,[v]paletteuse=alpha_threshold=128 -gifflags -offsetting");
        } else {
            argument.append(" -lavfi split[v],palettegen,[v]paletteuse");
        }
    }
    // Looping gif
    if (exParam.videoParams.format == availableVideoFormats::gif) {
        if (loopGif) {
            argument.append(" -loop 0");
        } else {
            argument.append(" -loop 1");
        }
    }
    // Output
    argument.append(" " + exParam.generalParams.exportFileName.absolutePath().replace(" ", "&#32"));
    return argument;
}
} // namespace ffmpeg

namespace projectExporter {
class Exporter {
public:
    Exporter(core::Project& aProject, QDialog* widget, exportParam& exParam, ffmpeg::ffmpegObject& ffmpeg);
    ~Exporter();
    // Class initializers and other stuff
    typedef std::unique_ptr<QOpenGLFramebufferObject> FramebufferPtr;
    typedef std::list<FramebufferPtr> FramebufferList;
    FramebufferList mFramebuffers;
    QScopedPointer<core::ClippingFrame> mClippingFrame;
    QScopedPointer<core::DestinationTexturizer> mDestinationTexturizer;
    gl::EasyTextureDrawer mTextureDrawer;
    core::TimeInfo mOriginTimeInfo;
    QScopedPointer<QProcess> mProcess;
    ffmpeg::ffmpegObject& export_obj;
    core::Project& mProject;
    QDialog* mWidget;
    exportParam& mParam;
    int mIndex;
    int mDigitCount;
    int mFIndex;
    int mFrameCount;
    QSize exportSize;
    int* encodedFrame = new int(0);
    float mProgress;
    bool mExporting;
    bool mCancelled;
    bool mSaveAsImage;
    bool mOverwriteConfirmation;
    QImage currentFrame;
    QString dirAddress;
    std::string formatChar;
    QString imageSuffix;

    enum ResultType { Success, Cancelled, Errored, Queued, Ongoing };

    struct ExportResult {
        ResultType resultType = Queued;
        QString returnStr;
    };

    static void
    generateMessageBox(const ffmpeg::ffmpegObject* fObj, const ExportResult* eRes, const exportParam* exParam) {
        QMessageBox msgBox;
        msgBox.setDefaultButton(QMessageBox::Ok);
        QString title;
        QString description;
        QString detailed = {
            "FFmpeg Object" + QString("\n-----") + "\nArgument: " + fObj->argument +
            ".\nExecutable: " + fObj->executable + '.' + "\nResult: " + QString::number(fObj->result) +
            ".\nLog: " + fObj->log.join("\n") + '.' + "\nError log: " + fObj->errorLog.join("\n") + '.' + "\n------\n" +
            "Export Results" + QString("\n-----") + "\nResult: " + QString::number(eRes->resultType) + '.' +
            "\nExport String: " + eRes->returnStr + "."};
        // Initialization is here because otherwise the compiler will yell at me
        QString path = QDir::toNativeSeparators(exParam->generalParams.exportDirectory.absolutePath());
        auto* gotoDirectory = new QPushButton(tr("Open export folder"));
        QMessageBox::connect(gotoDirectory, &QPushButton::clicked, [=]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
        switch (eRes->resultType) {
        case Success:
            title = tr("Export success");
            description = tr("Animation successfully exported to:\n") + '"' + path + '"';
            msgBox.addButton(gotoDirectory, QMessageBox::HelpRole);
            break;
        case Cancelled:
            if(fObj->result != ffmpeg::ExportCanceled) {
                title = tr("Export errored");
                description = tr("The export process was cancelled due to an unexpected error, the error description "
                                 "is as follows: \"") + resultToStr(fObj->result) + tr("\".");
                break;
            }
            title = tr("Export cancelled");
            description = tr("Animation export was cancelled by the user.");
            break;
        case Errored:
            title = tr("Export errored");
            description = tr("The export process was cancelled due to an unexpected error, the error description "
                             "is as follows: \"") + resultToStr(fObj->result) + tr("\".");
            break;
        case Queued:
        case Ongoing:
        default:
            title = tr("Something went wrong when halting the export process as the export result is invalid");
            description = tr("The export process stopped abnormaly, please send the info below to the devs.");
            break;
        }
        msgBox.setWindowTitle(title);
        msgBox.setText(description);
        msgBox.setDetailedText(detailed);
        msgBox.exec();
    }

    bool finish(const std::function<bool()>& aWaiter, const exportUI::form* formPointer, int frameTickMax) {
        QApplication::setOverrideCursor(Qt::BusyCursor);
        if (mProcess) {
            mProcess->closeWriteChannel();
            int progressTicks = 0;
            while (!mProcess->waitForFinished()) {
                QApplication::processEvents();
                progressTicks++;
                if (progressTicks == 4) {
                    progressTicks = 0;
                }
                if(formPointer != nullptr) {
                    formPointer->loadingMessage->setText(getProgressMessage(progressTicks));
                    formPointer->frameCounter->setText(getFrameMessage(frameTickMax, progressTicks));
                    formPointer->progressBar->setValue(static_cast<int>(100 * mProgress));
                }
                qDebug() << "Waiting for encode finish...";
                mProcess->waitForFinished(5);
                if (!aWaiter() || isExportFinished(export_obj.result) || export_errored(export_obj.result)) {
                    break;
                }
            }
            auto exitStatus = mProcess->exitStatus();
            qDebug("------------");
            qDebug() << "Exit status: " << exitStatus;
            qDebug() << "Exit code: " << mProcess->exitCode();
            qDebug() << "Exit log: " << export_obj.log;
            qDebug() << "Exit error log: " << export_obj.errorLog;
            qDebug() << "Export object result: " << export_obj.result;
            qDebug() << "Argument: " << export_obj.argument;
            qDebug("------------");
            mProcess.reset();
            QApplication::restoreOverrideCursor();
            return exitStatus == QProcess::NormalExit;
        }
        QApplication::restoreOverrideCursor();
        return QProcess::NotRunning;
    }
    void destroyFramebuffers() {
        for (auto& fbo : mFramebuffers) {
            fbo.reset();
        }
    }
    static void setTextureParam(const QOpenGLFramebufferObject& aFbo) {
        auto id = aFbo.texture();
        gl::Global::Functions& ggl = gl::Global::functions();
        ggl.glBindTexture(GL_TEXTURE_2D, id);
        ggl.glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        ggl.glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        ggl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        ggl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        ggl.glBindTexture(GL_TEXTURE_2D, 0);
    }
    void createFramebuffers(const QSize& aOriginSize, const QSize& aExportSize) {
        destroyFramebuffers();
        mFramebuffers.emplace_back(FramebufferPtr(new QOpenGLFramebufferObject(aOriginSize))); // NOLINT(*-make-unique)
        // setup buffers for scaling
        if (aOriginSize != aExportSize) {
            static constexpr int kMaxCount = 3;
            QSize size = aExportSize;

            for (int i = 0; i < kMaxCount; ++i) {
                const double scaleX = aExportSize.width() / static_cast<double>(size.width());
                const double scaleY = aExportSize.height() / static_cast<double>(size.height());
                const double scaleMax = std::max(scaleX, scaleY);

                if (scaleMax >= 0.5 || i == kMaxCount - 1) {
                    mFramebuffers.emplace_back(FramebufferPtr(new QOpenGLFramebufferObject(aExportSize))
                    ); // NOLINT(*-make-unique)
                } else {
                    size.setWidth(static_cast<int>(size.width() * 0.5));
                    size.setHeight(static_cast<int>(size.height() * 0.5));
                    mFramebuffers.emplace_back(FramebufferPtr(new QOpenGLFramebufferObject(size))
                    ); // NOLINT(*-make-unique)
                }
            }
        }
        // setup texture
        for (auto& fbo : mFramebuffers) {
            setTextureParam(*fbo);
        }
    }
    ResultType initialize(ExportResult* exportResult) {
        // Reset values
        mIndex = 0;
        mProgress = 0.0f;
        mDigitCount = 0;
        mOriginTimeInfo = mProject.currentTimeInfo();
        exportSize = QSize(mParam.generalParams.exportWidth, mParam.generalParams.exportHeight);
        formatChar = mParam.exportType == exportTarget::image
            ? imageFormats[static_cast<int>(mParam.imageParams.format)].toUpper().toStdString()
            : intermediateImageFormats[static_cast<int>(mParam.videoParams.intermediateFormat)].toStdString();
        // Value initialization
        mFrameCount = mParam.generalParams.framesToBeExported.count();
        mSaveAsImage = mParam.exportType == exportTarget::image;
        mParam.generalParams.fileName = QFileInfo(mParam.generalParams.exportFileName.absolutePath()).baseName();
        int ffc = mFrameCount;
        mDigitCount = static_cast<int>(trunc(log10(ffc))) + 1;
        if (mSaveAsImage) {
            QDir dir = mParam.generalParams.exportDirectory.absolutePath();
            dirAddress = dir.absolutePath() + "/" + mParam.generalParams.fileName;
            if (!QDir(dirAddress).exists()) {
                if (!dir.mkdir(mParam.generalParams.fileName)) {
                    export_obj.errorLog.append("Could not create directory for export");
                    return Errored;
                }
            }
        }
        imageSuffix = imageFormats[static_cast<int>(mParam.imageParams.format)];
        // Initialize graphics
        {
            gl::Global::makeCurrent();
            // texture drawer
            if (!mTextureDrawer.init()) {
                export_obj.errorLog.append("Failed to initialize TextureDrawer");
                return Errored;
            }
            // framebuffers
            createFramebuffers(mProject.attribute().imageSize(), exportSize);
            // clipping frame
            mClippingFrame.reset(new core::ClippingFrame());
            mClippingFrame->resize(mProject.attribute().imageSize());
            // create texturizer for destination colors of the framebuffer
            mDestinationTexturizer.reset(new core::DestinationTexturizer());
            mDestinationTexturizer->resize(mProject.attribute().imageSize());
        }
        mExporting = true;
        exportResult->resultType = Ongoing;
        return Success;
    }
    // html centering because there is no "center text" function :(
    QString sCenterL = "<html><head/><body><p align=\"center\">";
    QString sCenterR = "</p></body></html>";
    // message generation
    QString getProgressMessage(int dotTick) const {
        QString pDots;
        const QString currentStatus = mIndex == mFrameCount ? tr("Encoding, please wait") : tr("Exporting");
        if (dotTick == 0) {
            return sCenterL + currentStatus + sCenterR;
        }
        for (int x = 0; x < dotTick; x++) {
            pDots.append(".");
        }
        return sCenterL + currentStatus + pDots + sCenterR;
    }
    QString getFrameMessage(int tick, int dotTick) const {
        QString rString;
        QString pDots;
        for (int x = 0; x < dotTick; x++) {
            pDots.append(".");
        }
        if (mSaveAsImage) {
            rString = sCenterL + tr("Current frame: ") + QString::number(tick) + "/" + QString::number(mFrameCount) +
                sCenterR;
        }
        // As it turns out doing pDots makes the encoder go wobbly and it's kind of disorientating, will keep here anyway
        // in case i figure out later how to make it not so hard on the eyes.
        else {
            if(*encodedFrame == 0){
                rString = sCenterL + tr("Frame rendered: ") + QString::number(mIndex) + "/" + QString::number(mFrameCount) +
                    " | " + tr("Frame encoded: ") + tr(" Processing...") +
                    sCenterR;
            }
            else {
                rString = sCenterL + tr("Frame rendered: ") + QString::number(mIndex) + "/" + QString::number(mFrameCount) +
                    " | " + tr("Frame encoded: ") + QString::number(*encodedFrame) + "/" + QString::number(mFrameCount) +
                    sCenterR;
            }
        }
        return rString;
    }
    // exporter stuff
    bool checkOverwriting(const QFileInfo& aPath) {
        if (!mOverwriteConfirmation && aPath.exists()) {
            if (aPath.isDir() || !exportUI::overwrite(aPath.filePath())) {
                return false;
            }
            mOverwriteConfirmation = true;
        }
        return true;
    }
    bool decideImagePath(int aIndex, QFileInfo& aPath) {
        QString number = QString("%1").arg(aIndex, mDigitCount, 10, QChar('0'));
        QFileInfo filePath(dirAddress + "/" + mParam.generalParams.fileName + number + "." + imageSuffix);
        // check overwrite
        if (!checkOverwriting(filePath)) {
            return false;
        }
        aPath = filePath;
        return true;
    }
    void write(const QByteArray& bytes) const {
        XC_ASSERT(mProcess);
        if(!bytes.isEmpty()) {
            mProcess->write(bytes);
        }
    }
    // framebuffer management
    void exportImage(const QImage& aFboImage, int aIndex) {
        if (mSaveAsImage) {
            QFileInfo savePath;
            if (!decideImagePath(aIndex, savePath)) {
                export_obj.errorLog.append("Unable to get image path");
                export_obj.errorLog.append("Params : " + QString::number(aIndex) + " | " + savePath.absolutePath());
                export_obj.result = ffmpeg::ExportWriteError;
                return;
            }
            bool saveImage =
                aFboImage.save(savePath.filePath(), formatChar.c_str(), mParam.generalParams.imageExportQuality);
            if (!saveImage) {
                export_obj.errorLog.append("Unable to save image");
                export_obj.errorLog.append(
                    "Params :" + savePath.filePath() + " | " + QString::fromStdString(formatChar) + "| " +
                    QString::number(mParam.generalParams.imageExportQuality)
                );
                export_obj.result = ffmpeg::ExportWriteError;
            }
        } else {
            QByteArray byteArray;
            QBuffer buffer(&byteArray);
            buffer.open(QIODevice::ReadWrite);
            if (!aFboImage.save(&buffer, formatChar.c_str(), mParam.generalParams.imageExportQuality)) {
                buffer.close();
                export_obj.errorLog.append("Unable to save image to buffer");
                export_obj.errorLog.append(
                    "Params: " + QString::fromStdString(formatChar) + " | " +
                    QString::number(mParam.generalParams.imageExportQuality)
                );
                export_obj.result = ffmpeg::ExportWriteError;
                return;
            }
            buffer.close();
            write(byteArray);
        }
    }
    bool updateTime(core::TimeInfo& aDst) {
        aDst = mOriginTimeInfo;
        const double current = mIndex * mOriginTimeInfo.fps / static_cast<double>(mParam.generalParams.fps);
        const double frame = mParam.generalParams.framesToBeExported.first() + current;
        // end of export
        if (0 < mIndex && mParam.generalParams.framesToBeExported.last() < frame) {
            return false;
        }
        if (mOriginTimeInfo.frameMax < static_cast<int>(frame)) {
            return false;
        }
        aDst.frame = core::Frame::fromDecimal(static_cast<float>(frame));
        mProgress = static_cast<float>(current) / static_cast<float>(mFrameCount);
        // to next index
        mIndex++;
        return true;
    }
    // renderer
    bool update(const int* frameTick) {
        if (!mExporting) {
            return false;
        }
        const int currentIndex = mIndex;
        // Setup parameter
        core::TimeInfo timeInfo;
        if (!updateTime(timeInfo)) {
            return false;
        }
        // Should hopefully be faster to just check the first condition instead of the second for most exports.
        if (mParam.generalParams.exportAllFrames ||
            mParam.generalParams.framesToBeExported.contains(timeInfo.frame.get())) {
            frameTick += 1;
            // begin rendering
            gl::Global::makeCurrent();
            gl::Global::Functions& ggl = gl::Global::functions();
            const QSize originSize = mProject.attribute().imageSize();
            // clear clipping
            mClippingFrame->clearTexture();
            mClippingFrame->resetClippingId();
            // clear destination texture
            mDestinationTexturizer->clearTexture();
            // bind framebuffer
            if (!mFramebuffers.front()->bind()) {
                XC_FATAL_ERROR("OpenGL Error", "Failed to bind framebuffer.", "");
            }
            // setup
            gl::Util::setViewportAsActualPixels(originSize);
            gl::Util::clearColorBuffer(0.0, 0.0, 0.0, 0.0);
            gl::Util::resetRenderState();
            // render
            core::RenderInfo renderInfo;
            renderInfo.camera.reset(originSize, 1.0, originSize, QPoint());
            renderInfo.time = timeInfo;
            renderInfo.framebuffer = mFramebuffers.front()->handle();
            renderInfo.dest = mFramebuffers.front()->texture();
            renderInfo.isGrid = false;
            renderInfo.clippingId = 0;
            renderInfo.clippingFrame = mClippingFrame.data();
            renderInfo.destTexturizer = mDestinationTexturizer.data();
            // assert info and render
            XC_ASSERT(renderInfo.framebuffer != 0);
            XC_ASSERT(renderInfo.dest != 0);
            mProject.objectTree().render(renderInfo, true);
            // unbind framebuffer
            if (!mFramebuffers.front()->release()) {
                XC_FATAL_ERROR("OpenGL Error", "Failed to bind framebuffer.", "");
            }
            // manage scaling
            {
                QOpenGLFramebufferObject* prev = nullptr;
                for (auto& fbo : mFramebuffers) {
                    if (prev) {
                        fbo->bind();
                        const QSize size = fbo->size();
                        gl::Util::setViewportAsActualPixels(size);
                        gl::Util::clearColorBuffer(0.0, 0.0, 0.0, 0.0);
                        mTextureDrawer.draw(prev->texture());
                        fbo->release();
                    }
                    prev = fbo.get();
                }
            }
            // Create image, pass to UI and save or write to ffmpeg
            {
                auto outImage = mFramebuffers.back()->toImage();
                currentFrame = outImage;
                // flush
                ggl.glFlush();
                // export
                exportImage(outImage, currentIndex);
            }
        }
        return true;
    }

    ExportResult renderAndExport() {
        auto exportResult = ExportResult();
        auto* widget = new QWidget();
        auto* form = new exportUI::form;
        form->setupUi(widget);
        widget->show();
        form->loadingMessage->setText(tr("<html><head/><body><p align=\"center\">Initializing</p></body></html>"));
        QPushButton::connect(form->cancelButton, &QPushButton::clicked, [=] { mCancelled = true; });
        auto initResult = initialize(&exportResult);
        if (initResult == Errored) {
            exportResult.returnStr = "Failed to initialize";
            exportResult.resultType = Errored;
            return exportResult;
        }
        if (!mSaveAsImage) {
            // Initialize export process.
            if (!startExport(export_obj.argument)) {
                exportResult.returnStr = "Failed to start ffmpeg";
                exportResult.resultType = Errored;
                return exportResult;
            }
        }
        int progressTicks = 0;
        int frameTicks = 0;

        while (true) {
            if (mCancelled) {
                qDebug("Export cancelled");
                exportResult.returnStr = "Export cancelled";
                exportResult.resultType = Cancelled;
                if(!export_errored(export_obj.result)){
                    export_obj.result = ffmpeg::ExportCanceled;
                }
                finish([=]() -> bool{ return true; }, form, frameTicks );
                widget->deleteLater();
                // Clean-up
                if (!mSaveAsImage) {
                    QFile(mParam.generalParams.osExportTarget).remove();
                } else {
                    QDir(mParam.generalParams.exportDirectory.absolutePath() + "/" + mParam.generalParams.fileName)
                        .removeRecursively();
                }
                return exportResult;
            }
            if (export_errored(export_obj.result)) {
                qDebug("Export errored");
                export_obj.errorLog.append("FFmpeg error detected, stopping render.");
                finish([=]() -> bool { return true; }, form, frameTicks );
                widget->deleteLater();
                // Clean-up
                if (!mSaveAsImage) {
                    QFile(mParam.generalParams.osExportTarget).remove();
                } else {
                    QDir(mParam.generalParams.exportDirectory.absolutePath() + "/" + mParam.generalParams.fileName)
                        .removeRecursively();
                }
                return exportResult;
            }
            if (!update(&frameTicks)) {
                break;
            }
            progressTicks++;
            if (progressTicks == 4) {
                progressTicks = 0;
            }
            // This does slow down progress somewhat but I prefer better user feedback in this instance, if you have
            // a better idea to achieve the same please let me know!
            form->pixmapLabel->setPixmap(QPixmap::fromImage(currentFrame).scaled(500, 300, Qt::KeepAspectRatio));
            form->loadingMessage->setText(getProgressMessage(progressTicks));
            form->frameCounter->setText(getFrameMessage(frameTicks, progressTicks));
            form->progressBar->setValue(static_cast<int>(100 * mProgress));
            QApplication::processEvents();
        }
        if (!mSaveAsImage) {
            finish([=]() -> bool { return true; }, form, frameTicks );
        }
        if(exportResult.resultType == Ongoing && !export_errored(export_obj.result)) {
            export_obj.result = ffmpeg::ExportSuccess;
            exportResult.resultType = Success;
            exportResult.returnStr = "Export success";
        }
        qDebug("---------");
        qDebug() << export_obj.argument;
        qDebug("---------");
        qDebug() << export_obj.log;
        qDebug("---------");
        widget->deleteLater();
        return exportResult;
    }

    bool startExport(const QString& aArguments) {
#if defined(Q_OS_WIN)
        const QFileInfo localEncoderInfo("./tools/ffmpeg.exe");
        const bool hasLocalEncoder = localEncoderInfo.exists() && localEncoderInfo.isExecutable();
        const QString program = hasLocalEncoder ? QString(".\\tools\\ffmpeg") : QString("ffmpeg");
#else
        const QFileInfo localEncoderInfo("./tools/ffmpeg");
        const bool hasLocalEncoder = localEncoderInfo.exists() && localEncoderInfo.isExecutable();
        const QString program = hasLocalEncoder ? QString("./tools/ffmpeg") : QString("ffmpeg");
#endif
        export_obj.executable = program;
        export_obj.result = ffmpeg::ExportOngoing;
        export_obj.errorLog.clear();
        mProcess.reset(new QProcess(nullptr));
        mProcess->setProcessChannelMode(QProcess::MergedChannels);
        auto process = mProcess.data();

        mProcess->connect(process, &QProcess::readyRead, [=] {
            if (this->mProcess) {
                if(export_obj.log.isEmpty()) { export_obj.log.append("-Ready read-"); }
                if(export_obj.errorLog.isEmpty()) { export_obj.errorLog.append("-Ready read-"); }
                if(export_obj.writeLog.isEmpty()) { export_obj.writeLog.append("-Ready read-"); }
                export_obj.log.push_back(QString(this->mProcess->readAll().data()));
                QStringList logs = {export_obj.log.last(), export_obj.writeLog.last(), export_obj.errorLog.last()};
                bool containsErrors = (logs.contains("error", Qt::CaseInsensitive)
                || logs.contains("conversion failed", Qt::CaseInsensitive)
                || logs.contains("invalid", Qt::CaseInsensitive)
                || logs.contains("null", Qt::CaseInsensitive))
                // Safely ignored errors
                && !logs.contains("invalid maxval", Qt::CaseInsensitive);

                // Some errors during encoding are acceptable as FFmpeg will take care of them, as such we'll
                // ignore them if the encoder has done at least one frame as this means it's doing something and not
                // just failing to encode anything at all.
                if (containsErrors && *encodedFrame == 0) {
                    export_obj.result = ffmpeg::ExportArgError;
                    export_obj.errorLog.append("The argument given to FFmpeg is invalid, conversion failed.");
                    export_obj.errorLog.append(logs);
                    mCancelled = true;
                }
                if (export_obj.log.last().contains("frame")) {
                    QRegularExpression rx(R"(frame\s*=\s*(\d+))");
                    *encodedFrame = rx.match(export_obj.log.last()).captured(1).toInt();
                }
            }
        });

        // TODO: maybe make the process wait time user controllable later on
        mProcess->connect(process, &QProcess::bytesWritten, [=] {
            if (this->mProcess) {
                if (export_obj.procWaitTick >= 25) {
                    if(this->mProcess.isNull() || !this->mProcess->isReadable() || !this->mProcess->isWritable()) {
                        export_obj.result = ffmpeg::ExportTimedout;
                        export_obj.errorLog.append("Unable to read process data for 25+ ticks, FFmpeg timed out.");
                        mCancelled = true;
                    }
                    else {
                        export_obj.procWaitTick = 0;
                    }
                }
                if(export_obj.log.isEmpty()) { export_obj.log.append("-Ready Write-"); }
                if(export_obj.writeLog.isEmpty()) { export_obj.writeLog.append("-Ready Write-"); }
                export_obj.writeLog.push_back(QString(this->mProcess->readAll().data()));
                QString lastLog = export_obj.log.last();
                QString lastWriteLog = export_obj.writeLog.last();
                if(lastLog == export_obj.latestLog && lastWriteLog == export_obj.latestWriteLog) {
                    export_obj.procWaitTick += 1;
                }
                else {
                    // Check whichever gets the IO log first.
                    if (lastLog.isEmpty() && lastWriteLog.isEmpty()) {
                        export_obj.procWaitTick += 1;
                    }
                    else {
                        export_obj.latestLog = lastLog;
                        export_obj.latestWriteLog = lastWriteLog;
                        export_obj.procWaitTick = 0;
                    }
                }
            }
            if (!this->mProcess->isOpen() || !this->mProcess->isWritable()) {
                export_obj.result = ffmpeg::ExportWriteError;
                export_obj.errorLog.append("Process is either not open or cannot be written into.");
                mCancelled = true;
            }
            if (!this->mProcess) {
                export_obj.result = ffmpeg::ExportWriteError;
                export_obj.errorLog.append("Process assertion failed on write call.");
                mCancelled = true;
            }
        });

        mProcess->connect(process, &QProcess::errorOccurred, [=](QProcess::ProcessError aCode) {
            qDebug() << &"Proc code: " [ aCode];
            qDebug() << &"| Export result: " [ static_cast<ffmpeg::exportResult>(aCode)];
            export_obj.result = static_cast<ffmpeg::exportResult>(aCode);
            if (mProcess) {
                export_obj.errorLog.append(mProcess->errorString());
            }
            export_obj.result = ffmpeg::ExportCrashed;
            mCancelled = true;
        });
        mProcess->connect(
            process,
            util::SelectArgs<int, QProcess::ExitStatus>::from(&QProcess::finished),
            [=](int exitStatus, QProcess::ExitStatus) {
                if (exitStatus == 0 && !export_errored(export_obj.result) && export_obj.result != ffmpeg::ExportCanceled)
                    export_obj.result = ffmpeg::ExportSuccess;
                else if (export_obj.result == ffmpeg::ExportOngoing) {
                    export_obj.result = ffmpeg::ExportCrashed;
                }
            }
        );

        QStringList arguments = aArguments.split(' ');
        // Add spaces back to the path
        arguments.last().replace("&#32", " ");
        mProcess->start(program, arguments, QIODevice::ReadWrite);
        return true;
    }
};

inline Exporter::Exporter(core::Project& aProject, QDialog* widget, exportParam& exParam, ffmpeg::ffmpegObject& ffmpeg):
    export_obj(ffmpeg),
    mProject(aProject),
    mWidget(widget),
    mParam(exParam),
    mIndex(0),
    mDigitCount(0),
    mFIndex(0),
    mFrameCount(0),
    mProgress(0),
    mExporting(false),
    mCancelled(false),
    mSaveAsImage(false),
    mOverwriteConfirmation(false) {}
inline Exporter::~Exporter() {
    // Finalize
    finish([=] { return true; }, nullptr, 0);
    // Kill buffer
    gl::Global::makeCurrent();
    destroyFramebuffers();
}
} // namespace projectExporter
#endif // ANIMEEFFECTS_EXPORTPARAMS_H

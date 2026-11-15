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
#include "core/FilterFrame.h"
#include "core/DestinationTexturizer.h"
#include "gl/Util.h"
#include "qpushbutton.h"
#include "qregularexpression.h"
#include "qurl.h"
#include "util/SelectArgs.h"
#include "gui/AudioPlaybackWidget.h"

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

QStringList const videoFormats{ "apng", "avi", "f4v", "flv", "gif", "mkv", "mov", "mp2", "mp4", "ogv", "swf", "webm", "webp"};
QStringList const audioTargettable{ "avi", "mkv", "mov", "mp4", "webm"};
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
    bool loop = true;
    bool exportWithAudio = false;
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
    qsizetype index = -1;
    switch (target) {
    case exportTarget::video:
        index = videoFormats.indexOf(format);
    case exportTarget::image:
        index = imageFormats.indexOf(format);
    case exportTarget::pxFmt:
        index = pxFormats.indexOf(format);
    case exportTarget::aviEnc:
        index = aviEnc.indexOf(format);
    case exportTarget::mkvEnc:
        index = mkvEnc.indexOf(format);
    case exportTarget::movEnc:
        index = movEnc.indexOf(format);
    case exportTarget::mp4Enc:
        index = mp4Enc.indexOf(format);
    case exportTarget::webmEnc:
        index = webmEnc.indexOf(format);
    default:
        index =  0;
    }
    // Error handling for out of bound encoders and formats
    if(index == -1){ return 0; }
    return static_cast<int>(index);
}

inline bool formatAllowsTransparency(const QString& format) {
    QStringList formatsWithTransparency{"apng", "gif", "png", "webp", "webm", "tiff"};
    return formatsWithTransparency.contains(format.toLower());
}
inline bool pixelFormatAllowsTransparency(const QString& format) {
    QStringList pxWithTransparency{"Auto", "yuva420p", "rgba", "bgra"};
    return pxWithTransparency.contains(format);
}

inline QString q_translate(const QString& str) { return QCoreApplication::translate("ExportParams", str.toStdString().c_str(), nullptr); }

// #define tr(str) QCoreApplication::translate("exportParam", str, nullptr)

inline bool isExportParamValid(exportParam* exParam, QWidget* widget, std::vector<audioConfig>* pConf) {
    QStringList warnings;
    QStringList warningDetail;
    QStringList errors;
    QStringList errorDetail;
    // For convenience
    auto* params = &exParam->generalParams;
    if (params->osExportTarget.contains("&#32")) {
        errors.append(q_translate("Export name or location contains an invalid character (\"&#32\")"));
        errorDetail.append(q_translate("Export name or location has the invalid character, please rename it to proceed."));
    }
    if (params->allowTransparency && exParam->exportType == exportTarget::video && exParam->videoParams.intermediateFormat != availableIntermediateFormats::png){
        warnings.append(q_translate("Transparent export does not have PNG intermediate"));
        warningDetail.append(q_translate("Export was set to allow transparency but the intermediate format does not support it, "
            "please change it to PNG."));
    }
    if (params->exportWithAudio && pConf->empty()){
        warnings.append(q_translate("No valid audio streams."));
        warningDetail.append(q_translate("Export with audio was selected but the audio player does not contain any valid audio streams, "
            "please check if the audio files exist and the playback for them is enabled. If the export proceeds no audio will be added."));
    }
    if (exParam->exportType == exportTarget::image && params->allowTransparency &&
        !formatAllowsTransparency(getFormatAsString(exportTarget::image, (int)exParam->imageParams.format))) {
        warnings.append(q_translate("IExport target does not support transparency"));
        warningDetail.append(
            q_translate("Export with transparency was marked but the selected image format "
               "does not support alpha layers, this means that if the export proceeds it will not have transparency.")
        );
    }
    if (params->exportHeight == 0) {
        errors.append(q_translate("Export height is zero"));
        errorDetail.append(q_translate("Export height was set to zero, please increase the resolution."));
    }
    if (params->exportWidth == 0) {
        errors.append(q_translate("Export width is zero"));
        errorDetail.append(q_translate("Export width was set to zero, please increase the resolution."));
    }
    // Generic indivisible by two resolution error for all video targets,
    // as I don't wanna go through each format to check :P
    if (params->exportHeight % 2 != 0 && exParam->exportType == exportTarget::video) {
        errors.append(q_translate("Export height contains an odd number."));
        errorDetail.append(
            q_translate("Height is ") + QString::number(params->exportHeight) + q_translate(", please change it to ") +
            QString::number(params->exportHeight + 1) + q_translate(" or to ") + QString::number(params->exportHeight - 1) +
            q_translate(" to continue with the export, as animation exports do not support resolutions that cannot be "
               "divided by two.")
        );
    }
    if (params->exportWidth % 2 != 0 && exParam->exportType == exportTarget::video) {
        errors.append(q_translate("Export width contains an odd number."));
        errorDetail.append(
            q_translate("Width is ") + QString::number(params->exportWidth) + q_translate(", please change it to ") +
            QString::number(params->exportWidth + 1) + q_translate(" or to ") + QString::number(params->exportWidth - 1) +
            q_translate(" to continue with the export, as animation exports do not support resolutions that cannot be "
               "divided by two.")
        );
    }
    if (params->fps == 0) {
        errors.append(q_translate("Export FPS is equal to zero."));
        errorDetail.append(q_translate("Target FPS was set to zero, cannot proceed with a framerate lower than 1."));
    }
    double a = static_cast<double>(params->nativeHeight) / static_cast<double>(params->nativeWidth);
    double b = static_cast<double>(params->exportHeight) / static_cast<double>(params->exportWidth);
    double epsilon = std::numeric_limits<double>::epsilon();
    bool aspectRatioKindaEqual = fabs(a - b) <= (fabs(a) > fabs(b) ? fabs(b) : fabs(a)) * epsilon;
    if (params->aspectRatio == keep && !aspectRatioKindaEqual) {
        warnings.append(q_translate("Target aspect ratio is not met"));
        warningDetail.append(
            q_translate("The aspect ratio was set to keep but the aspect ratio of the export is very "
               "different to that of the original image, consider setting the aspect ratio to \"Custom\" or to change"
               " the resolution while having \"Keep aspect ratio\" selected.")
        );
    }
    if (!params->exportDirectory.exists()) {
        errors.append(q_translate("Target directory could not be found"));
        errorDetail.append(q_translate("The directory \"") + params->osExportTarget + q_translate("\" could not be found."));
    }
    QFileInfo exDir(params->exportDirectory.absolutePath());
    if (!exDir.isWritable()) {
        errors.append(q_translate("Target directory is protected"));
        errorDetail.append(
            q_translate("The directory \"") + params->osExportTarget +
            q_translate("\" could not be written into as it's protected or otherwise inaccessible, "
               "if you wish to save onto it please launch AnimeEffects as admin.")
        );
    }
    if (params->fps > 10 && params->nativeFPS < 10 && params->fps != 0) {
        warnings.append(q_translate("Target framerate is low"));
        warningDetail.append(q_translate(
            "The target framerate for exporting is generally considered very low for "
            "exporting animations and does not to seem intended as the project's fps is higher, consider raising it."
        ));
    }
    if (params->bitrate > 5 * 1000) { // Recommended for standard 720p encoding at 24 fps (5 Mbps)
        warnings.append(q_translate("Target bitrate is low"));
        warningDetail.append(
            q_translate("The target bitrate for encoding will generate a low quality export, we "
               "recommend letting FFmpeg decide for you unless you know what you're doing.")
        );
    }
    if (params->bitrate == -1) {
        warnings.append(q_translate("Target bitrate invalid"));
        warningDetail.append(
            q_translate("The target bitrate for encoding was invalid in some way, we have changed it to automatic so the"
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
        warnings.append(q_translate("Export target does not support transparency"));
        warningDetail.append(
            q_translate("Export with transparency was marked but the selected format does not "
               "support an alpha layer, this means that if the export proceeds it will not have transparency.")
        );
    }
    if (params->allowTransparency && !pixelFormatAllowsTransparency(pixelFormat)) {
        warnings.append(q_translate("Pixel format does not support transparency"));
        warningDetail.append(
            q_translate("Export with transparency was marked but the selected pixel format does not "
               "support alpha channels, this means that if the export proceeds it will not have transparency.")
        );
    }
    if (pixelFormat != "auto" && pixelFormat != "Auto" && !params->allowTransparency &&
        pixelFormatAllowsTransparency(pixelFormat) && formatAllowsTransparency(format) &&
        (exParam->exportType != exportTarget::video || formatAllowsTransparency(intermediateFormat))) {
        warnings.append(q_translate("Forced pixel format with transparency"));
        warningDetail.append(
            q_translate("A pixel and target format that allow for alpha layers were selected but the option for allowing "
               "transparent export was not, please select it or change your format to one without the 'a' in it to "
               "avoid unintentional transparency.")
        );
    }
    if (params->useCustomPalette && params->palettePath.exists()) {
        if (!params->palettePath.isReadable()) {
            errors.append(q_translate("Selected palette is unreadable"));
            errorDetail.append(
                q_translate("The palette located at \"") + params->palettePath.absolutePath() +
                   q_translate("\" could not be read, please ensure it's a valid file."));
        }
        QFileInfo customPalette(params->palettePath.absolutePath());
        if (!customPalette.suffix().contains(QRegularExpression("(png|\\.png)"))) {
            errors.append(q_translate("Selected palette format unsupported"));
            errorDetail.append(
                q_translate("The palette located at \"") + params->palettePath.absolutePath() +
                   q_translate("\" seems to not be a PNG file, please convert it into one."));
        }
    } else if (!params->palettePath.exists()) {
        errors.append(q_translate("Selected palette does not exist"));
        errorDetail.append(
            q_translate("The palette referenced at \"") + params->palettePath.absolutePath() +
               q_translate("\" does not exist or is otherwise inaccessible."));
    }
    if (params->useCustomParam) {
        if (params->customPostCommand.isEmpty() && params->customInterCommand.isEmpty()) {
            warnings.append(q_translate("Param field is empty"));
            warningDetail.append(
                q_translate("Export with custom params was selected but both command fields are empty, "
                   "the export process can continue without them should you choose to do so.")
            );
        } else if ((params->customPostCommand.isEmpty() && params->usePost) || (params->customInterCommand.isEmpty() && params->usePost)) {
            if (params->customPostCommand.isEmpty()) {
                warnings.append(
                    q_translate("Export with custom post parameter was selected but no command was "
                       "given.")
                );
                warningDetail.append(q_translate("Custom post parameter not found, proceeding without it."));
            } else {
                warnings.append(
                    q_translate("Export with custom intermediate parameter was selected but no command was "
                       "given.")
                );
                warningDetail.append(q_translate("Custom intermediate parameter not found, proceeding without it."));
            }
        }
        if (!params->useIntermediate && !params->usePost) {
            warnings.append(q_translate("Custom parameters disabled"));
            warningDetail.append(
                q_translate("Export with custom params was selected but both command types are disabled, "
                   "the export process can continue as is should you choose to do so.")
            );
        }
    }

    if (params->exportRange.isEmpty() ||
        (params->exportRange.size() == 1 && params->exportRange.first().firstFrame == 0 &&
         params->exportRange.first().lastFrame == 0) ||
        params->exportRange.first().firstFrame - params->exportRange.first().lastFrame >= 0) {
        errors.append(q_translate("Export range is empty"));
        errorDetail.append(
            q_translate("The export range is either empty, set to zero or lasts less than one frame,"
               " please select a range of one frame or more.")
        );
    }

    if (params->exportRange.size() > 1) {
        int x = 0;
        for (auto exportRange : params->exportRange) {
            if (exportRange.firstFrame > exportRange.lastFrame) {
                warnings.append(q_translate("Export range at index ") + QString::number(x) + q_translate(" is invalid, swapping."));
                int firstFrame = exportRange.lastFrame;
                int lastFrame = exportRange.firstFrame;
                params->exportRange[x].firstFrame = firstFrame;
                params->exportRange[x].lastFrame = lastFrame;
                warningDetail.append(q_translate("Swapped frames from range at index ") + QString::number(x) + q_translate("."));
            }
            if (exportRange.firstFrame == exportRange.lastFrame) {
                warnings.append(q_translate("Export range at index ") + QString::number(x) + q_translate(" is the same, removing."));
                params->exportRange.removeAt(x);
                x--; // To compensate for the deletion range
                warningDetail.append(q_translate("Removing zero frame range at ") + QString::number(x) + q_translate("."));
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
        warnings.append(q_translate("Using custom encoder"));
        warningDetail.append(
            q_translate("\nThis warning will only be shown once.\n"
               "You're using a custom encoder for one of our supported formats, these might either just not work or may "
               "not support some of the features you may be expecting out of your export such as transparency, please "
               "learn about the features, advantages and disadvantages of your selected encoder before using it.")
        );
        settings.setValue("export_custom_encoder_warning_shown", true);
    }
    // Should be synced, but just in case I'll leave this here to easily revert it.
    // exParam->generalParams = *params;
    bool proceedToExport = true;
    QVariant ignoreWarnings = settings.value("export_ignore_warning");
    if (!ignoreWarnings.isValid()) { ignoreWarnings.setValue(false); }
    if (!errors.isEmpty() || (!ignoreWarnings.toBool() && !warnings.isEmpty())) {
        QMessageBox msg;
        bool error = !errors.isEmpty();
        msg.setIcon(error ? QMessageBox::Icon::Critical : QMessageBox::Warning);
        msg.setWindowTitle(
            error ? q_translate("Critical errors have been found, cannot continue exporting.")
                  : q_translate("Minor errors have been found, please review.")
        );
        QString conMsg = error ? q_translate("You cannot proceed with the export until the critical issues found have been "
                                    "resolved.")
                               : q_translate("You may ignore the errors and proceed should you choose to, click \"Ok\" "
                                    "to export anyway or \"Cancel\" if you wish to cancel the export.");
        QString textMsg = q_translate("Some issues have been found while exporting, you can review them down below:\n") +
            "-----\n" + q_translate("Critical errors: ") + QString::number(errors.size()) + q_translate(" | Warnings: ") +
            QString::number(warnings.size()) + "\n-----\n" + conMsg;
        msg.setText(textMsg);
        QString detMsg = "-----\n" + q_translate("Critical errors (") + QString::number(errors.size()) + "): \n";
        if (!errors.isEmpty()) {
            int x = 0;
            for (const auto& err : errors) {
                detMsg.append("-----\n" + q_translate("Error: ") + err + "\n");
                detMsg.append("-\n" + q_translate("Error detail: ") + errorDetail[x] + "\n");
                x++;
            }
        } else {
            detMsg.append("-----\n" + q_translate("No errors found.\n"));
        }

        detMsg.append("-----\n" + q_translate("Warnings (") + QString::number(warnings.size()) + "): \n");
        if (!warnings.isEmpty()) {
            int x = 0;
            for (const auto& warn : warnings) {
                detMsg.append("-----\n" + q_translate("Warning: ") + warn + "\n");
                detMsg.append("-\n" + q_translate("Warning detail: ") + warningDetail[x] + "\n");
                x++;
            }
        } else {
            detMsg.append("-----\n" + q_translate("No warnings found.\n"));
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
#endif // ANIMEEFFECTS_EXPORTPARAMS_H

// Decode helpers shared by the media tests.
//
// Why this exists: libswscale may write up to 16 bytes PAST the end of a
// tightly sized packed destination buffer. The overrun is width-dependent
// (measured: 1080 wide overruns, 1440 and 1920 do not) and comes from the
// SIMD tail writer, so it is invisible on most sizes and most runs. Scaling
// straight into QImage::bits() therefore corrupts whatever follows the pixel
// buffer in glibc's heap: a 1080x810 destination is large enough that glibc
// may serve it from the main heap instead of mmap, so the tail write lands on
// the next chunk's header and the process dies later with
// "free(): invalid next size" or "double free or corruption (!prev)" in a
// frame that looks unrelated to the test that caused it.
//
// The fix is to give libswscale a destination allocated by libav itself
// (av_image_alloc pads every plane and the buffer), then copy out of it. Never
// hand sws_scale a caller-sized buffer.
#ifndef AE_TESTS_DECODE_UTIL_H
#define AE_TESTS_DECODE_UTIL_H

#include <QImage>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

// Convert one decoded frame to an owned RGBA8888 QImage.
//
// Returns a null QImage if the scaler fails. The returned image owns its
// pixels, so the libav buffer is freed before returning.
inline QImage scaleToRgba(SwsContext* aSws, const AVFrame* aSrc, int aWidth, int aHeight)
{
    if (!aSws || !aSrc || aWidth <= 0 || aHeight <= 0) return QImage();
    uint8_t* data[4] = {nullptr, nullptr, nullptr, nullptr};
    int stride[4] = {0, 0, 0, 0};
    if (::av_image_alloc(data, stride, aWidth, aHeight, AV_PIX_FMT_RGBA, 64) < 0) return QImage();
    const int scaled = ::sws_scale(aSws, aSrc->data, aSrc->linesize, 0, aHeight, data, stride);
    QImage result;
    if (scaled > 0) {
        const QImage view(data[0], aWidth, aHeight, stride[0], QImage::Format_RGBA8888);
        result = view.copy(); // detach from the libav buffer before freeing it
    }
    ::av_freep(&data[0]);
    return result;
}

#endif // AE_TESTS_DECODE_UTIL_H

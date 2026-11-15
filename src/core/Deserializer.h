#ifndef CORE_DESERIALIZER
#define CORE_DESERIALIZER

#include <QStringList>
#include <QVector>
#include <QPoint>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <QRect>
#include <QRectF>
#include <QMatrix4x4>
#include <QPolygonF>
#include <QtOpenGL/QtOpenGL>
#include <QVersionNumber>
#include "XC.h"
#include "util/Segment2D.h"
#include "util/Easing.h"
#include "util/IndexTable.h"
#include "util/StreamReader.h"
#include "util/IDSolver.h"
#include "util/IProgressReporter.h"
#include "gl/Vector2.h"
#include "gl/Vector3.h"
#include "gl/DeviceInfo.h"
#include "core/Frame.h"

namespace core {

class Deserializer {
public:
    typedef std::istream::pos_type PosType;
    typedef util::IDSolver<void*> IDSolverType;

    Deserializer(
        util::LEStreamReader& aIn,
        IDSolverType& aSolver,
        size_t aMaxFileSize,
        QVersionNumber aVersion,
        const gl::DeviceInfo& aGLDeviceInfo,
        util::IProgressReporter& aRepoter,
        int aRShiftCount
    );

    QVersionNumber version() const { return mVersion; }

    void read(bool& aValue);
    void read(int& aValue);
    void read(float& aValue);
    void read(QList<int>& aValue, bool errorCorrection = false);
    void read(QPoint& aValue);
    void read(QVector2D& aValue);
    void read(QVector3D& aValue);
    void read(QVector4D& aValue);
    void read(util::Segment2D& aValue);
    void read(QSize& aValue);
    void read(QRect& aValue);
    void read(QRectF& aValue);
    void read(QMatrix4x4& aValue);
    void read(Frame& aValue);
    bool read(util::Easing::Param& aValue, bool errorCorrection=false);
    // One key's whole easing array. Files from before per-component easing
    // (minor < 14) wrote exactly ONE param for every key, so that one is read
    // and BROADCAST to all aCount components - lossless for what such a file
    // described, where every component shared that param. Files at 14 onward
    // write aCount.
    bool readEasings(util::Easing::Param* aValue, int aCount, bool errorCorrection = false);
    void read(QPolygonF& aValue);
    void read(QString& aValue, int aMax = 0);
    bool read(const XCMemBlock& aValue);
    bool readWithAlloc(XCMemBlock& aEmptyValue);
    bool readWithAlloc(util::IndexTable& aEmptyValue);

    void readGL(GLint* aArray, int aCount);
    void readGL(GLuint* aArray, int aCount);
    void readGL(GLfloat* aArray, int aCount);
    void readGL(gl::Vector2* aArray, int aCount);
    void readGL(gl::Vector3* aArray, int aCount);

    bool bindIDData(void* aData);
    bool orderIDData(const IDSolverType::Solver& aSolver);

    // Consume a run of derived bytes without reading or allocating them.
    // `aBytes` is checked against the remaining stream first, so a corrupt
    // count fails here instead of silently desynchronising the next field.
    // (The only current caller is BoneKey's legacy influence-cache discard.)
    bool skipBytes(size_t aBytes);

    bool readImage(XCMemBlock& aEmptyValue);
    void readFixedString(QString& aValue, int aSize);

    template<typename tValue>
    tValue getRead() {
        tValue value = tValue();
        read(value);
        return value;
    }

    bool beginBlock(const std::string& aSignature);
    // True when the next bytes are a block with this signature. The stream is
    // left untouched, so an OPTIONAL trailing block can be tested without
    // committing the reader to it.
    bool hasBlock(const std::string& aSignature);
    bool endBlock();

    // Bytes left in the file from the current position. Used to bound a count
    // read from the stream before it allocates or loops; a count cannot exceed
    // the smallest record it could describe.
    size_t remainingBytes() const { return getRestSize(); }

    bool failure() const;

    void pushLogScope(const QString& aScope);
    void popLogScope();
    void setLog(const QString& aLog);
    const QVector<QString>& logScopes() const;
    const QStringList& log() const { return mLog; }

    bool errored(const QString& aLog) {
        if (!mValue.isEmpty()) {
            setLog(aLog + "(" + mValue + ")");
            mValue.clear();
        } else {
            setLog(aLog);
        }
        return false;
    }

    bool checkStream() {
        if (failure()) {
            setLog("stream error");
            return false;
        }
        return true;
    }

    void reportCurrent();

private:
    void alignBy(size_t aSize);
    size_t getRestSize() const;

    util::LEStreamReader& mIn;
    IDSolverType& mIDSolver;
    QVector<PosType> mBlockEnds;
    QVector<QString> mScopes;
    QStringList mLog;
    QString mValue;
    size_t mMaxFileSize;
    QVersionNumber mVersion;
    gl::DeviceInfo mGLDeviceInfo;
    util::IProgressReporter& mReporter;
    int mRShiftCount;
    std::ios::pos_type mFileBegin;
};

} // namespace core

#endif // CORE_DESERIALIZER

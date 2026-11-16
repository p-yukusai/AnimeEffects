#ifndef IMG_GRIDMESHCREATOR_H
#define IMG_GRIDMESHCREATOR_H

#include <QScopedArrayPointer>
#include <QSize>
#include <QRect>
#include <QtOpenGL/QtOpenGL>
#include "XC.h"
#include "img/Buffer.h"

namespace img {

class GridMeshCreator {
public:
    // The content test's threshold: a pixel counts as content when its alpha is above this.
    // MeshContentLevel derives it per layer; this constant is the creator's default and the
    // ceiling of what the derivation produces.
    enum { kContentAlphaThreshold = 10 };
    // The pull test's threshold: a boundary vertex may only trim while anything here is drawn.
    // A cell that exists draws its whole texture, so this is any nonzero alpha, not the content
    // level.
    enum { kEdgeAlphaThreshold = 0 };
    struct HexaConnection {
        int id[6];
        bool has(int aIndex) const { return id[aIndex] != -1; }
        void clear();
    };

    static int getCellTableCount(const QSize& aImageSize, int aCellWidth);

    GridMeshCreator(const uint8* aPtr, const QSize& aSize, int aCellPx,
                    int aContentAlphaThreshold = kContentAlphaThreshold);

    int vertexCount() const { return mVertexCount; }
    int indexCount() const { return mIndexCount; }
    QRect vertexRect() const;

    void writeVertices(GLfloat* aPosVec3, GLfloat* aTexVec2);
    void writeIndices(GLuint* aIndices);
    void writeConnections(HexaConnection* aDest);

private:
    struct Vertex {
        bool isExist;
        float x;
        float y;
        GLuint index;
        sint32 cell[6]; // clockwise from right side
        QVector2D reduceVec;
        float maxReduce;
        float reduceRate;

        QVector2D pos() const { return QVector2D(x, y); }
        QVector2D posReduced() const { return QVector2D(x, y) + (reduceVec * maxReduce * reduceRate); }
    };

    struct Cell {
        bool isExist;
        bool inverted;
        bool nonReducing;
        bool reducingFixed;
        float x;
        float y;
        Vertex* vtx[3];
    };

    class Image {
        img::Buffer mBuffer;
        const uint8* mData;
        QSize mSize;
        int mContentAlphaThreshold;

    public:
        Image(const uint8* aPtr, const QSize& aSize,
              int aContentAlphaThreshold = GridMeshCreator::kContentAlphaThreshold);

        QSize size() const { return mSize; }
        // The content test: does this pixel make its cell exist?
        bool hasRawAlpha(int aX, int aY) const {
            return mData[(aX + aY * mSize.width()) * 4 + 3] > mContentAlphaThreshold;
        }
        bool hasAlpha(int aX, int aY) const {
            if (aX < 0 || mSize.width() <= aX || aY < 0 || mSize.height() <= aY)
                return false;
            return hasRawAlpha(aX, aY);
        }
        // The pull test: is there anything here the layer would draw? A pull may not step over
        // even alpha 1, while cells exist only where the content test says the artist painted.
        bool hasEdgeAlpha(int aX, int aY) const {
            if (aX < 0 || mSize.width() <= aX || aY < 0 || mSize.height() <= aY)
                return false;
            return mData[(aX + aY * mSize.width()) * 4 + 3] > kEdgeAlphaThreshold;
        }
        bool hasSomeAlphaIn3x3(int aX, int aY) const;
        bool getOpaExistence(const Cell& aCell, const QSizeF& aCellSize) const;
    };

    class VertexTable {
        QScopedArrayPointer<Vertex> mVertices;
        QSize mSize;
        const float mHalfSqrt3;

    public:
        VertexTable(int aWidth, int aHeight);

        int width() const { return mSize.width(); }
        int height() const { return mSize.height(); }

        Vertex& vertex(int aX, int aY) {
            XC_MSG_ASSERT(0 <= aX && aX < mSize.width(), "x=%d", aX);
            XC_MSG_ASSERT(0 <= aY && aY < mSize.height(), "y=%d", aY);
            return mVertices[aX + aY * mSize.width()];
        }

        const Vertex& vertex(int aX, int aY) const {
            XC_MSG_ASSERT(0 <= aX && aX < mSize.width(), "x=%d", aX);
            XC_MSG_ASSERT(0 <= aY && aY < mSize.height(), "y=%d", aY);
            return mVertices[aX + aY * mSize.width()];
        }

        const Vertex* findVertex(int aX, int aY) const {
            if (aX < 0 || mSize.width() <= aX)
                return nullptr;
            if (aY < 0 || mSize.height() <= aY)
                return nullptr;
            return &(mVertices[aX + aY * mSize.width()]);
        }

        void setExistance(bool aValue, int aX, int aY) {
            if (aX < 0 || mSize.width() <= aX)
                return;
            if (aY < 0 || mSize.height() <= aY)
                return;
            mVertices[aX + aY * mSize.width()].isExist = aValue;
        }

        void setReducingVectors(float aCellWidth);
        void shortenReducingVectorsOnePixel();
    };

    class CellTable {
        QScopedArrayPointer<Cell> mCells;
        QSizeF mCellSize;
        int mWidth;
        int mHeight;

    public:
        static QSizeF calculateCellSize(int aCellWidth);
        static QSize calculateCellTableSize(const QSize& aImageSize, const QSizeF& aCellSize);

        CellTable(int aCellWidth);
        int initCells(const Image& aImage);
        void connectCellsToVertices(VertexTable& aTable);
        Cell& cell(int aX, int aY);
        Cell* findExistingCell(int aX, int aY);
        // The existing cell that owns a pixel, or nullptr. Used when a content pixel is covered by
        // no rasterized cell, so its cell index is unknown.
        Cell* cellAtPixel(int aPixX, int aPixY);
        QSizeF cellSize() const { return mCellSize; }
        float cellWidth() const { return mCellSize.width(); }
        float cellHeight() const { return mCellSize.height(); }
        int tableWidth() const { return mWidth; }
        int tableHeight() const { return mHeight; }
    };

    void execute(const uint8* aPtr, const QSize& aSize, int aCellPx);
    void initPositionsOfVertices(VertexTable& aTable);
    const Vertex* findConnectVertex(int aX, int aY, int aConnectId) const;
    void setIndicesOfExistingVertices(VertexTable& aTable);
    void reduceBurrs(VertexTable& aTable, const Image& aImage);

    QScopedPointer<CellTable> mCells;
    QScopedPointer<VertexTable> mVertices;
    int mVertexCount;
    int mIndexCount;
    QSize mImageSize;
    int mContentAlphaThreshold;
};

} // namespace img

#endif // IMG_GRIDMESHCREATOR_H

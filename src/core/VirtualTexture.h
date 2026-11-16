#ifndef CORE_VIRTUALTEXTURE_H
#define CORE_VIRTUALTEXTURE_H

#include <QImage>
#include <QMatrix4x4>
#include <QRectF>
#include <QSize>
#include <QtGlobal>
#include <memory>
#include <vector>
#include "XC.h"
#include "core/ImagePyramid.h"
#include "gl/BufferObject.h"
#include "gl/PagePool.h"
#include "gl/Texture.h"
#include "img/ResourceHandle.h"
#include "util/NonCopyable.h"

namespace gl {
class EasyShaderProgram;
struct Vector2;
} // namespace gl

namespace core {

class MaskPyramid;

/// ONE PAGED RESOURCE: the always-resident coarse chain, the page table, the demand map and the
/// frame's upload work. Owned by the ImageKey cache that built it (one per key, like the texture
/// it replaces) or by a MaskKey's shared holder, registered with the static frame loop so demand,
/// upload and readback happen even while the key is not the one being drawn.
///
/// THE THREE TIERS, and why each exists:
///   - the COARSE CHAIN is every level from the one whose longest side is at most
///     gl::PagePool::coarseSize() (512) down to 1x1, held as one ordinary mipmapped texture.
///     It is always resident, so a page that is not there samples it: blur, never a hole. Its
///     levels are the full chain's levels, copied, not rebuilt - a fallback that is a different
///     filter is a second thing to debug.
///   - the PAGE TABLE is one R16UI texel per page of every level sharper than the coarse chain.
///     0 means "not resident"; otherwise (atlas << 8) | (slot + 1). It is rebuilt from the
///     resident set whenever that set changes, in one upload - the set is a few KB, and an
///     incremental table is a second source of truth that can disagree with the pool.
///   - the DEMAND MAP is one R8 texel per page of the same grid, marked by a geometry pass that
///     walks the layer's own triangles with their deformed positions (see markDemand), or by one
///     quad when the mapping is affine (markScreenDemand, markScreenMaskDemand). It is one
///     LAYER of a shared GL_TEXTURE_2D_ARRAY owned by the demand map (DemandMap in the .cpp), so
///     the whole scene is read back with one copy and one fence however many resources there are;
///     the pages stay wanted for kKeepFrames so a one-frame occlusion or a boundary jitter cannot
///     evict a page that is about to be sampled again.
///
/// WHY THE DEMAND PASS IS GEOMETRY, NOT A TRANSFORM ESTIMATE: a layer with FFD, bone or mesh
/// deformation has a different texel-to-pixel ratio across its own surface, and a whole-layer
/// level would have to be the worst triangle's - blurring the 95 % of the area that is not
/// stretched. Per triangle, the ratio is exact: the fragment shader's derivatives of an affine
/// interpolant are the triangle's own edge ratios, so the level the demand pass computes IS the
/// level the fragment shader will pick (see assets/shaders/LayerDrawingFrag.glsl). The pass also
/// rasterizes the triangle's page rectangle into the map, so the demand is a page set, not a
/// number.
///
/// A MASK (buildMask) is the same three tiers over an R8 field: its pages are filled from
/// core::MaskPyramid (the CPU authority an edit patches), they live in the pool's R8 stack, and
/// its demand is a QUAD over the screen rather than a mesh walk - a matte's uv is the
/// screen->content affine at every fragment, so it has one Jacobian and one level
/// (markScreenMaskDemand, shared with a folder's composite matte). Its coarse chain samples
/// GL_CLAMP_TO_EDGE, the mask chain's policy, and the page gutter repeats the level's edge texel to
/// match.
class VirtualTexture: public gl::PagePool::Listener, private util::NonCopyable {
public:
    enum {
        // the number of levels below the coarse chain a table/uniform array can name
        kMaxPageLevels = 16,
        // a page demanded in the last N frames stays wanted (see the class comment)
        kKeepFrames = 3,
    };

    /// True when the resource is big enough to be paged at all. A resource whose longest side is
    /// at most the coarse size is entirely coarse, so a full texture of it is the same residency
    /// with none of the machinery.
    static bool pageable(const QSize& aSize);

    /// The frame boundary. beginFrame maps last frame's demand readbacks and resolves/upload
    /// pages; endFrame issues this frame's readbacks. Both are no-ops when the pool is off.
    static void beginFrame();
    static void endFrame();
    /// Blocks on this frame's demand readbacks, resolves and uploads them, and returns when
    /// every wanted page has been published. The EXPORT policy: a frame is rendered once and
    /// cannot wait two frames for a readback, so the exporter runs a demand-only pass, calls
    /// this, then renders. Never call it from the viewport - the stall it takes is a frame.
    static void synchronize();

    /// How many live paged images there are (for a diagnostic or a gate).
    static int instanceCount();

    VirtualTexture();
    ~VirtualTexture() override;

    /// Builds everything for `aResource`: captures the chain's levels below the coarse threshold,
    /// copies the coarse tail out of `aFullChain`, creates the table and demand textures.
    /// `aFullChain` must be the production chain for the same pixels (created with
    /// gl::Texture::GenerateMipmap). Returns false with nothing built when the size is not
    /// pageable or the capture failed.
    bool build(const img::ResourceHandle& aResource, const QSize& aSize, gl::Texture& aFullChain);

    /// Builds a paged MASK: an R8 field whose levels already live on the CPU (core::MaskPyramid,
    /// captured from the mask's GPU chain). The coarse chain is copied from the pyramid, pages are
    /// filled by MaskPyramid::fillPage and updated in place by a stroke's box. The pyramid must
    /// outlive this object - the mask's holder owns both. Returns false when the field is not
    /// pageable or the pool is off.
    bool buildMask(MaskPyramid& aPyramid);
    bool isMask() const { return mMaskPyramid != nullptr; }
    void destroy();

    bool isValid() const { return mSize.isValid(); }
    const QSize& size() const { return mSize; }
    int maxLevel() const { return mMaxLevel; }
    int coarseLevel() const { return mCoarseLevel; }
    qint64 pyramidBytes() const;

    const gl::Texture& coarseTexture() const { return mCoarse; }
    const gl::Texture& pageTable() const { return mTable; }

    /// Binds the coarse texture, the table and the atlases, and sets every uniform the
    /// VIRTUAL_TEXTURE shader variation reads. Unit assignment is fixed: 4 coarse, 5 table,
    /// 6..9 atlases.
    void bind(gl::EasyShaderProgram& aShader) const;

    /// THE MASK'S OWN STACK: coarse on unit 10, table on 11, R8 atlases on 12..15, and the
    /// uMask* uniforms (see LayerSampling.glsl's sampleMask). The page geometry constants the
    /// two stacks share are set here too, so a draw that pages only the mask (the rubylith
    /// overlay) has them.
    void bindMask(gl::EasyShaderProgram& aShader) const;

    /// A stroke's damage: rebuilds the pyramid's boxes and refreshes what is resident - the
    /// coarse chain's levels and every RESIDENT page the box touches. A page that is not
    /// resident needs nothing: its next fill comes from the pyramid, which is now current.
    void updateMaskBox(const QImage& aField, const QRect& aTexelBox);

    /// THE BLEND VIEW'S FRAME. An animated matte's pyramid is a view over two keys (see
    /// MaskPyramid::setBlend): its content moves with ONE integer weight, not with a damage box,
    /// and the weight changes every frame. Re-copies the whole always-resident coarse chain and
    /// re-fills EVERY resident page - not only the demanded set, because a page outside it can
    /// still be sampled this frame and would otherwise read the previous frame's weight. Called
    /// once per frame by the Data that owns the view, on the first accessor call of the frame.
    void refreshMaskView();

    /// The mask's level-0 field. The pyramid does not store level 0 (it is the field), so a page
    /// fill reads it here; the mask holder refreshes this on every accessor call, and
    /// updateMaskBox replaces it. A QImage copy shares the pixels, so this is a refcount, not a
    /// download.
    void setMaskField(const QImage& aField) { mMaskField = aField; }

    /// THE DEMAND'S INPUTS, as one value. The pass is a pure function of them - every uniform it
    /// reads is in this struct - so a repeat of the same inputs cannot demand a different page
    /// set, and the result can be replayed instead of re-walking the triangles (see markDemand).
    /// `positions` is the caller's stamp for the deformed vertices: a monotonic value a node bumps
    /// whenever its skinning pass writes a new result, because the buffer it writes into is stable
    /// while the values behind it change (TimeKeyExpans::revision does the same job one level up).
    /// A fullscreen pass has no vertex data, so its stamp stays 0 and its matrices are the whole
    /// signature.
    ///
    /// A CAMERA MOVE IS ALWAYS A NEW SIGNATURE, even for a mesh walk whose page rects do not
    /// move: the fragment shader's level comes from the hardware's derivatives of the screen
    /// positions, and their rounding follows the camera, so a mark made at one camera does not
    /// predict what the fragment samples at another (measured, see the definition).
    struct DemandSignature {
        quint64 positions = 0;
        // device pixels for a mesh walk; the pass's own target for a fullscreen mark
        QSize targetSize;
        QMatrix4x4 viewMatrix;
        // a MASK's uv mapping, read by the shader's uUvFromScreen branch (mesh and fullscreen)
        QMatrix4x4 screenToContent;
        QVector2D texCoordOffset;
        QVector4D maskRect;
        float bias = 0.0f;
        // which entry point produced it; filled in by the entry point, not the caller
        int kind = 0;
        bool operator==(const DemandSignature& aRhs) const;
    };

    /// A MATTE'S DEMAND, when its uv is the screen->content affine at every fragment: a layer
    /// mask and a folder's composite matte both read `(uScreenToLayer * vDestCoord - rect) / rect`,
    /// so there is no mesh to walk and no per-vertex uv - one Jacobian and one level over the whole
    /// target. `aSignature.targetSize` is the pass's own coord space (the device screen for a layer,
    /// the FilterFrame's size for a folder), and `maskRect` the field's content rect. The page
    /// extent is the affine image of the target rect, which CONTAINS every fragment the draw can
    /// produce - so the quad is conservative over the geometry, and the signature carries no mesh
    /// revision: a deformation moves the fragments, never the matte's pages.
    void markScreenMaskDemand(const DemandSignature& aSignature);
    /// The AFFINE fast path for an IMAGE: the layer has no deformation, so its uv is one affine
    /// function of the screen position and the pages it can sample are the quad's page extent -
    /// the same union a mesh walk computes, at four vertices instead of the grid's triangles
    /// (measured on nene: 100 % of the demand pass's 1.74 M triangles belong to such layers).
    /// `aSignature.maskRect` is the content rect (0, 0, W, H) and `screenToContent` the layer's
    /// own mapping; a degenerate one must not be passed.
    void markScreenDemand(const DemandSignature& aSignature);

    /// Marks the pages this layer's triangles sample this frame. `aPositions` is the skinned
    /// position buffer the layer is drawn from; `aTexCoords` its mesh uv (content pixels).
    ///
    /// A REPEATED SIGNATURE replays the pages the last mark produced instead of drawing: a still
    /// view repaints the same frame (a pointer moving over the canvas, a dock animating, a brush
    /// stroke on one frame), and its 130 layer walks would demand exactly the same pages. The
    /// replay re-stamps those pages as wanted THIS frame, which is what keeps them resident -
    /// skipping the mark outright would let them age out of the kKeepFrames window while still
    /// being sampled, i.e. a blur pop on a frame that did not change.
    void markDemand(
        const DemandSignature& aSignature,
        const gl::BufferObject& aPositions,
        const gl::Vector2* aTexCoords,
        int aVtxCount,
        const gl::BufferObject& aIndexBuffer,
        int aIndexCount
    );

    int tableWidth() const { return mTableWidth; }
    int tableRows() const { return mTableRows; }
    int pageGridX(int aLevel) const;
    int pageGridY(int aLevel) const;
    int pageCount() const;

    /// Diagnostics: the pages currently wanted (demanded within kKeepFrames) and how many of
    /// those are resident. The gate uses it to assert no sampled page fell back.
    int wantedPageCount() const { return (int)mActivePages.size(); }
    int residentWantedCount() const;

    /// Diagnostics, for the frame being built: how many demand MARKS drew a layer's triangles and
    /// how many were REPLAYED from a repeated signature. A frame that replays everything draws no
    /// demand geometry at all, which is the measurement behind the replay (see markDemand).
    /// Session-wide counters, reset in beginFrame.
    static int demandMarks();
    static int demandReplays();

    /// AE_VT_VERIFY_PAGES=end: runs the slot comparison ONCE, after every frame has been drawn,
    /// with no frame in flight to perturb (see the definition). The per-frame form stalls the
    /// pipeline, which re-times the page landing that the check is asked about. Call it with the
    /// GL context current, after the last frame; it is a no-op for every other mode.
    static void debugVerifyAfterRun();

private:
    // gl::PagePool::Listener
    void pageEvicted(const gl::PagePool::PageId& aPage) override;

    bool buildCoarse(gl::Texture& aFullChain);
    bool buildMaskCoarse();
    bool createTargets();
    bool uploadCoarseMaskBox(int aRelLevel, const QRect& aBox);
    /// Writes one page's bytes (fillPage) into `aDst`, or into mPageBuffer when no destination is
    /// given. A landing frame passes the page's slot in the upload stage (PagePool::stagedPage).
    void fillPageBuffer(int aLevel, int aPageX, int aPageY);
    void fillPageBuffer(int aLevel, int aPageX, int aPageY, uint8* aDst);
    bool uploadPage(int aLevel, int aPageX, int aPageY, const gl::PagePool::Slot& aSlot);
    void resolveDemand();
    void flushTable();
    void consumeDemand();
    /// AE_VT_VERIFY_PAGES: reads every wanted page's slot back and compares it with the page's
    /// own bytes (a diagnostic - see the definition).
    void debugVerifyPages();
    /// Re-stamps the pages the last consumed copy marked as wanted this frame (see markDemand).
    void replayDemand();
    /// Stores `aSignature` and reports whether it repeats the last mark's (and so can be replayed).
    bool demandSignatureRepeats(const DemandSignature& aSignature) const;
    void rebuildTable();
    int pageIndex(int aLevel, int aPageX, int aPageY) const;
    gl::PagePool::PageId pageId(int aLevel, int aPageX, int aPageY) const;
    int levelOfIndex(int aIndex, int* aPageX, int* aPageY) const;
    gl::PagePool::PageId pageIdForIndex(int aIndex) const;

    img::ResourceHandle mResource;
    QSize mSize;
    int mMaxLevel = 0;
    int mCoarseLevel = 0;
    gl::PagePool::Format mFormat = gl::PagePool::Format_RGBA8;
    // the mask's levels, owned by the mask holder (null for an image)
    MaskPyramid* mMaskPyramid = nullptr;
    // the mask's level-0 field (see setMaskField)
    QImage mMaskField;
    ImagePyramid mPyramid;
    gl::Texture mCoarse;
    gl::Texture mTable;

    int mTableWidth = 0;
    int mTableRows = 0;
    int mLevelRow[kMaxPageLevels] = {};
    int mLevelGridX[kMaxPageLevels] = {};
    int mLevelGridY[kMaxPageLevels] = {};

    // the resident set, as the table texture holds it: 0 or (atlas << 8) | (slot + 1)
    std::vector<quint16> mEntries;
    bool mTableDirty = false;
    // pages demanded within kKeepFrames, and the frame each was last marked in
    std::vector<quint32> mActivePages;
    // membership in mActivePages: without it the same page is appended once per frame and the
    // wanted set grows without bound (every duplicate would be resident, inflating the count
    // and the per-frame work)
    std::vector<quint8> mActiveFlags;
    std::vector<int> mDemandFrame;
    // the shared demand map's layer for this resource (-1 until built)
    int mDemandLayer = -1;
    // the mapped frame this resource's layer was last parsed in
    quint64 mDemandParsedFrame = 0;
    quint64 mPoolGeneration = 0;

    // WHAT THE LAST MARK WAS, and the pages it marked: a repeat of the signature replays them.
    // mLastMarked is what the last CONSUMED copy contained, not what the last call marked - the
    // map is a frame behind, so the pages a replay wants are the ones the GPU has already
    // confirmed. It is left alone when a copy marks nothing, because a replayed frame writes no
    // marks at all.
    DemandSignature mDemandSignature;
    bool mDemandSignatureValid = false;
    std::vector<quint32> mLastMarked;

    // one page's upload scratch, kept between uploads (one resource is resolved at a time)
    std::vector<uint8> mPageBuffer;
    int bytesPerTexel() const { return gl::PagePool::bytesPerTexel(mFormat); }
};

} // namespace core

#endif // CORE_VIRTUALTEXTURE_H

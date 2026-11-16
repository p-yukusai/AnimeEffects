#ifndef GL_PAGEPOOL_H
#define GL_PAGEPOOL_H

#include <QtGlobal>
#include <QtOpenGL/QtOpenGL>
#include <list>
#include <unordered_map>
#include <vector>
#include "XC.h"
#include "util/NonCopyable.h"

class QOpenGLContext;

namespace gl {

/// THE SLOT ALLOCATOR FOR PAGED TEXTURES.
///
/// A page is 256x256 texels of one mip level, held in a growable set of 4096^2 atlases. The
/// pool knows nothing about images, levels or tables: it owns slots, hands one to a caller,
/// evicts the least recently touched page when it is full, and tells that page's owner by
/// callback so the owner can clear the page's entry in its table. The owner - core::VirtualTexture
/// - does the upload, the table and the demand. Keeping the split there is what makes the two
/// testable apart: the pool's gate fills slots with a known pattern and reads them back, with
/// no project, no image and no shader in the way.
///
/// GEOMETRY. A slot is 260x260: the 256x256 content, plus a 2-texel gutter on every side. The
/// gutter holds the NEIGHBOURING content texels (and transparent outside the resource), so a
/// bilinear tap at the content's edge reads the texel the level actually has there instead of
/// the next page's slot or the atlas' edge. 2 texels is the exact requirement: a filtered read
/// at a content texel centre reaches at most one texel outside the content, not two. 4096 / 260
/// leaves 15 slots per axis (3900 of 4096 texels used); the remaining 196 texels are dead and
/// deliberate - a pitch that packed 16 slots would leave no gutter at all.
///
/// WHY SEPARATE ATLASES and not one bigger texture or a TEXTURE_2D_ARRAY: a full atlas is an
/// allocation with no copy (a bigger texture is a realloc plus a copy of everything already
/// resident, with both live at the peak), it can be released whole when the view shrinks, and
/// `GL_MAX_TEXTURE_SIZE` is 8192/16384 on much of the hardware this runs on. The shader pays for
/// that with a four-way branch instead of one sampler: at most kMaxAtlasCount atlases are
/// addressable from the layer shader.
///
/// The first atlas is 4096^2 (225 slots, 61 MB), so even a small project pays a 61 MB floor for
/// its pool. A 2048^2 first atlas was built and measured: 57 pages on test.anie then needed a
/// second 4096 atlas for the remaining 8, i.e. 77 MB against 61 MB for one large atlas, and the
/// demand that decides between them is not known until the frame that asks. The fixed large atlas
/// is the simpler, smaller-worst-case choice until a demand-aware policy is measured.
///
/// WHY THE ATLASES ARE CREATED EMPTY AND GROW ON DEMAND: the budget is one number in bytes and
/// the hardware differs by an order of magnitude. Growth is bounded by the budget; an allocation
/// that fails is not a fatal error - the pool reports the failure, keeps the budget it had, and
/// the frame degrades to the coarse chain (see core::VirtualTexture).
class PagePool: private util::NonCopyable {
public:
    /// A page carries one of two formats: RGBA8 for an image, R8 for a mask. The shader's
    /// four-way atlas branch is per STACK, so each format owns its own range of
    /// kMaxAtlasCount atlases (RGBA8 at 0..3, R8 at 4..7) and the two stacks cannot be
    /// confused by an index.
    enum Format {
        Format_RGBA8 = 0,
        Format_R8 = 1,
        kFormatCount = 2,
    };

    enum {
        kPageSize = 256,
        kGutter = 2,
        kPitch = kPageSize + kGutter * 2, // 260
        kAtlasSize = 4096,
        kSlotsPerAxis = kAtlasSize / kPitch, // 15
        kSlotsPerAtlas = kSlotsPerAxis * kSlotsPerAxis, // 225
        kMaxAtlasCount = 4,              // PER FORMAT: what one shader stack can address
        kAtlasSlots = kMaxAtlasCount * kFormatCount,
        kBytesPerTexel = 4,              // the RGBA8 page, which most totals are quoted in
        kPageBytes = kPitch * kPitch * kBytesPerTexel, // 270400
        kAtlasBytes = kAtlasSize * kAtlasSize * kBytesPerTexel, // 60.8 MB
    };

    static int bytesPerTexel(Format aFormat) { return aFormat == Format_R8 ? 1 : 4; }
    static qint64 pageBytes(Format aFormat) { return (qint64)kPitch * kPitch * bytesPerTexel(aFormat); }
    static qint64 atlasBytes(Format aFormat) {
        return (qint64)kAtlasSize * kAtlasSize * bytesPerTexel(aFormat);
    }

    /// The identity of a page, assigned by its owner. `owner` is opaque to the pool (the
    /// VirtualTexture's address); `level/x/y` is the page's position in that resource's pyramid.
    struct PageId {
        quint64 owner = 0;
        int level = 0;
        int x = 0;
        int y = 0;

        bool operator==(const PageId& aRhs) const {
            return owner == aRhs.owner && level == aRhs.level && x == aRhs.x && y == aRhs.y;
        }
    };

    struct PageIdHash {
        size_t operator()(const PageId& aPage) const {
            size_t h = (size_t)aPage.owner;
            h = h * 1000003u + (size_t)aPage.level;
            h = h * 1000003u + (size_t)aPage.x;
            h = h * 1000003u + (size_t)aPage.y;
            return h;
        }
    };

    /// Where a page lives: the atlas, its slot index, and the CONTENT origin in atlas texels
    /// (the gutter is at x-1, x-2, ...). `slot` is 0..kSlotsPerAtlas-1.
    struct Slot {
        int atlas = -1;
        int slot = -1;
        int x = 0;
        int y = 0;
        Format format = Format_RGBA8;

        bool isValid() const { return atlas >= 0; }
    };

    class Listener {
    public:
        virtual ~Listener() {}
        /// The page was evicted: its slot is about to be reused and every table entry that
        /// names it must be cleared. Called BEFORE the slot's new content is written.
        virtual void pageEvicted(const PageId& aPage) = 0;
    };

    static PagePool& instance();

    /// The switch. On by default; `AE_VIRTUAL_TEXTURE=0` selects the full-resolution control
    /// path for A/B measurements and for the gates that compare the two.
    static bool enabled();
    /// The upload-stage switch (`AE_VT_STAGE=0` is the direct-upload control). On by default.
    static bool stagingEnabled();
    /// The longest side of a resource that stays a plain mipmapped texture, and the size the
    /// always-resident coarse chain starts at (`AE_VT_COARSE`, default 512).
    static int coarseSize();
    static qint64 defaultBudgetBytes();
    static qint64 defaultUploadCapBytes();

    void beginFrame();
    void endFrame();

    bool find(const PageId& aPage, Slot* aSlot) const;
    /// Reserves a slot for a page, evicting the least recently touched resident page if the
    /// pool is full. The entry is installed immediately; the caller uploads into `aSlot` and
    /// calls cancel() if the upload cannot be made. Returns false when the frame's upload budget
    /// is spent, the byte budget forbids another atlas, or there is nothing to evict.
    /// `aFormat` selects the atlas stack: an RGBA8 page evicts only RGBA8 pages, an R8 page only
    /// R8 pages, and a format that has exhausted its four atlases under the shared byte budget
    /// fails rather than borrowing the other stack's slots.
    bool acquire(const PageId& aPage, Listener* aListener, Slot* aSlot, Format aFormat = Format_RGBA8);
    void cancel(const Slot& aSlot);
    /// Marks a resident page as used this frame (moves it to the front of the eviction order).
    void touch(const PageId& aPage);
    /// Drops every page of one owner. The listener is NOT called: the owner is going away.
    void releaseOwner(quint64 aOwner);

    /// Drops every resident page and every atlas. The owners' tables are stale afterwards, so
    /// the pool's generation changes and each VirtualTexture clears its table on its next
    /// resolve (see VirtualTexture::resolveDemand). Used by the export gate's "render from an
    /// empty pool" case and by the budget/shrink tests.
    void purge();

    /// Changes whenever the resident set was thrown away wholesale.
    quint64 generation() const { return mGeneration; }

    /// `aAtlas` is 0..kAtlasSlots-1: 0..3 the RGBA8 stack, 4..7 the R8 one.
    GLuint atlasTextureId(int aAtlas) const;
    Format atlasFormat(int aAtlas) const;

    /// Reads a slot back to the caller's buffer (kPitch x kPitch in the slot's format), for the
    /// gate. Binds the atlas, which is otherwise never left bound by the pool.
    bool readSlot(const Slot& aSlot, uint8* aDst) const;

    //----------------------------------------------------------------------------------------------
    // THE UPLOAD STAGE: the bytes of a landing frame's pages, on their way into the atlas
    //----------------------------------------------------------------------------------------------
    //
    /// A landing frame's pages are FILLED INTO MAPPED BUFFER MEMORY and copied to the atlas by the
    /// GPU, instead of being filled into a private buffer that glTexSubImage2D then copies through
    /// the driver's staging on the way in. Both halves are measured (texture-residency.md, item 3):
    /// filling into mapped memory costs the same as filling into a cached buffer (0.88 ms against
    /// 0.91 for 8.4 MB of 4-byte stores), while the same 31 pages cost **2.8 ms of CPU as 31
    /// glTexSubImage2D calls from client memory and 0.00 ms from a buffer object** - the driver's
    /// staging copy is what the direct path pays for.
    ///
    /// A mapped buffer may not be read by GL, so a staged frame is TWO PHASES: the caller fills
    /// into stagedPage() and records the copy with stageCopy(), and endStaging() unmaps and issues
    /// every copy. A page is therefore not in the atlas until endStaging(); while staging is on,
    /// nothing may sample it before then (nothing does - the copies precede the frame's draws).
    ///
    /// The stage is a RING OF TWO buffers a frame apart, so the CPU never writes into bytes the GPU
    /// may still be copying. A frame whose ring slot the GPU has not released does NOT wait:
    /// beginStaging() returns false and that frame's pages take the direct path, the same pixels
    /// by a slower route. `reservePage` can also refuse when the frame's stage is full (the lander
    /// may exceed it at a raised AE_VT_UPLOAD_MB); that page and the rest go direct.
    ///
    /// NO OTHER GL UPLOAD MAY RUN BETWEEN THE TWO PHASES while the buffer is bound as
    /// GL_PIXEL_UNPACK_BUFFER with a client pointer: that pointer would be read as an offset into
    /// the stage. endStaging() binds it only around its own copies, which is why the page table's
    /// publish (flushTable) comes after it and the sentinel atlas is created outside it.
    ///
    /// The stage's size is the upload cap, clamped to kStagingMaxBytes so a debug cap of "999 MB"
    /// does not allocate that much.
    bool beginStaging();
    /// Room for one page of `aFormat`, or -1 when the stage is off/full and the page must go
    /// direct.
    qint64 reservePage(Format aFormat);
    /// The mapped bytes at a reserved offset. Only valid between beginStaging() and endStaging().
    uint8* stagedPage(qint64 aOffset) const;
    /// Records the atlas copy that will read the page at `aOffset` (issued by endStaging()).
    void stageCopy(const Slot& aSlot, Format aFormat, qint64 aOffset);
    /// Unmaps the stage and issues every recorded copy, then fences the buffer for the ring.
    void endStaging();
    /// Pages the stage carried in the frame just ended (0 when the stage was skipped or full).
    int stagedPages() const { return mStagingPages; }
    /// Pages the stage has ever carried - a regression guard for the gates: a paged render that
    /// silently fell back to direct uploads must not pass as "the stage works".
    qint64 stagedPageTotal() const { return mStagedPagesTotal; }

    /// Every resident page with the slot it holds, for a diagnostic that must ask whether TWO
    /// owners agree about who lives in a slot (see VirtualTexture::debugVerifyPages).
    struct Resident {
        PageId page;
        Slot slot;
    };
    std::vector<Resident> residentList() const;

    void setBudgetBytes(qint64 aBytes);
    qint64 budgetBytes() const { return mBudgetBytes; }

    int residentPages() const { return (int)mPages.size(); }
    int atlasCount() const { return mAtlasCount; }
    qint64 residentBytes() const;
    qint64 allocatedBytes() const;
    int frameUploads() const { return mFrameUploads; }
    qint64 frameUploadBytes() const { return mFrameUploadBytes; }
    int frameEvictions() const { return mFrameEvictions; }
    int frameFailures() const { return mFrameFailures; }
    quint64 frame() const { return mFrame; }

private:
    PagePool();

    struct Entry {
        Slot slot;
        Listener* listener = nullptr;
        std::list<PageId>::iterator lru;
    };

    bool makeAtlas(Format aFormat);
    void releaseAtlas(int aAtlas);
    bool ensureContext();
    void resetState();

    std::vector<GLuint> mAtlas;
    std::vector<Format> mAtlasFormat;
    std::vector<int> mAtlasUsed;
    std::vector<quint64> mAtlasLastUsed;
    int mAtlasCount = 0;
    // free slots per atlas, a small stack each; the scan is bounded by kSlotsPerAtlas
    std::vector<std::vector<int>> mFree;
    std::unordered_map<PageId, Entry, PageIdHash> mPages;
    std::list<PageId> mLru; // front = most recently touched
    quint64 mFrame = 0;
    qint64 mBudgetBytes = 0;
    qint64 mUploadCapBytes = 0;
    qint64 mFrameUploadBytes = 0;
    int mFrameUploads = 0;
    int mFrameEvictions = 0;
    int mFrameFailures = 0;
    quint64 mGeneration = 1;
    QOpenGLContext* mContext = nullptr;

    enum { kStagingRing = 2, kStagingMaxBytes = 32 * 1024 * 1024 };
    struct StagedCopy {
        Slot slot;
        Format format = Format_RGBA8;
        qint64 offset = 0;
    };
    GLuint mStaging[kStagingRing] = {};
    GLsync mStagingFence[kStagingRing] = {};
    qint64 mStagingBytes[kStagingRing] = {};
    bool mapStaging();
    bool mStagingOpen = false; // the landing wants the stage; the map is lazy
    int mStagingWrite = 0;     // the ring slot the next staged frame maps
    int mStagingActive = -1;   // the ring slot being filled now, -1 when nothing is mapped
    int mStagingPages = 0;
    qint64 mStagedPagesTotal = 0;
    uint8* mStagingMap = nullptr;
    qint64 mStagingUsed = 0;
    std::vector<StagedCopy> mStagingCopies;
};

} // namespace gl

#endif // GL_PAGEPOOL_H

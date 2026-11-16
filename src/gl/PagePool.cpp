#include <QOpenGLContext>
#include <algorithm>
#include <cstdio>
#include <vector>
#include "gl/FrameStats.h"
#include "gl/Global.h"
#include "gl/PagePool.h"

namespace gl {

namespace {
    bool envSwitchOn(const char* aName, bool aDefault) {
        const QByteArray value = qgetenv(aName);
        if (value.isEmpty())
            return aDefault;
        const bool on = value != "0" && value.compare("false", Qt::CaseInsensitive) != 0;
        return on;
    }

    int envInt(const char* aName, int aDefault) {
        bool ok = false;
        const int value = qEnvironmentVariableIntValue(aName, &ok);
        return ok && value > 0 ? value : aDefault;
    }
} // namespace

PagePool& PagePool::instance() {
    // Deliberately never destroyed: its GL objects belong to a context that dies before a
    // static destructor would run (see gl/Texture.cpp's program cache for the same reason).
    static PagePool* pool = new PagePool();
    return *pool;
}

bool PagePool::enabled() {
    // Read ONCE: the caches are built under one policy, so a switch that changed halfway through
    // a session would leave half the images texture-bound and half paged. It is a startup control.
    static const bool on = envSwitchOn("AE_VIRTUAL_TEXTURE", true);
    return on;
}

bool PagePool::stagingEnabled() {
    // `AE_VT_STAGE=0` is the CONTROL for the staging path: the same pages uploaded through
    // glTexSubImage2D from client memory instead of from the frame's staging buffer. Read once -
    // it is a measurement control, not a per-frame decision.
    static const bool on = envSwitchOn("AE_VT_STAGE", true);
    return on;
}

int PagePool::coarseSize() {
    static const int size = std::max(64, envInt("AE_VT_COARSE", 512));
    return size;
}

qint64 PagePool::defaultBudgetBytes() {
    // Four atlases per format is what one shader stack can address; the default asks for enough
    // to fill both stacks' four RGBA atlases and leaves the R8 stack inside the same byte budget.
    // A machine with less VRAM lowers this with AE_VT_BUDGET_MB, and an allocation that fails
    // lowers it for the session (see makeAtlas).
    const qint64 mb = envInt("AE_VT_BUDGET_MB", 256);
    const qint64 maxBytes =
        (qint64)kMaxAtlasCount * (kAtlasBytes + atlasBytes(Format_R8));
    return std::min<qint64>(mb * 1024 * 1024, maxBytes);
}

qint64 PagePool::defaultUploadCapBytes() {
    const qint64 mb = envInt("AE_VT_UPLOAD_MB", 8);
    return std::max<qint64>(kPageBytes, mb * 1024 * 1024);
}

PagePool::PagePool() {
    mBudgetBytes = defaultBudgetBytes();
    mUploadCapBytes = defaultUploadCapBytes();
    mAtlas.resize(kAtlasSlots, 0);
    mAtlasFormat.resize(kAtlasSlots, Format_RGBA8);
    mAtlasUsed.resize(kAtlasSlots, 0);
    mAtlasLastUsed.resize(kAtlasSlots, 0);
    mFree.resize(kAtlasSlots);
}

bool PagePool::ensureContext() {
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (context == mContext)
        return mContext != nullptr;

    // A different context means every GL object this pool named belongs to a context that is
    // no longer current. Nothing here can delete those objects (that would need the old context
    // made current), so the CPU state is dropped and the ids forgotten - the same bounded leak
    // gl/Texture.cpp's shared program takes on a context recreation. There is no page table left
    // pointing at the old slots because every VirtualTexture rebuilds against the new context.
    mContext = context;
    resetState();
    return mContext != nullptr;
}

void PagePool::resetState() {
    ++mGeneration;
    mPages.clear();
    mLru.clear();
    for (int i = 0; i < kAtlasSlots; ++i) {
        mAtlas[i] = 0;
        mAtlasUsed[i] = 0;
        mAtlasLastUsed[i] = 0;
        mFree[i].clear();
    }
    mAtlasCount = 0;
    mFrameUploadBytes = 0;
    mFrameUploads = 0;
    mFrameEvictions = 0;
    mFrameFailures = 0;
    // THE STAGING RING BELONGS TO THE CONTEXT THAT IS GOING AWAY: forget its objects, do not
    // delete them (their ids are not valid in the new context). A frame that was mid-stage loses
    // its mapping with the context; beginStaging() re-creates the buffers.
    for (int i = 0; i < kStagingRing; ++i) {
        mStaging[i] = 0;
        mStagingFence[i] = nullptr;
        mStagingBytes[i] = 0;
    }
    mStagingOpen = false;
    mStagingWrite = 0;
    mStagingActive = -1;
    mStagingMap = nullptr;
    mStagingUsed = 0;
    mStagingPages = 0;
    mStagingCopies.clear();
}

void PagePool::beginFrame() {
    if (!ensureContext())
        return;
    ++mFrame;
    mFrameUploadBytes = 0;
    mFrameUploads = 0;
    mFrameEvictions = 0;
    mFrameFailures = 0;
    mStagingPages = 0;
}

void PagePool::endFrame() {
    if (!ensureContext())
        return;

    // SHRINK: an atlas with no resident page and no use for a sustained period is released.
    // A window resized smaller (or a project closed) must return the memory without a restart,
    // and a whole atlas is the only unit that can be released.
    enum { kIdleFramesBeforeRelease = 600 }; // 10 s at 60 Hz
    for (int i = 0; i < kAtlasSlots; ++i) {
        if (mAtlas[i] != 0 && mAtlasUsed[i] == 0 && mFrame > mAtlasLastUsed[i] &&
            mFrame - mAtlasLastUsed[i] > kIdleFramesBeforeRelease) {
            releaseAtlas(i);
        }
    }

    FrameStats::instance().virtualTexture(residentPages(), residentBytes(), mFrameUploadBytes, mFrameEvictions);
}

void PagePool::setBudgetBytes(qint64 aBytes) { mBudgetBytes = std::max<qint64>(0, aBytes); }

/// AE_VT_ATLAS_SENTINEL: a fresh atlas starts MAGENTA instead of whatever the driver left in the
/// allocation. It answers the one question no verifier in this file or in core::VirtualTexture
/// asks: does any READ reach a texel that no page upload wrote? A slot check reads the slot the
/// pool maps, so a read that lands anywhere else (another slot, an atlas that holds no page, the
/// dead margin) is invisible to it - and its content is whatever the allocation held, which is
/// stable for a run and different on the next. That is exactly the shape of the panned-camera
/// defect in docs/design/renderer/texture-residency.md, so the sentinel is how the class is
/// confirmed or struck out: the defect appears as magenta, or it does not exist.
std::vector<uint8>& sentinelAtlasBytes(PagePool::Format aFormat) {
    static std::vector<uint8> rgba;
    static std::vector<uint8> r8;
    std::vector<uint8>& bytes = aFormat == PagePool::Format_R8 ? r8 : rgba;
    if (bytes.empty()) {
        const size_t texels = (size_t)PagePool::kAtlasSize * PagePool::kAtlasSize;
        const int channels = aFormat == PagePool::Format_R8 ? 1 : 4;
        bytes.resize(texels * (size_t)channels);
        for (size_t i = 0; i < texels; ++i) {
            uint8* texel = bytes.data() + i * (size_t)channels;
            texel[0] = 255;
            if (channels == 4) {
                texel[1] = 0;
                texel[2] = 255;
                texel[3] = 255;
            }
        }
    }
    return bytes;
}

bool PagePool::makeAtlas(Format aFormat) {
    if (!ensureContext())
        return false;
    const int first = (int)aFormat * kMaxAtlasCount;
    const int end = first + kMaxAtlasCount;
    if (allocatedBytes() + atlasBytes(aFormat) > mBudgetBytes)
        return false;

    int index = -1;
    for (int i = first; i < end; ++i) {
        if (mAtlas[i] == 0) {
            index = i;
            break;
        }
    }
    if (index < 0)
        return false;

    auto& ggl = Global::functions();
    GLuint id = 0;
    ggl.glGenTextures(1, &id);
    if (id == 0)
        return false;
    ggl.glBindTexture(GL_TEXTURE_2D, id);
    // No mip chain and no mip filter: a page IS the level, and the shader reads it with
    // texture() at level 0. NEAREST between slots would break the gutter, so the atlas is
    // LINEAR and the gutter is what keeps the filter inside a page's own content. The mask
    // stack is R8: its pages are one channel and the sampler reads .r.
    const bool sentinel = qEnvironmentVariableIsSet("AE_VT_ATLAS_SENTINEL");
    const void* initial = sentinel ? (const void*)sentinelAtlasBytes(aFormat).data() : nullptr;
    if (aFormat == Format_R8)
        ggl.glTexImage2D(
            GL_TEXTURE_2D, 0, GL_R8, kAtlasSize, kAtlasSize, 0, GL_RED, GL_UNSIGNED_BYTE, initial
        );
    else
        ggl.glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA8, kAtlasSize, kAtlasSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, initial
        );
    ggl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    ggl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    ggl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    ggl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    ggl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    ggl.glBindTexture(GL_TEXTURE_2D, 0);

    if (ggl.glGetError() != GL_NO_ERROR) {
        ggl.glDeleteTextures(1, &id);
        // The allocation was refused (VRAM, size, driver): this is exactly the case the design
        // says must degrade, not crash. Drop the budget to what is already allocated so the
        // next frame does not try again, and let the caller keep the coarse fallback.
        mBudgetBytes = allocatedBytes();
        return false;
    }

    mAtlas[index] = id;
    mAtlasFormat[index] = aFormat;
    ++mAtlasCount;
    mAtlasUsed[index] = 0;
    mAtlasLastUsed[index] = mFrame;
    mFree[index].clear();
    mFree[index].reserve(kSlotsPerAtlas);
    // High slot indices first, so a fresh atlas hands out slot 0, 1, 2... in that order.
    for (int s = kSlotsPerAtlas - 1; s >= 0; --s)
        mFree[index].push_back(s);

    FrameStats::instance().textureCreated((int)atlasBytes(aFormat));
    return true;
}

void PagePool::releaseAtlas(int aAtlas) {
    if (aAtlas < 0 || aAtlas >= kAtlasSlots || mAtlas[aAtlas] == 0)
        return;
    auto& ggl = Global::functions();
    ggl.glDeleteTextures(1, &mAtlas[aAtlas]);
    mAtlas[aAtlas] = 0;
    mAtlasUsed[aAtlas] = 0;
    mFree[aAtlas].clear();
    --mAtlasCount;
    FrameStats::instance().textureDestroyed((int)atlasBytes(mAtlasFormat[aAtlas]));
}

bool PagePool::find(const PageId& aPage, Slot* aSlot) const {
    auto itr = mPages.find(aPage);
    if (itr == mPages.end())
        return false;
    if (aSlot)
        *aSlot = itr->second.slot;
    return true;
}

bool PagePool::acquire(const PageId& aPage, Listener* aListener, Slot* aSlot, Format aFormat) {
    XC_PTR_ASSERT(aSlot);
    if (!ensureContext())
        return false;
    if (mPages.find(aPage) != mPages.end())
        return false; // already resident: the caller should have used find()
    const qint64 bytes = pageBytes(aFormat);
    if (mFrameUploadBytes + bytes > mUploadCapBytes) {
        ++mFrameFailures;
        return false;
    }
    const int firstAtlas = (int)aFormat * kMaxAtlasCount;
    const int endAtlas = firstAtlas + kMaxAtlasCount;

    // A page is only acquired once, but the same page may be demanded by several layers of the
    // same resource in one frame; the caller touches residents before acquiring, so a page
    // touched this frame is at the front of the LRU and is never the victim here.
    int atlas = -1;
    int slot = -1;
    for (int i = firstAtlas; i < endAtlas && atlas < 0; ++i) {
        if (!mFree[i].empty()) {
            atlas = i;
            slot = mFree[i].back();
            mFree[i].pop_back();
        }
    }
    if (atlas < 0) {
        if (makeAtlas(aFormat)) {
            // the fresh atlas has every slot free; take one (there may be several free
            // atlases after a shrink, so scan rather than assume an index)
            for (int i = firstAtlas; i < endAtlas; ++i) {
                if (!mFree[i].empty()) {
                    atlas = i;
                    slot = mFree[i].back();
                    mFree[i].pop_back();
                    break;
                }
            }
        } else {
            // no room under the budget: reuse the least recently touched page OF THIS FORMAT
            // (evicting the other stack's page would not give a usable slot)
            PageId victim;
            auto itr = mPages.end();
            for (auto lru = mLru.rbegin(); lru != mLru.rend(); ++lru) {
                const auto found = mPages.find(*lru);
                if (found != mPages.end() && found->second.slot.format == aFormat) {
                    victim = *lru;
                    itr = found;
                    break;
                }
            }
            if (itr == mPages.end()) {
                ++mFrameFailures;
                return false;
            }
            atlas = itr->second.slot.atlas;
            slot = itr->second.slot.slot;
            Listener* listener = itr->second.listener;
            mLru.erase(itr->second.lru);
            mPages.erase(itr);
            --mAtlasUsed[atlas];
            ++mFrameEvictions;
            // the owner clears its table entry BEFORE the slot is rewritten
            if (listener)
                listener->pageEvicted(victim);
        }
    }
    if (atlas < 0 || slot < 0) {
        ++mFrameFailures;
        return false;
    }

    const int sx = (slot % kSlotsPerAxis) * kPitch;
    const int sy = (slot / kSlotsPerAxis) * kPitch;
    Slot entrySlot;
    entrySlot.atlas = atlas;
    entrySlot.slot = slot;
    entrySlot.x = sx + kGutter;
    entrySlot.y = sy + kGutter;
    entrySlot.format = aFormat;

    Entry entry;
    entry.slot = entrySlot;
    entry.listener = aListener;
    entry.lru = mLru.insert(mLru.begin(), aPage);
    mPages.emplace(aPage, entry);
    ++mAtlasUsed[atlas];
    mAtlasLastUsed[atlas] = mFrame;
    mFrameUploadBytes += bytes;
    ++mFrameUploads;
    *aSlot = entrySlot;
    return true;
}

void PagePool::cancel(const Slot& aSlot) {
    if (!aSlot.isValid() || aSlot.atlas < 0 || aSlot.atlas >= kAtlasSlots)
        return;
    // Find the entry that names this slot (there is at most one) and forget it, returning the
    // slot to its atlas' free stack. The upload budget was counted in acquire; refund it here
    // so a failed upload does not eat the frame.
    for (auto itr = mPages.begin(); itr != mPages.end(); ++itr) {
        if (itr->second.slot.atlas == aSlot.atlas && itr->second.slot.slot == aSlot.slot) {
            mLru.erase(itr->second.lru);
            mPages.erase(itr);
            mFree[aSlot.atlas].push_back(aSlot.slot);
            --mAtlasUsed[aSlot.atlas];
            if (mFrameUploads > 0)
                --mFrameUploads;
            mFrameUploadBytes =
                std::max<qint64>(0, mFrameUploadBytes - pageBytes(aSlot.format));
            return;
        }
    }
}

bool PagePool::beginStaging() {
    if (mStagingActive >= 0)
        return true; // already open
    if (!stagingEnabled())
        return false;
    // MAP LAZILY, ON THE FIRST PAGE: a frame that lands nothing (the steady state) must not map
    // and unmap the stage every frame.
    mStagingOpen = true;
    return true;
}

bool PagePool::mapStaging() {
    if (!ensureContext())
        return false;
    const qint64 cap = std::min<qint64>(mUploadCapBytes, (qint64)kStagingMaxBytes);
    if (cap < pageBytes(Format_RGBA8))
        return false;
    const int slot = mStagingWrite;
    auto& ggl = Global::functions();
    if (mStaging[slot] == 0 || mStagingBytes[slot] != cap) {
        if (mStaging[slot] != 0)
            ggl.glDeleteBuffers(1, &mStaging[slot]);
        ggl.glGenBuffers(1, &mStaging[slot]);
        if (mStaging[slot] == 0)
            return false;
        ggl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, mStaging[slot]);
        ggl.glBufferData(GL_PIXEL_UNPACK_BUFFER, (GLsizeiptr)cap, nullptr, GL_STREAM_DRAW);
        ggl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        mStagingBytes[slot] = cap;
    }
    // THE GPU MUST BE DONE COPYING FROM THIS SLOT before the CPU overwrites it. It is two frames
    // old, so this is normally already signaled; when it is not, the frame takes the direct path
    // instead of waiting - a landing that blocks the CPU to use a staging buffer has lost.
    if (mStagingFence[slot]) {
        const GLenum status = ggl.glClientWaitSync(mStagingFence[slot], 0, 0);
        if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED)
            return false;
        ggl.glDeleteSync(mStagingFence[slot]);
        mStagingFence[slot] = nullptr;
    }
    ggl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, mStaging[slot]);
    // UNSYNCHRONIZED because the fence above already said the GPU is done with this slot: asking
    // the driver to synchronise again would make it throw the buffer's pages away each frame.
    uint8* map = (uint8*)ggl.glMapBufferRange(
        GL_PIXEL_UNPACK_BUFFER, 0, (GLsizeiptr)mStagingBytes[slot],
        GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT
    );
    ggl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    if (!map)
        return false;
    mStagingMap = map;
    mStagingUsed = 0;
    mStagingCopies.clear();
    mStagingActive = slot;
    return true;
}

qint64 PagePool::reservePage(Format aFormat) {
    if (!mStagingOpen)
        return -1;
    if (mStagingActive < 0 && !mapStaging()) {
        mStagingOpen = false; // the stage is unavailable: this frame's pages go direct
        return -1;
    }
    if (!mStagingMap)
        return -1;
    const qint64 bytes = pageBytes(aFormat);
    if (mStagingUsed + bytes > mStagingBytes[mStagingActive])
        return -1;
    const qint64 offset = mStagingUsed;
    mStagingUsed += bytes;
    return offset;
}

uint8* PagePool::stagedPage(qint64 aOffset) const {
    if (mStagingActive < 0 || !mStagingMap || aOffset < 0)
        return nullptr;
    return mStagingMap + aOffset;
}

void PagePool::stageCopy(const Slot& aSlot, Format aFormat, qint64 aOffset) {
    if (mStagingActive < 0 || aOffset < 0)
        return;
    StagedCopy copy;
    copy.slot = aSlot;
    copy.format = aFormat;
    copy.offset = aOffset;
    mStagingCopies.push_back(copy);
    ++mStagingPages;
    ++mStagedPagesTotal;
}

void PagePool::endStaging() {
    mStagingOpen = false;
    if (mStagingActive < 0)
        return;
    const int slot = mStagingActive;
    mStagingActive = -1;
    auto& ggl = Global::functions();
    ggl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, mStaging[slot]);
    // the unmap is what makes the CPU's fill visible to the copies below; a mapped buffer may not
    // be read by GL, which is why the frame has these two phases at all
    ggl.glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    ggl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    GLuint boundAtlas = 0;
    for (const StagedCopy& copy : mStagingCopies) {
        const GLuint atlas = atlasTextureId(copy.slot.atlas);
        if (atlas == 0 || !copy.slot.isValid())
            continue;
        if (atlas != boundAtlas) {
            ggl.glBindTexture(GL_TEXTURE_2D, atlas);
            boundAtlas = atlas;
        }
        ggl.glTexSubImage2D(
            GL_TEXTURE_2D, 0, copy.slot.x - kGutter, copy.slot.y - kGutter, kPitch, kPitch,
            copy.format == Format_R8 ? GL_RED : GL_RGBA, GL_UNSIGNED_BYTE,
            (const void*)(intptr_t)copy.offset
        );
    }
    if (boundAtlas != 0)
        ggl.glBindTexture(GL_TEXTURE_2D, 0);
    ggl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    mStagingCopies.clear();
    mStagingMap = nullptr;
    mStagingUsed = 0;
    if (mStagingFence[slot])
        ggl.glDeleteSync(mStagingFence[slot]);
    mStagingFence[slot] = ggl.glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    mStagingWrite = (slot + 1) % kStagingRing;
}

void PagePool::touch(const PageId& aPage) {
    auto itr = mPages.find(aPage);
    if (itr == mPages.end())
        return;
    if (itr->second.lru != mLru.begin())
        mLru.splice(mLru.begin(), mLru, itr->second.lru);
    mAtlasLastUsed[itr->second.slot.atlas] = mFrame;
}

void PagePool::purge() {
    ++mGeneration;
    mPages.clear();
    mLru.clear();
    for (int i = 0; i < kAtlasSlots; ++i) {
        if (mAtlas[i] != 0)
            releaseAtlas(i);
        mFree[i].clear();
    }
    mAtlasCount = 0;
}

void PagePool::releaseOwner(quint64 aOwner) {
    for (auto itr = mPages.begin(); itr != mPages.end();) {
        if (itr->first.owner == aOwner) {
            mLru.erase(itr->second.lru);
            mFree[itr->second.slot.atlas].push_back(itr->second.slot.slot);
            --mAtlasUsed[itr->second.slot.atlas];
            itr = mPages.erase(itr);
        } else {
            ++itr;
        }
    }
}

GLuint PagePool::atlasTextureId(int aAtlas) const {
    if (aAtlas < 0 || aAtlas >= kAtlasSlots)
        return 0;
    return mAtlas[aAtlas];
}

PagePool::Format PagePool::atlasFormat(int aAtlas) const {
    if (aAtlas < 0 || aAtlas >= kAtlasSlots)
        return Format_RGBA8;
    return mAtlasFormat[aAtlas];
}

qint64 PagePool::residentBytes() const {
    qint64 total = 0;
    for (const auto& page : mPages)
        total += pageBytes(page.second.slot.format);
    return total;
}

qint64 PagePool::allocatedBytes() const {
    qint64 total = 0;
    for (int i = 0; i < kAtlasSlots; ++i) {
        if (mAtlas[i] != 0)
            total += atlasBytes(mAtlasFormat[i]);
    }
    return total;
}

bool PagePool::readSlot(const Slot& aSlot, uint8* aDst) const {
    if (aSlot.atlas < 0 || aSlot.atlas >= kAtlasSlots || mAtlas[aSlot.atlas] == 0 || !aDst)
        return false;
    auto& ggl = Global::functions();

    GLint prevRead = 0;
    ggl.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    GLuint fbo = 0;
    ggl.glGenFramebuffers(1, &fbo);
    ggl.glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    ggl.glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mAtlas[aSlot.atlas], 0);
    ggl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
    if (aSlot.format == Format_R8)
        ggl.glReadPixels(aSlot.x - kGutter, aSlot.y - kGutter, kPitch, kPitch, GL_RED, GL_UNSIGNED_BYTE, aDst);
    else
        ggl.glReadPixels(
            aSlot.x - kGutter, aSlot.y - kGutter, kPitch, kPitch, GL_RGBA, GL_UNSIGNED_BYTE, aDst
        );
    ggl.glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
    ggl.glDeleteFramebuffers(1, &fbo);
    return true;
}

std::vector<PagePool::Resident> PagePool::residentList() const {
    std::vector<Resident> list;
    list.reserve(mPages.size());
    for (const auto& itr : mPages) {
        Resident resident;
        resident.page = itr.first;
        resident.slot = itr.second.slot;
        list.push_back(resident);
    }
    return list;
}

} // namespace gl

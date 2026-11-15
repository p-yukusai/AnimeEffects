#ifndef CORE_MESHTRANSFORMERRESOURCE_H
#define CORE_MESHTRANSFORMERRESOURCE_H

#include <array>
#include <memory>
#include "gl/EasyShaderProgram.h"

namespace core {

class MeshTransformerResource {
public:
    /// The uniform-block binding point for the bone palette (BonePalette).
    enum { kPaletteUniformBinding = 0 };

    MeshTransformerResource();
    void setup(const QString& aShaderPath);

    /// The program for a draw. aSlotCount is the map's influence slot count
    /// (1..8): 1..7 compile the sparse form, 8 the dense form. Programs are
    /// built lazily, because a project's maps usually use one to three slots.
    gl::EasyShaderProgram& program(bool aUseSkinning, bool aUseDualQuaternion, int aSlotCount);

private:
    static int programIndex(bool aUseSkinning, bool aUseDualQuaternion, int aSlotCount);
    void loadFile(const QString& aPath, QString& aDstCode);
    void buildShader(
        gl::EasyShaderProgram& aProgram,
        const QString& aCode,
        bool aUseSkinning,
        bool aUseDualQuaternion,
        int aSlotCount
    );

    QString mCode;
    // 0 = no skinning; 1..8 LBS x slots; 9..16 DQ x slots.
    std::array<std::unique_ptr<gl::EasyShaderProgram>, 17> mPrograms;
};

} // namespace core

#endif // CORE_MESHTRANSFORMERRESOURCE_H

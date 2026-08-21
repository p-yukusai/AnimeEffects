// LeakSanitizer suppressions for the known process-lifetime caches pulled
// in by the Qt gtk3 platform theme (GTK3 -> Pango -> fontconfig):
// fontconfig never frees its font cache, and LSan flags ~315 KB of
// FcPattern/FcCharSet objects at exit. Nothing app-owned appears in the
// stacks (only Main.cpp startup frames), so these are suppressed to keep
// real leaks visible.
//
// The suppressions are embedded via __lsan_default_suppressions() (the
// documented LSan hook), so an ASan build (cmake -DAE_ENABLE_ASAN=ON)
// runs clean with no env vars and no run scripts — no external .supp file
// is consulted at runtime. The guard strips this file entirely from
// normal builds.
#if defined(__SANITIZE_ADDRESS__)

// Declared by liblsan (<sanitizer/lsan_interface.h>); self-declared here
// to avoid depending on the sanitizer header search path.
extern "C" const char* __lsan_default_suppressions();

extern "C" const char* __lsan_default_suppressions() {
    return
        "leak:libfontconfig.so\n"
        "leak:libpango-1.0.so.0\n"
        "leak:libpangoft2-1.0.so.0\n"
        "leak:libpangocairo-1.0.so.0\n"
        "leak:libgtk-3.so.0\n"
        "leak:libgobject-2.0.so.0\n"
        "leak:libqgtk3.so\n";
}

#endif // __SANITIZE_ADDRESS__

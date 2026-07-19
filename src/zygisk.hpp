// zygisk.hpp - Zygisk Next v4 compatible (LSPosed-verified API)
// Key: uses __zn_api global for callback, not arg parameter

#pragma once

#include <jni.h>

namespace zygisk {

// ============================================================
// API interface
// ============================================================

struct Api {
    virtual JNIEnv *getEnv() = 0;
    virtual void setOption(int option) = 0;
    virtual const char *getModuleDir() = 0;
    virtual int connectCompanion() = 0;
    virtual int getCompanionFd() = 0;
    virtual void setCompanionFd(int fd) = 0;
    virtual const char *getModuleName() = 0;
};

enum Option {
    FORCE_DENYLIST_UNMOUNT = 0,
    DLCLOSE_LIBRARY = 1,
};

// ============================================================
// Specialization args
// ============================================================

struct AppSpecializeArgs {
    jint &uid;
    jint &gid;
    jintArray &gids;
    jint &runtime_flags;
    jobjectArray &rlimits;
    jint &mount_external;
    jstring &se_info;
    jstring &nice_name;
    jstring &instruction_set;
    jstring &app_data_dir;
    jboolean &is_child_zygote;
    jboolean &is_usap;
    jstring &app_profile_path;
    jstring &sandbox_process_name;
    jint &sandbox_uid;
};

struct ServerSpecializeArgs {
    jint &uid;
    jint &gid;
    jintArray &gids;
    jint &runtime_flags;
    jlong &permitted_capabilities;
    jlong &effective_capabilities;
};

// ============================================================
// ModuleBase - matches ZN v4 vtable (WITH virtual dtor)
// ============================================================

class ModuleBase {
public:
    virtual ~ModuleBase() = default;
    virtual void onLoad(Api *api, JNIEnv *env) {}
    virtual void preAppSpecialize(AppSpecializeArgs *args) {}
    virtual void postAppSpecialize(AppSpecializeArgs *args) {}
    virtual void preServerSpecialize(ServerSpecializeArgs *args) {}
    virtual void postServerSpecialize(ServerSpecializeArgs *args) {}
};

// ============================================================
// Zygisk Next v4: callback through __zn_api global, not arg
// ============================================================

typedef void (*EntryCallback)(void *);

// __zn_api: ZN writes the API struct here. registerModule is at offset 0x20.
extern "C" __attribute__((visibility("default")))
void* __zn_api[16] = {};

#define REGISTER_ZYGISK_MODULE(cls)                                 \
    static cls _zygisk_module_instance;                             \
                                                                    \
    extern "C" [[gnu::visibility("default")]]                       \
    void zygisk_module_entry(void *arg) {                           \
        /* ZN v4: callback via __zn_api[4] (offset 0x20) */        \
        auto *cb = reinterpret_cast<zygisk::EntryCallback>(zygisk::__zn_api[4]);   \
        if (cb) {                                                   \
            cb(reinterpret_cast<void*>(&_zygisk_module_instance));  \
        }                                                           \
    }

} // namespace zygisk

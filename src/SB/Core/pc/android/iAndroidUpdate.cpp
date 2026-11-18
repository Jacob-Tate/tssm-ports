// Android's side of in-app updates (iUpdate.h; docs/ANDROID.md "Updates").
//
// The work is Java's (android/app/src/update/java/io/github/tssmport/game/
// Updater.java), called here through JNI on the updater's worker thread:
//
//   - the transport is HttpsURLConnection with the system's own trusted
//     roots and its checks left on (the chain, the host name, the dates), no
//     redirect followed, the headers iUpdate.h names;
//   - the download goes to the app's private cache folder;
//   - the install is a PackageInstaller session, which Android confirms with
//     the player in its own window. Android then ends the app to replace it,
//     so there is no restart to arrange: the player opens it again.
//
// The file this copy takes from a release is the one for the process's own
// ABI: arm64-v8a "-Android.apk", armeabi-v7a "-Android-32bit.apk". The
// releases have nothing else, and CMakeLists.txt builds the updater for
// those two only.

#include "iUpdate.h"

#include <jni.h>

#include <atomic>
#include <string>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#if !defined(TSSM_UPDATE_VERSION)
#error "CMakeLists.txt sets TSSM_UPDATE_VERSION for this file"
#endif

namespace
{
#if defined(__aarch64__)
    const char kAssetSuffix[] = "-Android.apk";
#elif defined(__arm__)
    const char kAssetSuffix[] = "-Android-32bit.apk";
#else
#error "The releases have Android files for arm64-v8a and armeabi-v7a only"
#endif

    const char kUpdaterClass[] = "io.github.tssmport.game.Updater";

    // Set as the game quits (Stop, from iUpdateShutdown; AtExit): Java's
    // waits for the player or for Android give up, so the worker thread can
    // be joined.
    std::atomic<bool> sStopping{ false };

    jclass sClass; // Updater, a global reference
    jmethodID sGet;
    jmethodID sInstallBlocked;
    jmethodID sPrepareDownload;
    jmethodID sInstall;

    void Say(char* out, size_t outSize, const char* text)
    {
        if (out != NULL && outSize > 0)
        {
            snprintf(out, outSize, "%s", text);
        }
    }

    // True when a Java exception was pending: it is cleared, and its text
    // goes in `why` when there is room for it.
    bool TakeException(JNIEnv* env, char* why, size_t whySize)
    {
        if (!env->ExceptionCheck())
        {
            return false;
        }
        jthrowable e = env->ExceptionOccurred();
        env->ExceptionClear();
        Say(why, whySize, "a Java exception");
        jclass cls = env->GetObjectClass(e);
        jmethodID toString = env->GetMethodID(cls, "toString", "()Ljava/lang/String;");
        jstring text = toString != NULL ? (jstring)env->CallObjectMethod(e, toString) : NULL;
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
        }
        else if (text != NULL)
        {
            const char* utf = env->GetStringUTFChars(text, NULL);
            if (utf != NULL)
            {
                Say(why, whySize, utf);
                env->ReleaseStringUTFChars(text, utf);
            }
        }
        if (text != NULL)
        {
            env->DeleteLocalRef(text);
        }
        env->DeleteLocalRef(cls);
        env->DeleteLocalRef(e);
        return true;
    }

    // A Java string into `out`; false for null.
    bool CopyString(JNIEnv* env, jstring s, char* out, size_t outSize)
    {
        if (s == NULL)
        {
            return false;
        }
        const char* utf = env->GetStringUTFChars(s, NULL);
        if (utf == NULL)
        {
            env->ExceptionClear();
            Say(out, outSize, "");
            return true;
        }
        Say(out, outSize, utf);
        env->ReleaseStringUTFChars(s, utf);
        return true;
    }

    // The worker thread's JNIEnv (SDL attaches the thread the first time, and
    // detaches it when the thread ends), with a frame for the call's local
    // references: a native thread has no Java frame that would free them.
    struct JavaFrame
    {
        JNIEnv* env;

        JavaFrame() : env((JNIEnv*)SDL_GetAndroidJNIEnv())
        {
            if (env != NULL && (sClass == NULL || env->PushLocalFrame(16) != 0))
            {
                env->ExceptionClear();
                env = NULL;
            }
        }

        ~JavaFrame()
        {
            if (env != NULL)
            {
                env->PopLocalFrame(NULL);
            }
        }

        JavaFrame(const JavaFrame&) = delete;
        JavaFrame& operator=(const JavaFrame&) = delete;
    };

    // What Updater.get's callbacks reach: the core's sink for this request.
    struct GetCall
    {
        iUpdateSink* sink;
        std::string buffer;
    };

    GetCall* CallOf(jlong ctx)
    {
        return (GetCall*)(intptr_t)ctx;
    }

    class AndroidUpdate : public iUpdatePlatform
    {
    public:
        const char* AssetSuffix() override { return kAssetSuffix; }

        iUpdateNet Get(const iUpdateRequest& req, iUpdateSink* sink, char* detail,
                       size_t detailSize) override
        {
            JavaFrame java;
            JNIEnv* env = java.env;
            if (env == NULL)
            {
                Say(detail, detailSize, "no Java on the updater's thread");
                return IUPDATE_NET_ERROR;
            }
            GetCall call;
            call.sink = sink;
            jstring host = env->NewStringUTF(req.host);
            jstring path = env->NewStringUTF(req.path);
            jstring accept = env->NewStringUTF(req.accept);
            jstring result = NULL;
            if (host != NULL && path != NULL && accept != NULL)
            {
                result = (jstring)env->CallStaticObjectMethod(
                    sClass, sGet, (jlong)(intptr_t)&call, host, path, accept, (jint)req.timeoutMs);
            }
            if (TakeException(env, detail, detailSize) || result == NULL)
            {
                return IUPDATE_NET_ERROR;
            }

            // "<code>:<detail>", the code being iUpdateNet's.
            char text[320];
            CopyString(env, result, text, sizeof(text));
            char* end = NULL;
            const long code = strtol(text, &end, 10);
            if (end == text || code < IUPDATE_NET_OK || code > IUPDATE_NET_ERROR)
            {
                Say(detail, detailSize, text);
                return IUPDATE_NET_ERROR;
            }
            Say(detail, detailSize, *end == ':' ? end + 1 : "");
            return (iUpdateNet)code;
        }

        bool DownloadPath(char* out, size_t outSize, char* why, size_t whySize) override
        {
            JavaFrame java;
            JNIEnv* env = java.env;
            if (env == NULL)
            {
                Say(why, whySize, "The updater could not reach Android's side of the app. Nothing "
                                  "was changed.");
                return false;
            }
            jstring result = (jstring)env->CallStaticObjectMethod(sClass, sPrepareDownload);
            char text[1024] = "no answer";
            if (TakeException(env, text, sizeof(text)) || !CopyString(env, result, text, sizeof(text)))
            {
                printf("tssm: update: android: preparing the download failed: %s\n", text);
                Say(why, whySize, "The download could not be prepared. Nothing was changed.");
                return false;
            }
            // "!<why>", or the path.
            if (text[0] == '!' || text[0] != '/')
            {
                Say(why, whySize, text[0] == '!' ? text + 1 : text);
                return false;
            }
            Say(out, outSize, text);
            return strlen(text) < outSize;
        }

        bool Install(const char* path, const iUpdateVersion& version, char* why,
                     size_t whySize) override
        {
            JavaFrame java;
            JNIEnv* env = java.env;
            if (env == NULL)
            {
                Say(why, whySize, "The updater could not reach Android's side of the app. The "
                                  "installed version is unchanged.");
                return false;
            }
            char latest[32];
            iUpdateFormatVersion(version, latest, sizeof(latest));
            jstring jpath = env->NewStringUTF(path);
            jstring jversion = env->NewStringUTF(latest);
            jstring result = NULL;
            if (jpath != NULL && jversion != NULL)
            {
                result = (jstring)env->CallStaticObjectMethod(sClass, sInstall, jpath, jversion);
            }
            char text[512] = "";
            if (TakeException(env, text, sizeof(text)))
            {
                printf("tssm: update: android: the install failed: %s\n", text);
                Say(why, whySize, "Android could not install the update. The installed version is "
                                  "unchanged.");
                return false;
            }
            // null: installed (rarely seen here: Android ends the app first).
            if (result == NULL)
            {
                return true;
            }
            CopyString(env, result, why, whySize);
            return false;
        }

        void Stop() override { sStopping = true; }

        bool InstallAllowed(char* why, size_t whySize) override
        {
            JavaFrame java;
            JNIEnv* env = java.env;
            if (env == NULL)
            {
                Say(why, whySize, "The updater could not reach Android's side of the app.");
                return false;
            }
            jstring result = (jstring)env->CallStaticObjectMethod(sClass, sInstallBlocked);
            char text[256] = "";
            if (TakeException(env, text, sizeof(text)))
            {
                printf("tssm: update: android: %s\n", text);
                Say(why, whySize, "Android's side of the updater failed.");
                return false;
            }
            return !CopyString(env, result, why, whySize);
        }
    };

    // exit(), which is how the game quits on Android: the updater stops
    // before the static objects it uses are destroyed (a joinable worker
    // thread would end the process with std::terminate). iUpdateShutdown
    // calls Stop; the flag is set here as well for a core that never got
    // this platform (iUpdateInit refused the build's version).
    void AtExit()
    {
        sStopping = true;
        iUpdateShutdown();
    }

    // Updater, through the activity's class loader: on another thread
    // FindClass would search the system's, which does not have the app's
    // classes.
    bool FindUpdater(JNIEnv* env, jobject activity, char* why, size_t whySize)
    {
        jclass activityClass = env->GetObjectClass(activity);
        jmethodID getLoader =
            env->GetMethodID(activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
        jobject loader = getLoader != NULL ? env->CallObjectMethod(activity, getLoader) : NULL;
        if (TakeException(env, why, whySize) || loader == NULL)
        {
            return false;
        }
        jclass loaderClass = env->GetObjectClass(loader);
        jmethodID loadClass =
            env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        jstring name = env->NewStringUTF(kUpdaterClass);
        jclass cls = loadClass != NULL && name != NULL ?
                         (jclass)env->CallObjectMethod(loader, loadClass, name) :
                         NULL;
        if (TakeException(env, why, whySize) || cls == NULL)
        {
            return false;
        }
        sGet = env->GetStaticMethodID(
            cls, "get", "(JLjava/lang/String;Ljava/lang/String;Ljava/lang/String;I)Ljava/lang/String;");
        sInstallBlocked = env->GetStaticMethodID(cls, "installBlocked", "()Ljava/lang/String;");
        sPrepareDownload = env->GetStaticMethodID(cls, "prepareDownload", "()Ljava/lang/String;");
        sInstall = env->GetStaticMethodID(cls, "install",
                                          "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
        jmethodID init =
            env->GetStaticMethodID(cls, "init", "(Landroid/app/Activity;Ljava/lang/String;)V");
        if (TakeException(env, why, whySize) || sGet == NULL || sInstallBlocked == NULL ||
            sPrepareDownload == NULL || sInstall == NULL || init == NULL)
        {
            return false;
        }
        jstring version = env->NewStringUTF(TSSM_UPDATE_VERSION);
        env->CallStaticVoidMethod(cls, init, activity, version);
        if (TakeException(env, why, whySize))
        {
            return false;
        }
        sClass = (jclass)env->NewGlobalRef(cls);
        return sClass != NULL;
    }
} // namespace

iUpdatePlatform* iUpdateCreatePlatform()
{
    static AndroidUpdate sPlatform;
    if (sClass != NULL)
    {
        return &sPlatform;
    }

    char why[256] = "";
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    if (env == NULL || env->PushLocalFrame(32) != 0)
    {
        printf("tssm: update: android: off: no Java on this thread\n");
        fflush(stdout);
        return NULL;
    }
    jobject activity = (jobject)SDL_GetAndroidActivity();
    const bool found = activity != NULL && FindUpdater(env, activity, why, sizeof(why));
    env->PopLocalFrame(NULL);
    if (!found)
    {
        printf("tssm: update: android: off: Android's side of the updater is missing (%s)\n",
               why[0] != '\0' ? why : "no activity");
        fflush(stdout);
        return NULL;
    }
    atexit(AtExit);
    return &sPlatform;
}

// --- Updater.java's calls back -------------------------------------------------------------
//
// From Updater.get, on the worker thread (nativeOnResponse, nativeOnBody) and on
// its cancel watcher (nativeCancelled); from Java's waits (nativeStopping); and
// for Java's own lines in the game's log (nativeLog).

extern "C" JNIEXPORT jboolean JNICALL Java_io_github_tssmport_game_Updater_nativeOnResponse(
    JNIEnv* env, jclass, jlong ctx, jint status, jlong contentLength, jstring location)
{
    GetCall* call = CallOf(ctx);
    if (location == NULL)
    {
        return call->sink->OnResponse(status, contentLength, NULL) ? JNI_TRUE : JNI_FALSE;
    }
    const char* utf = env->GetStringUTFChars(location, NULL);
    if (utf == NULL)
    {
        return JNI_FALSE;
    }
    const bool wanted = call->sink->OnResponse(status, contentLength, utf);
    env->ReleaseStringUTFChars(location, utf);
    return wanted ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL Java_io_github_tssmport_game_Updater_nativeOnBody(
    JNIEnv* env, jclass, jlong ctx, jbyteArray data, jint n)
{
    GetCall* call = CallOf(ctx);
    if (n <= 0)
    {
        return JNI_TRUE;
    }
    if (n > env->GetArrayLength(data))
    {
        return JNI_FALSE;
    }
    call->buffer.resize((size_t)n);
    env->GetByteArrayRegion(data, 0, n, (jbyte*)&call->buffer[0]);
    return call->sink->OnBody(call->buffer.data(), (size_t)n) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL Java_io_github_tssmport_game_Updater_nativeCancelled(
    JNIEnv*, jclass, jlong ctx)
{
    return CallOf(ctx)->sink->Cancelled() || sStopping.load() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL Java_io_github_tssmport_game_Updater_nativeStopping(JNIEnv*,
                                                                                          jclass)
{
    return sStopping.load() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL Java_io_github_tssmport_game_Updater_nativeLog(JNIEnv* env, jclass,
                                                                                jstring line)
{
    char text[600];
    if (CopyString(env, line, text, sizeof(text)))
    {
        printf("tssm: update: android: %s\n", text);
        fflush(stdout);
    }
}

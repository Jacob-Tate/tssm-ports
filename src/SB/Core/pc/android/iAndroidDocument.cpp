// iAndroidDocument.cpp: see iAndroidDocument.h. The Java half is in
// android/app/src/main/java/io/github/tssmport/game/GameActivity.java.

#include "iAndroidDocument.h"

#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

namespace
{
    // The save picker's caller, from iAndroidShowSaveDocument (the game's
    // thread) until GameActivity answers (the UI thread).
    pthread_mutex_t sSaveLock = PTHREAD_MUTEX_INITIALIZER;
    SDL_DialogFileCallback sSaveCallback;
    void* sSaveUserdata;

    // TRUE when a Java exception is pending: it is cleared, and its text
    // ("java.lang.SecurityException: Permission Denial: ...") goes in `why`.
    bool TakeException(JNIEnv* env, char* why, size_t whySize)
    {
        if (!env->ExceptionCheck())
        {
            return false;
        }
        jthrowable e = env->ExceptionOccurred();
        env->ExceptionClear();
        snprintf(why, whySize, "Android refused it");

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
                snprintf(why, whySize, "%s", utf);
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

    // GameActivity and one of its static methods, for a call from the game's
    // thread. FindClass there would search the system's class loader, which
    // does not have the app's classes; the activity's own class is found from
    // the activity.
    struct JavaCall
    {
        JNIEnv* env;
        jobject activity;
        jclass cls;
        jmethodID method;

        JavaCall() : env(NULL), activity(NULL), cls(NULL), method(NULL)
        {
        }

        ~JavaCall()
        {
            if (cls != NULL)
            {
                env->DeleteLocalRef(cls);
            }
            if (activity != NULL)
            {
                env->DeleteLocalRef(activity);
            }
        }

        bool Begin(const char* name, const char* signature, char* why, size_t whySize)
        {
            env = (JNIEnv*)SDL_GetAndroidJNIEnv();
            if (env == NULL)
            {
                snprintf(why, whySize, "no Java on this thread");
                return false;
            }
            activity = (jobject)SDL_GetAndroidActivity();
            if (activity == NULL)
            {
                TakeException(env, why, whySize);
                snprintf(why, whySize, "the game's activity is gone");
                return false;
            }
            cls = env->GetObjectClass(activity);
            method = env->GetStaticMethodID(cls, name, signature);
            if (method == NULL)
            {
                TakeException(env, why, whySize);
                return false;
            }
            return true;
        }
    };

    // The extension `pattern` starts with: "zip" from "zip;gci".
    void FirstExtension(const char* pattern, char* out, size_t outSize)
    {
        out[0] = '\0';
        if (pattern == NULL || strcmp(pattern, "*") == 0)
        {
            return;
        }
        size_t i = 0;
        while (pattern[i] != '\0' && pattern[i] != ';' && i + 1 < outSize)
        {
            out[i] = pattern[i];
            i++;
        }
        out[i] = '\0';
    }
} // namespace

bool iAndroidIsDocument(const char* path)
{
    return path != NULL && strncmp(path, "content://", 10) == 0;
}

bool iAndroidShowSaveDocument(SDL_DialogFileCallback callback, void* userdata, const char* name,
                              const char* extension)
{
    char why[256] = "";
    JavaCall java;
    if (!java.Begin("showSaveDocument", "(Ljava/lang/String;Ljava/lang/String;)Z", why,
                    sizeof(why)))
    {
        printf("tssm: android: no save picker of the app's own (%s); SDL's instead\n", why);
        fflush(stdout);
        return false;
    }

    char ext[32];
    FirstExtension(extension, ext, sizeof(ext));

    // Set before the picker can answer.
    pthread_mutex_lock(&sSaveLock);
    sSaveCallback = callback;
    sSaveUserdata = userdata;
    pthread_mutex_unlock(&sSaveLock);

    JNIEnv* env = java.env;
    jstring jname = env->NewStringUTF(name != NULL ? name : "");
    jstring jext = env->NewStringUTF(ext);
    const bool shown =
        env->CallStaticBooleanMethod(java.cls, java.method, jname, jext) && !env->ExceptionCheck();
    if (TakeException(env, why, sizeof(why)) || !shown)
    {
        printf("tssm: android: the save picker did not open (%s); SDL's instead\n",
               why[0] != '\0' ? why : "no activity for it");
        fflush(stdout);
        pthread_mutex_lock(&sSaveLock);
        sSaveCallback = NULL;
        sSaveUserdata = NULL;
        pthread_mutex_unlock(&sSaveLock);
    }
    env->DeleteLocalRef(jext);
    env->DeleteLocalRef(jname);
    return shown;
}

// GameActivity.onActivityResult, on the UI thread: the picker's answer.
extern "C" JNIEXPORT void JNICALL
Java_io_github_tssmport_game_GameActivity_nativeSaveDocumentPicked(JNIEnv* env, jclass, jstring uri)
{
    pthread_mutex_lock(&sSaveLock);
    SDL_DialogFileCallback callback = sSaveCallback;
    void* userdata = sSaveUserdata;
    sSaveCallback = NULL;
    sSaveUserdata = NULL;
    pthread_mutex_unlock(&sSaveLock);
    if (callback == NULL)
    {
        return;
    }

    const char* utf = uri != NULL ? env->GetStringUTFChars(uri, NULL) : NULL;
    const char* files[2] = { utf, NULL }; // { NULL }: cancelled
    callback(userdata, files, -1);
    if (utf != NULL)
    {
        env->ReleaseStringUTFChars(uri, utf);
    }
}

bool iAndroidWriteDocument(const char* uri, const void* data, size_t n, char* why, size_t whySize)
{
    JavaCall java;
    if (!java.Begin("writeDocument", "(Ljava/lang/String;[B)V", why, whySize))
    {
        return false;
    }
    JNIEnv* env = java.env;
    jstring juri = env->NewStringUTF(uri);
    jbyteArray bytes = env->NewByteArray((jsize)n);
    bool ok = false;
    if (bytes != NULL)
    {
        env->SetByteArrayRegion(bytes, 0, (jsize)n, (const jbyte*)data);
        env->CallStaticVoidMethod(java.cls, java.method, juri, bytes);
        ok = !TakeException(env, why, whySize);
        env->DeleteLocalRef(bytes);
    }
    else if (!TakeException(env, why, whySize))
    {
        snprintf(why, whySize, "out of memory");
    }
    env->DeleteLocalRef(juri);
    return ok;
}

void* iAndroidReadDocument(const char* uri, size_t* n, char* why, size_t whySize)
{
    *n = 0;
    JavaCall java;
    if (!java.Begin("readDocument", "(Ljava/lang/String;)[B", why, whySize))
    {
        return NULL;
    }
    JNIEnv* env = java.env;
    jstring juri = env->NewStringUTF(uri);
    jbyteArray bytes = (jbyteArray)env->CallStaticObjectMethod(java.cls, java.method, juri);
    env->DeleteLocalRef(juri);
    if (TakeException(env, why, whySize))
    {
        return NULL;
    }
    if (bytes == NULL)
    {
        snprintf(why, whySize, "the storage app gave nothing");
        return NULL;
    }

    const jsize size = env->GetArrayLength(bytes);
    void* out = SDL_malloc(size > 0 ? (size_t)size : 1);
    if (out == NULL)
    {
        snprintf(why, whySize, "out of memory");
    }
    else
    {
        env->GetByteArrayRegion(bytes, 0, size, (jbyte*)out);
        *n = (size_t)size;
    }
    env->DeleteLocalRef(bytes);
    return out;
}

// iAndroidJava.h: a static method of GameActivity, called from the game's
// thread (iAndroidNet.cpp, iAndroidSaves.cpp). FindClass there would search
// the system's class loader, which does not have the app's classes, so the
// class is the activity's own.

#ifndef IANDROIDJAVA_H
#define IANDROIDJAVA_H

#include <jni.h>
#include <stdio.h>

#include <SDL3/SDL.h>

struct iAndroidJava
{
    JNIEnv* env;
    jobject activity;
    jclass cls;
    jmethodID method;

    iAndroidJava() : env(NULL), activity(NULL), cls(NULL), method(NULL)
    {
    }

    ~iAndroidJava()
    {
        if (env == NULL)
        {
            return;
        }
        if (cls != NULL)
        {
            env->DeleteLocalRef(cls);
        }
        if (activity != NULL)
        {
            env->DeleteLocalRef(activity);
        }
    }

    bool Begin(const char* name, const char* signature)
    {
        env = (JNIEnv*)SDL_GetAndroidJNIEnv();
        if (env == NULL)
        {
            return false;
        }
        activity = (jobject)SDL_GetAndroidActivity();
        if (activity == NULL)
        {
            Clear();
            return false;
        }
        cls = env->GetObjectClass(activity);
        method = env->GetStaticMethodID(cls, name, signature);
        if (method == NULL)
        {
            Clear();
            printf("tssm: android: GameActivity.%s%s is missing\n", name, signature);
            fflush(stdout);
            return false;
        }
        return true;
    }

    // TRUE when the call threw; the exception is logged and cleared.
    bool Clear()
    {
        if (env == NULL || !env->ExceptionCheck())
        {
            return false;
        }
        env->ExceptionDescribe();
        env->ExceptionClear();
        return true;
    }
};

#endif

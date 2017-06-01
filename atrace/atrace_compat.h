/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


// From platform/system/core/+/master/libcutils/include/cutils/trace.h

/**
 * The ATRACE_TAG macro can be defined before including this header to trace
 * using one of the tags defined below.  It must be defined to one of the
 * following ATRACE_TAG_* macros.  The trace tag is used to filter tracing in
 * userland to avoid some of the runtime cost of tracing when it is not desired.
 *
 * Defining ATRACE_TAG to be ATRACE_TAG_ALWAYS will result in the tracing always
 * being enabled - this should ONLY be done for debug code, as userland tracing
 * has a performance cost even when the trace is not being recorded.  Defining
 * ATRACE_TAG to be ATRACE_TAG_NEVER or leaving ATRACE_TAG undefined will result
 * in the tracing always being disabled.
 *
 * ATRACE_TAG_HAL should be bitwise ORed with the relevant tags for tracing
 * within a hardware module.  For example a camera hardware module would set:
 * #define ATRACE_TAG  (ATRACE_TAG_CAMERA | ATRACE_TAG_HAL)
 *
 * Keep these in sync with frameworks/base/core/java/android/os/Trace.java.
 */
#define ATRACE_TAG_NEVER            0       // This tag is never enabled.
#define ATRACE_TAG_ALWAYS           (1<<0)  // This tag is always enabled.
#define ATRACE_TAG_GRAPHICS         (1<<1)
#define ATRACE_TAG_INPUT            (1<<2)
#define ATRACE_TAG_VIEW             (1<<3)
#define ATRACE_TAG_WEBVIEW          (1<<4)
#define ATRACE_TAG_WINDOW_MANAGER   (1<<5)
#define ATRACE_TAG_ACTIVITY_MANAGER (1<<6)
#define ATRACE_TAG_SYNC_MANAGER     (1<<7)
#define ATRACE_TAG_AUDIO            (1<<8)
#define ATRACE_TAG_VIDEO            (1<<9)
#define ATRACE_TAG_CAMERA           (1<<10)
#define ATRACE_TAG_HAL              (1<<11)
#define ATRACE_TAG_APP              (1<<12)
#define ATRACE_TAG_RESOURCES        (1<<13)
#define ATRACE_TAG_DALVIK           (1<<14)
#define ATRACE_TAG_RS               (1<<15)
#define ATRACE_TAG_BIONIC           (1<<16)
#define ATRACE_TAG_POWER            (1<<17)
#define ATRACE_TAG_PACKAGE_MANAGER  (1<<18)
#define ATRACE_TAG_SYSTEM_SERVER    (1<<19)
#define ATRACE_TAG_DATABASE         (1<<20)
#define ATRACE_TAG_NETWORK          (1<<21)
#define ATRACE_TAG_ADB              (1<<22)
#define ATRACE_TAG_LAST             ATRACE_TAG_ADB

#define ALOGI(x) printf("%s\n", x);

// ndk: /usr/include/rs/rsCppUtils.h
#include <sys/time.h>
typedef int64_t nsecs_t;  // nano-seconds
static inline nsecs_t systemTime(int clock)
{
#if defined(HAVE_POSIX_CLOCKS)
    static const clockid_t clocks[] = {
            CLOCK_REALTIME,
            CLOCK_MONOTONIC,
            CLOCK_PROCESS_CPUTIME_ID,
            CLOCK_THREAD_CPUTIME_ID
    };
    struct timespec t;
    t.tv_sec = t.tv_nsec = 0;
    clock_gettime(clocks[clock], &t);
    return nsecs_t(t.tv_sec)*1000000000LL + t.tv_nsec;
#else
    // we don't support the clocks here.
    struct timeval t;
    t.tv_sec = t.tv_usec = 0;
    gettimeofday(&t, NULL);
    return nsecs_t(t.tv_sec)*1000000000LL + nsecs_t(t.tv_usec)*1000LL;
#endif
}

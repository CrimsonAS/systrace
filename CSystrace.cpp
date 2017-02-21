/*
 * Copyright (c) 2017 Crimson AS <info@crimson.no>
 * Author: Robin Burchell <robin.burchell@crimson.no>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "CSystrace.h"

static int systrace_trace_target = -1;

__attribute__((constructor)) void systrace_init()
{
    if (systrace_trace_target != -1)
        return; // already initialized

    systrace_trace_target = open("/sys/kernel/debug/tracing/trace_marker", O_WRONLY);
    if (systrace_trace_target == -1)
        perror("can't open /sys/kernel/debug/tracing/trace_marker");
}

__attribute__((destructor)) void systrace_deinit() {
    if (systrace_trace_target != -1) {
        close(systrace_trace_target);
        systrace_trace_target = -1;
    }
}

int systrace_should_trace(const char *module)
{
    // hack this if you want to temporarily omit some traces.
    return 1;
}

// begin and end (must nest)
// B|pid|message
// E
const int SYSTRACE_MAX_LEN = 1024;

void systrace_duration_begin(const char *module, const char *tracepoint)
{
    if (!systrace_should_trace(module))
        return;
    char buffer[SYSTRACE_MAX_LEN];
    int len = snprintf(buffer, SYSTRACE_MAX_LEN, "B|%i|%s", getpid(), tracepoint);
    write(systrace_trace_target, buffer, len);
}

void systrace_duration_end(const char *module, const char *tracepoint)
{
    if (!systrace_should_trace(module))
        return;
    char buffer[SYSTRACE_MAX_LEN];
    int len = snprintf(buffer, SYSTRACE_MAX_LEN, "E|%i|%s", getpid(), tracepoint);
    write(systrace_trace_target, buffer, len);
}

void systrace_record_counter(const char *module, const char *tracepoint, int value)
{
    if (!systrace_should_trace(module))
        return;
    char buffer[SYSTRACE_MAX_LEN];
    int len = snprintf(buffer, SYSTRACE_MAX_LEN, "C|%i|%s-%i", getpid(), tracepoint, value);
    write(systrace_trace_target, buffer, len);
}


// async range:
// S|pid|msg|cookie
// F|pid|msg|cookie

void systrace_async_begin(const char *module, const char *tracepoint, const void *cookie)
{
    if (!systrace_should_trace(module))
        return;
    char buffer[SYSTRACE_MAX_LEN];
    int len = snprintf(buffer, SYSTRACE_MAX_LEN, "S|%i|%s|%p", getpid(), tracepoint, cookie);
    write(systrace_trace_target, buffer, len);
}

void systrace_async_end(const char *module, const char *tracepoint, const void *cookie)
{
    if (!systrace_should_trace(module))
        return;
    char buffer[SYSTRACE_MAX_LEN];
    int len = snprintf(buffer, SYSTRACE_MAX_LEN, "F|%i|%s|%p", getpid(), tracepoint, cookie);
    write(systrace_trace_target, buffer, len);
}


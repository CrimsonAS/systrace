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

#include <sys/time.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "CSystrace.h"
#include "CTraceMessages.h"

// Information about SHM chunks
const int ShmChunkSize = 1024 * 10;
static thread_local int shm_fd = -1;
static thread_local char *shm_ptr = 0;
static thread_local const char *current_chunk_name = 0;

// FD to communicate with traced
static int traced_fd = 0;

//gettid(); except that mac sucks
#include <unistd.h> // syscall()
#include <sys/syscall.h> // SYS_thread_selfid

static int gettid()
{
    return syscall(SYS_thread_selfid);
}

/*!
 * Make sure we have a valid SHM chunk to write events to, or abort if not.
 */
static void ensure_chunk()
{
    if (shm_fd != -1)
        return;

    // ### linux via /dev/shm or memfd_create
    // ### pick a name based on process name + an incrementing id
    current_chunk_name = strdup("testchunk"); //tempnam("/tmp/", "trace");
    shm_unlink(current_chunk_name);
    shm_fd = shm_open(current_chunk_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (shm_fd == -1) {
        perror("Can't open SHM!");
        abort();
    }
    if (ftruncate(shm_fd, ShmChunkSize) == -1) {
        perror("Can't ftruncate SHM!");
        abort();
    }
    shm_ptr = (char*)mmap(0, ShmChunkSize, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("Can't map SHM!");
        abort();
    }

    ChunkHeader *h = (ChunkHeader*)shm_ptr;
    h->version = TRACED_PROTOCOL_VERSION;
    h->pid = getpid();
    h->tid = gettid();
    shm_ptr += sizeof(ChunkHeader);
}

/*!
 * Send the current chunk to traced for processing. ensure_chunk will create a
 * new chunk on the next call.
 */
static void submit_chunk()
{
    if (shm_fd == -1)
        return;

    close(shm_fd);
    shm_fd = -1;
    shm_ptr = 0;

    write(traced_fd, current_chunk_name, strlen(current_chunk_name));
    free((void*)current_chunk_name);
    current_chunk_name = 0;
}

__attribute__((constructor)) void systrace_init()
{
    traced_fd = open("/tmp/traced", O_WRONLY);
    if (traced_fd == -1) {
        perror("Can't open traced command");
        abort();
    }
}

__attribute__((destructor)) void systrace_deinit()
{
    submit_chunk();
    close(traced_fd);
    traced_fd = -1;
}

int systrace_should_trace(const char *module)
{
    // hack this if you want to temporarily omit some traces.
    return 1;
}

static uint64_t getBareMicroseconds()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}
static uint64_t originalMicroseconds = getBareMicroseconds();
static uint64_t getMicroseconds()
{
    // ### use monotonic time instead
    return getBareMicroseconds() - originalMicroseconds;
}


const int SYSTRACE_MAX_LEN = 1024;

// ### use "X" events?
void systrace_duration_begin(const char *module, const char *tracepoint)
{
    if (!systrace_should_trace(module))
        return;

    ensure_chunk();
    // ### don't allow writes over the end
    int len = snprintf(shm_ptr, SYSTRACE_MAX_LEN, "B|%llu|%s\n", getMicroseconds(), tracepoint);
    // ### allocate a new chunk & send if required
    shm_ptr += len;
}

void systrace_duration_end(const char *module, const char *tracepoint)
{
    if (!systrace_should_trace(module))
        return;
    // ### don't allow writes over the end
    int len = snprintf(shm_ptr, SYSTRACE_MAX_LEN, "E|%llu|%s\n", getMicroseconds(), tracepoint);
    // ### allocate a new chunk & send if required
    shm_ptr += len;
}

void systrace_record_counter(const char *module, const char *tracepoint, int value)
{
    if (!systrace_should_trace(module))
        return;
    // ### don't allow writes over the end
    int len = snprintf(shm_ptr, SYSTRACE_MAX_LEN, "C|%llu|%s|%i\n", getMicroseconds(), tracepoint, value);
    // ### allocate a new chunk & send if required
    shm_ptr += len;
}

void systrace_async_begin(const char *module, const char *tracepoint, const void *cookie)
{
    if (!systrace_should_trace(module))
        return;
    // ### don't allow writes over the end
    int len = snprintf(shm_ptr, SYSTRACE_MAX_LEN, "S|%llu|%s|%p\n", getMicroseconds(), tracepoint, cookie);
    // ### allocate a new chunk & send if required
    shm_ptr += len;
}

void systrace_async_end(const char *module, const char *tracepoint, const void *cookie)
{
    if (!systrace_should_trace(module))
        return;
    // ### don't allow writes over the end
    int len = snprintf(shm_ptr, SYSTRACE_MAX_LEN, "F|%llu|%s|%p\n", getMicroseconds(), tracepoint, cookie);
    // ### allocate a new chunk & send if required
    shm_ptr += len;
}



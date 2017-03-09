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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
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
static thread_local char *current_chunk_name = 0;

// How much of the SHM chunk for this thread is left, in bytes?
static thread_local int remainingChunkSize;

// FD to communicate with traced
static int traced_fd = -1;

// Each thread registers unique strings as it comes across them here and sends a
// registration message to traced.
#include <unordered_map>
static uint64_t currentStringId; // ### not thread-local! racey! fix registration
static thread_local std::unordered_map<const char *, uint64_t> registeredStrings;

//gettid(); except that mac sucks
#include <unistd.h> // syscall()
#include <sys/syscall.h> // SYS_thread_selfid

static int gettid()
{
    return syscall(SYS_thread_selfid);
}

/*! Update the book keeping for the current position in the chunk.
 */
static void advance_chunk(int len)
{
    shm_ptr += len;
    remainingChunkSize -= len;
    assert(remainingChunkSize >= 0);
}

/*!
 * Send the current chunk to traced for processing.
 */
static void submit_chunk()
{
    if (shm_fd == -1)
        return;

    shutdown(shm_fd, SHUT_WR);
    close(shm_fd);
    shm_fd = -1;
    shm_ptr = 0;

    char buf[1024];
    int blen = sprintf(buf, "%s\n", current_chunk_name);
    printf("TID %d sending %s", gettid(), buf);
    int ret = write(traced_fd, buf, blen);
    if (ret == -1) {
        perror("Can't write to traced!");
    }
    
    free((void*)current_chunk_name);
    current_chunk_name = 0;
}

// How many SHM chunks have we allocated?
// ### this needs to be thread safe
static int allocatedChunkCount = 0;

// When the trace started (when systrace_init was called).
// Do not modify this outside of systrace_init! It is read from multiple
// threads.
static struct timespec originalTp;


static void systrace_debug()
{
    static thread_local bool debugging = false;
    if (debugging)
        return;

    debugging = true;
    // These vars are to try avoid spurious reporting.
    static thread_local int lastRemainingChunkSize = 0;
    if (remainingChunkSize != lastRemainingChunkSize) {
        lastRemainingChunkSize = remainingChunkSize;
        systrace_record_counter("systrace",  "remainingChunkSize",  remainingChunkSize, gettid());
    }
    static uint64_t lastStringCount = 0;
    if (lastStringCount != currentStringId) {
        lastStringCount = currentStringId;
        systrace_record_counter("systrace", "registeredStringCount", currentStringId); // not thread-specific
    }
    debugging = false;
}

/*!
 * Make sure we have a valid SHM chunk to write events to, or abort if not.
 */
static void ensure_chunk(int mlen)
{
    if (shm_fd != -1 && remainingChunkSize >= mlen)
        return;

    if (shm_fd != -1) {
        submit_chunk();
    }

    // ### linux via /dev/shm or memfd_create
    // ### multiple processes!
    asprintf(&current_chunk_name, "tracechunk-%d", allocatedChunkCount++);
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
    remainingChunkSize = ShmChunkSize;

    ChunkHeader *h = (ChunkHeader*)shm_ptr;
    h->magic = TRACED_PROTOCOL_MAGIC;
    h->version = TRACED_PROTOCOL_VERSION;
    h->pid = getpid();
    h->tid = gettid();
    advance_chunk(sizeof(ChunkHeader));
}

__attribute__((constructor)) void systrace_init()
{
    if (clock_gettime(CLOCK_MONOTONIC, &originalTp) == -1) {
        perror("Can't get time");
        abort();
    }

    // ### cleanup hack
    for (int i = 0; i < 9999; ++i) {
        char buf[1024];
        sprintf(buf, "tracechunk-%d", i);
        shm_unlink(buf);
    }

    if (getenv("TRACED") == NULL) {
        traced_fd = open("/tmp/traced", O_WRONLY);

        if ((traced_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
            perror("Can't create socket for traced!");
        }

        struct sockaddr_un remote;
        remote.sun_family = AF_UNIX;
        strcpy(remote.sun_path, "/tmp/traced");
        int len = strlen(remote.sun_path) + sizeof(remote.sun_family) + 1;
        if (connect(traced_fd, (struct sockaddr *)&remote, len) == -1) {
            perror("Can't connect to traced!");
        }
    } else {
        fprintf(stderr, "Running trace daemon. Not tracing.\n");
    }
}

__attribute__((destructor)) void systrace_deinit()
{
    if (traced_fd == -1)
        return;
    submit_chunk();
    close(traced_fd);
    traced_fd = -1;
}

int systrace_should_trace(const char *module)
{
    if (traced_fd == -1)
        return 0;
    // hack this if you want to temporarily omit some traces.
    return 1;
}

static uint64_t getMicroseconds()
{
    struct timespec tp;
    if (clock_gettime(CLOCK_MONOTONIC, &tp) == -1) {
        perror("Can't get time");
        abort();
    }

    return (tp.tv_sec - originalTp.tv_sec) * 1000000 +
           (tp.tv_nsec / 1000) - (originalTp.tv_nsec / 1000);
}

// ### should make use of process id, thread id, and maybe some uniqueness too
// just in case we repeat either PIDs or TIDs.
static uint64_t getStringId(const char *string)
{
    auto it = registeredStrings.find(string);
    if (it == registeredStrings.end()) {
        uint64_t nid = currentStringId++;
        registeredStrings[string] = nid;

        int slen = strlen(string);
        assert(slen < ShmChunkSize / 100); // 102 characters, assuming 10kb
        ensure_chunk(sizeof(RegisterStringMessage) + slen);
        RegisterStringMessage *m = (RegisterStringMessage*)shm_ptr;
        m->messageType = MessageType::RegisterStringMessage;
        m->id = nid;
        m->length = slen;
        strncpy(&m->stringData, string, slen);
        advance_chunk(sizeof(RegisterStringMessage) + slen);
        systrace_debug();
        return nid;
    }

    return it->second;
}

// ### use "X" events?
void systrace_duration_begin(const char *module, const char *tracepoint)
{
    if (!systrace_should_trace(module))
        return;

    uint64_t modid = getStringId(module);
    uint64_t tpid = getStringId(tracepoint);

    ensure_chunk(sizeof(BeginMessage));
    BeginMessage *m = (BeginMessage*)shm_ptr;
    m->messageType = MessageType::BeginMessage;
    m->microseconds = getMicroseconds();
    m->categoryId = modid;
    m->tracepointId = tpid;
    advance_chunk(sizeof(BeginMessage));

    systrace_debug();
}

void systrace_duration_end(const char *module, const char *tracepoint)
{
    if (!systrace_should_trace(module))
        return;

    uint64_t modid = getStringId(module);
    uint64_t tpid = getStringId(tracepoint);

    ensure_chunk(sizeof(EndMessage));
    EndMessage *m = (EndMessage*)shm_ptr;
    m->messageType = MessageType::EndMessage;
    m->microseconds = getMicroseconds();
    m->categoryId = modid;
    m->tracepointId = tpid;
    advance_chunk(sizeof(EndMessage));

    systrace_debug();
}

void systrace_record_counter(const char *module, const char *tracepoint, int value, int id)
{
    if (!systrace_should_trace(module))
        return;

    uint64_t modid = getStringId(module);
    uint64_t tpid = getStringId(tracepoint);

    if (id == -1) {
        ensure_chunk(sizeof(CounterMessage));
        CounterMessage *m = (CounterMessage*)shm_ptr;
        m->messageType = MessageType::CounterMessage;
        m->microseconds = getMicroseconds();
        m->categoryId = modid;
        m->tracepointId = tpid;
        m->value = value;
        advance_chunk(sizeof(CounterMessage));
    } else {
        ensure_chunk(sizeof(CounterMessageWithId));
        CounterMessageWithId *m = (CounterMessageWithId*)shm_ptr;
        m->messageType = MessageType::CounterMessageWithId;
        m->microseconds = getMicroseconds();
        m->categoryId = modid;
        m->tracepointId = tpid;
        m->value = value;
        m->id = id;
        advance_chunk(sizeof(CounterMessageWithId));
    }

    systrace_debug();
}

void systrace_async_begin(const char *module, const char *tracepoint, const void *cookie)
{
    if (!systrace_should_trace(module))
        return;

    uint64_t modid = getStringId(module);
    uint64_t tpid = getStringId(tracepoint);

    ensure_chunk(sizeof(AsyncBeginMessage));
    AsyncBeginMessage *m = (AsyncBeginMessage*)shm_ptr;
    m->messageType = MessageType::AsyncBeginMessage;
    m->microseconds = getMicroseconds();
    m->categoryId = modid;
    m->tracepointId = tpid;
    m->cookie = (intptr_t)cookie;
    advance_chunk(sizeof(AsyncBeginMessage));

    systrace_debug();
}

void systrace_async_end(const char *module, const char *tracepoint, const void *cookie)
{
    if (!systrace_should_trace(module))
        return;

    uint64_t modid = getStringId(module);
    uint64_t tpid = getStringId(tracepoint);

    ensure_chunk(sizeof(AsyncEndMessage));
    AsyncEndMessage *m = (AsyncEndMessage*)shm_ptr;
    m->messageType = MessageType::AsyncEndMessage;
    m->microseconds = getMicroseconds();
    m->categoryId = modid;
    m->tracepointId = tpid;
    m->cookie = (intptr_t)cookie;
    advance_chunk(sizeof(AsyncEndMessage));

    systrace_debug();
}



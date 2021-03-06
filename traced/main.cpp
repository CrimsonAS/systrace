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

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <signal.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>

// ### remove Qt dep
#include <QDebug>
#include <QCoreApplication>
#include <QByteArray>
#include <QSocketNotifier>
#include <QObject>
#include <QProcess>
#include <QFile>

#include <unordered_map>

#include "CTraceMessages.h"

const int ShmChunkSize = 1024 * 10;
static FILE *traceOutputFile;

class TraceClient : public QObject
{
    Q_OBJECT

public:
    TraceClient(int f)
        : fd(f), ptr(nullptr), remainingChunkSize(0)
    {
        qInfo() << "New process connected on " << fd;
    }

    ~TraceClient()
    {
        qInfo() << "Process disconnected on " << fd;
        close(fd);
    }

    int fd;
    QByteArray buf;

public slots:
    void readControlSocket();
private:
    bool advanceChunk(size_t len);
    bool processChunk(const char *name);
    const char *getString(uint64_t id);

    std::unordered_map<uint64_t, std::string> registeredStrings;

    // Only valid while processing a chunk.
    char *ptr;
    size_t remainingChunkSize;
};

const char *TraceClient::getString(uint64_t id)
{
    auto it = registeredStrings.find(id);
    if (it == registeredStrings.end()) {
        return 0;
    }

    return it->second.c_str();
}

bool TraceClient::advanceChunk(size_t len)
{
    assert(len > 0);
    assert(len <= remainingChunkSize);

    if (len > remainingChunkSize) {
        qWarning() << "Advanced chunk too far for client " << this->fd;
        this->deleteLater();
        return false;
    }

    ptr += len;
    remainingChunkSize -= len;
    return true;
}

// ### this function should become a little more robust and less sloppy.
// * change asserts into runtime checks too
// * remove abort calls, instead, clean up safely and disconnect the client.
bool TraceClient::processChunk(const char *name)
{
    int shm_fd;
    char *initialPtr;

    shm_fd = shm_open(name, O_RDONLY, S_IRUSR | S_IWUSR);
    if (shm_fd == -1) {
        qWarning() << "shm_open: " << name << strerror(errno);
        return false;
    }

    // Immediately unlink
    if (shm_unlink(name) == -1) {
        qWarning() << "shm_unlink: " << strerror(errno);
        abort();
    }

    remainingChunkSize = ShmChunkSize;
    initialPtr = ptr = (char*)mmap(0, ShmChunkSize, PROT_READ, MAP_SHARED, shm_fd, 0);
    if (ptr == MAP_FAILED) {
        qWarning() << "mmap: " << strerror(errno);
        abort();
    }

    ChunkHeader *h = (ChunkHeader*)ptr;
    const uint64_t processEpoch = h->epoch;
    if (!advanceChunk(sizeof(ChunkHeader)))
        goto out;

    assert(h->magic == TRACED_PROTOCOL_MAGIC);
    assert(h->version == TRACED_PROTOCOL_VERSION);

    if (h->magic != TRACED_PROTOCOL_MAGIC || h->version != TRACED_PROTOCOL_VERSION) {
        qWarning() << "malformed chunk! magic " << h->magic
                   << " version " << h->version
                   << " epoch " << h->epoch;
        munmap(initialPtr, ShmChunkSize);
        return true;
    }

    while (remainingChunkSize) {
        MessageType mtype = (MessageType)*ptr;
        switch (mtype) {
        case MessageType::RegisterStringMessage: {
            // String registration
            assert(remainingChunkSize >= sizeof(RegisterStringMessage)); // can we read the header?
            RegisterStringMessage *m = (RegisterStringMessage*)ptr;
            assert(remainingChunkSize >= sizeof(RegisterStringMessage) + m->length); // and the whole string?
            std::string s(&m->stringData, m->length);
            registeredStrings[m->id] = s;
            if (!advanceChunk(sizeof(RegisterStringMessage) + m->length))
                goto out;
            break;
        }
        case MessageType::BeginMessage: {
            assert(remainingChunkSize >= sizeof(BeginMessage));
            BeginMessage *m = (BeginMessage*)ptr;
            fprintf(traceOutputFile, "{\"pid\":%" PRIu64 ",\"tid\":%" PRIu64 ",\"ts\":%llu,\"ph\":\"B\",\"cat\":\"%s\",\"name\":\"%s\"},\n", h->pid, h->tid, processEpoch + m->microseconds, getString(m->categoryId), getString(m->tracepointId));
            if (!advanceChunk(sizeof(BeginMessage)))
                goto out;
            break;
        }
        case MessageType::EndMessage: {
            assert(remainingChunkSize >= sizeof(EndMessage));
            EndMessage *m = (EndMessage*)ptr;
            fprintf(traceOutputFile, "{\"pid\":%" PRIu64 ",\"tid\":%" PRIu64 ",\"ts\":%llu,\"ph\":\"E\",\"cat\":\"%s\",\"name\":\"%s\"},\n", h->pid, h->tid, processEpoch + m->microseconds, getString(m->categoryId), getString(m->tracepointId));
            if (!advanceChunk(sizeof(EndMessage)))
                goto out;
            break;
        }
        case MessageType::DurationMessage: {
            assert(remainingChunkSize >= sizeof(DurationMessage));
            DurationMessage *m = (DurationMessage*)ptr;
            fprintf(traceOutputFile, "{\"pid\":%" PRIu64 ",\"tid\":%" PRIu64 ",\"ts\":%llu,\"dur\":%llu,\"ph\":\"X\",\"cat\":\"%s\",\"name\":\"%s\"},\n", h->pid, h->tid, processEpoch + m->microseconds, m->duration, getString(m->categoryId), getString(m->tracepointId));
            if (!advanceChunk(sizeof(DurationMessage)))
                goto out;
            break;
        }
        case MessageType::CounterMessage: {
            assert(remainingChunkSize >= sizeof(CounterMessage));
            CounterMessage *m = (CounterMessage*)ptr;
            fprintf(traceOutputFile, "{\"pid\":%" PRIu64 ",\"ts\":%llu,\"ph\":\"C\",\"cat\":\"%s\",\"name\":\"%s\",\"args\":{\"%s\":%" PRIu64 "}},\n", h->pid, processEpoch + m->microseconds, getString(m->categoryId), getString(m->tracepointId), getString(m->tracepointId), m->value);
            if (!advanceChunk(sizeof(CounterMessage)))
                goto out;
            break;
        }
        case MessageType::CounterMessageWithId: {
            assert(remainingChunkSize >= sizeof(CounterMessageWithId));
            CounterMessageWithId *m = (CounterMessageWithId*)ptr;
            fprintf(traceOutputFile, "{\"pid\":%" PRIu64 ",\"ts\":%llu,\"ph\":\"C\",\"cat\":\"%s\",\"name\":\"%s\",\"id\":%" PRIu64 ",\"args\":{\"%s\":%" PRIu64 "}},\n", h->pid, processEpoch + m->microseconds, getString(m->categoryId), getString(m->tracepointId), m->id, getString(m->tracepointId), m->value);
            if (!advanceChunk(sizeof(CounterMessageWithId)))
                goto out;
            break;
        }
        case MessageType::AsyncBeginMessage: {
            assert(remainingChunkSize >= sizeof(AsyncBeginMessage));
            AsyncBeginMessage *m = (AsyncBeginMessage*)ptr;
            fprintf(traceOutputFile, "{\"pid\":%" PRIu64 ",\"ts\":%llu,\"ph\":\"b\",\"cat\":\"%s\",\"name\":\"%s\",\"id\":\"%p\",\"args\":{}},\n", h->pid, processEpoch + m->microseconds, getString(m->categoryId), getString(m->tracepointId), (void*)m->cookie);
            if (!advanceChunk(sizeof(AsyncBeginMessage)))
                goto out;
            break;
        }
        case MessageType::AsyncEndMessage: {
            assert(remainingChunkSize >= sizeof(AsyncEndMessage));
            AsyncEndMessage *m = (AsyncEndMessage*)ptr;
            fprintf(traceOutputFile, "{\"pid\":%" PRIu64 ",\"ts\":%llu,\"ph\":\"e\",\"cat\":\"%s\",\"name\":\"%s\",\"id\":\"%p\",\"args\":{}},\n", h->pid, processEpoch + m->microseconds, getString(m->categoryId), getString(m->tracepointId), (void*)m->cookie);
            if (!advanceChunk(sizeof(AsyncEndMessage)))
                goto out;
            break;
        }
        case MessageType::NoMessage:
            goto out;
            break;
        default:
            qWarning() << "Unknown token " << (uint)mtype;
            this->deleteLater();
            goto out;
            break;
        }
    }

#if 0
            fprintf(traceOutputFile, 
            "{\"pid\":%" PRIu64 ",\"tid\":0,\"ts\":%llu,\"ph\":\"v\",\"cat\":\"%s\",\"name\":\"periodic_interval\",\"args\":{\"dumps\":{\"allocators\":{", h->pid, processEpoch + m->microseconds, getString(m->categoryId));
                fprintf(traceOutputFile, "\"RootCategory\":{\"attrs\":{\"size\":{\"type\":\"scalar\",\"units\":\"bytes\",\"value\":\"%06x\"}},\"guid\":\"801c8c513b1eb102\"},", rand());
                fprintf(traceOutputFile, "\"AnotherRootCategory\":{\"attrs\":{\"size\":{\"type\":\"scalar\",\"units\":\"bytes\",\"value\":\"%06x\"}},\"guid\":\"801c8c513b1eb102\"},", rand());
                fprintf(traceOutputFile, "\"AnotherRootCategory/SubCategory\":{\"attrs\":{\"size\":{\"type\":\"scalar\",\"units\":\"bytes\",\"value\":\"%06x\"}},\"guid\":\"806a715752927f85\"}", rand());
            fprintf(traceOutputFile, "},");
            fprintf(traceOutputFile, "\"allocators_graph\":[{\"importance\":0,\"source\":\"ded2e1173e7b598\",\"target\":\"95abf146875695a7\",\"type\":\"ownership\"},{\"importance\":0,\"source\":\"842bb4d00b4c8889\",\"target\":\"8cc156f029476744\",\"type\":\"ownership\"},{\"importance\":0,\"source\":\"993000ebdb6eae1d\",\"target\":\"587fda87208d4e6c\",\"type\":\"ownership\"}]");
            fprintf(traceOutputFile, ",\"level_of_detail\":\"light\",");
            fprintf(traceOutputFile, "\"process_totals\":{\"resident_set_bytes\":\"%d\"}}},\"tts\":681796,\"id\":\"%p\"},", rand(), (void*)rand());

#endif
    fflush(traceOutputFile);

out:
    munmap(initialPtr, ShmChunkSize);
    close(shm_fd);

    return true;
}

void TraceClient::readControlSocket()
{
    int lcmd;
    char cmd[1024];

    if ((lcmd = ::read(this->fd, cmd, sizeof(cmd)-1)) <= 0) {
        this->deleteLater();
        return;
    }

    buf += QByteArray(cmd, lcmd);
    QList<QByteArray> bufs = buf.split('\n');
    for (const QByteArray &abuf : bufs) {
        if (abuf.isEmpty()) {
            buf = "";
            break;
        }
        qDebug() << "Trying chunk "  << abuf;
        if (!processChunk(abuf.constData())) {
            // segment got eaten out from under us perhaps
            continue;
        }
        qDebug() << "Done chunk " << abuf;
    }
}

void sigintHandler(int signo)
{
    assert(signo == SIGINT);
    // Force a normal exit so we flush the file
    qApp->exit();
}

// Experimental.
//#define USE_ATRACE

int main(int argc, char **argv) 
{
    // Unlink all chunks on startup to prevent leaks.
    for (int i = 0; i < TRACED_MAX_SHM_CHUNKS; ++i) {
        char buf[1024];
        sprintf(buf, "tracechunk-%d", i);
        shm_unlink(buf);
    }

    struct sigaction act;
    act.sa_handler = &sigintHandler;
    sigaction(SIGINT, &act, NULL);

    QCoreApplication app(argc, argv);

#if defined(USE_ATRACE)
    QProcess traceProcess;
    QObject::connect(&traceProcess, &QProcess::readyReadStandardError, [&]() {
        fprintf(stderr, "%s", traceProcess.readAllStandardError().constData());
    });

    traceProcess.start("./atrace/atrace", QStringList() << "-b" << "10240" << "-c" << "-t" << "20"
        << "sched"
        //<< "irq"
        //<< "i2c"
        << "freq"
        //<< "membus"
        << "idle"
        //<< "disk"
        //<< "mmc"
        //<< "load"
        //<< "sync"
        //<< "workq"
        //<< "memreclaim"
        //<< "regulators"
        //<< "binder_driver"
        //<< "binder_lock"
        //<< "pagecache"
    );

    if (!traceProcess.waitForStarted(1000)) {
        qWarning("Can't start trace-cmd!");
    }
#endif // USE_ATRACE

    traceOutputFile = stdout;

    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "-o")  == 0 && i < argc - 1) {
            traceOutputFile = fopen(argv[i+1], "w");
            if (traceOutputFile == NULL) {
                perror("Can't open trace file");
                exit(-1);
            }
        }
    }

    struct sockaddr_un local;
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s == -1) {
        perror("Can't create socket");
        abort();
    }

    int optval = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);

    local.sun_family = AF_UNIX;
    strcpy(local.sun_path, "/tmp/traced");
    unlink(local.sun_path);
    int len = strlen(local.sun_path) + sizeof(local.sun_family) + 1;
    if (bind(s, (struct sockaddr *)&local, len) == -1) {
        perror("Can't bind()");
        abort();
    }

    if (listen(s, 5) == -1) {
        perror("Can't listen");
        abort();
    }

    QObject::connect(new QSocketNotifier(s, QSocketNotifier::Read),
        &QSocketNotifier::activated, [s]() {
        struct sockaddr_un remote;
        int len = sizeof(struct sockaddr_un);
        int client = accept(s, (struct sockaddr*)&remote, (socklen_t *)&len);
        TraceClient *tc = new TraceClient(client);
        QSocketNotifier *csn = new QSocketNotifier(client,  QSocketNotifier::Read);
        csn->setParent(tc);
        QObject::connect(csn, 
            &QSocketNotifier::activated, tc, &TraceClient::readControlSocket);
    });

    fprintf(traceOutputFile, "{\"traceEvents\": [\n");

    int ret = app.exec();

#if defined(USE_ATRACE)
    if (traceProcess.waitForFinished(-1)) {
        qWarning("Can't stop trace-cmd!");
    }
#endif

    // Remove trailing , from traceOutputFile (-2 because there's a \n there too).
    fseek(traceOutputFile, -2, SEEK_CUR);
    fprintf(traceOutputFile, "]\n");

#if defined(USE_ATRACE)
    QByteArray out = traceProcess.readAllStandardOutput();
    out = out.replace("\n", "\\n");

    fprintf(traceOutputFile, ",\"systemTraceEvents\":\"# tracer:\\n");
    fprintf(traceOutputFile, "%s", out.constData());
    fprintf(traceOutputFile, "\"\n");
#endif

    fprintf(traceOutputFile, "}\n");

    fclose(traceOutputFile);
    return ret;
}

#include "main.moc"

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

#ifndef CTRACEMESSAGES_H
#define CTRACEMESSAGES_H

#define TRACED_MAX_SHM_CHUNKS 99999
#define TRACED_PROTOCOL_MAGIC 0xDEADBEEFBAAD
#define TRACED_PROTOCOL_VERSION 256

enum class MessageType : uint8_t
{
    NoMessage = 0,
    RegisterStringMessage = 1,
    BeginMessage = 2,
    EndMessage = 3,
    AsyncBeginMessage = 4,
    AsyncEndMessage = 5,
    CounterMessage = 6,
    CounterMessageWithId = 7
};

struct ChunkHeader
{
    uint64_t magic;
    uint16_t version;
    uint64_t pid;
    uint64_t tid;

    // when the process under trace started. traced uses this against its own start
    // time to calculate relative times.
    uint64_t epoch;
};

struct BaseMessage
{
    MessageType messageType;
};

struct RegisterStringMessage : public BaseMessage
{
    uint64_t id;
    uint8_t length;
    char stringData; // and it follows on for length bytes
};

struct BeginMessage : public BaseMessage
{
    uint64_t microseconds;
    uint16_t categoryId;
    uint64_t tracepointId;
};

struct EndMessage : public BaseMessage
{
    uint64_t microseconds;
    uint16_t categoryId;
    uint64_t tracepointId;
};

struct AsyncBeginMessage : public BaseMessage
{
    uint64_t microseconds;
    uint16_t categoryId;
    uint64_t tracepointId;
    uint64_t cookie;
};

struct AsyncEndMessage : public BaseMessage
{
    uint64_t microseconds;
    uint16_t categoryId;
    uint64_t tracepointId;
    uint64_t cookie;
};

struct CounterMessage : public BaseMessage
{
    uint64_t microseconds;
    uint16_t categoryId;
    uint64_t tracepointId;
    uint64_t value;
};

struct CounterMessageWithId : public CounterMessage
{
    uint64_t id;
};


#endif // CTRACEMESSAGES_H

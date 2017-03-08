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

#ifndef SYSTRACE_H
#define SYSTRACE_H

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(__CYGWIN__)
# if defined(BUILDING_DLL)
#  if defined(__GNUC__)
#   define SYSTRACE_EXPORT __attribute__ ((dllexport))
#  else
#   define SYSTRACE_EXPORT __declspec(dllexport)
#  endif
# else
#  if defined(__GNUC__)
#   define SYSTRACE_EXPORT __attribute__ ((dllimport))
#  else
#   define SYSTRACE_EXPORT __declspec(dllimport)
#  endif
# endif
#else
# define SYSTRACE_EXPORT __attribute__ ((visibility ("default")))
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/*!
 * Perform necessary set up. Should be called before any other functions.
 *
 * \note If your compiler supports the `constructor` attribute (gcc does),
 * then this method will be called for you. Calling it multiple times will not
 * cause trouble, however, so feel free to call it yourself.
 *
 * \sa systrace_deinit()
 */
SYSTRACE_EXPORT void systrace_init();

/*!
 * Perform necessary tear down. Should be called before termination, and no systrace
 * methods should be called after it.
 *
 * \note If your compiler supports the `destructor` attribute (gcc does),
 * then this method will be called for you. Calling it multiple times will not
 * cause trouble, however, so feel free to call it yourself.
 *
 * \sa systrace_init()
 */
SYSTRACE_EXPORT void systrace_deinit();

/*!
 * Determine whether or not a given \a module should be traced.
 * This can be used to avoid expensive setup (such as allocation of data for the
 * trace event).
 *
 * Returns 1 if the event should be traced, 0 otherwise.
 */
SYSTRACE_EXPORT int systrace_should_trace(const char *module);

/*!
 * Record the start of a duration event in a given \a module and \a tracepoint.
 *
 * \note You must call systrace_duration_end with the same parameters once done.
 *
 * \sa systrace_async_begin(), systrace_duration_end()
 */
SYSTRACE_EXPORT void systrace_duration_begin(const char *module, const char *tracepoint);

/*!
 * Record the end of a duration event in a given \a module and \a tracepoint.
 *
 * \note A call to this must have been preceeded by a systrace_duration_begin call
 * with the same parameters.
 *
 * \sa systrace_async_end()
 */
SYSTRACE_EXPORT void systrace_duration_end(const char *module, const char *tracepoint);

/*!
 * Record a counter event for the given \a module and \a tracepoint as being of
 * value \a value.
 *
 * In this particular case, \a tracepoint is most likely most useful to
 * represent a variable rather than a code location
 */
SYSTRACE_EXPORT void systrace_record_counter(const char *module, const char *tracepoint, int value);

// ### 64 bit needed?

/*!
 * Record the start of an asynchronous event for the given \a module and
 * \a tracepoint, tracking an event identified by the given \a cookie
 * (e.g. a pointer).
 *
 * \note You must call systrace_async_end with the same parameters once done.
 *
 * \sa systrace_duration_begin(), systrace_async_end()
 */
SYSTRACE_EXPORT void systrace_async_begin(const char *module, const char *tracepoint, const void *cookie);

/*!
 * Record the end of an asynchronous event for the given \a module and
 * \a tracepoint, tracking an event identified by the given \a cookie (e.g. a
 * pointer)
 *
 * \note A call to this must have been preceeded by a systrace_duration_begin call
 * with the same parameters.
 *
 * \sa systrace_async_begin()
 */
SYSTRACE_EXPORT void systrace_async_end(const char *module, const char *tracepoint, const void *cookie);

#ifdef __cplusplus
}

/*!
 * Simple C++ wrapper for a duration event.
 *
 * This is equivilent to using systrace_duration_begin() and
 * systrace_duration_end(), without the requirement to ensure
 * systrace_duration_end() is called in all exits.
 */
struct SYSTRACE_EXPORT CSystraceEvent
{
    /*!
     * \internal
     *
     * Use fromData and fromRawData instead.
     */
private:
    CSystraceEvent(const char *module, const char *tracepoint, bool ownsData)
        : m_module(module)
        , m_tracepoint(tracepoint)
        , m_ownsData(ownsData)
    {
        systrace_duration_begin(m_module, m_tracepoint);
    }

public:
    /*! Ends the tracepoint. */
    ~CSystraceEvent()
    {
        end();
    }

    /*!
     * Starts a tracepoint for \a module and \a tracepoint.
     *
     * The module & tracepoint data is copied. If you can ensure you do not need
     * this copy, use fromRawData instead (for a slight improvement in
     * efficiency).
     *
     * To stop the event, delete it (usually done by using the copy ctor &
     * letting it go out of scope).
     */
    static CSystraceEvent fromData(const char *module, const char *tracepoint)
    {
        return CSystraceEvent(strdup(module), strdup(tracepoint), true);
    }

    /*!
     * Starts a tracepoint for \a module and \a tracepoint.
     *
     * \note Ownership is not automatically taken over the provided
     * data; so you must ensure that they outlive the CSystraceEvent
     * instance, or use fromData instead (and suffer a slight performance
     * penalty).
     *
     * To stop the event, delete it (usually done by using the copy ctor &
     * letting it go out of scope).
     */
    static CSystraceEvent fromRawData(const char *module, const char *tracepoint)
    {
        return CSystraceEvent(module, tracepoint, false);
    }

    /*!
     * Ends the current tracepoint, and starts a new one with the given \a tracepoint.
     * This is useful for tracing what a function is doing over a number of steps
     * of its execution with a single CSystraceEvent which will always be ended
     * when the function returns. For instance:
     *
     * \code
     * void Foo::doThing()
     * {
     *   CSystraceEvent ev(CSystraceEvent::fromRawData("app", "Foo::doThing"));
     *   CSystraceEvent activeEv(CSystraceEvent::fromRawData("app", "loading"));
     *   load();
     *   activeEv.reset("app", "processing");
     *   process();
     * }
     * \endcode
     *
     * With this example, there will be three events recorded. "Foo::doThing"
     * for the total duration of the function, and two nested "loading" and
     * "processing" events inside it.
     *
     * \note The provided data will be copied if the CSystraceEvent was created
     * with fromData, but not if it was created with fromRawData. If you use
     * fromRawData, you must ensure that they are either statically allocated,
     * or that they outlive the CSystraceEvent instance.
     */
    void reset(const char *module, const char *tracepoint)
    {
        end();
        if (m_ownsData) {
            m_module = strdup(module);
            m_tracepoint = strdup(tracepoint);
        } else {
            m_module = module;
            m_tracepoint = tracepoint;
        }
        systrace_duration_begin(m_module, m_tracepoint);
    }

private:
    void end()
    {
        systrace_duration_end(m_module, m_tracepoint);
        if (m_ownsData) {
            free((char *)m_module);
            free((char *)m_tracepoint);
        }
    }

    const char *m_module;
    const char *m_tracepoint;
    bool m_ownsData;
};

/*!
 * Simple wrapper for an asynchronous event.
 *
 * This is equivilent to using systrace_async_begin() and
 * systrace_async_end(), without the requirement to ensure
 * systrace_async_end() is called in all exits.
 */
struct SYSTRACE_EXPORT CSystraceAsyncEvent
{
private:
    /*!
     * \internal
     *
     * Use fromData and fromRawData instead.
     */
    CSystraceAsyncEvent(const char *module, const char *tracepoint, const void *cookie, bool ownsData)
        : m_module(module)
        , m_tracepoint(tracepoint)
        , m_cookie(cookie)
        , m_ownsData(ownsData)
    {
        systrace_async_begin(m_module, m_tracepoint, m_cookie);
    }

public:
    /*! Ends the event. */
    ~CSystraceAsyncEvent()
    {
        systrace_async_end(m_module, m_tracepoint, m_cookie);
        if (m_ownsData) {
            free((char *)m_module);
            free((char *)m_tracepoint);
        }
    }

    /*!
     * Starts and returns an asynchronous event for \a module and \a tracepoint,
     * with the given unique \a cookie.
     *
     * The module & tracepoint data is copied. If you can ensure you do not need
     * this copy, use fromRawData instead (for a slight improvement in
     * efficiency).
     *
     * To stop the event, delete it.
     */
    static CSystraceAsyncEvent *fromData(const char *module, const char *tracepoint, const void *cookie)
    {
        return new CSystraceAsyncEvent((const char *)strdup(module), strdup(tracepoint), cookie, true);
    }

    /*!
     * Starts and returns an asynchronous event for \a module and \a tracepoint,
     * with the given unique \a cookie.
     *
     * \note Ownership is not automatically taken over the provided
     * data; so you must ensure that they outlive the CSystraceAsyncEvent
     * instance, or use fromData instead (and suffer a slight performance
     * penalty).
     *
     * To stop the event, delete it.
     */
    static CSystraceAsyncEvent *fromRawData(const char *module, const char *tracepoint, const void *cookie)
    {
        return new CSystraceAsyncEvent(module, tracepoint, cookie, false);
    }

private:
    const char *m_module;
    const char *m_tracepoint;
    const void *m_cookie;
    bool m_ownsData;
};
#endif

#endif // SYSTRACE_H

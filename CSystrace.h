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
void systrace_init();

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
void systrace_deinit();

/*!
 * Determine whether or not a given \a module should be traced.
 * This can be used to avoid expensive setup (such as allocation of data for the
 * trace event).
 *
 * Returns 1 if the event should be traced, 0 otherwise.
 */
int systrace_should_trace(const char *module);

/*!
 * Record the start of a duration event in a given \a module and \a tracepoint.
 *
 * \note You must call systrace_duration_end with the same parameters once done.
 *
 * \sa systrace_async_begin(), systrace_duration_end()
 */
void systrace_duration_begin(const char *module, const char *tracepoint);

/*!
 * Record the end of a duration event in a given \a module and \a tracepoint.
 *
 * \note A call to this must have been preceeded by a systrace_duration_begin call
 * with the same parameters.
 *
 * \sa systrace_async_end()
 */
void systrace_duration_end(const char *module, const char *tracepoint);

/*!
 * Record a counter event for the given \a module and \a tracepoint as being of
 * value \a value.
 *
 * In this particular case, \a tracepoint is most likely most useful to
 * represent a variable rather than a code location
 */
void systrace_record_counter(const char *module, const char *tracepoint, int value);

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
void systrace_async_begin(const char *module, const char *tracepoint, const void *cookie);

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
void systrace_async_end(const char *module, const char *tracepoint, const void *cookie);

#ifdef __cplusplus
}

/*!
 * Simple C++ wrapper for a duration event.
 *
 * This is equivilent to using systrace_duration_begin() and
 * systrace_duration_end(), without the requirement to ensure
 * systrace_duration_end() is called in all exits.
 */
struct CSystraceEvent
{
    /*!
     * Starts a tracepoint for \a module and \a tracepoint.
     *
     * \note Ownership is not automatically taken over the provided
     * data; so you must ensure that they are either statically allocated,
     * or that they outlive the CSystraceEvent instance.
     */
    CSystraceEvent(const char *module, const char *tracepoint)
        : m_module(module)
        , m_tracepoint(tracepoint)
    {
        systrace_duration_begin(m_module, m_tracepoint);
    }

    /*! Ends the tracepoint. */
    ~CSystraceEvent()
    {
        end();
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
     *   CSystraceEvent ev("app", "Foo::doThing");
     *   CSystraceEvent activeEv("app", "loading");
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
     * \note Ownership is not automatically taken over the provided
     * data; so you must ensure that they are either statically allocated,
     * or that they outlive the CSystraceEvent instance.
     */
    void reset(const char *module, const char *tracepoint)
    {
        end();
        m_module = module;
        m_tracepoint = tracepoint;
        systrace_duration_begin(m_module, m_tracepoint);
    }

private:
    void end()
    {
        systrace_duration_end(m_module, m_tracepoint);
    }

    const char *m_module;
    const char *m_tracepoint;
};

/*!
 * Simple wrapper for an asynchronous event.
 *
 * This is equivilent to using systrace_async_begin() and
 * systrace_async_end(), without the requirement to ensure
 * systrace_async_end() is called in all exits.
 */
struct CSystraceAsyncEvent
{
    /*!
     * Starts an asynchronous event for \a module and \a tracepoint, with the
     * given unique \a cookie.
     *
     * \note Ownership of \a module and \a tracepoint is not automatically
     * taken; so you must ensure that they are either statically allocated,
     * or that they outlive the CSystraceAsyncEvent instance.
     */
    CSystraceAsyncEvent(const char *module, const char *tracepoint, const void *cookie)
        : m_module(module)
        , m_tracepoint(tracepoint)
        , m_cookie(cookie)
    {
        systrace_async_begin(m_module, m_tracepoint, m_cookie);
    }

    /*! Ends the event. */
    ~CSystraceAsyncEvent()
    {
        systrace_async_end(m_module, m_tracepoint, m_cookie);
    }

private:
    const char *m_module;
    const char *m_tracepoint;
    const void *m_cookie;
};
#endif

#endif // SYSTRACE_H

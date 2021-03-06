//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#ifndef TF_DIAGNOSTIC_MGR_H
#define TF_DIAGNOSTIC_MGR_H


#include "pxr/base/tf/callContext.h"
#include "pxr/base/tf/copyOnWritePtr.h"
#include "pxr/base/tf/debug.h"
#include "pxr/base/tf/diagnosticLite.h"
#include "pxr/base/tf/error.h"
#include "pxr/base/tf/singleton.h"
#include "pxr/base/tf/status.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/warning.h"
#include "pxr/base/tf/weakPtr.h"
#include "pxr/base/tf/enum.h"

#include "pxr/base/arch/inttypes.h"
#include "pxr/base/arch/attributes.h"
#include "pxr/base/arch/functionLite.h"

#include <tbb/enumerable_thread_specific.h>
#include <tbb/atomic.h>

#include <boost/scoped_ptr.hpp>

#include <cstdarg>
#include <list>
#include <string>

TF_DEBUG_CODES(
    TF_LOG_STACK_TRACE_ON_ERROR,
    TF_ERROR_MARK_TRACKING
    );

class TfError;
class TfErrorMark;
template <typename T> class TfSingleton;

/*!
 * \class TfDiagnosticMgr
 * \brief Singleton class through which all errors and diagnostics pass.
 * \ingroup group_tf_Diagnostic
 */
class TfDiagnosticMgr: public TfWeakBase {
public:

    typedef TfDiagnosticMgr This;

    typedef std::list<TfError> ErrorList;
    
    /*!
     * \brief Synonym for standard STL iterator to traverse the error list.
     *
     * The error list for a thread is an STL list.  The \c ErrorIterator type is
     * an STL iterator and can be used without restriction in any way that it is
     * legal to use an STL iterator.
     *
     * Given an iterator, one accesses the error in the standard
     * STL fashion:
     * \code
     *     TfErrorMark m;
     *
     *     ... ;
     *     if (!m.IsClean()) {
     *         TfErrorMark::Iterator i;
     *         for (i = m.GetBegin(); i != m.GetEnd(); ++i) {
     *            cout << "file = " << i->GetSourceFileName()
     *                 << "line = " << i->GetSourceLineNumber() << "\n";
     *         }
     * \endcode
     */
    typedef ErrorList::iterator ErrorIterator;

    /*! 
     * \brief Returns the name of the given diagnostic code.
     */
    static std::string GetCodeName(const TfEnum &code);

    /*! \class Delegate
     * \brief One may set a delegate with the \c TfDiagnosticMgr which will be
     * called to respond to errors and diagnostics.
     */
    class Delegate : public TfWeakBase {
      public:
        virtual ~Delegate() = 0;
        //! \brief Called when a \c TfError is posted.

        virtual void IssueError(TfError const &err) = 0;
        //! \brief Called when a \c TF_FATAL_ERROR is issued (or a failed

        // \c TF_AXIOM).
        virtual void IssueFatalError(TfCallContext const &context,
                                     std::string const &msg) = 0;
        //! \brief Called when a \c TF_STATUS() is issued.
        virtual void IssueStatus(TfStatus const &status) = 0;

        //! \brief Called when a \c TF_WARNING() is issued.

        virtual void IssueWarning(TfWarning const &warning) = 0;

    protected:
        //! \brief Abort the program, but avoid the session logging mechanism.
        // This is intended to be used for fatal error cases where any
        // information has already been logged.
        void _UnhandledAbort() const;
    };
    typedef TfWeakPtr<Delegate> DelegateWeakPtr;


    //! \brief Return the singleton instance.
    static This &GetInstance() {
        return TfSingleton<This>::GetInstance();
    }

    /*! \brief Set the delegate to \a delegate.
     *
     * \a delegate will be called when diagnostics and errors are invoked.
     * Note that only one delegate may be registered in an application.  Any
     * subsequent registrations will be ignored.
     *
     * XXX: For now, we overwrite the delegate, since some tests need their
     * own delegates, yet are dependant on libDid, which creates a global delegate.
     * This is bug 3237.
     * SetDelegate will still print a warning when overwriting the delegate.
     */
    void SetDelegate(DelegateWeakPtr const &delegate);

    /*! \brief Set whether errors, warnings and status messages should be
     *        printed out to the terminal.
     */
    void SetQuiet(bool quiet) { _quiet = quiet; }
    
    /*!
     * \brief Return an iterator to the beginning of this thread's error list.
     */
    ErrorIterator GetErrorBegin() { return _errorList.local().begin(); }

    /*!
     * \brief Return an iterator to the end of this thread's error list.
     */
    ErrorIterator GetErrorEnd() { return _errorList.local().end(); }

    /*
     * Deprecated.  Do not use.  Use EraseRange() instead.
     */
    ErrorIterator EraseError(ErrorIterator i);

    /*!
     * \brief Remove all the errors in [first, last) from this thread's error
     * stream.  This should generally not be invoked directly.  Use TfErrorMark
     * instead.
     */
    ErrorIterator EraseRange(ErrorIterator first, ErrorIterator last);

    // Append an error to the list of active errors.  This is generally not
    // meant to be called by user code.  It is public so that the system which
    // translates tf errors to and from python exceptions can manage errors.
    void AppendError(TfError const &e);
    
    // If called in a main thread, this method will create a TfError,
    // append it to the error list, and pass it to the delegate.
    //
    // If called in a non-main thread, this method will print the
    // error to stderr and will not add it to the error list or pass
    // it to the delegate.
    void PostError(TfEnum errorCode, const char* errorCodeString,
        TfCallContext const &context,  
        const std::string& commentary, TfDiagnosticInfo info,
        bool quiet);
    
    // If called in a main thread, this method will create a TfError,
    // append it to the error list, and pass it to the delegate.
    //
    // If called in a non-main thread, this method will print the
    // error to stderr and will not add it to the error list or pass
    // it to the delegate.
    void PostError(const TfDiagnosticBase& diagnostic);

    // If called in a non-main thread, this method will print the warning msg
    // rather than passing it to the delegate.
    void PostWarning(TfEnum warningCode, const char *warningCodeString,
        TfCallContext const &context, std::string const &commentary,
        TfDiagnosticInfo info, bool quiet) const;

    // If called in a non-main thread, this method will print the warning msg
    // rather than passing it to the delegate.
    void PostWarning(const TfDiagnosticBase& diagnostic) const;

    // If called in a non-main thread, this method will print the status msg
    // rather than passing it to the delegate.
    void PostStatus(TfEnum statusCode, const char *statusCodeString,
        TfCallContext const &context, std::string const &commentary,
        TfDiagnosticInfo info, bool quiet) const;

    // If called in a non-main thread, this method will print the status msg
    // rather than passing it to the delegate.
    void PostStatus(const TfDiagnosticBase& diagnostic) const;

    // If called in a non-main thread, this method will print the error msg
    // and handle the fatal error itself rather than passing it to the delegate.
    void PostFatal(TfCallContext const &context, TfEnum statusCode,
                   std::string const &msg) const;

    // Return true if an instance of TfErrorMark exists in the curren thread of
    // exection, false otherwise.
    bool HasActiveErrorMark() { return _errorMarkCounts.local(); }

#if !defined(doxygen)
    /*
     * Public, but *only* meant to be used by the TF_ERROR() macro.
     */
    class ErrorHelper {
      public:
        ErrorHelper(TfCallContext const &context, TfEnum errorCode,
                    const char* errorCodeString)
            : _context(context), _errorCode(errorCode),
              _errorCodeString(errorCodeString)
        {
        }

        ErrorIterator Post(const char* fmt, ...) const
            ARCH_PRINTF_FUNCTION(2,3);

        ErrorIterator PostQuietly(const char* fmt, ...) const
            ARCH_PRINTF_FUNCTION(2,3);

        ErrorIterator Post(const std::string& msg) const;

        ErrorIterator PostWithInfo(
                const std::string& msg,
                TfDiagnosticInfo info = TfDiagnosticInfo()) const;

        ErrorIterator PostQuietly(const std::string& msg,
                            TfDiagnosticInfo info = TfDiagnosticInfo()) const;

      private:
        TfCallContext _context;
        TfEnum _errorCode;
        const char *_errorCodeString;
    };


    struct WarningHelper {
        WarningHelper(TfCallContext const &context, TfEnum warningCode,
                      const char *warningCodeString)
            : _context(context), _warningCode(warningCode),
              _warningCodeString(warningCodeString)
        {
        }

        void Post(const char* fmt, ...) const
            ARCH_PRINTF_FUNCTION(2,3);

        void PostQuietly(const char* fmt, ...) const
            ARCH_PRINTF_FUNCTION(2,3);

        void Post(const std::string &str) const;

        void PostWithInfo(
                const std::string& msg,
                TfDiagnosticInfo info = TfDiagnosticInfo()) const;

        void PostQuietly(const std::string& msg) const;

      private:
        TfCallContext _context;
        TfEnum _warningCode;
        const char *_warningCodeString;
    };

    struct StatusHelper {
        StatusHelper(TfCallContext const &context, TfEnum statusCode,
                     const char *statusCodeString)
            : _context(context), _statusCode(statusCode),
              _statusCodeString(statusCodeString)
        {
        }

        void Post(const char* fmt, ...) const
            ARCH_PRINTF_FUNCTION(2,3);

        void PostQuietly(const char* fmt, ...) const
            ARCH_PRINTF_FUNCTION(2,3);

        void Post(const std::string &str) const;

        void PostWithInfo(
                const std::string& msg,
                TfDiagnosticInfo info = TfDiagnosticInfo()) const;

        void PostQuietly(const std::string& msg) const;

      private:
        TfCallContext _context;
        TfEnum _statusCode;
        const char *_statusCodeString;
    };

    struct FatalHelper {
        FatalHelper(TfCallContext const &context, TfEnum statusCode)
            : _context(context),
              _statusCode(statusCode)
        {
        }
        
        void Post(const std::string &str) const {
            This::GetInstance().PostFatal(_context, _statusCode, str);
        }
      private:
        TfCallContext _context;
        TfEnum _statusCode;
    };

        
#endif
    
private:

    TfDiagnosticMgr();
    virtual ~TfDiagnosticMgr();
    friend class TfSingleton<This>;
    
    // Return an iterator to the first error with serial number >= mark, or the
    // past-the-end iterator, if no such errors exist.
    ErrorIterator _GetErrorMarkBegin(size_t mark, size_t *nErrors);

    // Invoked by ErrorMark ctor.
    inline void _CreateErrorMark() { ++_errorMarkCounts.local(); }

    // Invoked by ErrorMark dtor.
    inline bool _DestroyErrorMark() { return --_errorMarkCounts.local() == 0; }

    // Report an error, either via delegate or print to stderr, and issue a
    // notice if this thread of execution is the main thread.
    void _ReportError(const TfError &err);

    // Splice the errors in src into this thread's local list.  Also reassign
    // serial numbers to all the spliced errors to ensure they work correctly
    // with local error marks.
    void _SpliceErrors(ErrorList &src);

    // Helper to append pending error messages to the crash log.
    void _AppendErrorsToLogText(ErrorIterator i);

    // Helper to fully rebuild the crash log error text when errors are erased
    // from the middle.
    void _RebuildErrorLogText();

    // Helper to actually publish log text into the Arch crash handler.
    void _SetLogInfoForErrors(const std::string &logText) const;

    // Helper to write error text from all errors in [i, end) to out.
    void _AppendErrorTextToString(ErrorIterator i, std::string &out);

    // The current diagnostic delegate.
    DelegateWeakPtr _delegate;

    // Global serial number for sorting.
    tbb::atomic<size_t> _nextSerial;

    // Thread-specific error list.
    tbb::enumerable_thread_specific<ErrorList> _errorList;

    // Thread-specific diagnostic log text for pending errors.
    tbb::enumerable_thread_specific<
        boost::scoped_ptr<std::string> > _logText;

    // Thread-specific error mark counts.  Use a native key for best performance
    // here.
    tbb::enumerable_thread_specific<
        size_t, tbb::cache_aligned_allocator<size_t>,
        tbb::ets_key_per_instance> _errorMarkCounts;

    bool _quiet;

    friend class TfError;
    friend class TfErrorTransport;
    friend class TfErrorMark;
};

#endif // TF_DIAGNOSTIC_MGR_H

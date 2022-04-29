/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkPythonInterpreter.h

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/**
 * @class   svtkPythonInterpreter
 * @brief   wrapper for an embedded Python interpreter.
 *
 * svtkPythonInterpreter only offers static methods. However, there may be need
 * to register callbacks to call after the Python interpreter is initialized
 * and before the interpreter is finalized. One can register observers for
 * svtkCommand:EnterEvent and svtkCommand::ExitEvent to a local instance of
 * svtkPythonInterpreter. svtkPythonInterpreter keeps tracks of all instances and
 * invokes those events on all instances at appropriate times.
 *
 * Same is true for obtaining outputs/errors generated by Python.
 * svtkCommand::ErrorEvent and svtkCommand::SetOutputEvent will be fired with
 * calldata being const char* to the messages. Errors and output messages will
 * also be forwarded to the svtkOutputWindow singleton (via
 * `svtkOutputWindowDisplayErrorText` and `svtkOutputWindowDisplayText` calls).
 * Changing the output window temporarily (e.g. using a
 * svtkStringOutputWindow) is another way of capturing messages generated through
 * Python scripts.
 *
 * To capture stdin, especially for non-terminal applications, set CaptureStdin
 * to true. In that case svtkCommand::UpdateEvent will be fired with the calldata
 * being a reference to a svtkStdString that should be filled in with the text to
 * be passed in as the input.
 *
 * A few of the methods on this class implicitly call
 * svtkPythonInterpreter::Initialize() to ensure Python is initialized viz.
 * svtkPythonInterpreter::PyMain() and svtkPythonInterpreter::RunSimpleString().
 * These implicit initialization always calls
 * svtkPythonInterpreter::Initialize(1). If that's not what is expected,
 * ensure that you call svtkPythonInterpreter::Initialize(0) before calling such
 * methods. Refer to Py_InitializeEx() documentation for details on the
 * differences between the two.
 *
 * Notes on calling Initialize/Finalize multiple times: Although applications
 * are free to call Initialize/Finalize pairs multiple times, there are subtle
 * differences between the first Initialize and subsequence Initialize calls
 * after Finalize especially when concerning with imported modules. Refer to
 * Python docs for details. In short, modules like numpy don't continue to work
 * after a re-initialize. Hence use it with caution.
 */

#ifndef svtkPythonInterpreter_h
#define svtkPythonInterpreter_h

#include "svtkObject.h"
#include "svtkPythonInterpreterModule.h" // For export macro
#include "svtkStdString.h"               // needed for svtkStdString.

class SVTKPYTHONINTERPRETER_EXPORT svtkPythonInterpreter : public svtkObject
{
public:
  static svtkPythonInterpreter* New();
  svtkTypeMacro(svtkPythonInterpreter, svtkObject);
  void PrintSelf(ostream& os, svtkIndent indent) override;

  /**
   * Call this method to initialize Python. This has no effect if Python is
   * already initialized. Returns true if Python was initialized by this call,
   * or false if Python was already initialized.
   * Although, one can call Initialize()/Finalize() pair multiple times, Python
   * documentation warns that "Some extensions may not work properly if their
   * initialization routine is called more than once; this can happen if an
   * application calls Py_InitializeEx() and Py_Finalize() more than once."
   */
  static bool Initialize(int initsigs = 1);

  /**
   * Call this method to finalize Python. This has no effect if Python hasn't
   * been initialized already.
   */
  static void Finalize();

  /**
   * Returns true is Python is initialized.
   */
  static bool IsInitialized();

  /**
   * Set the program name. This internally calls `Py_SetProgramName`.
   * Python uses the program name to determine values for prefix and exec_prefix
   * paths that are used to locate Python standard libraries and hence call this
   * if you if you know what you are doing.
   *
   * If not explicitly overridden, `Initialize` will try to guess a good default
   * for the `Py_SetProgramName` to  help find Python standard libraries based
   * on Python libraries used to build SVTK.
   */
  static void SetProgramName(const char* programname);

  /**
   * Call this method to start the Python event loop (Py_Main()).
   * This will initialize Python if not already initialized.
   *
   * @note This function handles `--enable-bt` command line argument before
   * passing it on to the `Py_Main(..)` call. Thus, the `--enable-bt` flag is
   * also removed from the arguments passed to `Py_Main`.
   */
  static int PyMain(int argc, char** argv);

  /**
   * Developers are free to call Python C API directly. This convenience method
   * is provided to overcome an issue with the Python interpreter with handling
   * of DOS line endings.
   * This will initialize Python if not already initialized.
   * Returns 0 on success or -1 if a python exception was raised.
   */
  static int RunSimpleString(const char* script);

  /**
   * Prepends the path to the sys.path variable. If Python has been
   * initialized, this call will update the sys.path variable otherwise the same
   * will be done once Python has been initialized. The paths added are saved so
   * that if Python is initialized again (by calls to Initialize()), then these
   * paths will be re-added.
   */
  static void PrependPythonPath(const char* path);

  //@{
  /**
   * Prepend custom paths to `sys.path` after attempt to find the `landmark` using the
   * `anchor` prefix provided. If found, the path to the landmark gets added the python path
   * using `PrependPythonPath`. Applications can use this to add paths to custom modules
   * in the module search path. This is also needed for static builds to assist the
   * interpreter in locating the path to `svtk` package.
   *
   * When `add_landmark` is true, then instead of adding the path to the
   * landmark to the module search path, the successfully located landmark itself is
   * added to the module search path. This is helpful when using zip-modules,
   * for example, since in that case, the zip file itself should be added to the
   * module search path and not its location.
   */
  static void PrependPythonPath(
    const char* anchor, const char* landmark, bool add_landmark = false);
  //@}

  //@{
  /**
   * To capture stdin, especially for non-terminal applications, set CaptureStdin
   * to true. In that case svtkCommand::UpdateEvent will be fired with the calldata
   * being a reference to a svtkStdString that should be filled in with the text to
   * be passed in as the input.
   */
  static void SetCaptureStdin(bool);
  static bool GetCaptureStdin();
  //@}

  SVTK_LEGACY(static int GetPythonVerboseFlag());

  //@{
  /**
   * Get/Set the verbosity level at which svtkPythonInterpreter should generate
   * log output. Default value is `svtkLogger::VERBOSITY_TRACE`.
   */
  static void SetLogVerbosity(int);
  static int GetLogVerbosity();
  //@}

protected:
  svtkPythonInterpreter();
  ~svtkPythonInterpreter() override;

  friend struct svtkPythonStdStreamCaptureHelper;

  //@{
  /**
   * Internal methods used by Python. Don't call directly.
   */
  static void WriteStdOut(const char* txt);
  static void FlushStdOut();
  static void WriteStdErr(const char* txt);
  static void FlushStdErr();
  static svtkStdString ReadStdin();
  //@}

private:
  svtkPythonInterpreter(const svtkPythonInterpreter&) = delete;
  void operator=(const svtkPythonInterpreter&) = delete;

  static bool InitializedOnce;
  static bool CaptureStdin;
  /**
   * If true, buffer output to console and sent it to other modules at
   * the end of the operation. If false, send the output as it becomes available.
   */
  static bool ConsoleBuffering;
  //@{
  /**
   * Accumulate here output printed to console by the python interpreter.
   */
  static std::string StdErrBuffer;
  static std::string StdOutBuffer;
  //@}

  /**
   * Since svtkPythonInterpreter is often used outside CPython executable, e.g.
   * svtkpython, the default logic to locate Python standard libraries used by
   * Python (which depends on the executable path) may fail or pickup incorrect
   * Python libs. This methods address the issue by setting program name to help
   * guide Python's default prefix/exec_prefix searching logic.
   */
  static void SetupPythonPrefix();

  /**
   * Add paths to SVTK's Python modules.
   */
  static void SetupSVTKPythonPaths();

  /**
   * Verbosity level to use when logging info.
   */
  static int LogVerbosity;
};

// For tracking global interpreters
class SVTKPYTHONINTERPRETER_EXPORT svtkPythonGlobalInterpreters
{
public:
  svtkPythonGlobalInterpreters();
  ~svtkPythonGlobalInterpreters();

private:
  svtkPythonGlobalInterpreters(const svtkPythonGlobalInterpreters&) = delete;
  svtkPythonGlobalInterpreters& operator=(const svtkPythonGlobalInterpreters&) = delete;
};

// This is here to implement the Schwarz counter idiom.
static svtkPythonGlobalInterpreters svtkPythonInterpreters;

#endif
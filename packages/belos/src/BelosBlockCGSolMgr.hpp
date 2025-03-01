// @HEADER
// *****************************************************************************
//                 Belos: Block Linear Solvers Package
//
// Copyright 2004-2016 NTESS and the Belos contributors.
// SPDX-License-Identifier: BSD-3-Clause
// *****************************************************************************
// @HEADER

#ifndef BELOS_BLOCK_CG_SOLMGR_HPP
#define BELOS_BLOCK_CG_SOLMGR_HPP

/*! \file BelosBlockCGSolMgr.hpp
 *  \brief The Belos::BlockCGSolMgr provides a solver manager for the BlockCG linear solver.
*/

#include "BelosConfigDefs.hpp"
#include "BelosTypes.hpp"

#include "BelosLinearProblem.hpp"
#include "BelosSolverManager.hpp"

#include "BelosCGIter.hpp"
#include "BelosCGSingleRedIter.hpp"
#include "BelosBlockCGIter.hpp"
#include "BelosOrthoManagerFactory.hpp"
#include "BelosStatusTestMaxIters.hpp"
#include "BelosStatusTestGenResNorm.hpp"
#include "BelosStatusTestCombo.hpp"
#include "BelosStatusTestOutputFactory.hpp"
#include "BelosOutputManager.hpp"
#include "Teuchos_LAPACK.hpp"
#include "Teuchos_RCPDecl.hpp"
#ifdef BELOS_TEUCHOS_TIME_MONITOR
#  include "Teuchos_TimeMonitor.hpp"
#endif
#if defined(HAVE_TEUCHOSCORE_CXX11)
#  include <type_traits>
#endif // defined(HAVE_TEUCHOSCORE_CXX11)
#include <algorithm>

/** \example epetra/example/BlockCG/BlockCGEpetraExFile.cpp
    This is an example of how to use the Belos::BlockCGSolMgr solver manager in Epetra.
*/
/** \example tpetra/example/BlockCG/BlockCGTpetraExFile.cpp
    This is an example of how to use the Belos::BlockCGSolMgr solver manager in Tpetra.
*/
/** \example epetra/example/BlockCG/BlockPrecCGEpetraExFile.cpp
    This is an example of how to use the Belos::BlockCGSolMgr solver manager with an Ifpack preconditioner.
*/

/*! \class Belos::BlockCGSolMgr
 *
 *  \brief The Belos::BlockCGSolMgr provides a powerful and fully-featured solver manager over the CG and BlockCG linear solver.

 \ingroup belos_solver_framework

 \author Heidi Thornquist, Chris Baker, and Teri Barth
 */

namespace Belos {

  //! @name BlockCGSolMgr Exceptions
  //@{

  /** \brief BlockCGSolMgrLinearProblemFailure is thrown when the linear problem is
   * not setup (i.e. setProblem() was not called) when solve() is called.
   *
   * This std::exception is thrown from the BlockCGSolMgr::solve() method.
   *
   */
  class BlockCGSolMgrLinearProblemFailure : public BelosError {public:
    BlockCGSolMgrLinearProblemFailure(const std::string& what_arg) : BelosError(what_arg)
    {}};

  template<class ScalarType, class MV, class OP,
           const bool lapackSupportsScalarType =
           Belos::Details::LapackSupportsScalar<ScalarType>::value>
  class BlockCGSolMgr :
    public Details::SolverManagerRequiresLapack<ScalarType,MV,OP>
  {
    static const bool requiresLapack =
      Belos::Details::LapackSupportsScalar<ScalarType>::value;
    using base_type = Details::SolverManagerRequiresLapack<ScalarType, MV, OP, requiresLapack>;

  public:
    BlockCGSolMgr () :
      base_type ()
    {}
    BlockCGSolMgr (const Teuchos::RCP<LinearProblem<ScalarType,MV,OP> >& problem,
                  const Teuchos::RCP<Teuchos::ParameterList>& pl) :
      base_type ()
    {}
    virtual ~BlockCGSolMgr () = default;
  };


  // Partial specialization for ScalarType types for which
  // Teuchos::LAPACK has a valid implementation.  This contains the
  // actual working implementation of BlockCGSolMgr.
  template<class ScalarType, class MV, class OP>
  class BlockCGSolMgr<ScalarType, MV, OP, true> :
    public Details::SolverManagerRequiresLapack<ScalarType, MV, OP, true>
  {
    // This partial specialization is already chosen for those scalar types
    // that support lapack, so we don't need to have an additional compile-time
    // check that the scalar type is float/double/complex.
// #if defined(HAVE_TEUCHOSCORE_CXX11)
// #  if defined(HAVE_TEUCHOS_COMPLEX)
//     static_assert (std::is_same<ScalarType, std::complex<float> >::value ||
//                    std::is_same<ScalarType, std::complex<double> >::value ||
//                    std::is_same<ScalarType, float>::value ||
//                    std::is_same<ScalarType, double>::value,
//                    "Belos::GCRODRSolMgr: ScalarType must be one of the four "
//                    "types (S,D,C,Z) supported by LAPACK.");
// #  else
//     static_assert (std::is_same<ScalarType, float>::value ||
//                    std::is_same<ScalarType, double>::value,
//                    "Belos::GCRODRSolMgr: ScalarType must be float or double.  "
//                    "Complex arithmetic support is currently disabled.  To "
//                    "enable it, set Teuchos_ENABLE_COMPLEX=ON.");
// #  endif // defined(HAVE_TEUCHOS_COMPLEX)
// #endif // defined(HAVE_TEUCHOSCORE_CXX11)

  private:
    using MVT = MultiVecTraits<ScalarType, MV>;
    using OPT = OperatorTraits<ScalarType, MV, OP>;
    using SCT = Teuchos::ScalarTraits<ScalarType>;
    using MagnitudeType = typename Teuchos::ScalarTraits<ScalarType>::magnitudeType;
    using MT = Teuchos::ScalarTraits<MagnitudeType>;

  public:

    //! @name Constructors/Destructor
    //@{

    /*! \brief Empty constructor for BlockCGSolMgr.
     * This constructor takes no arguments and sets the default values for the solver.
     * The linear problem must be passed in using setProblem() before solve() is called on this object.
     * The solver values can be changed using setParameters().
     */
     BlockCGSolMgr();

    /*! \brief Basic constructor for BlockCGSolMgr.
     *
     * This constructor accepts the LinearProblem to be solved in addition
     * to a parameter list of options for the solver manager. These options include the following:
     *   - "Block Size" - an \c int specifying the block size to be used by the underlying block
     *                    conjugate-gradient solver. Default: 1
     *   - "Adaptive Block Size" - a \c bool specifying whether the block size can be modified
     *                             throughout the solve. Default: true
     *   - "Use Single Reduction" - a \c bool specifying whether the iteration should apply a single
     *                              reduction (only for block size of 1). Default: false
     *   - "Maximum Iterations" - an \c int specifying the maximum number of iterations the
     *                            underlying solver is allowed to perform. Default: 1000
     *   - "Convergence Tolerance" - a \c MagnitudeType specifying the level that residual norms
     *                               must reach to decide convergence. Default: 1e-8.
     *   - "Orthogonalization" - a \c std::string specifying the desired orthogonalization:
     *                           DGKS ,ICGS, and IMGS. Default: "ICGS"
     *   - "Orthogonalization Constant" - a \c MagnitudeType used by DGKS orthogonalization to
     *                                    determine whether another step of classical Gram-Schmidt
     *                                    is necessary.  Default: -1 (use DGKS default)
     *   - "Verbosity" - a sum of MsgType specifying the verbosity. Default: Belos::Errors
     *   - "Output Style" - a OutputType specifying the style of output. Default: Belos::General
     *   - "Output Stream" - a reference-counted pointer to the output stream where all
     *                       solver output is sent.  Default: Teuchos::rcp(&std::cout,false)
     *   - "Output Frequency" - an \c int specifying how often convergence information should be
     *                          outputted.  Default: -1 (never)
     *   - "Show Maximum Residual Norm Only" - a \c bool specifying whether that only the maximum
     *                                         relative residual norm is printed if convergence
     *                                         information is printed. Default: false
     *   - "Timer Label" - a \c std::string to use as a prefix for the timer labels.  Default: "Belos"
     *          \param pl [in] ParameterList with construction information
     *                  \htmlonly
     *                  <iframe src="belos_BlockCG.xml" width=100% scrolling="no" frameborder="0">
     *                  </iframe>
     *                  <hr />
     *                  \endhtmlonly
     */
    BlockCGSolMgr( const Teuchos::RCP<LinearProblem<ScalarType,MV,OP> > &problem,
                   const Teuchos::RCP<Teuchos::ParameterList> &pl );

    //! Destructor.
    virtual ~BlockCGSolMgr() = default;

    //! clone for Inverted Injection (DII)
    Teuchos::RCP<SolverManager<ScalarType, MV, OP> > clone () const override {
      return Teuchos::rcp(new BlockCGSolMgr<ScalarType,MV,OP>);
    }
    //@}

    //! @name Accessor methods
    //@{

    const LinearProblem<ScalarType,MV,OP>& getProblem() const override {
      return *problem_;
    }

    /*! \brief Get a parameter list containing the valid parameters for this object.
     */
    Teuchos::RCP<const Teuchos::ParameterList> getValidParameters() const override;

    /*! \brief Get a parameter list containing the current parameters for this object.
     */
    Teuchos::RCP<const Teuchos::ParameterList> getCurrentParameters() const override { return params_; }

    /*! \brief Return the timers for this object.
     *
     * The timers are ordered as follows:
     *   - time spent in solve() routine
     */
    Teuchos::Array<Teuchos::RCP<Teuchos::Time> > getTimers() const {
      return Teuchos::tuple(timerSolve_);
    }

    /// \brief Tolerance achieved by the last \c solve() invocation.
    ///
    /// This is the maximum over all right-hand sides' achieved
    /// convergence tolerances, and is set whether or not the solve
    /// actually managed to achieve the desired convergence tolerance.
    MagnitudeType achievedTol() const override {
      return achievedTol_;
    }

    //! Get the iteration count for the most recent call to \c solve().
    int getNumIters() const override {
      return numIters_;
    }

    /*! \brief Return whether a loss of accuracy was detected by this solver during the most current solve.
     */
    bool isLOADetected() const override { return false; }

    //@}

    //! @name Set methods
    //@{

    //! Set the linear problem that needs to be solved.
    void setProblem( const Teuchos::RCP<LinearProblem<ScalarType,MV,OP> > &problem ) override { problem_ = problem; }

    //! Set the parameters the solver manager should use to solve the linear problem.
    void setParameters( const Teuchos::RCP<Teuchos::ParameterList> &params ) override;

    //@}

    //! @name Reset methods
    //@{
    /*! \brief Performs a reset of the solver manager specified by the \c ResetType.  This informs the
     *  solver manager that the solver should prepare for the next call to solve by resetting certain elements
     *  of the iterative solver strategy.
     */
    void reset( const ResetType type ) override { if ((type & Belos::Problem) && !Teuchos::is_null(problem_)) problem_->setProblem(); }
    //@}

    //! @name Solver application methods
    //@{

    /*! \brief This method performs possibly repeated calls to the underlying linear solver's
     *         iterate() routine until the problem has been solved (as decided by the solver manager)
     *         or the solver manager decides to quit.
     *
     * This method calls BlockCGIter::iterate() or CGIter::iterate(), which will return either because a
     * specially constructed status test evaluates to ::Passed or an std::exception is thrown.
     *
     * A return from BlockCGIter::iterate() signifies one of the following scenarios:
     *    - the maximum number of iterations has been exceeded. In this scenario, the current solutions
     *      to the linear system will be placed in the linear problem and return ::Unconverged.
     *    - global convergence has been met. In this case, the current solutions to the linear system
     *      will be placed in the linear problem and the solver manager will return ::Converged
     *
     * \returns ::ReturnType specifying:
     *     - ::Converged: the linear problem was solved to the specification required by the solver manager.
     *     - ::Unconverged: the linear problem was not solved to the specification desired by the solver manager.
     */
    ReturnType solve() override;

    //@}

    /** \name Overridden from Teuchos::Describable */
    //@{

    /** \brief Method to return description of the block CG solver manager */
    std::string description() const override;

    //@}

  private:

    //! The linear problem to solve.
    Teuchos::RCP<LinearProblem<ScalarType,MV,OP> > problem_;

    //! Output manager, that handles printing of different kinds of messages.
    Teuchos::RCP<OutputManager<ScalarType> > printer_;
    //! Output stream to which the output manager prints.
    Teuchos::RCP<std::ostream> outputStream_;

    /// \brief Aggregate stopping criterion.
    ///
    /// This is an OR combination of the maximum iteration count test
    /// (\c maxIterTest_) and convergence test (\c convTest_).
    Teuchos::RCP<StatusTest<ScalarType,MV,OP> > sTest_;

    //! Maximum iteration count stopping criterion.
    Teuchos::RCP<StatusTestMaxIters<ScalarType,MV,OP> > maxIterTest_;

    //! Convergence stopping criterion.
    Teuchos::RCP<StatusTestGenResNorm<ScalarType,MV,OP> > convTest_;

    //! Output "status test" that controls all the other status tests.
    Teuchos::RCP<StatusTestOutput<ScalarType,MV,OP> > outputTest_;

    //! Orthogonalization manager.
    Teuchos::RCP<MatOrthoManager<ScalarType,MV,OP> > ortho_;

    //! Current parameter list.
    Teuchos::RCP<Teuchos::ParameterList> params_;

    //
    // Default solver parameters.
    //
    static constexpr int maxIters_default_ = 1000;
    static constexpr bool adaptiveBlockSize_default_ = true;
    static constexpr bool showMaxResNormOnly_default_ = false;
    static constexpr bool useSingleReduction_default_ = false;
    static constexpr int blockSize_default_ = 1;
    static constexpr int verbosity_default_ = Belos::Errors;
    static constexpr int outputStyle_default_ = Belos::General;
    static constexpr int outputFreq_default_ = -1;
    static constexpr const char * resNorm_default_ = "TwoNorm";
    static constexpr bool foldConvergenceDetectionIntoAllreduce_default_ = false;
    static constexpr const char * resScale_default_ = "Norm of Initial Residual";
    static constexpr const char * label_default_ = "Belos";
    static constexpr const char * orthoType_default_ = "ICGS";
    static constexpr bool assertPositiveDefiniteness_default_ = true;

    //
    // Current solver parameters and other values.
    //

    //! Convergence tolerance (read from parameter list).
    MagnitudeType convtol_;

    //! Orthogonalization parameter (read from parameter list).
    MagnitudeType orthoKappa_;

    /// \brief Tolerance achieved by the last \c solve() invocation.
    ///
    /// This is the maximum over all right-hand sides' achieved
    /// convergence tolerances, and is set whether or not the solve
    /// actually managed to achieve the desired convergence tolerance.
    MagnitudeType achievedTol_;

    //! Maximum iteration count (read from parameter list).
    int maxIters_;

    //! Number of iterations taken by the last \c solve() invocation.
    int numIters_;

    //! Current solver values
    int blockSize_, verbosity_, outputStyle_, outputFreq_;
    bool adaptiveBlockSize_, showMaxResNormOnly_, useSingleReduction_;
    std::string orthoType_, resScale_;
    bool assertPositiveDefiniteness_;
    bool foldConvergenceDetectionIntoAllreduce_;

    Teuchos::RCP<CGIterationStateBase<ScalarType, MV> > state_;

    //! Prefix label for all the timers.
    std::string label_;

    //! Solve timer.
    Teuchos::RCP<Teuchos::Time> timerSolve_;

    //! Whether or not the parameters have been set (via \c setParameters()).
    bool isSet_;
  };


// Empty Constructor
template<class ScalarType, class MV, class OP>
BlockCGSolMgr<ScalarType,MV,OP,true>::BlockCGSolMgr() :
  outputStream_(Teuchos::rcpFromRef(std::cout)),
  convtol_(DefaultSolverParameters::convTol),
  orthoKappa_(DefaultSolverParameters::orthoKappa),
  achievedTol_(Teuchos::ScalarTraits<MagnitudeType>::zero()),
  maxIters_(maxIters_default_),
  numIters_(0),
  blockSize_(blockSize_default_),
  verbosity_(verbosity_default_),
  outputStyle_(outputStyle_default_),
  outputFreq_(outputFreq_default_),
  adaptiveBlockSize_(adaptiveBlockSize_default_),
  showMaxResNormOnly_(showMaxResNormOnly_default_),
  useSingleReduction_(useSingleReduction_default_),
  orthoType_(orthoType_default_),
  resScale_(resScale_default_),
  assertPositiveDefiniteness_(assertPositiveDefiniteness_default_),
  foldConvergenceDetectionIntoAllreduce_(foldConvergenceDetectionIntoAllreduce_default_),
  label_(label_default_),
  isSet_(false)
{}


// Basic Constructor
template<class ScalarType, class MV, class OP>
BlockCGSolMgr<ScalarType,MV,OP,true>::
BlockCGSolMgr(const Teuchos::RCP<LinearProblem<ScalarType,MV,OP> > &problem,
              const Teuchos::RCP<Teuchos::ParameterList> &pl) :
  problem_(problem),
    outputStream_(Teuchos::rcpFromRef(std::cout)),
  convtol_(DefaultSolverParameters::convTol),
  orthoKappa_(DefaultSolverParameters::orthoKappa),
  achievedTol_(Teuchos::ScalarTraits<MagnitudeType>::zero()),
  maxIters_(maxIters_default_),
  numIters_(0),
  blockSize_(blockSize_default_),
  verbosity_(verbosity_default_),
  outputStyle_(outputStyle_default_),
  outputFreq_(outputFreq_default_),
  adaptiveBlockSize_(adaptiveBlockSize_default_),
  showMaxResNormOnly_(showMaxResNormOnly_default_),
  useSingleReduction_(useSingleReduction_default_),
  orthoType_(orthoType_default_),
  resScale_(resScale_default_),
  assertPositiveDefiniteness_(assertPositiveDefiniteness_default_),
  foldConvergenceDetectionIntoAllreduce_(foldConvergenceDetectionIntoAllreduce_default_),
  label_(label_default_),
  isSet_(false)
{
  TEUCHOS_TEST_FOR_EXCEPTION(problem_.is_null(), std::invalid_argument,
    "BlockCGSolMgr's constructor requires a nonnull LinearProblem instance.");

  // If the user passed in a nonnull parameter list, set parameters.
  // Otherwise, the next solve() call will use default parameters,
  // unless the user calls setParameters() first.
  if (! pl.is_null()) {
    setParameters (pl);
  }
}

template<class ScalarType, class MV, class OP>
void
BlockCGSolMgr<ScalarType,MV,OP,true>::
setParameters (const Teuchos::RCP<Teuchos::ParameterList> &params)
{
  // Create the internal parameter list if one doesn't already exist.
  if (params_ == Teuchos::null) {
    params_ = Teuchos::rcp( new Teuchos::ParameterList(*getValidParameters()) );
  }
  else {
    params->validateParameters(*getValidParameters());
  }

  // Check for maximum number of iterations
  if (params->isParameter("Maximum Iterations")) {
    maxIters_ = params->get("Maximum Iterations",maxIters_default_);

    // Update parameter in our list and in status test.
    params_->set("Maximum Iterations", maxIters_);
    if (maxIterTest_!=Teuchos::null)
      maxIterTest_->setMaxIters( maxIters_ );
  }

  // Check for blocksize
  if (params->isParameter("Block Size")) {
    blockSize_ = params->get("Block Size",blockSize_default_);
    TEUCHOS_TEST_FOR_EXCEPTION(blockSize_ <= 0, std::invalid_argument,
                       "Belos::BlockCGSolMgr: \"Block Size\" must be strictly positive.");

    // Update parameter in our list.
    params_->set("Block Size", blockSize_);
  }

  // Check if the blocksize should be adaptive
  if (params->isParameter("Adaptive Block Size")) {
    adaptiveBlockSize_ = params->get("Adaptive Block Size",adaptiveBlockSize_default_);

    // Update parameter in our list.
    params_->set("Adaptive Block Size", adaptiveBlockSize_);
  }

  // Check if the user is requesting the single-reduction version of CG (only for blocksize == 1)
  if (params->isParameter("Use Single Reduction")) {
    useSingleReduction_ = params->get("Use Single Reduction", useSingleReduction_default_);
  }

  if (params->isParameter("Fold Convergence Detection Into Allreduce")) {
    foldConvergenceDetectionIntoAllreduce_ = params->get("Fold Convergence Detection Into Allreduce",
                                                         foldConvergenceDetectionIntoAllreduce_default_);
  }

  // Check to see if the timer label changed.
  if (params->isParameter("Timer Label")) {
    std::string tempLabel = params->get("Timer Label", label_default_);

    // Update parameter in our list and solver timer
    if (tempLabel != label_) {
      label_ = tempLabel;
      params_->set("Timer Label", label_);
      std::string solveLabel = label_ + ": BlockCGSolMgr total solve time";
#ifdef BELOS_TEUCHOS_TIME_MONITOR
      timerSolve_ = Teuchos::TimeMonitor::getNewCounter(solveLabel);
#endif
      if (ortho_ != Teuchos::null) {
        ortho_->setLabel( label_ );
      }
    }
  }

  // Check for a change in verbosity level
  if (params->isParameter("Verbosity")) {
    if (Teuchos::isParameterType<int>(*params,"Verbosity")) {
      verbosity_ = params->get("Verbosity", verbosity_default_);
    } else {
      verbosity_ = (int)Teuchos::getParameter<Belos::MsgType>(*params,"Verbosity");
    }

    // Update parameter in our list.
    params_->set("Verbosity", verbosity_);
    if (printer_ != Teuchos::null)
      printer_->setVerbosity(verbosity_);
  }

  // Check for a change in output style
  if (params->isParameter("Output Style")) {
    if (Teuchos::isParameterType<int>(*params,"Output Style")) {
      outputStyle_ = params->get("Output Style", outputStyle_default_);
    } else {
      outputStyle_ = (int)Teuchos::getParameter<Belos::OutputType>(*params,"Output Style");
    }

    // Update parameter in our list.
    params_->set("Output Style", outputStyle_);
    outputTest_ = Teuchos::null;
  }

  // output stream
  if (params->isParameter("Output Stream")) {
    outputStream_ = Teuchos::getParameter<Teuchos::RCP<std::ostream> >(*params,"Output Stream");

    // Update parameter in our list.
    params_->set("Output Stream", outputStream_);
    if (printer_ != Teuchos::null)
      printer_->setOStream( outputStream_ );
  }

  // frequency level
  if (verbosity_ & Belos::StatusTestDetails) {
    if (params->isParameter("Output Frequency")) {
      outputFreq_ = params->get("Output Frequency", outputFreq_default_);
    }

    // Update parameter in out list and output status test.
    params_->set("Output Frequency", outputFreq_);
    if (outputTest_ != Teuchos::null)
      outputTest_->setOutputFrequency( outputFreq_ );
  }

  // Create output manager if we need to.
  if (printer_ == Teuchos::null) {
    printer_ = Teuchos::rcp( new OutputManager<ScalarType>(verbosity_, outputStream_) );
  }

  // Check if the orthogonalization changed.
  bool changedOrthoType = false;
  if (params->isParameter("Orthogonalization")) {
    std::string tempOrthoType = params->get("Orthogonalization",orthoType_default_);
    if (tempOrthoType != orthoType_) {
      orthoType_ = tempOrthoType;
      changedOrthoType = true;
    }
  }
  params_->set("Orthogonalization", orthoType_);

  // Check which orthogonalization constant to use.
  if (params->isParameter("Orthogonalization Constant")) {
    if (params->isType<MagnitudeType> ("Orthogonalization Constant")) {
      orthoKappa_ = params->get ("Orthogonalization Constant",
                                 static_cast<MagnitudeType> (DefaultSolverParameters::orthoKappa));
    }
    else {
      orthoKappa_ = params->get ("Orthogonalization Constant",
                                 DefaultSolverParameters::orthoKappa);
    }

    // Update parameter in our list.
    params_->set("Orthogonalization Constant",orthoKappa_);
    if (orthoType_=="DGKS") {
      if (orthoKappa_ > 0 && ortho_ != Teuchos::null && !changedOrthoType) {
        Teuchos::rcp_dynamic_cast<DGKSOrthoManager<ScalarType,MV,OP> >(ortho_)->setDepTol( orthoKappa_ );
      }
    }
  }

  // Create orthogonalization manager if we need to.
  if (ortho_ == Teuchos::null || changedOrthoType) {
    Belos::OrthoManagerFactory<ScalarType, MV, OP> factory;
    Teuchos::RCP<Teuchos::ParameterList> paramsOrtho;
    if (orthoType_=="DGKS" && orthoKappa_ > 0) {
      paramsOrtho = Teuchos::rcp(new Teuchos::ParameterList());
      paramsOrtho->set ("depTol", orthoKappa_ );
    }

    ortho_ = factory.makeMatOrthoManager (orthoType_, Teuchos::null, printer_, label_, paramsOrtho);
  }

  // Convergence
  using StatusTestCombo_t = Belos::StatusTestCombo<ScalarType, MV, OP>;
  using StatusTestResNorm_t = Belos::StatusTestGenResNorm<ScalarType, MV, OP>;

  // Check for convergence tolerance
  if (params->isParameter("Convergence Tolerance")) {
    if (params->isType<MagnitudeType> ("Convergence Tolerance")) {
      convtol_ = params->get ("Convergence Tolerance",
                              static_cast<MagnitudeType> (DefaultSolverParameters::convTol));
    }
    else {
      convtol_ = params->get ("Convergence Tolerance", DefaultSolverParameters::convTol);
    }

    // Update parameter in our list and residual tests.
    params_->set("Convergence Tolerance", convtol_);
    if (convTest_ != Teuchos::null)
      convTest_->setTolerance( convtol_ );
  }

  if (params->isParameter("Show Maximum Residual Norm Only")) {
    showMaxResNormOnly_ = Teuchos::getParameter<bool>(*params,"Show Maximum Residual Norm Only");

    // Update parameter in our list and residual tests
    params_->set("Show Maximum Residual Norm Only", showMaxResNormOnly_);
    if (convTest_ != Teuchos::null)
      convTest_->setShowMaxResNormOnly( showMaxResNormOnly_ );
  }

  // Check for a change in scaling, if so we need to build new residual tests.
  bool newResTest = false;
  {
    std::string tempResScale = resScale_;
    if (params->isParameter ("Implicit Residual Scaling")) {
      tempResScale = params->get<std::string> ("Implicit Residual Scaling");
    }

    // Only update the scaling if it's different.
    if (resScale_ != tempResScale) {
      const Belos::ScaleType resScaleType =
        convertStringToScaleType (tempResScale);
      resScale_ = tempResScale;

      // Update parameter in our list and residual tests
      params_->set ("Implicit Residual Scaling", resScale_);

      if (! convTest_.is_null ()) {
        try {
          NormType normType = Belos::TwoNorm;
          if (params->isParameter("Residual Norm")) {
            if (params->isType<std::string> ("Residual Norm")) {
              normType = convertStringToNormType(params->get<std::string> ("Residual Norm"));
            }
          }
          convTest_->defineResForm(StatusTestResNorm_t::Implicit, normType);
          convTest_->defineScaleForm (resScaleType, Belos::TwoNorm);
        }
        catch (std::exception& e) {
          // Make sure the convergence test gets constructed again.
          newResTest = true;
        }
      }
    }
  }

  // Create status tests if we need to.

  // Basic test checks maximum iterations and native residual.
  if (maxIterTest_ == Teuchos::null)
    maxIterTest_ = Teuchos::rcp( new StatusTestMaxIters<ScalarType,MV,OP>( maxIters_ ) );

  // Implicit residual test, using the native residual to determine if convergence was achieved.
  if (convTest_.is_null () || newResTest) {

    NormType normType = Belos::TwoNorm;
    if (params->isParameter("Residual Norm")) {
      if (params->isType<std::string> ("Residual Norm")) {
        normType = convertStringToNormType(params->get<std::string> ("Residual Norm"));
      }
    }

    convTest_ = rcp (new StatusTestResNorm_t (convtol_, 1, showMaxResNormOnly_));
    convTest_->defineResForm(StatusTestResNorm_t::Implicit, normType);
    convTest_->defineScaleForm (convertStringToScaleType (resScale_), Belos::TwoNorm);
  }

  if (sTest_.is_null () || newResTest)
    sTest_ = Teuchos::rcp( new StatusTestCombo_t( StatusTestCombo_t::OR, maxIterTest_, convTest_ ) );

  if (outputTest_.is_null () || newResTest) {

    // Create the status test output class.
    // This class manages and formats the output from the status test.
    StatusTestOutputFactory<ScalarType,MV,OP> stoFactory( outputStyle_ );
    outputTest_ = stoFactory.create( printer_, sTest_, outputFreq_, Passed+Failed+Undefined );

    // Set the solver string for the output test
    std::string solverDesc = " Block CG ";
    outputTest_->setSolverDesc( solverDesc );

  }

  // BelosCgIter accepts a parameter specifying whether to assert for the positivity of p^H*A*p in the CG iteration
  if (params->isParameter("Assert Positive Definiteness")) {
    assertPositiveDefiniteness_ = Teuchos::getParameter<bool>(*params,"Assert Positive Definiteness");
    params_->set("Assert Positive Definiteness", assertPositiveDefiniteness_);
  }

  // Create the timer if we need to.
  if (timerSolve_ == Teuchos::null) {
    std::string solveLabel = label_ + ": BlockCGSolMgr total solve time";
#ifdef BELOS_TEUCHOS_TIME_MONITOR
    timerSolve_ = Teuchos::TimeMonitor::getNewCounter(solveLabel);
#endif
  }

  // Inform the solver manager that the current parameters were set.
  isSet_ = true;
}


template<class ScalarType, class MV, class OP>
Teuchos::RCP<const Teuchos::ParameterList>
BlockCGSolMgr<ScalarType,MV,OP,true>::getValidParameters() const
{
  static Teuchos::RCP<const Teuchos::ParameterList> validPL;

  // Set all the valid parameters and their default values.
  if(is_null(validPL)) {
    Teuchos::RCP<Teuchos::ParameterList> pl = Teuchos::parameterList();
    pl->set("Convergence Tolerance", static_cast<MagnitudeType>(DefaultSolverParameters::convTol),
      "The relative residual tolerance that needs to be achieved by the\n"
      "iterative solver in order for the linear system to be declared converged.");
    pl->set("Maximum Iterations", static_cast<int>(maxIters_default_),
      "The maximum number of block iterations allowed for each\n"
      "set of RHS solved.");
    pl->set("Block Size", static_cast<int>(blockSize_default_),
      "The number of vectors in each block.");
    pl->set("Adaptive Block Size", static_cast<bool>(adaptiveBlockSize_default_),
      "Whether the solver manager should adapt to the block size\n"
      "based on the number of RHS to solve.");
    pl->set("Verbosity", static_cast<int>(verbosity_default_),
      "What type(s) of solver information should be outputted\n"
      "to the output stream.");
    pl->set("Output Style", static_cast<int>(outputStyle_default_),
      "What style is used for the solver information outputted\n"
      "to the output stream.");
    pl->set("Output Frequency", static_cast<int>(outputFreq_default_),
      "How often convergence information should be outputted\n"
      "to the output stream.");
    pl->set("Output Stream", Teuchos::rcpFromRef(std::cout),
      "A reference-counted pointer to the output stream where all\n"
      "solver output is sent.");
    pl->set("Show Maximum Residual Norm Only", static_cast<bool>(showMaxResNormOnly_default_),
      "When convergence information is printed, only show the maximum\n"
      "relative residual norm when the block size is greater than one.");
    pl->set("Use Single Reduction", static_cast<bool>(useSingleReduction_default_),
      "Use single reduction iteration when the block size is one.");
    pl->set("Implicit Residual Scaling", resScale_default_,
      "The type of scaling used in the residual convergence test.");
    pl->set("Timer Label", static_cast<const char *>(label_default_),
      "The string to use as a prefix for the timer labels.");
    pl->set("Orthogonalization", static_cast<const char *>(orthoType_default_),
      "The type of orthogonalization to use: DGKS, ICGS, or IMGS.");
    pl->set("Assert Positive Definiteness",static_cast<bool>(assertPositiveDefiniteness_default_),
      "Assert for positivity of p^H*A*p in CG iteration.");
    pl->set("Orthogonalization Constant",static_cast<MagnitudeType>(DefaultSolverParameters::orthoKappa),
      "The constant used by DGKS orthogonalization to determine\n"
      "whether another step of classical Gram-Schmidt is necessary.");
    pl->set("Residual Norm",static_cast<const char *>(resNorm_default_),
      "Norm used for the convergence check on the residual.");
    pl->set("Fold Convergence Detection Into Allreduce",static_cast<bool>(foldConvergenceDetectionIntoAllreduce_default_),
      "Merge the allreduce for convergence detection with the one for CG.\n"
      "This saves one all-reduce, but incurs more computation.");
    validPL = pl;
  }
  return validPL;
}


// solve()
template<class ScalarType, class MV, class OP>
ReturnType BlockCGSolMgr<ScalarType,MV,OP,true>::solve() {
  using Teuchos::RCP;
  using Teuchos::rcp;
  using Teuchos::rcp_const_cast;
  using Teuchos::rcp_dynamic_cast;

  // Set the current parameters if they were not set before.  NOTE:
  // This may occur if the user generated the solver manager with the
  // default constructor and then didn't set any parameters using
  // setParameters().
  if (!isSet_) {
    setParameters(Teuchos::parameterList(*getValidParameters()));
  }

  Teuchos::LAPACK<int,ScalarType> lapack;

  TEUCHOS_TEST_FOR_EXCEPTION( !problem_->isProblemSet(),
    BlockCGSolMgrLinearProblemFailure,
    "Belos::BlockCGSolMgr::solve(): Linear problem is not ready, setProblem() "
    "has not been called.");

  // Create indices for the linear systems to be solved.
  int startPtr = 0;
  int numRHS2Solve = MVT::GetNumberVecs( *(problem_->getRHS()) );
  int numCurrRHS = ( numRHS2Solve < blockSize_) ? numRHS2Solve : blockSize_;

  std::vector<int> currIdx, currIdx2;
  //  If an adaptive block size is allowed then only the linear
  //  systems that need to be solved are solved.  Otherwise, the index
  //  set is generated that informs the linear problem that some
  //  linear systems are augmented.
  if ( adaptiveBlockSize_ ) {
    blockSize_ = numCurrRHS;
    currIdx.resize( numCurrRHS  );
    currIdx2.resize( numCurrRHS  );
    for (int i=0; i<numCurrRHS; ++i)
      { currIdx[i] = startPtr+i; currIdx2[i]=i; }

  }
  else {
    currIdx.resize( blockSize_ );
    currIdx2.resize( blockSize_ );
    for (int i=0; i<numCurrRHS; ++i)
      { currIdx[i] = startPtr+i; currIdx2[i]=i; }
    for (int i=numCurrRHS; i<blockSize_; ++i)
      { currIdx[i] = -1; currIdx2[i] = i; }
  }

  // Inform the linear problem of the current linear system to solve.
  problem_->setLSIndex( currIdx );

  ////////////////////////////////////////////////////////////////////////////
  // Set up the parameter list for the Iteration subclass.
  Teuchos::ParameterList plist;
  plist.set("Block Size",blockSize_);

  // Reset the output status test (controls all the other status tests).
  outputTest_->reset();

  // Assume convergence is achieved, then let any failed convergence
  // set this to false.  "Innocent until proven guilty."
  bool isConverged = true;

  ////////////////////////////////////////////////////////////////////////////
  // Set up the BlockCG Iteration subclass.

  plist.set("Assert Positive Definiteness", assertPositiveDefiniteness_);

  RCP<CGIteration<ScalarType,MV,OP> > block_cg_iter;
  if (blockSize_ == 1) {
    // Standard (nonblock) CG is faster for the special case of a
    // block size of 1.  A single reduction iteration can also be used
    // if collectives are more expensive than vector updates.
    plist.set("Fold Convergence Detection Into Allreduce",
              foldConvergenceDetectionIntoAllreduce_);
    if (useSingleReduction_) {
      block_cg_iter =
        rcp (new CGSingleRedIter<ScalarType,MV,OP> (problem_, printer_,
                                                    outputTest_, convTest_, plist));
      if (state_.is_null() || Teuchos::rcp_dynamic_cast<CGSingleRedIterationState<ScalarType, MV> >(state_).is_null())
        state_ = Teuchos::rcp(new CGSingleRedIterationState<ScalarType, MV>());

    }
    else {
      block_cg_iter =
        rcp (new CGIter<ScalarType,MV,OP> (problem_, printer_,
                                           outputTest_, convTest_, plist));
      if (state_.is_null() || Teuchos::rcp_dynamic_cast<CGIterationState<ScalarType, MV> >(state_).is_null())
        state_ = Teuchos::rcp(new CGIterationState<ScalarType, MV>());
    }
  } else {
    block_cg_iter =
      rcp (new BlockCGIter<ScalarType,MV,OP> (problem_, printer_, outputTest_,
                                              ortho_, plist));
    if (state_.is_null() || Teuchos::rcp_dynamic_cast<BlockCGIterationState<ScalarType, MV> >(state_).is_null())
        state_ = Teuchos::rcp(new BlockCGIterationState<ScalarType, MV>());
  }


  // Enter solve() iterations
  {
#ifdef BELOS_TEUCHOS_TIME_MONITOR
    Teuchos::TimeMonitor slvtimer(*timerSolve_);
#endif

    while ( numRHS2Solve > 0 ) {
      //
      // Reset the active / converged vectors from this block
      std::vector<int> convRHSIdx;
      std::vector<int> currRHSIdx( currIdx );
      currRHSIdx.resize(numCurrRHS);

      // Reset the number of iterations.
      block_cg_iter->resetNumIters();

      // Reset the number of calls that the status test output knows about.
      outputTest_->resetNumCalls();

      // Get the current residual for this block of linear systems.
      RCP<MV> R_0 = MVT::CloneViewNonConst( *(rcp_const_cast<MV>(problem_->getInitResVec())), currIdx );

      // Set the new state and initialize the solver.
      block_cg_iter->initializeCG(state_, R_0);

      while(true) {

        // tell block_cg_iter to iterate
        try {
          block_cg_iter->iterate();
          //
          // Check whether any of the linear systems converged.
          //
          if (convTest_->getStatus() == Passed) {
            // At least one of the linear system(s) converged.
            //
            // Get the column indices of the linear systems that converged.
            using conv_test_type = StatusTestGenResNorm<ScalarType, MV, OP>;
            std::vector<int> convIdx =
              rcp_dynamic_cast<conv_test_type>(convTest_)->convIndices();

            // If the number of converged linear systems equals the
            // number of linear systems currently being solved, then
            // we are done with this block.
            if (convIdx.size() == currRHSIdx.size())
              break;  // break from while(1){block_cg_iter->iterate()}

            // Inform the linear problem that we are finished with
            // this current linear system.
            problem_->setCurrLS();

            // Reset currRHSIdx to contain the right-hand sides that
            // are left to converge for this block.
            int have = 0;
            std::vector<int> unconvIdx(currRHSIdx.size());
            for (unsigned int i=0; i<currRHSIdx.size(); ++i) {
              bool found = false;
              for (unsigned int j=0; j<convIdx.size(); ++j) {
                if (currRHSIdx[i] == convIdx[j]) {
                  found = true;
                  break;
                }
              }
              if (!found) {
                currIdx2[have] = currIdx2[i];
                currRHSIdx[have++] = currRHSIdx[i];
              }
              else {
              }
            }
            currRHSIdx.resize(have);
            currIdx2.resize(have);

            // Set the remaining indices after deflation.
            problem_->setLSIndex( currRHSIdx );

            // Get the current residual vector.
            std::vector<MagnitudeType> norms;
            R_0 = MVT::CloneCopy( *(block_cg_iter->getNativeResiduals(&norms)),currIdx2 );
            for (int i=0; i<have; ++i) { currIdx2[i] = i; }

            // Set the new blocksize for the solver.
            block_cg_iter->setBlockSize( have );

            // Set the new state and initialize the solver.
            block_cg_iter->initializeCG(state_, R_0);
          }
          //
          // None of the linear systems converged.  Check whether the
          // maximum iteration count was reached.
          //
          else if (maxIterTest_->getStatus() == Passed) {
            isConverged = false; // None of the linear systems converged.
            break;  // break from while(1){block_cg_iter->iterate()}
          }
          //
          // iterate() returned, but none of our status tests Passed.
          // This indicates a bug.
          //
          else {
            TEUCHOS_TEST_FOR_EXCEPTION(true,std::logic_error,
              "Belos::BlockCGSolMgr::solve(): Neither the convergence test nor "
              "the maximum iteration count test passed.  Please report this bug "
              "to the Belos developers.");
          }
        }
        catch (const StatusTestNaNError& e) {
          // A NaN was detected in the solver.  Set the solution to zero and return unconverged.
          achievedTol_ = MT::one();
          Teuchos::RCP<MV> X = problem_->getLHS();
          MVT::MvInit( *X, SCT::zero() );
          printer_->stream(Warnings) << "Belos::BlockCGSolMgr::solve(): Warning! NaN has been detected!"
                                     << std::endl;
          return Unconverged;
        }
        catch (const std::exception &e) {
          std::ostream& err = printer_->stream (Errors);
          err << "Error! Caught std::exception in CGIteration::iterate() at "
              << "iteration " << block_cg_iter->getNumIters() << std::endl
              << e.what() << std::endl;
          throw;
        }
      }

      // Inform the linear problem that we are finished with this
      // block linear system.
      problem_->setCurrLS();

      // Update indices for the linear systems to be solved.
      startPtr += numCurrRHS;
      numRHS2Solve -= numCurrRHS;
      if ( numRHS2Solve > 0 ) {
        numCurrRHS = ( numRHS2Solve < blockSize_) ? numRHS2Solve : blockSize_;

        if ( adaptiveBlockSize_ ) {
          blockSize_ = numCurrRHS;
          currIdx.resize( numCurrRHS  );
          currIdx2.resize( numCurrRHS  );
          for (int i=0; i<numCurrRHS; ++i)
            { currIdx[i] = startPtr+i; currIdx2[i] = i; }
        }
        else {
          currIdx.resize( blockSize_ );
          currIdx2.resize( blockSize_ );
          for (int i=0; i<numCurrRHS; ++i)
            { currIdx[i] = startPtr+i; currIdx2[i] = i; }
          for (int i=numCurrRHS; i<blockSize_; ++i)
            { currIdx[i] = -1; currIdx2[i] = i; }
        }
        // Set the next indices.
        problem_->setLSIndex( currIdx );

        // Set the new blocksize for the solver.
        block_cg_iter->setBlockSize( blockSize_ );
      }
      else {
        currIdx.resize( numRHS2Solve );
      }

    }// while ( numRHS2Solve > 0 )

  }

  // print final summary
  sTest_->print( printer_->stream(FinalSummary) );

  // print timing information
#ifdef BELOS_TEUCHOS_TIME_MONITOR
  // Calling summarize() requires communication in general, so don't
  // call it unless the user wants to print out timing details.
  // summarize() will do all the work even if it's passed a "black
  // hole" output stream.
  if (verbosity_ & TimingDetails) {
    Teuchos::TimeMonitor::summarize( printer_->stream(TimingDetails) );
  }
#endif

  // Save the iteration count for this solve.
  numIters_ = maxIterTest_->getNumIters();

  // Save the convergence test value ("achieved tolerance") for this solve.
  {
    using conv_test_type = StatusTestGenResNorm<ScalarType, MV, OP>;
    // testValues is nonnull and not persistent.
    const std::vector<MagnitudeType>* pTestValues =
      rcp_dynamic_cast<conv_test_type>(convTest_)->getTestValue();

    TEUCHOS_TEST_FOR_EXCEPTION(pTestValues == NULL, std::logic_error,
      "Belos::BlockCGSolMgr::solve(): The convergence test's getTestValue() "
      "method returned NULL.  Please report this bug to the Belos developers.");

    TEUCHOS_TEST_FOR_EXCEPTION(pTestValues->size() < 1, std::logic_error,
      "Belos::BlockCGSolMgr::solve(): The convergence test's getTestValue() "
      "method returned a vector of length zero.  Please report this bug to the "
      "Belos developers.");

    // FIXME (mfh 12 Dec 2011) Does pTestValues really contain the
    // achieved tolerances for all vectors in the current solve(), or
    // just for the vectors from the last deflation?
    achievedTol_ = *std::max_element (pTestValues->begin(), pTestValues->end());
  }

  if (!isConverged) {
    return Unconverged; // return from BlockCGSolMgr::solve()
  }
  return Converged; // return from BlockCGSolMgr::solve()
}

//  This method requires the solver manager to return a std::string that describes itself.
template<class ScalarType, class MV, class OP>
std::string BlockCGSolMgr<ScalarType,MV,OP,true>::description() const
{
  std::ostringstream oss;
  oss << "Belos::BlockCGSolMgr<...,"<<Teuchos::ScalarTraits<ScalarType>::name()<<">";
  oss << "{";
  oss << "Ortho Type='"<<orthoType_<<"\', Block Size=" << blockSize_;
  oss << "}";
  return oss.str();
}

} // end Belos namespace

#endif /* BELOS_BLOCK_CG_SOLMGR_HPP */

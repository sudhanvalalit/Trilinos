// @HEADER
// *****************************************************************************
//            LOCA: Library of Continuation Algorithms Package
//
// Copyright 2001-2005 NTESS and the LOCA contributors.
// SPDX-License-Identifier: BSD-3-Clause
// *****************************************************************************
// @HEADER

#include "LOCA.H"
#include "LOCA_LAPACK.H"

#include "LOCA_Eigensolver_AbstractStrategy.H"
#include "LOCA_Parameter_SublistParser.H"

#include "ChanProblemInterface.H"
#include "NOX_TestCompare.H"

#include "Teuchos_GlobalMPISession.hpp"

int main(int argc, char *argv[])
{
  Teuchos::GlobalMPISession mpi_session(&argc, &argv);

  int n = 100;
  double alpha = 0.0;
  double beta = 0.0;
  double scale = 1.0;
  int ierr = 0;
  int nev = 10;
  int narn = 11;
  double arntol = 1.0e-12;

  alpha = alpha / scale;

  try {

    bool verbose = false;
    // Check for verbose output
    if (argc>1)
      if (argv[1][0]=='-' && argv[1][1]=='v')
    verbose = true;

    // Create parameter list
    Teuchos::RCP<Teuchos::ParameterList> paramList =
      Teuchos::rcp(new Teuchos::ParameterList);

    // Create LOCA sublist
    Teuchos::ParameterList& locaParamsList = paramList->sublist("LOCA");

    // Create the stepper sublist and set the stepper parameters
    Teuchos::ParameterList& stepperList = locaParamsList.sublist("Stepper");

    // Create Anasazi Eigensolver sublist (needs --with-loca-anasazi)
    Teuchos::ParameterList& aList = stepperList.sublist("Eigensolver");
    aList.set("Method", "Anasazi");
    aList.set("Operator", "Jacobian Inverse");
    aList.set("Block Size", 1);
    aList.set("Num Blocks", narn);
    aList.set("Num Eigenvalues", nev);
    aList.set("Convergence Tolerance", arntol);
    aList.set("Step Size", 1);
    aList.set("Maximum Restarts",0);
    aList.set("Sorting Order","LM");
    if (verbose)
      aList.set("Debug Level",
        Anasazi::Errors +
        Anasazi::Warnings +
        Anasazi::FinalSummary);
    else
      aList.set("Debug Level", Anasazi::Errors);

    // Create the "Solver" parameters sublist to be used with NOX Solvers
    Teuchos::ParameterList& nlParams = paramList->sublist("NOX");

    Teuchos::ParameterList& nlPrintParams = nlParams.sublist("Printing");
    if (verbose)
       nlPrintParams.set("Output Information",
                  NOX::Utils::Error +
                  NOX::Utils::Details +
                  NOX::Utils::OuterIteration +
                  NOX::Utils::InnerIteration +
                  NOX::Utils::Warning +
                  NOX::Utils::TestDetails +
                  NOX::Utils::StepperIteration +
                  NOX::Utils::StepperDetails);
     else
       nlPrintParams.set("Output Information", NOX::Utils::Error);

    // Create LAPACK factory
    Teuchos::RCP<LOCA::Abstract::Factory> lapackFactory =
      Teuchos::rcp(new LOCA::LAPACK::Factory);

    // Create global data object
    Teuchos::RCP<LOCA::GlobalData> globalData =
      LOCA::createGlobalData(paramList, lapackFactory);

    // Create parsed parameter list
    Teuchos::RCP<LOCA::Parameter::SublistParser> parsedParams =
      Teuchos::rcp(new LOCA::Parameter::SublistParser(globalData));
    parsedParams->parseSublists(paramList);

    // Set up the problem interface
    ChanProblemInterface chan(globalData, n, alpha, beta, scale);
    LOCA::ParameterVector p;
    p.addParameter("alpha",alpha);
    p.addParameter("beta",beta);
    p.addParameter("scale",scale);

    // Create a group which uses that problem interface. The group will
    // be initialized to contain the default initial guess for the
    // specified problem.
    LOCA::LAPACK::Group grp(globalData, chan);

    grp.setParams(p);

    grp.computeF();
    grp.computeJacobian();

    // Create Anasazi eigensolver
    Teuchos::RCP<LOCA::Eigensolver::AbstractStrategy> anasaziStrategy
      = globalData->locaFactory->createEigensolverStrategy(
                     parsedParams,
                     parsedParams->getSublist("Eigensolver"));

    Teuchos::RCP< std::vector<double> > anasazi_evals_r;
    Teuchos::RCP< std::vector<double> > anasazi_evals_i;
    Teuchos::RCP< NOX::Abstract::MultiVector > anasazi_evecs_r;
    Teuchos::RCP< NOX::Abstract::MultiVector > anasazi_evecs_i;
    NOX::Abstract::Group::ReturnType anasaziStatus =
      anasaziStrategy->computeEigenvalues(grp,
                      anasazi_evals_r,
                      anasazi_evals_i,
                      anasazi_evecs_r,
                      anasazi_evecs_i);

    if (anasaziStatus != NOX::Abstract::Group::NotConverged)
      ++ierr;

    LOCA::destroyGlobalData(globalData);

  }

 catch (std::exception& e) {
    std::cout << e.what() << std::endl;
    ierr = 1;
  }
  catch (const char *s) {
    std::cout << s << std::endl;
    ierr = 1;
  }
  catch (...) {
    std::cout << "Caught unknown exception!" << std::endl;
    ierr = 1;
  }

   if (ierr == 0)
     std::cout << "All tests passed!" << std::endl;
   else
     std::cout << ierr << " test(s) failed!" << std::endl;

  return ierr;
}

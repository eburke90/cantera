/**
 *  @file BEulerInt.cpp
 *
 */

/*
 * Copywrite 2004 Sandia Corporation. Under the terms of Contract
 * DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government
 * retains certain rights in this software.
 * See file License.txt for licensing information.
 */

#include "BEulerInt.h"


#include "mdp_allo.h"
#include <iostream>

using namespace std;
using namespace mdp;

#define SAFE_DELETE(a) if (a) { delete (a); a = 0; }


/*
 * Blas routines
 */
extern "C" {
  extern void dcopy_(int *, double *, int *, double *, int *);
}
namespace Cantera {

  //================================================================================================
  /*
   * Exception thrown when a BEuler error is encountered. We just call the 
   * Cantera Error handler in the initialization list
   */
  BEulerErr::BEulerErr(std::string msg) :
    CanteraError("BEulerInt", msg)
  {
  }

  //================================================================================================
  /*
   *  Constructor. Default settings: dense jacobian, no user-supplied
   *  Jacobian function, Newton iteration.
   */
  BEulerInt::BEulerInt() :
    m_iter(Newton_Iter), 
    m_method(BEulerVarStep),
    m_jacFormMethod(BEULER_JAC_NUM),
    m_rowScaling(true),
    m_colScaling(false),
    m_matrixConditioning(false),
    m_itol(0),
    m_reltol(1.e-4), 
    m_abstols(1.e-10),
    m_abstol(0),
    m_ewt(0),
    m_hmax(0.0),
    m_maxord(0),
    m_time_step_num(0),
    m_time_step_attempts(0),
    m_max_time_step_attempts(11000000),
    m_numInitialConstantDeltaTSteps(0),
    m_failure_counter(0),
    m_min_newt_its(0),
    m_printSolnStepInterval(1),
    m_printSolnNumberToTout(1),
    m_printSolnFirstSteps(0),
    m_dumpJacobians(false),
    m_neq(0),
    m_y_n(0), 
    m_y_nm1(0),
    m_y_pred_n(0),
    m_ydot_n(0),
    m_ydot_nm1(0),
    m_t0(0.0),
    m_time_final(0.0),
    time_n(0.0),
    time_nm1(0.0),
    time_nm2(0.0),
    delta_t_n(0.0),
    delta_t_nm1(0.0),
    delta_t_nm2(0.0),
    delta_t_np1(1.0E-8),
    delta_t_max(1.0E300),
    m_resid(0),
    m_residWts(0),
    m_wksp(0),
    m_func(0),
    m_rowScales(0),
    m_colScales(0),
    tdjac_ptr(0),
    m_print_flag(3),
    m_nfe(0),
    m_nJacEval(0),
    m_numTotalNewtIts(0),
    m_numTotalLinearSolves(0),
    m_numTotalConvFails(0),
    m_numTotalTruncFails(0),
    num_failures(0)
  {

  }
  //================================================================================================
  /*
   * Destructor
   */
  BEulerInt::~BEulerInt()
  {
    mdp::mdp_safe_free((void **) &m_y_n);
    mdp::mdp_safe_free((void **) &m_y_nm1);
    mdp::mdp_safe_free((void **) &m_y_pred_n);
    mdp::mdp_safe_free((void **) &m_ydot_n);
    mdp::mdp_safe_free((void **) &m_ydot_nm1);
    mdp::mdp_safe_free((void **) &m_resid);
    mdp::mdp_safe_free((void **) &m_residWts);
    mdp::mdp_safe_free((void **) &m_wksp);
    mdp::mdp_safe_free((void **) &m_ewt);
    mdp::mdp_safe_free((void **) &m_abstol);
    mdp::mdp_safe_free((void **) &m_rowScales);
    mdp::mdp_safe_free((void **) &m_colScales);
    SAFE_DELETE(tdjac_ptr);
  }
  //================================================================================================
  void BEulerInt::setTolerances(double reltol, int n, double* abstol) {
    m_itol = 1;
    if (!m_abstol) {
      m_abstol = mdp_alloc_dbl_1(m_neq, MDP_DBL_NOINIT);
    }
    if (n != m_neq) {
      printf("ERROR n is wrong\n");
      exit(-1);
    }
    for (int i = 0; i < m_neq; i++) {
      m_abstol[i] = abstol[i];
    }
    m_reltol = reltol; 
  }
  //================================================================================================
  void BEulerInt::setTolerances(double reltol, double abstol) {
    m_itol = 0;
    m_reltol = reltol; 
    m_abstols = abstol;
  }
  //================================================================================================
  void BEulerInt::setProblemType(int jacFormMethod) {
    m_jacFormMethod = jacFormMethod;
  }
  //================================================================================================
  void BEulerInt::setMethodBEMT(BEulerMethodType t) {
    m_method = t;
  }
  //================================================================================================
  void BEulerInt::setMaxStep(doublereal hmax) {
    m_hmax = hmax;
  }
  //================================================================================================
  void BEulerInt::setMaxNumTimeSteps(int maxNumTimeSteps) {
    m_max_time_step_attempts = maxNumTimeSteps;
  }
  //================================================================================================
  void BEulerInt::setNumInitialConstantDeltaTSteps(int num) {
    m_numInitialConstantDeltaTSteps = num;
  }
  //================================================================================================
  /*
   *
   * setPrintSolnOptins():
   *
   * This routine controls when the solution is printed
   *
   * @param printStepInterval If greater than 0, then the
   *                     soln is printed every printStepInterval
   *                     steps. 
   *
   * @param printNumberToTout The solution is printed at
   *                  regular invervals a total of 
   *                  "printNumberToTout" times.
   *
   * @param printSolnFirstSteps The solution is printed out
   *                   the first "printSolnFirstSteps"
   *                   steps. After these steps the other
   *                   parameters determine the printing.
   *                   default = 0
   *
   * @param dumpJacobians Dump jacobians to disk.
   *
   *                   default = false 
   *
   */
  void BEulerInt::setPrintSolnOptions(int printSolnStepInterval,
				      int printSolnNumberToTout,
				      int printSolnFirstSteps,
				      bool dumpJacobians) 
  {
    m_printSolnStepInterval = printSolnStepInterval;
    m_printSolnNumberToTout = printSolnNumberToTout;
    m_printSolnFirstSteps   = printSolnFirstSteps;
    m_dumpJacobians         = dumpJacobians;
  }
  //================================================================================================
  void BEulerInt::setIterator(IterType t) {
    m_iter = t;
  }
  //================================================================================================
  /*
   *
   * setNonLinOptions()
   *
   *  Set the options for the nonlinear method
   *
   *  Defaults are set in the .h file. These are the defaults:
   *    min_newt_its = 0
   *    matrixConditioning = false
   *    colScaling = false
   *    rowScaling = true
   */
  void BEulerInt::setNonLinOptions(int min_newt_its, bool matrixConditioning,
				   bool colScaling, bool rowScaling) 
  {
    m_min_newt_its = min_newt_its;
    m_matrixConditioning = matrixConditioning;
    m_colScaling = colScaling;
    m_rowScaling = rowScaling;
    if (m_colScaling) {
      if (!m_colScales) {
	m_colScales = mdp_alloc_dbl_1(m_neq, 1.0);
      }
    }
    if (m_rowScaling) {
      if (!m_rowScales) {
	m_rowScales = mdp_alloc_dbl_1(m_neq, 1.0);
      }
    }
  }
  //================================================================================================
  /*
   *
   * setInitialTimeStep():
   *
   * Set the initial time step. Right now, we set the
   * time step by setting delta_t_np1.
   */
  void BEulerInt::setInitialTimeStep(double deltaT)
  {
    delta_t_np1 = deltaT;
  }
  //================================================================================================
  /*
   * setPrintFlag():
   *
   */
  void BEulerInt::setPrintFlag(int print_flag)
  {
    m_print_flag = print_flag;
  }
  //================================================================================================
  /*
   *
   * initialize():
   *
   * Find the initial conditions for y and ydot.
   */
  void BEulerInt::initializeRJE(double t0, ResidJacEval &func) 
  {
    m_neq = func.nEquations();
    m_t0  = t0;
    internalMalloc();
   
    /*
     * Get the initial conditions.
     */
    func.getInitialConditions(m_t0, m_y_n, m_ydot_n);

    // Store a pointer to the residual routine in the object
    m_func = &func;

    /*
     * Initialize the various time counters in the object
     */
    time_n = t0;
    time_nm1 = time_n;
    time_nm2 = time_nm1;
    delta_t_n = 0.0;
    delta_t_nm1 = 0.0;
  }
  //================================================================================================
  /*
   *
   * reinitialize():
   *
   */
  void BEulerInt::reinitializeRJE(double t0, ResidJacEval& func) 
  {
    m_neq = func.nEquations();
    m_t0  = t0;
    internalMalloc();
    /*
     * At the initial time, get the initial conditions and time and store
     * them into internal storage in the object, my[].
     */
    m_t0  = t0;
    func.getInitialConditions(m_t0, m_y_n, m_ydot_n);
    /**
     * Set up the internal weights that are used for testing convergence
     */
    setSolnWeights();

    // Store a pointer to the function
    m_func = &func;
 
  }
  //================================================================================================
  /*
   *
   * getPrintTime():
   *
   */
  double BEulerInt::getPrintTime(double time_current)
  {
    double tnext;
    if (m_printSolnNumberToTout > 0) {
      double dt = (m_time_final - m_t0) / m_printSolnNumberToTout;
      for (int i = 0; i <= m_printSolnNumberToTout; i++) {
	tnext = m_t0 + dt * i;
	if (tnext >= time_current) return tnext;
      }
    }
    return 1.0E300;
  }
  //================================================================================================
  /*
   * nEvals():
   *
   * Return the total number of function evaluations
   */
  int BEulerInt::nEvals() const 
  {
    return m_nfe;
  }
  //================================================================================================
  /*
   *
   * internalMalloc():
   *
   *  Internal routine that sets up the fixed length storage based on 
   *  the size of the problem to solve.
   */
  void BEulerInt::internalMalloc() 
  {
    mdp_realloc_dbl_1(&m_ewt,      m_neq, 0, 0.0);
    mdp_realloc_dbl_1(&m_y_n,      m_neq, 0, 0.0);
    mdp_realloc_dbl_1(&m_y_nm1,    m_neq, 0, 0.0);
    mdp_realloc_dbl_1(&m_y_pred_n, m_neq, 0, 0.0);
    mdp_realloc_dbl_1(&m_ydot_n,   m_neq, 0, 0.0);
    mdp_realloc_dbl_1(&m_ydot_nm1, m_neq, 0, 0.0);
    mdp_realloc_dbl_1(&m_resid,    m_neq, 0, 0.0);
    mdp_realloc_dbl_1(&m_residWts, m_neq, 0, 0.0);
    mdp_realloc_dbl_1(&m_wksp,     m_neq, 0, 0.0);
    if (m_rowScaling) {
      mdp_realloc_dbl_1(&m_rowScales, m_neq, 0, 1.0);
    }
    if (m_colScaling) {
      mdp_realloc_dbl_1(&m_colScales, m_neq, 0, 1.0);
    }
    tdjac_ptr = new SquareMatrix(m_neq);
  }
  //================================================================================================
  /*
   * setSolnWeights():
   *
   * Set the solution weights
   *  This is a very important routine as it affects quite a few
   *  operations involving convergence.
   *
   */
  void BEulerInt::setSolnWeights() 
  {
    int i;
    if (m_itol == 1) {
      /*
       * Adjust the atol vector if we are using vector
       * atol conditions.
       */
      // m_func->adjustAtol(m_abstol);

      for (i = 0; i < m_neq; i++) {
	m_ewt[i] = m_abstol[i] + m_reltol * 0.5 *
	  (fabs(m_y_n[i]) + fabs(m_y_pred_n[i]));
      }
    } else {
      for (i = 0; i < m_neq; i++) {
	m_ewt[i] = m_abstols + m_reltol * 0.5 * 
	  (fabs(m_y_n[i]) + fabs(m_y_pred_n[i]));
      }
    }
  }
  //================================================================================================
  /*
   *
   * setColumnScales():
   *
   * Set the column scaling vector at the current time
   */
  void BEulerInt::setColumnScales()
  {
    m_func->calcSolnScales(time_n, m_y_n, m_y_nm1, m_colScales);
  }
  //================================================================================================
  /*
   * computeResidWts():
   *
   * We compute residual weights here, which we define as the L_0 norm
   * of the Jacobian Matrix, weighted by the solution weights.
   * This is the proper way to guage the magnitude of residuals. However,
   * it does need the evaluation of the jacobian, and the implementation
   * below is slow, but doesn't take up much memory.
   *
   * Here a small weighting indicates that the change in solution is
   * very sensitive to that equation.
   */
  void BEulerInt::computeResidWts(GeneralMatrix &jac) 
  {
    int i, j;
    double *data = &(*(jac.begin()));
    double value;
    for (i = 0; i < m_neq; i++) {
      m_residWts[i] = fabs(data[i] * m_ewt[0]);
      for (j = 1; j < m_neq; j++) {
	value = fabs(data[j*m_neq + i] * m_ewt[j]);
	m_residWts[i] = MAX(m_residWts[i], value);
      }
    }
  }
  //================================================================================================
  /*
   * filterNewStep():
   *
   * void BEulerInt::
   *
   */
  double BEulerInt::filterNewStep(double timeCurrent, double *y_current, double *ydot_current) {  
    return 0.0;
  }
  //==================================================================================================
  static void print_line(const char *str, int n)
  {
    for (int i = 0; i < n; i++) {
      printf("%s", str);
    }
    printf("\n");
  }
  //==================================================================================================
  /*
   * Print out for relevant time step information
   */
  static void print_time_step1(int order, int n_time_step, double time,
			       double delta_t_n, double delta_t_nm1,
			       bool step_failed, int num_failures)
  {
    const char *string = 0;
    if      (order == 0) string = "Backward Euler";
    else if (order == 1) string = "Forward/Backward Euler";
    else if (order == 2) string = "Adams-Bashforth/TR";
    printf("\n"); print_line("=", 80);
    printf("\nStart of Time Step: %5d       Time_n = %9.5g Time_nm1 = %9.5g\n",
	   n_time_step, time, time - delta_t_n);
    printf("\tIntegration method = %s\n", string);
    if (step_failed)
      printf("\tPreviously attempted step was a failure\n");
    if (delta_t_n > delta_t_nm1)
      string = "(Increased from previous iteration)";
    else if (delta_t_n < delta_t_nm1)
      string = "(Decreased from previous iteration)";
    else {
      string = "(same as previous iteration)";
    }
    printf("\tdelta_t_n        = %8.5e %s", delta_t_n, string);
    if (num_failures > 0)
      printf("\t(Bad_History Failure Counter = %d)", num_failures);
    printf("\n\tdelta_t_nm1      = %8.5e\n", delta_t_nm1);
  }
  //================================================================================================
  /*
   * Print out for relevant time step information
   */
  static void print_time_step2(int  time_step_num, int order,
			       double time, double time_error_factor,
			       double delta_t_n, double delta_t_np1)
  {
    printf("\tTime Step Number %5d was a success: time = %10g\n", time_step_num,
	   time);
    printf("\t\tEstimated Error\n");
    printf("\t\t--------------------   =   %8.5e\n", time_error_factor);
    printf("\t\tTolerated Error\n\n");
    printf("\t- Recommended next delta_t (not counting history) = %g\n",
	   delta_t_np1);
    printf("\n"); print_line("=", 80); printf("\n");
  }
  //================================================================================================
  /*
   * Print Out descriptive information on why the current step failed
   */
  static void print_time_fail(bool convFailure, int time_step_num,
			      double time, double delta_t_n,
			      double delta_t_np1, double  time_error_factor)
  {
    printf("\n"); print_line("=", 80);
    if (convFailure) {
      printf("\tTime Step Number %5d experienced a convergence "
	     "failure\n", time_step_num);
      printf("\tin the non-linear or linear solver\n");
      printf("\t\tValue of time at failed step           = %g\n", time);
      printf("\t\tdelta_t of the   failed step           = %g\n",
	     delta_t_n);
      printf("\t\tSuggested value of delta_t to try next = %g\n",
	     delta_t_np1);
    } else {
      printf("\tTime Step Number %5d experienced a truncation error "
	     "failure!\n", time_step_num);
      printf("\t\tValue of time at failed step           = %g\n", time);
      printf("\t\tdelta_t of the   failed step           = %g\n",
	     delta_t_n);
      printf("\t\tSuggested value of delta_t to try next = %g\n",
	     delta_t_np1);
      printf("\t\tCalculated truncation error factor  = %g\n",
	     time_error_factor);
    }
    printf("\n"); print_line("=", 80);
  }
  //================================================================================================
  /*
   * Print out the final results and counters
   */
  static void print_final(double time, int step_failed,
			  int time_step_num, int num_newt_its,
			  int total_linear_solves, int numConvFails,
			  int numTruncFails, int nfe, int nJacEval)
  {
    printf("\n"); print_line("=", 80);
    printf("TIME INTEGRATION ROUTINE HAS FINISHED: ");
    if (step_failed)
      printf(" IT WAS A FAILURE\n");
    else
      printf(" IT WAS A SUCCESS\n");
    printf("\tEnding time                   = %g\n", time);
    printf("\tNumber of time steps          = %d\n", time_step_num);
    printf("\tNumber of newt its            = %d\n", num_newt_its);
    printf("\tNumber of linear solves       = %d\n", total_linear_solves);
    printf("\tNumber of convergence failures= %d\n", numConvFails);
    printf("\tNumber of TimeTruncErr fails  = %d\n", numTruncFails);
    printf("\tNumber of Function evals      = %d\n", nfe);
    printf("\tNumber of Jacobian evals/solvs= %d\n", nJacEval);
    printf("\n"); print_line("=", 80);
  }
  //================================================================================================
  /*
   * Header info for one line comment about a time step
   */
  static void print_lvl1_Header(int nTimes) {
    printf("\n");
    if (nTimes) {
      print_line("-", 80);
    }
    printf("time       Time              Time                     Time  ");
    if (nTimes == 0) {
      printf("     START");
    } else {
      printf("    (continued)");
    }
    printf("\n");

    printf("step      (sec)              step  Newt   Aztc bktr  trunc  ");
    printf("\n");

    printf(" No.               Rslt      size    Its  Its  stps  error     |");
    printf("  comment");
    printf("\n");
    print_line("-", 80);
  } 
  //================================================================================================
  /*
   * One line entry about time step
   *   rslt -> 4 letter code
   */
  static void print_lvl1_summary(
				 int time_step_num, double time, const char *rslt,  double delta_t_n,
				 int newt_its, int aztec_its, int bktr_stps, double  time_error_factor,
				 const char *comment) {
    printf("%6d %11.6g %4s %10.4g %4d %4d %4d %11.4g",
	   time_step_num, time, rslt, delta_t_n, newt_its, aztec_its,
	   bktr_stps, time_error_factor);
    if (comment) printf(" | %s", comment);
    printf("\n");
  }
  //================================================================================================
  /*
   * subtractRD():
   *   This routine subtracts 2 numbers. If the difference is less
   *   than 1.0E-14 times the magnitude of the smallest number,
   *   then diff returns an exact zero. 
   *   It also returns an exact zero if the difference is less than
   *   1.0E-300.
   *
   *   returns:  a - b
   *
   *   This routine is used in numerical differencing schemes in order
   *   to avoid roundoff errors resulting in creating Jacobian terms.
   *   Note: This is a slow routine. However, jacobian errors may cause
   *         loss of convergence. Therefore, in practice this routine
   *         has proved cost-effective.
   */
  double subtractRD(double a, double b) {
    double diff = a - b;
    double d = MIN(fabs(a), fabs(b));
    d *= 1.0E-14;
    double ad = fabs(diff);
    if (ad < 1.0E-300) {
      diff = 0.0;
    }
    if (ad < d) {
      diff = 0.0;
    }
    return diff;
  }
  //================================================================================================
  /*
   *
   *  Function called by BEuler to evaluate the Jacobian matrix and the
   *  current residual at the current time step.
   *  @param N = The size of the equation system
   *  @param J = Jacobian matrix to be filled in
   *  @param f = Right hand side. This routine returns the current
   *             value of the rhs (output), so that it does
   *             not have to be computed again.
   *
   */
  void BEulerInt::beuler_jac(GeneralMatrix &J, double * const f,
			     double time_curr, double CJ, 
			     double * const y, 
			     double * const ydot,
			     int num_newt_its)		       
  {
    int i, j;
    double* col_j;
    double ysave, ydotsave, dy;
    /**
     * Clear the factor flag
     */
    J.clearFactorFlag();


    if (m_jacFormMethod & BEULER_JAC_ANAL) {
      /********************************************************************
       * Call the function to get a jacobian.
       */
      m_func->evalJacobian(time_curr, delta_t_n, CJ, y, ydot, J, f);
#ifdef DEBUG_HKM
      //double dddd = J(89, 89);
      //checkFinite(dddd);
#endif
      m_nJacEval++;
      m_nfe++;
    }  else {
      /*******************************************************************
       * Generic algorithm to calculate a numerical Jacobian
       */
      /*
       * Calculate the current value of the rhs given the
       * current conditions.
       */
 
      m_func->evalResidNJ(time_curr, delta_t_n, y, ydot, f, JacBase_ResidEval);
      m_nfe++;
      m_nJacEval++;


      /*
       * Malloc a vector and call the function object to return a set of
       * deltaY's that are appropriate for calculating the numerical
       * derivative.
       */
      double *dyVector = mdp::mdp_alloc_dbl_1(m_neq, MDP_DBL_NOINIT);
      m_func->calcDeltaSolnVariables(time_curr, y, m_y_nm1, dyVector, 
				     m_ewt);
#ifdef DEBUG_HKM
      bool print_NumJac = false;
      if (print_NumJac) {
	FILE *idy = fopen("NumJac.csv", "w");
	fprintf(idy, "Unk          m_ewt        y     "
		"dyVector      ResN\n");
	for (int iii = 0; iii < m_neq; iii++){
	  fprintf(idy, " %4d       %16.8e   %16.8e   %16.8e  %16.8e \n",
		  iii,   m_ewt[iii],  y[iii], dyVector[iii], f[iii]);
	}
	fclose(idy);
      }
#endif
      /*
       * Loop over the variables, formulating a numerical derivative
       * of the dense matrix.
       * For the delta in the variable, we will use a variety of approaches
       * The original approach was to use the error tolerance amount.
       * This may not be the best approach, as it could be overly large in
       * some instances and overly small in others.
       * We will first protect from being overly small, by using the usual
       * sqrt of machine precision approach, i.e., 1.0E-7,
       * to bound the lower limit of the delta.
       */
      for (j = 0; j < m_neq; j++) {


	/*
	 * Get a pointer into the column of the matrix
	 */


	col_j = (double *) J.ptrColumn(j);
	ysave = y[j];
	dy = dyVector[j];
	//dy = fmaxx(1.0E-6 * m_ewt[j], fabs(ysave)*1.0E-7);

	y[j] = ysave + dy;
	dy = y[j] - ysave;
	ydotsave = ydot[j];
	ydot[j] += dy * CJ;
	/*
	 * Call the functon
	 */


	m_func->evalResidNJ(time_curr, delta_t_n, y, ydot, m_wksp,
			    JacDelta_ResidEval, j, dy);
	m_nfe++;
	double diff;
	for (i = 0; i < m_neq; i++) {
	  diff = subtractRD(m_wksp[i], f[i]);
	  col_j[i] = diff / dy;
	  //col_j[i] = (m_wksp[i] - f[i])/dy;
	}

	y[j] = ysave;
	ydot[j] = ydotsave;

      }
      /*
       * Release memory
       */
      mdp::mdp_safe_free((void **) &dyVector);
    }

 
  }

   
  /*
   * Function to calculate the predicted solution vector, m_y_pred_n for the
   * (n+1)th time step.  This routine can be used by a first order - forward
   * Euler / backward Euler predictor / corrector method or for a second order
   * Adams-Bashforth / Trapezoidal Rule predictor / corrector method.  See Nachos
   * documentation Sand86-1816 and Gresho, Lee, Sani LLNL report UCRL - 83282 for
   * more information.
   *
   * variables:
   *
   * on input:
   *
   *     N          - number of unknowns
   *     order      - indicates order of method
   *                  = 1 -> first order forward Euler/backward Euler
   *                         predictor/corrector
   *                  = 2 -> second order Adams-Bashforth/Trapezoidal Rule
   *                         predictor/corrector
   *
   *    delta_t_n   - magnitude of time step at time n     (i.e., = t_n+1 - t_n)
   *    delta_t_nm1 - magnitude of time step at time n - 1 (i.e., = t_n - t_n-1)
   *    y_n[]       - solution vector at time n
   *    y_dot_n[]   - acceleration vector from the predictor at time n
   *    y_dot_nm1[] - acceleration vector from the predictor at time n - 1
   *
   * on output:
   *
   *    m_y_pred_n[]    - predicted solution vector at time n + 1
   */
  void BEulerInt::calc_y_pred(int order)
  {
    int i;
    double c1, c2;
    switch (order) {
    case 0:
    case 1:
      c1 = delta_t_n;
      for (i = 0; i < m_neq; i++) {
	m_y_pred_n[i] = m_y_n[i] + c1 * m_ydot_n[i];
      }
      break;  
    case 2:
      c1 = delta_t_n * (2.0 + delta_t_n / delta_t_nm1) / 2.0;
      c2 = (delta_t_n * delta_t_n) / (delta_t_nm1 * 2.0);
      for (i = 0; i < m_neq; i++) {
	m_y_pred_n[i] = m_y_n[i] + c1 * m_ydot_n[i] - c2 * m_ydot_nm1[i];
      }
      break;
    }

    /*
     * Filter the predictions.
     */
    m_func->filterSolnPrediction(time_n, m_y_pred_n);

  } /* calc_y_pred */
 

    /* Function to calculate the acceleration vector ydot for the first or
     * second order predictor/corrector time integrator.  This routine can be
     * called by a first order - forward Euler / backward Euler predictor /
     * corrector or for a second order Adams - Bashforth / Trapezoidal Rule
     * predictor / corrector.  See Nachos documentation Sand86-1816 and Gresho,
     * Lee, Sani LLNL report UCRL - 83282 for more information.
     *
     *  variables:
     *
     *    on input:
     *
     *       N          - number of local unknowns on the processor
     *                    This is equal to internal plus border unknowns.
     *       order      - indicates order of method
     *                    = 1 -> first order forward Euler/backward Euler
     *                           predictor/corrector
     *                    = 2 -> second order Adams-Bashforth/Trapezoidal Rule
     *                           predictor/corrector
     *
     *      delta_t_n   - Magnitude of the current time step at time n
     *                    (i.e., = t_n - t_n-1)
     *      y_curr[]    - Current Solution vector at time n
     *      y_nm1[]     - Solution vector at time n-1
     *      ydot_nm1[] - Acceleration vector at time n-1
     *
     *   on output:
     *
     *      ydot_curr[]   - Current acceleration vector at time n
     *
     * Note we use the current attribute to denote the possibility that
     * y_curr[] may not be equal to m_y_n[] during the nonlinear solve
     * because we may be using a look-ahead scheme.
     */
  void BEulerInt::
  calc_ydot(int order, double *y_curr, double *ydot_curr)
  {
    int    i;
    double c1;
    switch (order) {
    case 0:
    case 1:             /* First order forward Euler/backward Euler */
      c1 = 1.0 / delta_t_n;
      for (i = 0; i < m_neq; i++) {
	ydot_curr[i] = c1 * (y_curr[i] - m_y_nm1[i]);
      }
      return;
    case 2:             /* Second order Adams-Bashforth / Trapezoidal Rule */
      c1 = 2.0 / delta_t_n;
      for (i = 0; i < m_neq; i++) {
	ydot_curr[i] = c1 * (y_curr[i] - m_y_nm1[i])  - m_ydot_nm1[i];
      }
      return;
    }
  } /************* END calc_ydot () ****************************************/

    /* This function calculates the time step truncation error estimate
     * from a very simple formula based on Gresho et al.  This routine can be
     * called for a
     * first order - forward Euler/backward Euler predictor/ corrector and 
     * for a
     * second order Adams- Bashforth/Trapezoidal Rule predictor/corrector. See
     * Nachos documentation Sand86-1816 and Gresho, Lee, LLNL report
     *  UCRL - 83282
     * for more information.
     *
     *  variables:
     *
     *    on input:
     *
     *      abs_error   - Generic absolute error tolerance
     *      rel_error   - Generic realtive error tolerance
     *      x_coor[]    - Solution vector from the implicit corrector
     *      x_pred_n[]    - Solution vector from the explicit predictor
     *
     *   on output:
     *
     *      delta_t_n   - Magnitude of next time step at time t_n+1
     *      delta_t_nm1 - Magnitude of previous time step at time t_n
     */
  double BEulerInt::time_error_norm()
  {
    int    i;
    double rel_norm, error;
#ifdef DEBUG_HKM
#define NUM_ENTRIES 5
    if (m_print_flag > 2) {
      int imax[NUM_ENTRIES], j, jnum;
      double dmax;
      bool used;
      printf("\t\ttime step truncation error contributors:\n");
      printf("\t\t    I       entry   actual   predicted   "
	     "    weight       ydot\n");
      printf("\t\t"); print_line("-", 70);
      for (j = 0; j < NUM_ENTRIES; j++) imax[j] = -1;
      for (jnum = 0; jnum < NUM_ENTRIES; jnum++) {
	dmax = -1.0;
	for (i = 0; i < m_neq; i++) {
	  used = false;
	  for (j = 0; j < jnum; j++) {
	    if (imax[j] == i) used = true;
	  }
	  if (!used) {
	    error     = (m_y_n[i] - m_y_pred_n[i]) /  m_ewt[i];
	    rel_norm = sqrt(error * error);
	    if (rel_norm > dmax) {
	      imax[jnum] = i;
	      dmax = rel_norm;
	    }
	  } 
	}
	if (imax[jnum] >= 0) {
	  i = imax[jnum];
	  printf("\t\t%4d %12.4e %12.4e %12.4e %12.4e %12.4e\n",
		 i, dmax, m_y_n[i], m_y_pred_n[i], m_ewt[i], m_ydot_n[i]);
	}
      }
      printf("\t\t"); print_line("-", 70);
    }
#endif
    rel_norm = 0.0;
    for (i = 0; i < m_neq; i++) {
      error     = (m_y_n[i] - m_y_pred_n[i]) /  m_ewt[i];
      rel_norm += (error * error);
    }
    rel_norm = sqrt(rel_norm / m_neq);
    return rel_norm;
  }
 
  /************************************************************************* 
   * Time step control function for the selection of the time step size based on
   * a desired accuracy of time integration and on an estimate of the relative
   * error of the time integration process. This routine can be called for a
   * first order - forward Euler/backward Euler predictor/ corrector and for a
   * second order Adams- Bashforth/Trapezoidal Rule predictor/corrector. See
   * Nachos documentation Sand86-1816 and Gresho, Lee, Sani LLNL report UCRL -
   * 83282 for more information.
   *
   *  variables:
   *
   *    on input:
   *
   *       order      - indicates order of method
   *                    = 1 -> first order forward Euler/backward Euler
   *                           predictor/corrector
   *                    = 2 -> second order forward Adams-Bashforth/Trapezoidal
   *                          rule predictor/corrector
   *
   *      delta_t_n   - Magnitude of time step at time t_n
   *      delta_t_nm1 - Magnitude of time step at time t_n-1
   *      rel_error   - Generic realtive error tolerance
   *      time_error_factor   - Estimated value of the time step truncation error
   *                           factor. This value is a ratio of the computed
   *                           error norms. The premultiplying constants
   *                           and the power are not yet applied to normalize the
   *                           predictor/corrector ratio. (see output value)
   *
   *   on output:
   *
   *      return - delta_t for the next time step
   *               If delta_t is negative, then the current time step is
   *               rejected because the time-step truncation error is
   *               too large.  The return value will contain the negative
   *               of the recommended next time step.
   *
   *      time_error_factor  - This output value is normalized so that
   *                           values greater than one indicate the current time
   *                           integration error is greater than the user 
   *                           specified magnitude.
   */
  double BEulerInt::time_step_control(int order, double time_error_factor)
  {
    double factor = 0.0, power = 0.0, delta_t;
    const char  *yo = "time_step_control";

    /*
     * Special case time_error_factor so that zeroes don't cause a problem.
     */     
    time_error_factor = MAX(1.0E-50, time_error_factor);
      
    /*
     * Calculate the factor for the change in magnitude of time step.
     */
    switch (order) {
    case 1:
      factor = 1.0/(2.0 *(time_error_factor));
      power  = 0.5;
      break;
    case 2:
      factor = 1.0/(3.0 * (1.0 + delta_t_nm1 / delta_t_n) 
		    * (time_error_factor));
      power  = 0.3333333333333333;
    }
    factor = pow(factor, power);
    if (factor < 0.5) {
      if (m_print_flag > 1) {
	printf("\t%s: WARNING - Current time step will be chucked\n", yo);
	printf("\t\tdue to a time step truncation error failure.\n");
      }
      delta_t = - 0.5 * delta_t_n;
    } else {
      factor  = MIN(factor, 1.5);
      delta_t = factor * delta_t_n;
    }
    return delta_t;
  } /************ END of time_step_control()********************************/
  //================================================================================================
  /**************************************************************************
   *
   * integrate():
   *
   *  defaults are located in the .h file. They are as follows:
   *     time_init = 0.0
   */
  double BEulerInt::integrateRJE(double tout, double time_init)
  {
    double time_current;
    bool weAreNotFinished = true;
    m_time_final = tout;
    int flag = SUCCESS;
    /**
     * Initialize the time step number to zero. step will increment so that
     * the first time step is number 1
     */
    m_time_step_num = 0;
 

    /*
     * Do the integration a step at a time
     */
    int istep = 0;
    int printStep = 0;
    bool doPrintSoln = false;
    time_current = time_init;
    time_n = time_init;
    time_nm1 = time_init;
    time_nm2 = time_init;
    m_func->evalTimeTrackingEqns(time_current, 0.0, m_y_n, m_ydot_n);
    double print_time = getPrintTime(time_current);
    if (print_time == time_current) {
      m_func->writeSolution(4, time_current, delta_t_n,
			    istep, m_y_n, m_ydot_n);
    }
    /*
     * We print out column headers here for the case of 
     */
    if (m_print_flag == 1) {
      print_lvl1_Header(0);
    }
    /*
     * Call a different user routine at the end of each step,
     * that will probably print to a file.
     */
    m_func->user_out2(0, time_current, 0.0, m_y_n, m_ydot_n);
      
    do {
	
      print_time = getPrintTime(time_current);
      if (print_time >= tout) print_time = tout;

      /************************************************************
       * Step the solution
       */
      time_current = step(tout);
      istep++;
      printStep++;
      /***********************************************************/
      if (time_current < 0.0) {
	if (time_current == -1234.) {
	  time_current = 0.0;
	} else {
	  time_current = -time_current;
	}
	flag = FAILURE;
      }

      if (flag != FAILURE) {
	bool retn =
	  m_func->evalStoppingCritera(time_current, delta_t_n,
				      m_y_n, m_ydot_n);
	if (retn) {
	  weAreNotFinished = false;
	  doPrintSoln = true;
	}
      }

      /*
       * determine conditional printing of soln
       */
      if (time_current >= print_time) {
	doPrintSoln = true;
      }
      if (m_printSolnStepInterval == printStep) {
	doPrintSoln = true;
      }
      if (m_printSolnFirstSteps > istep) {
	doPrintSoln = true;
      }

      /*
       * Evaluate time integrated quantities that are calculated at the
       * end of every successful time step.
       */
      if (flag != FAILURE) {
	m_func->evalTimeTrackingEqns(time_current, delta_t_n,
				     m_y_n, m_ydot_n);
      }
	
      /*
       * Call the printout routine.
       */
      if (doPrintSoln) {
	m_func->writeSolution(1, time_current, delta_t_n,
			      istep, m_y_n, m_ydot_n);
	printStep = 0;
	doPrintSoln = false;
	if (m_print_flag == 1) {
	  print_lvl1_Header(1);
	}
      }
      /*
       * Call a different user routine at the end of each step,
       * that will probably print to a file.
       */
      if (flag == FAILURE) {
	m_func->user_out2(-1, time_current, delta_t_n, m_y_n, m_ydot_n);
      } else {
	m_func->user_out2(1, time_current, delta_t_n, m_y_n, m_ydot_n);
      }

    } while (time_current < tout && 
	     m_time_step_attempts <  m_max_time_step_attempts &&
	     flag == SUCCESS && weAreNotFinished);

    /*
     * Check current time against the max solution time.
     */
    if (time_current >= tout) {
      printf("Simulation completed time integration in %d time steps\n",
	     m_time_step_num);
      printf("Final Time: %e\n\n", time_current);
    } else if (m_time_step_attempts >= m_max_time_step_attempts) {
      printf("Simulation ran into time step attempt limit in"
	     "%d time steps\n",
	     m_time_step_num);
      printf("Final Time: %e\n\n", time_current);
    } else if (flag == FAILURE) {
      printf("ERROR: time stepper failed at time = %g\n", time_current);
    }

    /*
     * Print out the final results and counters.
     */
    print_final(time_n, flag, m_time_step_num, m_numTotalNewtIts,
		m_numTotalLinearSolves, m_numTotalConvFails,
		m_numTotalTruncFails, m_nfe, m_nJacEval);

    /*
     * Call a different user routine at the end of each step,
     * that will probably print to a file.
     */
    m_func->user_out2(2, time_current, delta_t_n, m_y_n, m_ydot_n);

   
    if (flag != SUCCESS) 
      throw BEulerErr(" BEuler error encountered.");
    return time_current;
  }

  /**************************************************************************
   *
   * step():
   *
   * This routine advances the calculations one step using a predictor
   * corrector approach. We use an implicit algorithm here.
   *
   */
  double BEulerInt::step(double t_max)
  {
    double CJ;
    int one = 1;
    bool step_failed = false;
    bool giveUp = false;
    bool convFailure = false;
    const char *rslt;
    double time_error_factor = 0.0;
    double normFilter = 0.0;
    int numTSFailures = 0;
    int bktr_stps = 0;
    int nonlinearloglevel = m_print_flag;
    int num_newt_its = 0;
    int aztec_its = 0;
    string comment;
    /*
     * Increment the time counter - May have to be taken back, 
     * if time step is found to be faulty.
     */
    m_time_step_num++;

    /**
     * Loop here until we achieve a successful step or we set the giveUp
     * flag indicating that repeated errors have occurred.
     */
    do {
      m_time_step_attempts++;
      comment.clear();

      /*
       * Possibly adjust the delta_t_n value for this time step from the
       * recommended delta_t_np1 value determined in the previous step
       *  due to maximum time step constraints or other occurences,
       * known to happen at a given time.
       */
      if ((time_n + delta_t_np1) >= t_max) {
	delta_t_np1 =t_max - time_n;
      }
	
      if (delta_t_np1 >= delta_t_max) {
	delta_t_np1 = delta_t_max;
      }

      /*
       * Increment the delta_t counters and the time for the current 
       * time step.
       */

      delta_t_nm2 = delta_t_nm1;
      delta_t_nm1 = delta_t_n;
      delta_t_n   = delta_t_np1;
      time_n     += delta_t_n;

      /*
       * Determine the integration order of the current step.
       *
       * Special case for start-up of time integration procedure
       *           First time step = Do a predictor step as we 
       *                             have recently added an initial
       *                             ydot input option. And, setting ydot=0
       *                             is equivalent to not doing a 
       *                             predictor step.
       *           Second step     = If 2nd order method, do a first order
       *                             step for this time-step, only.
       *
       *           If 2nd order method with a constant time step, the
       *           first and second steps are 1/10 the specified step, and
       *           the third step is 8/10 the specified step.  This reduces
       *           the error asociated with using lower order
       *           integration on the first two steps. (RCS 11-6-97)
       *
       * If the previous time step failed for one reason or another, 
       * do a linear step. It's more robust.
       */
      if (m_time_step_num == 1) {
	m_order = 1;                          /* Backward Euler          */
      }
      else if (m_time_step_num == 2) {
	m_order = 1;                          /* Forward/Backward Euler  */
      }
      else if (step_failed) {
	m_order = 1;                          /* Forward/Backward Euler  */
      }
      else if (m_time_step_num > 2) {
	m_order = 1;                          /* Specified
						 Predictor/Corrector 
						 - not implemented */
      }
	
      /*
       * Print out an initial statement about the step.
       */
      if (m_print_flag > 1) {
	print_time_step1(m_order, m_time_step_num, time_n, delta_t_n,
			 delta_t_nm1, step_failed, m_failure_counter);
      }

      /*
       * Calculate the predicted solution, m_y_pred_n, for the current
       * time step.
       */
      calc_y_pred(m_order);

      /*
       * HKM - Commented this out. I may need it for particles later.
       * If Solution bounds checking is turned on, we need to crop the
       * predicted solution to make sure bounds are enforced
       *
       *
       * cropNorm = 0.0;
       * if (Cur_Realm->Realm_Nonlinear.Constraint_Backtracking_Flag ==
       * Constraint_Backtrack_Enable) {
       * cropNorm = cropPredictor(mesh, x_pred_n, abs_time_error,
       *		   m_reltol);
       */

      /*
       * Save the old solution, before overwriting with the new solution 
       * - use
       */
      mdp_copy_dbl_1(m_y_nm1, m_y_n, m_neq);

      /*
       * Use the predicted value as the initial guess for the corrector 
       * loop, for
       * every step other than the first step.
       */
      if (m_order > 0) {
	mdp_copy_dbl_1(m_y_n, m_y_pred_n, m_neq);
      }

      /*
       * Save the old time derivative, if necessary, before it is 
       * overwritten.
       * This overwrites ydot_nm1, losing information from the previous time
       * step.
       */
      mdp_copy_dbl_1(m_ydot_nm1, m_ydot_n, m_neq);

      /*
       * Calculate the new time derivative, ydot_n, that is consistent 
       * with the
       * initial guess for the corrected solution vector.
       *
       */
      calc_ydot(m_order, m_y_n, m_ydot_n);

      /*
       * Calculate CJ, the coefficient for the jacobian corresponding to the
       * derivative of the residual wrt to the acceleration vector.
       */
      if (m_order < 2) CJ = 1.0 / delta_t_n;
      else             CJ = 2.0 / delta_t_n;

      /*
       * Calculate a new Solution Error Weighting vector
       */
      setSolnWeights();

      /*
       * Solve the system of equations at the current time step.
       * Note - x_corr_n and x_dot_n are considered to be updated, 
       * on return from this solution.
       */
      int ierror = solve_nonlinear_problem(m_y_n, m_ydot_n,
					   CJ, time_n, *tdjac_ptr, num_newt_its,
					   aztec_its, bktr_stps,
					   nonlinearloglevel);
      /*
       * Set the appropriate flags if a convergence failure is detected.
       */
      if (ierror < 0) {                    /* Step failed */
	convFailure = true;
	step_failed = true;
	rslt = "fail";
	m_numTotalConvFails++;
	m_failure_counter +=3;
	if (m_print_flag > 1) {
	  printf("\tStep is Rejected, nonlinear problem didn't converge,"
		 "ierror = %d\n", ierror);
	}
      }
      else {                               /* Step succeeded */
	convFailure = false;
	step_failed = false;
	rslt = "done";

	/*
	 *  Apply a filter to a new successful step
	 */	  
	normFilter = filterNewStep(time_n, m_y_n, m_ydot_n);
	if (normFilter > 1.0) {
	  convFailure = true;
	  step_failed = true;
	  rslt = "filt";
	  if (m_print_flag > 1) {
	    printf("\tStep is Rejected, too large filter adjustment = %g\n",
		   normFilter);
	  }
	} else if (normFilter > 0.0) {
	  if (normFilter > 0.3) {
	    if (m_print_flag > 1) {
	      printf("\tStep was filtered, norm = %g, next "
		     "time step adjusted\n",  normFilter);
	    }
	  } else {
	    if (m_print_flag > 1) {
	      printf("\tStep was filtered, norm = %g\n", normFilter);
	    }
	  }
	}
      }

      /*
       * Calculate the time step truncation error for the current step.
       */
      if (!step_failed) {
	time_error_factor = time_error_norm();
      } else {
	time_error_factor = 1000.;
      }

      /*
       * Dynamic time step control- delta_t_n, delta_t_nm1 are set here.
       */
      if (step_failed) {
	/*
	 * For convergence failures, decrease the step-size by a factor of
	 *  4 and try again.
	 */
	delta_t_np1 = 0.25 * delta_t_n;
      }
      else if (m_method == BEulerVarStep) {

	/*
	 * If we are doing a predictor/corrector method, and we are
	 * past a certain number of time steps given by the input file
	 * then either correct the DeltaT for the next time step or
	 * 
	 */
	if ((m_order > 0) && 
	    (m_time_step_num > m_numInitialConstantDeltaTSteps) ) {
	  delta_t_np1 = time_step_control(m_order, time_error_factor);
	  if (normFilter > 0.1) {
	    if (delta_t_np1 > delta_t_n) delta_t_np1 = delta_t_n;
	  }

	  /*
	   * Check for Current time step failing due to violation of 
	   * time step
	   * truncation bounds.
	   */
	  if (delta_t_np1 < 0.0) {
	    m_numTotalTruncFails++;
	    step_failed   = true;
	    delta_t_np1   = -delta_t_np1;
	    m_failure_counter += 2;
	    comment += "TIME TRUNC FAILURE";
	    rslt = "TRNC";
	  }

	  /*
	   * Prevent churning of the time step by not increasing the 
	   * time step,
	   * if the recent "History" of the time step behavior is still bad
	   */
	  else if (m_failure_counter > 0) {
	    delta_t_np1 = MIN(delta_t_np1, delta_t_n);
	  }
	} else {
	  delta_t_np1 = delta_t_n;
	}

	/* Decrease time step if a lot of Newton Iterations are
	 * taken.
	 * The idea being if more or less Newton iteration are taken
	 * than the target number of iterations, then adjust the time
	 * step downwards so that the target number of iterations or lower
	 * is achieved. This
	 * should prevent step failure by too many Newton iterations because
	 * the time step becomes too large.  CCO 
	 * hkm -> put in num_new_its min of 3 because the time step
	 *        was being altered even when num_newt_its == 1
	 */
	int max_Newton_steps = 10000;
	int target_num_iter  = 5;
	if (num_newt_its > 3000 && !step_failed) {
	  if (max_Newton_steps != target_num_iter){
	    double iter_diff        = num_newt_its     - target_num_iter;
	    double iter_adjust_zone = max_Newton_steps - target_num_iter;
	    double target_time_step = delta_t_n
	      *(1.0 - iter_diff*fabs(iter_diff)/
		((2.0*iter_adjust_zone*iter_adjust_zone)));
	    target_time_step = MAX(0.5*delta_t_n, target_time_step);
	    if (target_time_step < delta_t_np1) {
	      printf("\tNext time step will be decreased from %g to %g"
		     " because of new its restraint\n", 
		     delta_t_np1, target_time_step);
	      delta_t_np1 = target_time_step;
	    }
	  }
	}	

	
      }

      /*
       * The final loop in the time stepping algorithm depends on whether the
       * current step was a success or not.
       */
      if (step_failed) {
	/*
	 * Increment the counter indicating the number of consecutive
	 * failures
	 */
	numTSFailures++;
	/*
	 * Print out a statement about the failure of the time step.
	 */
	if (m_print_flag > 1) {
	  print_time_fail(convFailure, m_time_step_num, time_n, delta_t_n,
			  delta_t_np1, time_error_factor);
	} else if (m_print_flag == 1) {
	  print_lvl1_summary(m_time_step_num, time_n, rslt, delta_t_n,
			     num_newt_its, aztec_its, bktr_stps,
			     time_error_factor,
			     comment.c_str());
	}

	/*
	 * Change time step counters back to the previous step before
	 * the failed
	 * time step occurred.
	 */
	time_n     -= delta_t_n;
	delta_t_n   = delta_t_nm1;
	delta_t_nm1 = delta_t_nm2;

	/*
	 * Replace old solution vector and time derivative solution vector.
	 */
	dcopy_(&m_neq, m_y_nm1, &one, m_y_n, &one);
	dcopy_(&m_neq, m_ydot_nm1, &one, m_ydot_n,  &one);
	/*
	 * Decide whether to bail on the whole loop
	 */
	if (numTSFailures > 35) giveUp = true;
      }

      /*
       * Do processing for a successful step.
       */
      else {

	/*
	 * Decrement the number of consequative failure counter.
	 */
	m_failure_counter = MAX(0, m_failure_counter-1);

	/*
	 * Print out final results of a successfull time step.
	 */
	if (m_print_flag > 1) {
	  print_time_step2(m_time_step_num, m_order, time_n, time_error_factor,
			   delta_t_n, delta_t_np1);
	}
	else if (m_print_flag == 1) {
	  print_lvl1_summary(m_time_step_num, time_n, "    ", delta_t_n,
			     num_newt_its, aztec_its, bktr_stps, time_error_factor,
			     comment.c_str());
	}

	/*
	 * Output information at the end of every successful time step, if
	 * requested.
	 *
	 * fill in
	 */


      }
    } while (step_failed && !giveUp);
 
    /*
     * Send back the overall result of the time step.
     */
    if (step_failed) {
      if (time_n == 0.0) return -1234.0;
      return -time_n;
    }
    return time_n;
  }



  //-----------------------------------------------------------
  //                 Constants
  //-----------------------------------------------------------

  const double DampFactor = 4;
  const int NDAMP = 10;


  //-----------------------------------------------------------
  //                 MultiNewton methods
  //-----------------------------------------------------------
  /**
   * L2 Norm of a delta in the solution
   *
   *  The second argument has a default of false. However,
   *  if true, then a table of the largest values is printed
   *  out to standard output.
   */
  double BEulerInt::soln_error_norm(const double * const delta_y, 
				    bool printLargest)
  {
    int    i;
    double sum_norm = 0.0, error;
    for (i = 0; i < m_neq; i++) {
      error     = delta_y[i] / m_ewt[i];
      sum_norm += (error * error);
    }
    sum_norm = sqrt(sum_norm / m_neq); 
    if (printLargest) {
      const int num_entries = 8;
      double dmax1, normContrib;
      int j;
      int *imax = mdp_alloc_int_1(num_entries, -1);
      printf("\t\tPrintout of Largest Contributors to norm "
	     "of value (%g)\n", sum_norm);
      printf("\t\t         I    ysoln  deltaY  weightY  "
	     "Error_Norm**2\n");
      printf("\t\t   "); print_line("-", 80);
      for (int jnum = 0; jnum < num_entries; jnum++) {
	dmax1 = -1.0;
	for (i = 0; i < m_neq; i++) {
	  bool used = false;
	  for (j = 0; j < jnum; j++) {
	    if (imax[j] == i) used = true;
	  }
	  if (!used) {
	    error = delta_y[i] / m_ewt[i];
	    normContrib = sqrt(error * error);
	    if (normContrib > dmax1) {
	      imax[jnum] = i;
	      dmax1 = normContrib;
	    }
	  }
	}
	i = imax[jnum];
	if (i >= 0) {
	  printf("\t\t   %4d %12.4e %12.4e %12.4e %12.4e\n",
		 i, m_y_n[i], delta_y[i], m_ewt[i], dmax1);
	}	  
      }
      printf("\t\t   "); print_line("-", 80);
      mdp_safe_free((void **) &imax);
    }
    return sum_norm;
  }
#ifdef DEBUG_HKM_JAC
  SquareMatrix jacBack();
#endif
  /**************************************************************************
   *
   * doNewtonSolve():
   *
   * Compute the undamped Newton step.  The residual function is
   * evaluated at the current time, t_n, at the current values of the
   * solution vector, m_y_n, and the solution time derivative, m_ydot_n, 
   * but the Jacobian is not recomputed.
   */ 
  void BEulerInt::doNewtonSolve(double time_curr, double *y_curr, 
				double *ydot_curr, double* delta_y,
				GeneralMatrix& jac, int loglevel)
  {	
    int irow, jcol;
 
    m_func->evalResidNJ(time_curr, delta_t_n, y_curr,
			ydot_curr, delta_y, Base_ResidEval);
    m_nfe++;
    int sz = m_func->nEquations();
    for (int n = 0; n < sz; n++) {
      delta_y[n] = -delta_y[n];
    }


    /*
     * Column scaling -> We scale the columns of the Jacobian
     * by the nominal important change in the solution vector
     */
    if (m_colScaling) {
      if (!jac.factored()) {
	/*
	 * Go get new scales
	 */
	setColumnScales();

	/*
	 * Scale the new Jacobian
	 */
	double *jptr = &(*(jac.begin()));
	for (jcol = 0; jcol < m_neq; jcol++) {
	  for (irow = 0; irow < m_neq; irow++) {
	    *jptr *= m_colScales[jcol];
	    jptr++;
	  }
	}
      }	  
    }

    if (m_matrixConditioning) {
      if (jac.factored()) {
	m_func->matrixConditioning(0, sz, delta_y);
      } else {
	double *jptr = &(*(jac.begin()));
	m_func->matrixConditioning(jptr, sz, delta_y);
      }
    }

    /*
     * row sum scaling -> Note, this is an unequivical success
     *      at keeping the small numbers well balanced and
     *      nonnegative.
     */
    if (m_rowScaling) {
      if (! jac.factored()) {
	/*
	 * Ok, this is ugly. jac.begin() returns an vector<double> iterator
	 * to the first data location.
	 * Then &(*()) reverts it to a double *.
	 */
	double *jptr = &(*(jac.begin()));
	for (irow = 0; irow < m_neq; irow++) {
	  m_rowScales[irow] = 0.0;
	}
	for (jcol = 0; jcol < m_neq; jcol++) {
	  for (irow = 0; irow < m_neq; irow++) {
	    m_rowScales[irow] += fabs(*jptr);
	    jptr++;
	  }
	}
	
	jptr = &(*(jac.begin()));
	for (jcol = 0; jcol < m_neq; jcol++) {
	  for (irow = 0; irow < m_neq; irow++) {
	    *jptr /= m_rowScales[irow];
	    jptr++;
	  }
	}
      }
      for (irow = 0; irow < m_neq; irow++) {
	delta_y[irow] /= m_rowScales[irow];
      }
    }

#ifdef DEBUG_HKM_JAC
    bool  printJacContributions = false;
    if (m_time_step_num > 304) {
      printJacContributions = false;
    }
    int focusRow = 10;
    int numRows = 2;
    double RRow[2];
    bool freshJac = true;
    RRow[0] = delta_y[focusRow];
    RRow[1] = delta_y[focusRow+1];
    double Pcutoff = 1.0E-70;
    if (!jac.m_factored) {
      jacBack = jac;
    } else {
      freshJac = false;
    }
#endif
    /*
     * Solve the system -> This also involves inverting the
     * matrix
     */
    (void) jac.solve(delta_y);


    /*
     * reverse the column scaling if there was any.
     */
    if (m_colScaling) {
      for (irow = 0; irow < m_neq; irow++) {
	delta_y[irow] *= m_colScales[irow];
      }
    }
	
#ifdef DEBUG_HKM_JAC
    if (printJacContributions) {
      for (int iNum = 0; iNum < numRows; iNum++) {
	if (iNum > 0) focusRow++;
	double dsum = 0.0;
	vector_fp& Jdata = jacBack.data();
	double dRow = Jdata[m_neq * focusRow + focusRow];
	printf("\n Details on delta_Y for row %d \n", focusRow);
	printf("  Value before = %15.5e, delta = %15.5e,"
	       "value after = %15.5e\n", y_curr[focusRow], 
	       delta_y[focusRow],
	       y_curr[focusRow] +  delta_y[focusRow]);
	if (!freshJac) {
	  printf("    Old Jacobian\n");
	}
	printf("     col          delta_y            aij     "
	       "contrib   \n");
	printf("--------------------------------------------------"
	       "---------------------------------------------\n");
	printf(" Res(%d) %15.5e  %15.5e  %15.5e  (Res = %g)\n",
	       focusRow, delta_y[focusRow],
	       dRow, RRow[iNum] / dRow, RRow[iNum]);
	dsum +=  RRow[iNum] / dRow;
	for (int ii = 0; ii < m_neq; ii++) {
	  if (ii != focusRow) {
	    double aij =  Jdata[m_neq * ii + focusRow];
	    double contrib = aij * delta_y[ii] * (-1.0) / dRow;
	    dsum += contrib;
	    if (fabs(contrib) > Pcutoff) {
	      printf("%6d  %15.5e  %15.5e  %15.5e\n", ii,
		     delta_y[ii]  , aij, contrib); 
	    }
	  }
	}
	printf("--------------------------------------------------"
	       "---------------------------------------------\n");
	printf("        %15.5e                   %15.5e\n",
	       delta_y[focusRow], dsum);
      }
    }

#endif
	
    m_numTotalLinearSolves++;
  }

  //================================================================================================
  //  Bound the Newton step while relaxing the solution
  /*
   * Return the factor by which the undamped Newton step 'step0'
   * must be multiplied in order to keep all solution components in
   * all domains between their specified lower and upper bounds.
   * Other bounds may be applied here as well.
   *
   * Currently the bounds are hard coded into this routine:
   *
   *  Minimum value for all variables: - 0.01 * m_ewt[i]
   *  Maximum value = none.
   *
   * Thus, this means that all solution components are expected
   * to be numerical greater than zero in the limit of time step
   * truncation errors going to zero.
   *
   * Delta bounds: The idea behind these is that the Jacobian
   *               couldn't possibly be representative if the
   *               variable is changed by a lot. (true for
   *               nonlinear systems, false for linear systems) 
   *  Maximum increase in variable in any one newton iteration: 
   *   factor of 2
   *  Maximum decrease in variable in any one newton iteration:
   *   factor of 5
   *
   *   @param y       Current value of the solution
   *   @param step0   Current raw step change in y[]
   *   @param loglevel  Log level. This routine produces output if loglevel
   *                    is greater than one
   *
   *   @return        Returns the damping coefficient
   */
  double BEulerInt::boundStep(const double * const y, 
			      const double * const step0, int loglevel) {
    int i, i_lower = -1, i_fbounds, ifbd = 0, i_fbd = 0;
    double fbound = 1.0, f_lowbounds = 1.0, f_delta_bounds = 1.0;
    double ff, y_new, ff_alt;
    for (i = 0; i < m_neq; i++) {
      y_new = y[i] + step0[i];
      if ((y_new < (-0.01 * m_ewt[i])) && y[i] >= 0.0) {
	ff = 0.9 * (y[i] / (y[i] - y_new));
	if (ff < f_lowbounds) {
	  f_lowbounds = ff;
	  i_lower = i;
	}
      }
      /*
       * Now do a delta bounds
       * Increase variables by a factor of 2 only
       * decrease variables by a factor of 5 only
       */
      ff = 1.0;
      if ((fabs(y_new) > 2.0 * fabs(y[i])) && 
	  (fabs(y_new-y[i]) > m_ewt[i])) {
	ff = fabs(y[i]/(y_new - y[i]));
	ff_alt = fabs(m_ewt[i] / (y_new - y[i]));
	ff = MAX(ff, ff_alt);
	ifbd = 1;
      }
      if ((fabs(5.0 * y_new) < fabs(y[i])) &&
	  (fabs(y_new - y[i]) > m_ewt[i])) {
	ff = y[i]/(y_new-y[i]) * (1.0 - 5.0)/5.0;
	ff_alt = fabs(m_ewt[i] / (y_new - y[i]));
	ff = MAX(ff, ff_alt);
	ifbd = 0;
      }
      if (ff < f_delta_bounds) {
	f_delta_bounds = ff;
	i_fbounds = i;
	i_fbd = ifbd;
      }
      f_delta_bounds = MIN(f_delta_bounds, ff);
    }
    fbound = MIN(f_lowbounds, f_delta_bounds);
    /*
     * Report on any corrections
     */
    if (loglevel > 1) {
      if (fbound != 1.0) {
	if (f_lowbounds < f_delta_bounds) {
	  printf("\t\tboundStep: Variable %d causing lower bounds "
		 "damping of %g\n",
		 i_lower, f_lowbounds);
	} else {
	  if (ifbd) {
	    printf("\t\tboundStep: Decrease of Variable %d causing "
		   "delta damping of %g\n",
		   i_fbd, f_delta_bounds);
	  } else {
	    printf("\t\tboundStep: Increase of variable %d causing"
		   "delta damping of %g\n",
		   i_fbd, f_delta_bounds);
	  }
	}
      }
    }
    return fbound;
  }
  //================================================================================================
  /**************************************************************************
   *
   * dampStep():
   *
   * On entry, step0 must contain an undamped Newton step for the
   * solution x0. This method attempts to find a damping coefficient
   * such that the next undamped step would have a norm smaller than
   * that of step0. If successful, the new solution after taking the
   * damped step is returned in y1, and the undamped step at y1 is
   * returned in step1.
   */
  int BEulerInt::dampStep(double time_curr, const double * y0, 
			  const double *ydot0, const double* step0, 
			  double* y1, double* ydot1, double* step1,
			  double& s1, GeneralMatrix & jac, 
			  int& loglevel, bool writetitle,
			  int& num_backtracks) {

          
    // Compute the weighted norm of the undamped step size step0
    double s0 = soln_error_norm(step0);

    // Compute the multiplier to keep all components in bounds
    // A value of one indicates that there is no limitation
    // on the current step size in the nonlinear method due to
    // bounds constraints (either negative values of delta
    // bounds constraints.
    double fbound = boundStep(y0, step0, loglevel);

    // if fbound is very small, then y0 is already close to the
    // boundary and step0 points out of the allowed domain. In
    // this case, the Newton algorithm fails, so return an error
    // condition.
    if (fbound < 1.e-10) {
      if (loglevel > 1) printf("\t\t\tdampStep: At limits.\n");
      return -3;
    }

    //--------------------------------------------
    //           Attempt damped step
    //-------------------------------------------- 

    // damping coefficient starts at 1.0
    double damp = 1.0;
    int j, m;
    double ff;
    num_backtracks = 0;
    for (m = 0; m < NDAMP; m++) {

      ff = fbound*damp;

      // step the solution by the damped step size
      /*
       * Whenever we update the solution, we must also always
       * update the time derivative.
       */
      for (j = 0; j < m_neq; j++) {
	y1[j] = y0[j] + ff*step0[j];
	//                HKM setting intermediate y's to zero was a tossup.
	//                    slightly different, equivalent results
	//#ifdef DEBUG_HKM
	//	    y1[j] = MAX(0.0, y1[j]);
	//#endif
      }
      calc_ydot(m_order, y1, ydot1);
	  
      // compute the next undamped step, step1[], that would result 
      // if y1[] were accepted.

      doNewtonSolve(time_curr, y1, ydot1, step1, jac, loglevel);

#ifdef DEBUG_HKM
      for (j = 0; j < m_neq; j++) {
	checkFinite(step1[j]);
	checkFinite(y1[j]);
      }
#endif
      // compute the weighted norm of step1
      s1 = soln_error_norm(step1);

      // write log information
      if (loglevel > 3) {
	print_solnDelta_norm_contrib((const double *) step0, 
				     "DeltaSolnTrial",
				     (const double *) step1,
				     "DeltaSolnTrialTest",
				     "dampNewt: Important Entries for "
				     "Weighted Soln Updates:",
				     y0, y1, ff, 5);
      }
      if (loglevel > 1) {
	printf("\t\t\tdampNewt: s0 = %g, s1 = %g, fbound = %g,"
	       "damp = %g\n",  s0, s1, fbound, damp);
      }
#ifdef DEBUG_HKM
      if (loglevel > 2) {
	if (s1 > 1.00000001 * s0 && s1 > 1.0E-5) {
	  printf("WARNING: Possible Jacobian Problem "
		 "-> turning on more debugging for this step!!!\n");
	  print_solnDelta_norm_contrib((const double *) step0,
				       "DeltaSolnTrial",
				       (const double *) step1, 
				       "DeltaSolnTrialTest",
				       "dampNewt: Important Entries for "
				       "Weighted Soln Updates:", 
				       y0, y1, ff, 5);
	  loglevel = 4;
	}
      }
#endif

      // if the norm of s1 is less than the norm of s0, then
      // accept this damping coefficient. Also accept it if this
      // step would result in a converged solution. Otherwise,
      // decrease the damping coefficient and try again.
	  
      if (s1 < 1.0E-5 || s1 < s0) {
	if (loglevel > 2) {
	  if (s1 > s0) {
	    if (s1 > 1.0) {
	      printf("\t\t\tdampStep: current trial step and damping"
		     " coefficient accepted because test step < 1\n");
	      printf("\t\t\t          s1 = %g, s0 = %g\n", s1, s0);
	    }
	  }
	}
	break;
      } else {
	if (loglevel > 1) {
	  printf("\t\t\tdampStep: current step rejected: (s1 = %g > "
		 "s0 = %g)", s1, s0);
	  if (m < (NDAMP-1)) {
	    printf(" Decreasing damping factor and retrying");
	  } else {
	    printf(" Giving up!!!");
	  }
	  printf("\n");
	}
      }
      num_backtracks++;
      damp /= DampFactor;
    }

    // If a damping coefficient was found, return 1 if the
    // solution after stepping by the damped step would represent
    // a converged solution, and return 0 otherwise. If no damping
    // coefficient could be found, return -2.
    if (m < NDAMP) {
      if (s1 > 1.0) return 0;
      else return 1;
    } else {
      if (s1 < 0.5 && (s0 < 0.5)) return 1;
      if (s1 < 1.0) return 0;
      return -2;
    }
  }
  //================================================================================================ 
  // Solve a nonlinear system
  /*   
   * Find the solution to F(X, xprime) = 0 by damped Newton iteration.  On
   * entry, y_comm[] contains an initial estimate of the solution and 
   * ydot_comm[] contains an estimate of the derivative.
   *   On  successful return, y_comm[] contains the converged solution
   * and ydot_comm[] contains the derivative
   *
   *
   * @param y_comm[] Contains the input solution. On output y_comm[] contains
   *                 the converged solution
   * @param ydot_comm  Contains the input derivative solution. On output y_comm[] contains
   *                 the converged derivative solution
   * @param CJ       Inverse of the time step
   * @param time_curr  Current value of the time
   * @param jac      Jacobian
   * @param num_newt_its  number of newton iterations
   * @param num_linear_solves number of linear solves
   * @param num_backtracks number of backtracs
   * @param loglevel  Log level
   */
  int BEulerInt::solve_nonlinear_problem(double * const y_comm,
					 double * const ydot_comm, double CJ,
					 double time_curr, 
					 GeneralMatrix & jac,
					 int &num_newt_its,
					 int &num_linear_solves,
					 int &num_backtracks, 
					 int loglevel)
  {
    bool m_residCurrent = false;
    int m = 0;
    bool forceNewJac = false;
    double s1=1.e30;

    double * y_curr    = mdp_alloc_dbl_1(m_neq, 0.0);
    double * ydot_curr = mdp_alloc_dbl_1(m_neq, 0.0);
    double * stp       = mdp_alloc_dbl_1(m_neq, 0.0);
    double * stp1      = mdp_alloc_dbl_1(m_neq, 0.0);
    double * y_new     = mdp_alloc_dbl_1(m_neq, 0.0);
    double * ydot_new  = mdp_alloc_dbl_1(m_neq, 0.0);

    mdp_copy_dbl_1(y_curr, y_comm, m_neq);
    mdp_copy_dbl_1(ydot_curr, ydot_comm, m_neq);

    bool frst = true;
    num_newt_its = 0;
    num_linear_solves = - m_numTotalLinearSolves;
    num_backtracks = 0;
    int i_backtracks;
 
    while (1 > 0) {

      /*
       * Increment Newton Solve counter
       */
      m_numTotalNewtIts++;
      num_newt_its++;


      if (loglevel > 1) {
	printf("\t\tSolve_Nonlinear_Problem: iteration %d:\n",
	       num_newt_its);
      }

      // Check whether the Jacobian should be re-evaluated.
            
      forceNewJac = true;
            
      if (forceNewJac) {
	if (loglevel > 1) {
	  printf("\t\t\tGetting a new Jacobian and solving system\n");
	}
	beuler_jac(jac, m_resid, time_curr, CJ, y_curr, ydot_curr,
		   num_newt_its);
	m_residCurrent = true;
      } else {
	if (loglevel > 1) {
	  printf("\t\t\tSolving system with old jacobian\n");
	}
	m_residCurrent = false;
      }

      // compute the undamped Newton step
      doNewtonSolve(time_curr, y_curr, ydot_curr, stp, jac, loglevel);
	  
      // damp the Newton step
      m = dampStep(time_curr, y_curr, ydot_curr, stp, y_new, ydot_new, 
		   stp1, s1, jac, loglevel, frst, i_backtracks);
      frst = false;
      num_backtracks += i_backtracks;

      /*
       * Impose the minimum number of newton iterations critera
       */
      if (num_newt_its < m_min_newt_its) {
	if (m == 1) m = 0;
      }
      /*
       * Impose max newton iteration
       */
      if (num_newt_its > 20) {
	m = -1;
	if (loglevel > 1) {
	  printf("\t\t\tDampnewton unsuccessful (max newts exceeded) sfinal = %g\n", s1);
	}
      }

      if (loglevel > 1) {
	if (m == 1) {
	  printf("\t\t\tDampNewton iteration successful, nonlin "
		 "converged sfinal = %g\n", s1);
	} else if (m == 0) {
	  printf("\t\t\tDampNewton iteration successful, get new"
		 "direction, sfinal = %g\n", s1);
	} else {
	  printf("\t\t\tDampnewton unsuccessful sfinal = %g\n", s1);
	}
      }

      // If we are converged, then let's use the best solution possible
      // for an end result. We did a resolve in dampStep(). Let's update
      // the solution to reflect that.
      // HKM 5/16 -> Took this out, since if the last step was a 
      //             damped step, then adding stp1[j] is undamped, and
      //             may lead to oscillations. It kind of defeats the
      //             purpose of dampStep() anyway.
      // if (m == 1) {
      //  for (int j = 0; j < m_neq; j++) {
      //   y_new[j] += stp1[j];
      //                HKM setting intermediate y's to zero was a tossup.
      //                    slightly different, equivalent results
      // #ifdef DEBUG_HKM
      //	      y_new[j] = MAX(0.0, y_new[j]);
      // #endif
      //  }
      // }
	  
      bool m_filterIntermediate = false;
      if (m_filterIntermediate) {
	if (m == 0) {
	  (void) filterNewStep(time_n, y_new, ydot_new);
	}
      }
      // Exchange new for curr solutions
      if (m == 0 || m == 1) {
	mdp_copy_dbl_1(y_curr, y_new, m_neq);
	calc_ydot(m_order, y_curr, ydot_curr);
      }

      // convergence
      if (m == 1) goto done;

      // If dampStep fails, first try a new Jacobian if an old
      // one was being used. If it was a new Jacobian, then
      // return -1 to signify failure.
      else if (m < 0) {
	goto done;
      }
    }

  done:
    // Copy into the return vectors
    mdp_copy_dbl_1(y_comm, y_curr, m_neq);
    mdp_copy_dbl_1(ydot_comm, ydot_curr, m_neq);
    // Increment counters
    num_linear_solves += m_numTotalLinearSolves;
    // Free memory
    mdp_safe_free((void **) &y_curr);
    mdp_safe_free((void **) &ydot_curr);
    mdp_safe_free((void **) &stp);
    mdp_safe_free((void **) &stp1);
    mdp_safe_free((void **) &y_new);
    mdp_safe_free((void **) &ydot_new);

    double time_elapsed = 0.0;
    if (loglevel > 1) {
      if (m == 1) {
	printf("\t\tNonlinear problem solved successfully in "
	       "%d its, time elapsed = %g sec\n",
	       num_newt_its, time_elapsed);
      }
    }
    return m;
  }
  //================================================================================================
  /*
   *
   *
   */
  void BEulerInt::
  print_solnDelta_norm_contrib(const double * const solnDelta0,
			       const char * const s0,
			       const double * const solnDelta1,
			       const char * const s1,
			       const char * const title,
			       const double * const y0,
			       const double * const y1,
			       double damp,
			       int num_entries) {
    int i, j, jnum;
    bool used;
    double dmax0, dmax1, error, rel_norm;
    printf("\t\t%s currentDamp = %g\n", title, damp);
    printf("\t\t         I  ysoln %10s ysolnTrial "
	   "%10s weight relSoln0 relSoln1\n", s0, s1);
    int *imax = mdp_alloc_int_1(num_entries, -1);
    printf("\t\t   "); print_line("-", 90);
    for (jnum = 0; jnum < num_entries; jnum++) {
      dmax1 = -1.0;
      for (i = 0; i < m_neq; i++) {
	used = false;
	for (j = 0; j < jnum; j++) {
	  if (imax[j] == i) used = true;
	}
	if (!used) {
	  error     = solnDelta0[i] /  m_ewt[i];
	  rel_norm = sqrt(error * error);
	  error     = solnDelta1[i] /  m_ewt[i];
	  rel_norm += sqrt(error * error);
	  if (rel_norm > dmax1) {
	    imax[jnum] = i;
	    dmax1 = rel_norm;
	  }
	}
      }
      if (imax[jnum] >= 0) {
	i = imax[jnum];
	error = solnDelta0[i] /  m_ewt[i];
	dmax0 = sqrt(error * error);
	error = solnDelta1[i] /  m_ewt[i];
	dmax1 = sqrt(error * error);
	printf("\t\t   %4d %12.4e %12.4e %12.4e  %12.4e "
	       "%12.4e %12.4e %12.4e\n",
	       i, y0[i], solnDelta0[i], y1[i],
	       solnDelta1[i], m_ewt[i], dmax0, dmax1);
      }
    }
    printf("\t\t   "); print_line("-", 90);
    mdp_safe_free((void **) &imax);
  }
  //===============================================================================================

} // End of namespace Cantera

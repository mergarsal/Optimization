/** This header file provides several lightweight template classes implementing
* proximal gradient algorithms for minimizing a sum of the form
*
* F(x) := f(x) + g(x),
*
* where f and g are convex and f is continuously differentiable.  This
* implementation is based upon the algorithm described in Section 4.2 of Parikh
* and Boyd's monograph "Proximal Algorithms".
*
* Copyright (C) 2017 by David M. Rosen (drosen2000@gmail.com)
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <experimental/optional>
#include <functional>
#include <iostream>
#include <limits>
#include <vector>

#include "Optimization/Convex/Concepts.h"
#include "Optimization/Util/Stopwatch.h" // Useful timing functions

namespace Optimization {
namespace Convex {

/** An alias template for a user-definable function that can be
 * used to access various interesting bits of information about the internal
 * state of the proximal gradient optimization algorithm as it runs.  More
 * precisely, this function is called at the end of each completed iteration,
 * and is provided access to the following quantities:
 *
 * t: total elapsed computation time at the *start* of the current iteration
 * x: iterate at the *start* of the iteration
 * F: function value at the *start* of the current iteration
 * r: residual of the proximal fixed-point equation (measure of optimality)
 * linesearch_iters:  Number of iterations performed by the backtracking line
 *    search (only nonzero when backtracking linesearch is used)
 * dx: composite update step for this iteration (= x_next - x)
 * dF: decrease in function value during the current iteration
 */
template <typename Variable, typename... Args>
using ProximalGradientUserFunction =
    std::function<void(double t, const Variable &x, double F, double r,
                       unsigned int linesearch_iters, const Variable &dx,
                       double dF, Args &... args)>;

enum ProximalGradientMode {
  /// Basic proximal gradient
  SIMPLE,

  /// FISTA
  ACCELERATED,
  // ADAPTIVE_STEPSIZE, // SpaRSA
};

struct ProximalGradientParams : public OptimizerParams {

  /// Algorithm mode
  ProximalGradientMode mode = ACCELERATED;

  /// Step control parameters

  // An estimate of the Lipschitz parameter for grad_f (if known)
  double L = 1;

  // A Boolean value indicating whether to use a constant stepsize, or perform
  // backtracking line search.  If a constant stepsize is used, L must be set to
  // a Lipschitz constant for grad_f in order to ensure the algorithm's
  // convergence
  bool linesearch = true;

  // Shrinkage parameter (multiplicative factor) for the stepsize when
  // performing backtracking line search
  double beta = .5;

  // A Boolean value indicating whether to employ O'Donoghue and Candes's
  // gradient-based adaptive restart scheme for accelerated gradient methods
  // (only relevant for mode == ACCELERATED
  bool adaptive_restart = true;

  /// Termination criteria

  // Maximum number of iterations to perform during backtracking line search
  unsigned int max_LS_iterations = 100;

  // Stopping tolerance based on the normalized subgradient residual given in
  // eq. (42) of Goldstein et al.'s paper "A Field Guide to Forward-Backward
  // Splitting with a FASTA Implementation"
  double epsilon = 1e-3;
};

enum ProximalGradientStatus {
  PROX_GRAD_RESIDUAL,
  ITERATION_LIMIT,
  LINESEARCH,
  ELAPSED_TIME,
};

/** A useful struct used to hold the output of the proximal gradient
optimization method */
template <typename Variable>
struct ProximalGradientResult : OptimizerResult<Variable> {

  // The stopping condition that triggered algorithm termination
  ProximalGradientStatus status;

  // The residual of the proximal gradient fixed-point optimality condition at
  // the START of each iteration
  std::vector<double> residuals;
};

template <typename Variable, typename... Args>
ProximalGradientResult<Variable> ProximalGradient(
    const Objective<Variable, Args...> &f,
    const GradientOperator<Variable, Args...> &grad_f,
    const Objective<Variable, Args...> &g,
    const ProximalOperator<Variable, Args...> &prox_g, const Variable &x0,
    Args &... args,
    const ProximalGradientParams &params = ProximalGradientParams(),
    const std::experimental::optional<
        ProximalGradientUserFunction<Variable, Args...>> &user_function =
        std::experimental::nullopt) {

  /// Declare some useful variables

  // Composite objective function
  Objective<Variable, Args...> F = [&](const Variable &X, Args &... args) {
    return f(X, args...) + g(X, args...);
  };

  // Current iterate
  Variable x;

  // Previous iterate
  Variable x_prev;

  // The point at which to evaluate the proximal-gradient map p_L(y) (cf. eq.
  // (1.19) of Beck and Teboulle's "Gradient-Based Algorithms with Applications
  // to Signal Recovery Problems").  For simple (i.e. non-accelerated)
  // proximal-gradient methods, this is simply the previous accepted iterate
  // x_prev, while in accelerated gradient schemes, this is the momentum-based
  // forward prediction x + theta * (x - x_prev)
  Variable y;

  // Gradient of the smooth term f in the objective, evaluated at y
  Variable grad_f_y;

  // Current gradient update stepsize; related to the Lipschitz constant
  // L for grad_f(y) via lambda in (0, 1/L]
  double lambda;

  // Image of the gradient update for the smooth term f in the objective,
  // applied to y:
  //
  // hat_y := y - (1/L) * grad_f(y)
  //        = y - lambda * grad_f(y)
  //
  // Note that this is the 'first half' of the computation needed to evaluate
  // the proximal gradient map:
  //
  // p_L(y) := prox_{1/L}(y - (1/L) * grad_f(y))
  //         = prox_{lambda}(y - lambda * grad_f(y))
  //
  // (cf. eq. (1.19) of Beck and Teboulle's "Gradient-Based Algorithms with
  // Applications to Signal Recovery Problems")
  Variable hat_y;

  // Value of the composite objective at the current iterate x
  double F_x;

  // Value of composite objective at the previous iterate x_prev;
  double F_x_prev;

  // Residual of the fixed-point optimality condition at x
  double residual;

  // Number of iterations performed in the backtracking linesearch in the
  // current iteration
  unsigned int linesearch_iters = 0;

  /// Parameters for use with accelerated proximal gradient method
  // Momentum parameter for current iterate and previous iterates
  double t, t_prev;

  /// Output struct
  ProximalGradientResult<Variable> result;
  result.status = ITERATION_LIMIT;

  /// INITIALIZATION
  x_prev = x0;
  F_x_prev = F(x_prev, args...);
  y = x0;
  lambda = 1 / params.L;
  t_prev = 1;

  if (params.verbose) {
    std::cout << std::scientific;
    std::cout.precision(params.precision);
  }
  // Field with for displaying outer iterations
  unsigned int iter_field_width = floor(log10(params.max_iterations)) + 1;
  std::cout << "Proximal gradient optimization: " << std::endl << std::endl;

  /// ITERATE!
  auto start_time = Stopwatch::tick();
  for (unsigned int i = 0; i < params.max_iterations; i++) {

    double elapsed_time = Stopwatch::tock(start_time);

    /// Forward (gradient) step
    grad_f_y = grad_f(y, args...);
    hat_y = y - lambda * grad_f_y;

    /// Backward (proximal) step
    x = prox_g(hat_y, lambda, args...);
    F_x = F(x, args...);

    if (params.linesearch) {
      /// Beck and Teboulle's backtracking linesearch (cf. Secs. 1.4.3 and 1.5.2
      /// of Beck and Teboulle's "Gradient-Based Algorithms with Applications to
      /// Signal Processing")

      // Reset counter
      linesearch_iters = 0;

      // Ensure sufficient decrease with respect to the quadratic model Q_L(x,y)
      // defined at the beginning of Sec. 1.4.1
      Variable x_minus_y = x - y;
      double f_y = f(y, args...);

      while ((F_x > f_y + x_minus_y.dot(grad_f_y) +
                        (1 / (2 * lambda)) * sqrt(x_minus_y.dot(x_minus_y)) +
                        g(x, args...)) &&
             (linesearch_iters <= params.max_LS_iterations)) {

        linesearch_iters++;

        // Shrink stepsize
        lambda *= params.beta;

        // Update hat_y, x, F_x, and x - y
        /// Forward (gradient) step
        hat_y = y - lambda * grad_f_y;
        /// Backward (proximal step)
        x = prox_g(hat_y, lambda, args...);
        F_x = F(x, args...);
        x_minus_y = x - y;
      } // while(...)
    }   // linesearch

    if (params.linesearch && (linesearch_iters > params.max_LS_iterations)) {
      // The linesearch was unable to make sufficient progress in the allotted
      // number of iterations
      result.status = LINESEARCH;
      break;
    }

    /// ANALYZE THIS ITERATION

    // Compute composite step dx for this iteration
    Variable dx = x - x_prev;

    double norm_dx = sqrt(dx.dot(dx));

    // Compute improvement in objective
    double dF = F_x_prev - F_x;

    // Compute normalized subgradient stopping criterion according to eq. (42)
    // of Goldstein et al.'s "Field Guide to Forward-Backward Splitting"
    Variable grad_f_x = grad_f(x, args...);
    Variable subgrad_g_x = (1 / lambda) * (hat_y - x);
    Variable subgrad_F_x = grad_f_x + subgrad_g_x;
    residual = sqrt(subgrad_F_x.dot(subgrad_F_x)) /
               (std::max<double>(sqrt(grad_f_x.dot(grad_f_x)),
                                 sqrt(subgrad_g_x.dot(subgrad_g_x))) +
                1e-6);

    /// Display output for this iteration

    // Display information about this iteration, if requested
    if (params.verbose) {
      std::cout << "Iter: ";
      std::cout.width(iter_field_width);
      std::cout << i << ", time: " << elapsed_time << ", F: ";
      std::cout.width(params.precision + 7);
      std::cout << F_x_prev << ", res: " << residual
                << ", ls iters: " << linesearch_iters << ", |dx|: " << norm_dx
                << ", dF: ";
      std::cout.width(params.precision + 7);
      std::cout << dF << std::endl;
    }

    /// Record output
    result.time.push_back(elapsed_time);
    result.objective_values.push_back(F_x_prev);
    result.residuals.push_back(residual);

    /// Call user function and pass information about the current iteration,
    /// if one was provided
    if (user_function)
      (*user_function)(elapsed_time, x_prev, F_x_prev, residual,
                       linesearch_iters, dx, dF, args...);

    /// Test stopping criteria
    if (residual < params.epsilon) {
      result.status = PROX_GRAD_RESIDUAL;
      break;
    }
    if (Stopwatch::tock(start_time) > params.max_computation_time) {
      result.status = ELAPSED_TIME;
      break;
    }

    if (params.mode == ACCELERATED) {

      /// Parameter update and caching for this iteation

      // Update momentum parameter and perform forward prediction
      if (params.adaptive_restart) {
        // We employ the adaptive restart scheme described in eq. (13) of
        // O'Donoghue and Candes's paper "Adaptive Restart for Accelerated
        // Gradient Schemes"
        if (dx.dot(y - x) > 0)
          t_prev = 1;
      }

      t = (1 + sqrt(1 + 4 * t_prev * t_prev)) / 2;
      y = x + ((t_prev - 1) / t) * (x - x_prev);

      // Cache parameters for this iteration
      t_prev = t;
    } else {
      // In the case of non-accelerated gradient methods, we simply evaluate
      // the
      // proximal-gradient operator p_L(y) in the next iteration at the
      // current
      // iterate x;
      y = x;
    }
    x_prev = x;
    F_x_prev = F_x;
  } // Iterations

  /// Record output
  result.x = x;
  result.f = F_x;
  result.elapsed_time = Stopwatch::tock(start_time);

  /// Print final output, if requested
  if (params.verbose) {
    std::cout << std::endl
              << std::endl
              << "Optimization finished!" << std::endl;

    // Print the reason for termination
    switch (result.status) {
    case PROX_GRAD_RESIDUAL:
      std::cout << "Found minimizer! (Proximal fixed-point residual: "
                << residual << ")" << std::endl;
      break;
    case ITERATION_LIMIT:
      std::cout << "Algorithm exceeded maximum number of outer iterations"
                << std::endl;
      break;
    case LINESEARCH:
      std::cout << "Linesearch was unable to find an update with adequate "
                   "progress in the allotted number of iterations!"
                << std::endl;
      break;
    case ELAPSED_TIME:
      std::cout << "Algorithm exceeded maximum allowed computation time: "
                << result.elapsed_time << " > " << params.max_computation_time
                << std::endl;
      break;
    }

    std::cout << "Final objective value: " << result.f << std::endl;
    std::cout << "Total elapsed computation time: " << result.elapsed_time
              << " seconds" << std::endl
              << std::endl;
  }

  return result;
}

} // Convex

} // Optimization

/** This header provides a basic set of concepts that are common to all
 * numerical optimization methods.
 *
 * Copyright (C) 2017 by David M. Rosen (drosen2000@gmail.com)
 */

#pragma once

#include <functional>
#include <limits>
#include <vector>

namespace Optimization {

/** Useful typedefs for implementing templated/parameterized numerical
 * optimization methods.
 *
 * In the following definitions:
 *
 * - Variable is the typename for the argument of the objective function (i.e.,
 * this is the type of variable over which we optimize).
 *
 * - Args is a user-definable variadic template parameter whose instances will
 * be passed into each of the user-supplied functions required to instantiate a
 * particular optimization method (e.g. objective, derivative map, linear
 * operators, etc.).  This design enables the use of objective functions that
 * accept/require non-optimized parameters of arbitrary type, and also allows
 * users to define cache variables that can be used to store, share, and reuse
 * intermediate computational results.
*/

/** An alias template for a std::function that accepts as input an argument of
 * type Variable, and returns an objective value. */
template <typename Variable, typename... Args>
using Objective = std::function<double(const Variable &X, Args &... args)>;

/** A lightweight struct containing a set of basic configuration parameters
 * for an iterative numerical optimization method. */
struct OptimizerParams {

  // A limit on the total number of iterations to perform
  unsigned int max_iterations = 100;

  // A limit on the total permissible elapsed computation time (in seconds)
  double max_computation_time = std::numeric_limits<double>::max();

  // A Boolean flag indicating whether the optimization method should print
  // information to stdout as it runs
  bool verbose = false;

  // Decimal precision for real-valued variables in printed output
  unsigned int precision = 3;
};

/** A basic templated struct used to capture the output of an interative
 * numerical optimization method. */
template <typename Variable> struct OptimizerResult {

  // The estimated minimizer
  Variable x;

  // The estimated optimal value
  double f;

  // The elapsed computation time
  double elapsed_time;

  // The sequence of objective values at the *start* of each iteration
  std::vector<double> objective_values;

  // The total elapsed computation time at the *start* of each iteration (in
  // seconds)
  std::vector<double> time;
};
}

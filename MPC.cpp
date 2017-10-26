#include "MPC.h"
#include <cppad/cppad.hpp>
#include <cppad/ipopt/solve.hpp>
#include "Eigen-3.3/Eigen/Core"

using CppAD::AD;

// Global parameters used both be GF_eval & MPC
size_t N = 25;							// steps
size_t x_start = 0;
size_t y_start = x_start + N;
size_t psi_start = y_start + N;
size_t v_start = psi_start + N;
size_t cte_start = v_start + N;
size_t epsi_start = cte_start + N;
size_t delta_start = epsi_start + N;
size_t a_start = delta_start + N - 1;

class FG_eval
{//FG_eval
 public:
  // Fitted polynomial coefficients
  Eigen::VectorXd coeffs;
  double ref_v;
  double dt;
  double Lf;
  FG_eval(Eigen::VectorXd coeffs)
  {//construct
	  this->coeffs = coeffs;
	  ref_v        = 40.0;
	  dt           = 0.05;
	  Lf           = 2.67;
  }

  // define ADvector to interact with CppAD
  typedef CPPAD_TESTVECTOR(AD<double>) ADvector;

  // `fg` is a vector containing the cost and constraints.
  // `vars` is a vector containing the variable values (state & actuators)
  // this operator determines the cost
  void operator()(ADvector& fg, const ADvector& vars)
  {//operator
	  // cost = fg[0]
	  fg[0] = 0;

	  // The part of the cost based on the reference state.
	  for (unsigned int t = 0; t < N; t++)
	  {//cost
		  fg[0] += CppAD::pow(vars[cte_start + t], 2);		// cte
	      fg[0] += CppAD::pow(vars[epsi_start + t], 2);		// orientation
	      fg[0] += CppAD::pow(vars[v_start + t] - ref_v, 2);  // velocity penalty
	   }

	    // Minimize the use of actuators.
	    for (unsigned int t = 0; t < N - 1; t++)
	    {
	    	fg[0] += CppAD::pow(vars[delta_start + t], 2);  	// delta amplitude penalty
	        fg[0] += CppAD::pow(vars[a_start + t], 2);			// acceleration amplitude penalty
	    }

	    // Minimize the value gap between sequential actuations.
	    for (unsigned int t = 0; t < N - 2; t++)
	    {
	    	fg[0] += CppAD::pow(vars[delta_start + t + 1] - vars[delta_start + t], 2);
	        fg[0] += CppAD::pow(vars[a_start + t + 1] - vars[a_start + t], 2);
	    }

	    // Setup model Constraints
	    // -----------------------
	    // Initial constraints:
	    // add 1 to each of the starting indices due to cost being located at [0]
	    fg[1 + x_start] = vars[x_start];
	    fg[1 + y_start] = vars[y_start];
	    fg[1 + psi_start] = vars[psi_start];
	    fg[1 + v_start] = vars[v_start];
	    fg[1 + cte_start] = vars[cte_start];
	    fg[1 + epsi_start] = vars[epsi_start];

	    // The rest of the constraints
	    for (unsigned int t = 1; t < N; t++)
	    {// trajectory points
	    	// The idea here is to constraint this value to be 0

	    	// The state at time t+1 .
	    	AD<double> x1 = vars[x_start + t];
	    	AD<double> y1 = vars[y_start + t];
	    	AD<double> psi1 = vars[psi_start + t];
	    	AD<double> v1 = vars[v_start + t];
	    	AD<double> cte1 = vars[cte_start + t];
	    	AD<double> epsi1 = vars[epsi_start + t];

	    	// The state at time t.
	    	AD<double> x0 = vars[x_start + t - 1];
	    	AD<double> y0 = vars[y_start + t - 1];
	    	AD<double> psi0 = vars[psi_start + t - 1];
	    	AD<double> v0 = vars[v_start + t - 1];
	    	AD<double> cte0 = vars[cte_start + t - 1];
	    	AD<double> epsi0 = vars[epsi_start + t - 1];

	    	// Only consider the actuation at time t.
	    	AD<double> delta0 = vars[delta_start + t - 1];
	    	AD<double> a0 = vars[a_start + t - 1];

	    	AD<double> f0 = coeffs[0] + coeffs[1] * x0;
	    	AD<double> psides0 = CppAD::atan(coeffs[1]);

	    	// equations for the model:
	    	// x_[t+1] = x[t] + v[t] * cos(psi[t]) * dt
	    	// y_[t+1] = y[t] + v[t] * sin(psi[t]) * dt
	    	// psi_[t+1] = psi[t] + v[t] / Lf * delta[t] * dt
	    	// v_[t+1] = v[t] + a[t] * dt
	    	// cte[t+1] = f(x[t]) - y[t] + v[t] * sin(epsi[t]) * dt
	    	// epsi[t+1] = psi[t] - psides[t] + v[t] * delta[t] / Lf * dt
	    	fg[1 + x_start + t] = x1 - (x0 + v0 * CppAD::cos(psi0) * dt);
	    	fg[1 + y_start + t] = y1 - (y0 + v0 * CppAD::sin(psi0) * dt);
	    	fg[1 + psi_start + t] = psi1 - (psi0 + v0 * delta0 / Lf * dt);
	    	fg[1 + v_start + t] = v1 - (v0 + a0 * dt);
	    	fg[1 + cte_start + t] = cte1 - ((f0 - y0) + (v0 * CppAD::sin(epsi0) * dt));
	    	fg[1 + epsi_start + t] = epsi1 - ((psi0 - psides0) + v0 * delta0 / Lf * dt);
	    }// trajectory points
  	}//operator
  };//FG_eval


MPC::MPC()
{
	max_steering_angle_rad = 1.0;
	max_acc                = 1.0;

	// N time-steps == N-1 actuations
	n_vars 		  = N * 6 + (N - 1) * 2;
	n_constraints = N * 6;
}
MPC::~MPC() {}

vector<double> MPC::Solve(Eigen::VectorXd state, Eigen::VectorXd coeffs)
{//solve
  bool ok = true;
  typedef CPPAD_TESTVECTOR(double) Dvector;


  // easier notation
  double x = state[0];
  double y = state[1];
  double psi = state[2];
  double v =  state[3];
  double cte = state[4];
  double epsi = state[5];

  // (1) independent variables
  Dvector vars(n_vars);
  {//initialize vars
	  Dvector vars(n_vars);
	  for (unsigned int i = 0; i < n_vars; i++)
	  {
		  vars[i] = 0;
	  }

	  // Set the initial variable values
	  vars[x_start] = x;
	  vars[y_start] = y;
	  vars[psi_start] = psi;
	  vars[v_start] = v;
	  vars[cte_start] = cte;
	  vars[epsi_start] = epsi;
  }// initialize vars

  // (2) Lower and upper limits for x
  Dvector vars_lowerbound(n_vars);
  Dvector vars_upperbound(n_vars);
  {// sets bounds
    	// Set all non-actuators upper and lowerlimits
    	// to the max negative and positive values.
    	for (unsigned int i = 0; i < delta_start; i++)
    	{
    		vars_lowerbound[i] = -1.0e19;
    		vars_upperbound[i] = 1.0e19;
    	}

    	// bounds of delta in radians
    	for (unsigned int i = delta_start; i < a_start; i++)
    	{
    		vars_lowerbound[i] = -max_steering_angle_rad;
    		vars_upperbound[i] = max_steering_angle_rad;
    	}

    	// Acceleration/decceleration bounds
    	for (unsigned int i = a_start; i < n_vars; i++)
    	{
    		vars_lowerbound[i] = -max_acc;
    		vars_upperbound[i] = max_acc;
    	}
  }// set bounds

  // Lower and upper limits for constraints
  Dvector constraints_lowerbound(n_constraints);
  Dvector constraints_upperbound(n_constraints);
  {//constraint bounds
    for (unsigned int i = 0; i < n_constraints; i++)
    {
        constraints_lowerbound[i] = 0;
        constraints_upperbound[i] = 0;
    }
    constraints_lowerbound[x_start] = x;
    constraints_lowerbound[y_start] = y;
    constraints_lowerbound[psi_start] = psi;
    constraints_lowerbound[v_start] = v;
    constraints_lowerbound[cte_start] = cte;
    constraints_lowerbound[epsi_start] = epsi;

    constraints_upperbound[x_start] = x;
    constraints_upperbound[y_start] = y;
    constraints_upperbound[psi_start] = psi;
    constraints_upperbound[v_start] = v;
    constraints_upperbound[cte_start] = cte;
    constraints_upperbound[epsi_start] = epsi;
  }// constraint bounds


  // object that computes objective and constraints
  FG_eval fg_eval(coeffs);


  std::string options;	// options for IPOPT solver
  {//options
	  // Uncomment this if you'd like more print information
	  options += "Integer print_level  0\n";
	  // NOTE: Setting sparse to true allows the solver to take advantage
	  // of sparse routines, this makes the computation MUCH FASTER. If you
	  // can uncomment 1 of these and see if it makes a difference or not but
	  // if you uncomment both the computation time should go up in orders of
	  // magnitude.
	  options += "Sparse  true        forward\n";
	  options += "Sparse  true        reverse\n";
	  // NOTE: Currently the solver has a maximum time limit of 0.5 seconds.
	  // Change this as you see fit.
	  options += "Numeric max_cpu_time          0.5\n";
  }

  // place to return solution
  CppAD::ipopt::solve_result<Dvector> solution;

  // solve the problem
  CppAD::ipopt::solve<Dvector, FG_eval>(
      options, vars, vars_lowerbound, vars_upperbound, constraints_lowerbound,
      constraints_upperbound, fg_eval, solution);

  // Check some of the solution values
  ok &= solution.status == CppAD::ipopt::solve_result<Dvector>::success;

  // Cost
  auto cost = solution.obj_value;
  std::cout << "Cost " << cost << std::endl;

  //Return the first actuator values. The variables can be accessed with
  return {solution.x[delta_start],   solution.x[a_start]};
}//solve
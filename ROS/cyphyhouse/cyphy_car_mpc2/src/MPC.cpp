#include "MPC.h"
#include <cppad/cppad.hpp>
#include <cppad/ipopt/solve.hpp>
#include "Eigen/Dense"
#include <cmath>
#include "geometry_msgs/PoseStamped.h"
#include "geometry_msgs/PointStamped.h"

using CppAD::AD;

// Set the timestep length and duration
size_t N = 10;
double dt = 0.1;

//Geometric parameters of car
const double lr = 0.3;

//State and input hard constraints
const double x_bound = 3.0;
const double y_bound = 3.0;
const double dir_bound = 0.35;
const double vel_bound = 3.0;

//Initialize
size_t x_start = 0;
size_t y_start = x_start + N;
size_t psi_start = y_start + N;
size_t cte_start = psi_start + N;
size_t epsi_start = cte_start + N;
size_t v_start = epsi_start + N;
size_t delta_start = v_start + N - 1;

class FG_eval {
public:
    // Fitted polynomial coefficients
    Eigen::VectorXd coeffs;
    FG_eval(Eigen::VectorXd coeffs) { this->coeffs = coeffs; }

    typedef CPPAD_TESTVECTOR(AD<double>) ADvector;
    void operator()(ADvector& fg, const ADvector& vars) {
        //Initialize cost at 0
        fg[0] = 0;

        //State cost weights
        const double cte_weight = 1;
        const double epsi_weight = 1;   

        //Input and input derivative cost weights
        const double delta_weight = 200;
        const double delta_rate_weight = 250;
        const double v_weight = 50;
        const double v_rate_weight = 200;

        //Set up the cost function
        for (unsigned int t = 0; t < N; ++t){
            //Penalize cross track error
            fg[0] += cte_weight * CppAD::pow(vars[cte_start + t],2);
            //Penalize error in heading
            fg[0] += epsi_weight * CppAD::pow(vars[epsi_start + t], 2);
        }

        //Minimize inputs
        for (unsigned int t = 0; t < N - 1; ++t) {
            fg[0] += delta_weight * CppAD::pow(vars[delta_start + t], 2);
            fg[0] += v_weight * CppAD::pow(vars[v_start + t], 2);
        }

        //Minimize input derivatives
        for (unsigned int t = 0; t < N - 2; ++t) {
            fg[0] += delta_rate_weight * CppAD::pow(vars[delta_start + t + 1] - vars[delta_start + t], 2);
            fg[0] += v_rate_weight * CppAD::pow(vars[v_start + t + 1] - vars[v_start + t], 2);
        }

        //Set the constraints at time t=0
        fg[1 + x_start] = vars[x_start];
        fg[1 + y_start] = vars[y_start];
        fg[1 + psi_start] = vars[psi_start];
        fg[1 + cte_start] = vars[cte_start];
        fg[1 + epsi_start] = vars[epsi_start];

        for (unsigned int t = 1; t < N; ++t) {
            //State at time t+1
            AD<double> x1 = vars[x_start + t];
            AD<double> y1 = vars[y_start + t];
            AD<double> psi1 = vars[psi_start + t];
            AD<double> cte1 = vars[cte_start + t];
            AD<double> epsi1 = vars[epsi_start + t];

            //State at time t
            AD<double> x0 = vars[x_start + t - 1];
            AD<double> y0 = vars[y_start + t - 1];
            AD<double> psi0 = vars[psi_start + t - 1];
            AD<double> cte0 = vars[cte_start + t -1];
            AD<double> epsi0 = vars[epsi_start + t - 1];

            AD<double> x0_2 = x0 * x0;
            AD<double> x0_3 = x0_2 * x0;

            //Actuations at time, t
            AD<double> delta0 = vars[delta_start + t - 1];
            AD<double> v0 = vars[v_start + t - 1];

            //Errors at time t
            AD<double> f0 = coeffs[0] + coeffs[1] * x0 + coeffs[2] * x0_2 + coeffs[3] * x0_3;
            AD<double> psides0 = CppAD::atan(coeffs[1] + 2 * coeffs[2] * x0 + 3 * coeffs[3] * x0_2);

            //Set up the SS model constraints for time steps [1,N]
            fg[1 + x_start + t] = x1 - (x0 + v0 * CppAD::cos(psi0) * dt);
            fg[1 + y_start + t] = y1 - (y0 + v0 * CppAD::sin(psi0) * dt);
            fg[1 + psi_start + t] = psi1 - (psi0 + v0 * CppAD::tan(delta0) * dt / lr);
            fg[1 + cte_start + t] = cte1 - ((f0 - y0) + (v0 * CppAD::sin(epsi0) * dt));
            fg[1 + epsi_start + t] = epsi1 - ((psi0 - psides0) + v0 * delta0 / lr * dt);
        }
    }
};

//
// MPC class definition implementation.
//
MPC::MPC() = default;
MPC::~MPC() = default;

vector<double> MPC::Solve(Eigen::VectorXd state, Eigen::VectorXd coeffs) {
    bool ok = true;
    typedef CPPAD_TESTVECTOR(double) Dvector;
    double x = state[0];
    double y = state[1];
    double psi = state[2];
    double cte = state[3];
    double epsi = state[4];

    // Set number of model variables
    size_t n_vars = N * 5 + (N - 1) * 2; // 5N state elements, 2(N-1) actuators
    // Set the number of constraints
    size_t n_constraints = N * 5; // (x, y, psi, cte, epsi)

    // Initialize model variables to zero
    Dvector vars(n_vars);
    for (unsigned int i = 0; i < n_vars; ++i) {
        vars[i] = 0;
    }

    //Set the initial state
    vars[x_start] = x;
    vars[y_start] = y;
    vars[psi_start] = psi;
    vars[cte_start] = cte;
    vars[epsi_start] = epsi;

    Dvector vars_lowerbound(n_vars);
    Dvector vars_upperbound(n_vars);

    //Define positive and negative infinities
    for (unsigned int i = 0; i < delta_start; ++i) {
        vars_lowerbound[i] = -1.0e19;
        vars_upperbound[i] = 1.0e19;
    }

    // Steering angle upper and lower limits [rad]
    for (unsigned int i = delta_start; i < n_vars; ++i) {
        vars_lowerbound[i] = -dir_bound;
        vars_upperbound[i] = dir_bound;
    }

    // Velocity upper and lower limits [m/s]
    for (unsigned int i = v_start; i < delta_start; ++i) {
        vars_lowerbound[i] = -vel_bound;
        vars_upperbound[i] = vel_bound;
    }

    // Lower and upper bounds for hard constraints (0 except for initial states)
    Dvector constraints_lowerbound(n_constraints);
    Dvector constraints_upperbound(n_constraints);
    for (unsigned int i = 0; i < n_constraints; ++i) {
        constraints_lowerbound[i] = 0.0;
        constraints_upperbound[i] = 0.0;
    }

    //Initial states constrained to last measured value
    constraints_lowerbound[x_start] = x;
    constraints_lowerbound[y_start] = y;
    constraints_lowerbound[psi_start] = psi;
    constraints_lowerbound[cte_start] = cte;
    constraints_lowerbound[epsi_start] = epsi;

    constraints_upperbound[x_start] = x;
    constraints_upperbound[y_start] = y;
    constraints_upperbound[psi_start] = psi;
    constraints_upperbound[cte_start] = cte;
    constraints_upperbound[epsi_start] = epsi;


    // Object that computes objective and constraints
    FG_eval fg_eval(coeffs);

    //
    // NOTE: You don't have to worry about these options
    //
    // options for IPOPT solver
    std::string options;
    // Uncomment this if you'd like more print information
    options += "Integer print_level  0\n";
    // NOTE: Setting sparse to true allows the solver to take advantage
    // of sparse routines, this makes the computation MUCH FASTER. If you
    // can uncomment 1 of these and see if it makes a difference or not but
    // if you uncomment both the computation time should go up in orders of
    // magnitude.
    options += "Sparse  true        forward\n";
    //options += "Sparse  true        reverse\n";
    // NOTE: Currently the solver has a maximum time limit of 0.5 seconds.
    // Change this as you see fit.
    options += "Numeric max_cpu_time          0.099\n";

    // place to return solution
    CppAD::ipopt::solve_result<Dvector> solution;

    // solve the problem
    CppAD::ipopt::solve<Dvector, FG_eval>(
            options, vars, vars_lowerbound, vars_upperbound, constraints_lowerbound,
            constraints_upperbound, fg_eval, solution);

    // Check some of the solution values
    ok &= solution.status == CppAD::ipopt::solve_result<Dvector>::success;

    // Cost
    //auto cost = solution.obj_value;
    //std::cout << "Cost " << cost << std::endl;

    std::vector<double> result;

    result.push_back(solution.x[delta_start]);
    result.push_back(solution.x[v_start]);

    //Clear the MPC x & y value vectors
    this->x_vals.clear();
    this->y_vals.clear();

    //push back the predicted x,y values into the attributes
    for (unsigned int i = 1; i < N; ++i){
        this->x_vals.push_back(solution.x[x_start+i]);
        this->y_vals.push_back(solution.x[y_start+i]);
    }
    return result;
}

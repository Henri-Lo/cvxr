#include "RcppEcosProblem.hpp"
#include <ecos.h>
#include <map>
#include <string>
#include <vector>
#include "ProblemData.hpp"
#include "BuildMatrix.hpp"
#include "LinOp.hpp"
#include "LinOpOperations.hpp"
#include "CVXcanon.hpp"
#include "Utils.hpp"

template <typename T>
void print(std::vector<T> v) {
  for (int i = 0; i < v.size(); i++) {
    std::cout << v[i] << std::endl;
  }
}

/******************
 * HELPER FUNCTIONS
********************/
std::vector<LinOp *> concatenate(std::vector<LinOp *> A, std::vector<LinOp *> B,
                                 std::vector<LinOp *> C) {
  std::vector<LinOp *> ABC;
  ABC.reserve(A.size() + B.size() + C.size());
  ABC.insert(ABC.end(), A.begin(), A.end());
  ABC.insert(ABC.end(), B.begin(), B.end());
  ABC.insert(ABC.end(), C.begin(), C.end());
  return ABC;
}

std::vector<double> negate(std::vector<double> &vec) {
  std::vector<double> neg_vec;
  for (int i = 0; i < vec.size(); i++) {
    neg_vec.push_back(-1 * vec[i]);
  }
  return neg_vec;
}

std::vector<double> get_obj_vec(ProblemData objData, int n) {
  std::vector<double> c(n, 0);
  std::vector<int> idxs = objData.J;
  for (int i = 0; i < idxs.size(); i++) {
    c[idxs[i]] = objData.V[i];
  }
  return c;
}

std::vector<long> to_long(std::vector<int> v_int) {
  std::vector<long> v_long;
  for (int i = 0; i < v_int.size(); i++) {
    v_long.push_back((long)v_int[i]);
  }
  return v_long;
}

int get_num_variables(std::vector<Variable> &vars) {
  int num_vars = 0;
  for (int i = 0; i < vars.size(); i++) {
    num_vars += vars[i].size[0] * vars[i].size[1];
  }
  return num_vars;
}

LinOp *negate_expression(LinOp *expr) {
  LinOp *lin = new LinOp;
  lin->type = NEG;
  lin->size.push_back(expr->size[0]);
  lin->size.push_back(expr->size[1]);
  lin->args.push_back(expr);
  return lin;
}

LinOp *mul_expression(Matrix lh_op, LinOp *rh_op, std::vector<int> size) {
  LinOp *lin = new LinOp;
  lin->type = MUL;
  lin->size = size;
  lin->args.push_back(rh_op);

  lin->sparse = true;
  lin->sparse_data = lh_op;

  return lin;
}

LinOp *sum_expression(std::vector<LinOp *> operators) {
  LinOp *lin = new LinOp;
  lin->type = SUM;
  lin->size = operators[0]->size;
  lin->args = operators;
  return lin;
}

// Returns a sparse matrix linOp that spaces out the expression.
Matrix get_spacing_matrix(std::vector<int> size, int spacing, int offset) {
  Matrix mat(size[0], size[1]);

  std::vector<Triplet> tripletList;
  for (int var_row = 0; var_row < size[1]; var_row++) {
    int row_idx = spacing * var_row + offset;
    int col_idx = var_row;
    tripletList.push_back(Triplet(row_idx, col_idx, 1.0));
  }
  mat.setFromTriplets(tripletList.begin(), tripletList.end());
  mat.makeCompressed();
  return mat;
}

/* Create matrices Ai such that 0 <= A0*x0 + ... + An*xn
 * gives the format for the elementwise cone constraints.
 */
LinOp *format_elementwise(std::vector<LinOp *> &vars) {
  // Compute dimensions
  std::vector<int> prod_size;
  std::vector<int> mat_size;

  int spacing = vars.size();
  prod_size.push_back(spacing * vars[0]->size[0]);
  prod_size.push_back(vars[0]->size[1]);
  mat_size.push_back(spacing * vars[0]->size[0]);
  mat_size.push_back(vars[0]->size[0]);

  // Gather terms for each matrix
  std::vector<LinOp *> terms;
  for (int i = 0; i < vars.size(); i++) {
    LinOp *var = vars[i];
    Matrix mat = get_spacing_matrix(mat_size, spacing, i);
    LinOp *term = mul_expression(mat, var, prod_size);
    terms.push_back(term);
  }
  return negate_expression(sum_expression(terms));
}

std::vector<LinOp *> format_affine_constr(std::vector<LinOp *> &constrs) {
  std::vector<LinOp *> formatted_constraints;
  for (int i = 0; i < constrs.size(); i++) {
    formatted_constraints.push_back(constrs[i]->args[0]);
  }
  return formatted_constraints;
}


std::vector<LinOp *> format_soc_constrs(std::vector<LinOp *> &constrs) {
  std::vector<LinOp *> formatted_constraints;
  for (int i = 0; i < constrs.size(); i++) {
    LinOp *constr = constrs[i];
    for (int j = 0; j < constr->args.size(); j++) {
      formatted_constraints.push_back(negate_expression(constr->args[j]));
    }
  }
  return formatted_constraints;
}

std::vector<LinOp *> format_exp_constrs(std::vector<LinOp *> &constrs) {
  std::vector<LinOp *> formatted_constraints;
  for (int i = 0; i < constrs.size(); i++) {
    std::vector<LinOp *> vars(3);  // x, z, y
    vars[0] = constrs[i]->args[0];
    vars[1] = constrs[i]->args[2];
    vars[2] = constrs[i]->args[1];
    formatted_constraints.push_back(format_elementwise(vars));
  }
  return formatted_constraints;
}

std::vector<Variable> get_dual_variables(std::vector<LinOp *> constrs) {
  std::vector<Variable> vars;
  // for (int i = 0; i < constrs.size(); i++) {
  //   LinOp constr = *constrs[i];
  //   vars.push_back(Variable());
  //   vars[i].id = static_cast<int>(constr.dense_data(0, 0));
  //   vars[i].size = constr.size;
  //   vars[i].type = constr.type;
  // }
  for (int i = 0; i < constrs.size(); i++) {
    // LinOp constr = *constrs[i];
    vars.push_back(Variable());
    vars[i].id = static_cast<int>(constrs[i]->dense_data(0, 0));
    vars[i].size = constrs[i]->size;
    vars[i].type = constrs[i]->type;
  }
  return vars;
}

void set_solver_options(pwork* problem, std::map<std::string, double> opts) {
  std::map<std::string, double>::iterator it;
  for (it = opts.begin(); it != opts.end(); it++) {
    std::string opt = it->first;
    double val = it->second;
    if (opt == "feastol") {
      problem->stgs->feastol = val;
    } else if (opt == "reltol") {
      problem->stgs->reltol = val;
    } else if (opt == "abstol") {
      problem->stgs->abstol = val;
    } else if (opt == "feastol_inacc") {
      problem->stgs->feastol_inacc = val;
    } else if (opt == "reltol_inacc") {
      problem->stgs->reltol_inacc = val;
    } else if (opt == "abstol_inacc") {
      problem->stgs->abstol_inacc = val;
    } else if (opt == "max_iters") {
      problem->stgs->maxit = static_cast<int>(val);
    } else if (opt == "verbose") {
      problem->stgs->verbose = static_cast<bool>(val);
    } else {
      std::cerr << "Error: ECOS solver option invalid." << opt << std::endl;
      exit(-1);
    }
  }
}

solverStatus canonicalize_status(idxint status) {
  switch (status) {
  case 0:
    return OPTIMAL;
  case 1:
    return INFEASIBLE;
  case 2:
    return UNBOUNDED;
  case 10:
    return OPTIMAL_INACCURATE;
  case 11:
    return INFEASIBLE_INACCURATE;
  case 12:
    return UNBOUNDED_INACCURATE;
  case -1:
    return SOLVER_ERROR;
  case -2:
    return SOLVER_ERROR;
  case -3:
    return SOLVER_ERROR;
  case -4:
    return SOLVER_ERROR;
  case -7:
    return SOLVER_ERROR;
  }
  return SOLVER_ERROR;
}

void save_values(std::map<int, Eigen::MatrixXd> &map,
                 std::vector<Variable> &vars, double *result_vec,
                 std::map<int, int> &var_offsets) {
  for (int i = 0; i < vars.size(); i++) {
    Variable var = vars[i];
    int rows = var.size[0];
    int cols = var.size[1];
    int offset = var_offsets[var.id];

    /* HACK */
    std::vector<double> data;
    for (int i = offset; i < offset + (rows * cols); i++) {
      data.push_back(result_vec[i]);
    }
    map[var.id] = Eigen::Map<Eigen::MatrixXd>(&data[0], rows, cols);
  }
}

void save_dual_values(std::map<int, Eigen::MatrixXd> &dual_map,
                      double *result_vec, std::vector<Variable> &dual_vars,
                      OperatorType active_type) {
  std::map<int, int> constr_offset;
  int offset = 0;
  for (int i = 0; i < dual_vars.size(); i++) {
    Variable dual_var = dual_vars[i];
    constr_offset[dual_var.id] = offset;
    if (dual_var.type == EXP) {
      offset += 3 * dual_var.size[0] * dual_var.size[1];
    } else {
      offset += dual_var.size[0] * dual_var.size[1];
    }
  }
  std::vector<Variable> active_dual_vars;
  for (int i = 0; i < dual_vars.size(); i++) {
    if (dual_vars[i].type == active_type) {
      active_dual_vars.push_back(dual_vars[i]);
    }
  }
  save_values(dual_map, active_dual_vars, result_vec, constr_offset);
}

/*********************
 * Public Functions
 *********************/
/* Constructor */
EcosProblem::EcosProblem(Sense sense, LinOp * objective, std::map<OperatorType,
                         std::vector<LinOp *> > constr_map,
                         std::map<OperatorType, std::vector<int> > dims_map,
                         std::vector<Variable> variables,
                         std::map<int, int> var_offsets) {
  /* Initialize metadata */
  prob_sense = sense;
  primal_vars = variables;
  primal_offsets = var_offsets;

  /* Store dual variables to recover optimal dual values */
  eq_dual_vars = get_dual_variables(constr_map[EQ]);
  ineq_dual_vars = get_dual_variables(concatenate(constr_map[LEQ],
                                      constr_map[SOC], constr_map[EXP]));

#ifdef CVXCANON_DEBUG
  Rcpp::Rcout << "before problem data" << std::endl;
#endif

  /* get problem data */
  std::vector<LinOp *> objVec;
  objVec.push_back(objective);

#ifdef CVXCANON_DEBUG
  Rcpp::Rcout << "after problem data" << std::endl;
#endif

  /* Format for ECOS solver */
  std::vector<LinOp *> eq_constrs = format_affine_constr(constr_map[EQ]);
#ifdef CVXCANON_DEBUG
  Rcpp::Rcout << "after eq constrs" << std::endl;
#endif
  std::vector<LinOp *> leq_constrs = format_affine_constr(constr_map[LEQ]);
#ifdef CVXCANON_DEBUG
  Rcpp::Rcout << "after leq constrs" << std::endl;
#endif
  std::vector<LinOp *> soc_constrs = format_soc_constrs(constr_map[SOC]);
#ifdef CVXCANON_DEBUG
  Rcpp::Rcout << "after soc constrs" << std::endl;
#endif
  std::vector<LinOp *> exp_constrs = format_exp_constrs(constr_map[EXP]);
#ifdef CVXCANON_DEBUG
  Rcpp::Rcout << "after ineq constrs" << std::endl;
#endif
  std::vector<LinOp *> ineq_constrs = concatenate(leq_constrs, soc_constrs,
                                                  exp_constrs);

#ifdef CVXCANON_DEBUG
  Rcpp::Rcout << "before objdata" << std::endl;
#endif

  ProblemData objData = build_matrix(objVec, var_offsets);
  ProblemData eqData = build_matrix(eq_constrs, var_offsets);
  ProblemData ineqData = build_matrix(ineq_constrs, var_offsets);

#ifdef CVXCANON_DEBUG
  Rcpp::Rcout << "after build_matrix" << std::endl;
#endif

  /* Problem Dimensions */
  int num_variables = get_num_variables(variables);
  n = num_variables;
  m = ineqData.num_constraints;
  p = dims_map[EQ][0];
  l = dims_map[LEQ][0];
  ncones = dims_map[SOC].size();
  q = to_long(dims_map[SOC]);
  e = dims_map[EXP][0];

  /* Extract G and A matrices in CCS */
  ineqData.toCSC(num_variables);
  Gpr = ineqData.vals;
  Gir = to_long(ineqData.row_idxs);
  Gjc = to_long(ineqData.col_ptrs);
  h = negate(ineqData.const_vec);

  eqData.toCSC(num_variables);
  Apr = eqData.vals;
  Air = to_long(eqData.row_idxs);
  Ajc = to_long(eqData.col_ptrs);
  b = negate(eqData.const_vec);


  offset = objData.const_vec[0];
  c = get_obj_vec(objData, num_variables);
  if (sense == MAXIMIZE) {
    c = negate(c);
  }

#ifdef CVXCANON_DEBUG
  std::cout << "DEBUG DIMENSIONS: " << std::endl;
  std::cout << "N: " << n << std::endl;
  std::cout << "M: " << m << std::endl;
  std::cout << "P: " << p << std::endl;
  std::cout << "L: " << l << std::endl;
  std::cout << "NCONES: " << ncones << std::endl;
  std::cout << "Q: " << std::endl;
  print(q);
  std::cout << "E: " << e << std::endl;
  std::cout << "OFFSET: " << offset << std::endl;
#endif

  problem = ECOS_setup(n, m, p, l, ncones, &q[0], e,
                       &Gpr[0], &Gjc[0], &Gir[0],
                       &Apr[0], &Ajc[0], &Air[0],
                       &c[0], &h[0], &b[0]);
}

EcosProblem::~EcosProblem() {
  ECOS_cleanup(problem, 0);  // TODO(john): keepvars for warmstart?
}

SEXP EcosProblem::solve(std::map<std::string, double> solver_options) {
  set_solver_options(problem, solver_options);
  idxint status = ECOS_solve(problem);
  Rcpp::XPtr<Solution> soln( new Solution( ), true );

  soln->status = canonicalize_status(status);
  soln->optimal_value = problem->info->pcost + offset;
  if (prob_sense == MAXIMIZE) {  // account for negating the objective
    soln->optimal_value *= -1;
  }

  save_values(soln->primal_values, primal_vars, problem->x, primal_offsets);
  save_dual_values(soln->dual_values, problem->y, eq_dual_vars, EQ);
  save_dual_values(soln->dual_values, problem->z, ineq_dual_vars, LEQ);
  return soln;
}

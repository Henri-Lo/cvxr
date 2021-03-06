Solver <- setClass("Solver", contains = "VIRTUAL")

Solver.choose_solver <- function(constraints) {
  constr_map <- SymData.filter_constraints(constraints)
  # If no constraints, use ECOS.
  if(length(constraints) == 0)
    return(ECOS())
  # If mixed integer constraints, use ECOS_BB.
  else if(length(constr_map[[BOOL_MAP]]) > 0 || length(constr_map[[INT_MAP]]) > 0)
    return(ECOS_BB())
  # If SDP, defaults to CVXOPT.
  else if(length(constr_map[[SDP_MAP]]) > 0)
    return(CVXOPT())
  # Otherwise use ECOS
  else
    return(ECOS())
}

setMethod("validate_solver", "Solver", function(solver, constraints) {
  constr_map <- SymData.filter_constraints(constraints)
  if(( (length(constr_map[[BOOL_MAP]]) > 0 || length(constr_map[[INT_MAP]]) > 0) && !mip_capable(solver)) ||
     (length(constr_map[[SDP_MAP]]) > 0 && !sdp_capable(solver)) ||
     (length(constr_map[[EXP_MAP]]) > 0 && !exp_capable(solver)) ||
     (length(constr_map[[SOC_MAP]]) > 0 && !socp_capable(solver)) || 
     (length(constraints) == 0 && name(solver) %in% c(SCS_NAME, GLPK_NAME)))
    stop("The solver ", name(solver), " cannot solve the problem")
})

setMethod("validate_cache", "Solver", function(solver, objective, constraints, cached_data) {
  prob_data <- cached_data[[name(solver)]]
  if(!is.null(prob_data@sym_data) && (objective != prob_data@sym_data@objective || constraints != prob_data@sym_data@constraints)) {
    prob_data@sym_data <- NULL
    prob_data@matrix_data <- NULL
  }
  cached_data[[name(solver)]] <- prob_data
  cached_data
})

setMethod("get_sym_data", "Solver", function(solver, objective, constraints, cached_data) {
  cached_data <- validate_cache(solver, objective, constraints, cached_data)
  prob_data <- cached_data[[name(solver)]]
  if(is.null(prob_data@sym_data))
    prob_data@sym_data <- SymData(objective, constraints, solver)
  prob_data@sym_data
})

setMethod("get_matrix_data", "Solver", function(solver, objective, constraints, cached_data) {
  sym_data <- get_sym_data(solver, objective, constraints, cached_data)
  prob_data <- cached_data[[name(solver)]]
  if(is.null(prob_data@matrix_data))
    prob_data@matrix_data <- MatrixData(sym_data, solver)
  prob_data@matrix_data
})

setMethod("Solver.get_problem_data", "Solver", function(solver, objective, constraints, cached_data) {
  sym_data <- get_sym_data(solver, objective, constraints, cached_data)
  matrix_data <- get_matrix_data(solver, objective, constraints, cached_data)
  
  data <- list()
  obj <- get_objective(matrix_data)
  eq <- get_eq_constr(matrix_data)
  ineq <- get_ineq_constr(matrix_data)
  
  data[[C]] <- obj[[1]]
  data[[OFFSET]] <- obj[[2]]
  data[[A]] <- eq[[1]]
  data[[B]] <- eq[[2]]
  data[[G]] <- ineq[[1]]
  data[[H]] <- ineq[[2]]
  data[[DIMS]] <- sym_data@dims
  
  conv_idx <- Solver._noncvx_id_to_idx(data[[DIMS]], sym_data@var_offsets, sym_data@var_sizes)
  data[[BOOL_IDX]] <- conv_idx$bool_idx
  data[[INT_IDX]] <- conv_idx$int_idx
  data
})

Solver.is_mip <- function(data) {
  length(data[BOOL_IDX]) > 0 || length(data[INT_IDX]) > 0
}

Solver._noncvx_id_to_idx <- function(dims, var_offsets, var_sizes) {
  bool_idx <- lapply(dims[BOOL_IDS], function(var_id) {
    offset <- var_offsets[var_id]
    size <- var_sizes[var_id]
    offset + seq(1, size[1]*size[2], by = 1)
  })
  
  int_idx <- lapply(dims[INT_IDS], function(var_id) {
    offset <- var_offsets[var_id]
    size <- var_sizes[var_id]
    offset + seq(1, size[1]*size[2], by = 1)
  })
  
  list(bool_idx = bool_idx, int_idx = int_idx)
}

setClass("ECOS", contains = "Solver")
ECOS <- function() { new("ECOS") }

# ECOS capabilities
setMethod("lp_capable", "ECOS", function(solver) { TRUE })
setMethod("socp_capable", "ECOS", function(solver) { TRUE })
setMethod("sdp_capable", "ECOS", function(solver) { FALSE })
setMethod("exp_capable", "ECOS", function(solver) { TRUE })
setMethod("mip_capable", "ECOS", function(solver) { FALSE })

# EXITCODES from ECOS
# ECOS_OPTIMAL  (0)   Problem solved to optimality
# ECOS_PINF     (1)   Found certificate of primal infeasibility
# ECOS_DINF     (2)   Found certificate of dual infeasibility
# ECOS_INACC_OFFSET (10)  Offset exitflag at inaccurate results
# ECOS_MAXIT    (-1)  Maximum number of iterations reached
# ECOS_NUMERICS (-2)  Search direction unreliable
# ECOS_OUTCONE  (-3)  s or z got outside the cone, numerics?
# ECOS_SIGINT   (-4)  solver interrupted by a signal/ctrl-c
# ECOS_FATAL    (-7)  Unknown problem in solver

# Map of ECOS status to CVXPY status.
setMethod("status_map", "ECOS", function(solver, status) {
  if(status == 0) OPTIMAL
  else if(status == 1) INFEASIBLE
  else if(status == 2) UNBOUNDED
  else if(status == 10) OPTIMAL_INACCURATE
  else if(status == 11) INFEASIBLE_INACCURATE
  else if(status == 12) UNBOUNDED_INACCURATE
  else if(status %in% c(-1, -2, -3, -4, -7)) SOLVER_ERROR
  else stop("ECOS status unrecognized: ", status)
})

setMethod("name", "ECOS", function(object) { ECOS_NAME })
setMethod("matrix_intf", "ECOS", function(solver) { DEFAULT_SPARSE_INTF })
setMethod("vec_intf", "ECOS", function(solver) { DEFAULT_INTF })
setMethod("split_constr", "ECOS", function(solver, constr_map) {
  list(eq_constr = constr_map[[EQ_MAP]], ineq_constr = constr_map[[LEQ_MAP]], nonlin_constr = list())  
})

setMethod("cvxr_solve_int", "ECOS", function(solver, objective, constraints, cached_data, warm_start, verbose, solver_opts) {
  require(ECOSolveR)
  data <- get_problem_data(solver, objective, constraints, cached_data)
  data[[DIMS]]['e'] <- data[[DIMS]][[EXP_DIM]]
  results_dict <- ECOSolveR::ECOS_csolve(c = data[[C]], G = data[[G]], h = data[[H]], dims = data[[DIMS]], A = data[[A]], b = data[[B]])
  format_results(solver, result_dict, data, cached_data)
})

setMethod("format_results", "ECOS", function(solver, results_dict, data, cached_data) {
  new_results <- list()
  status <- STATUS_MAP(solver, results_dict['info']['exitFlag'])
  new_results[STATUS] <- status
  
  # Timing data
  new_results[SOLVER_TIME] <- results_dict["info"]["timing"]["tsolve"]
  new_results[SETUP_TIME] <- results_dict["info"]["timing"]["tsetup"]
  new_results[NUM_ITERS] <- results_dict["info"]["iter"]
  
  if(new_results[STATUS] %in% SOLUTION_PRESENT) {
    primal_val <- results_dict['info']['pcost']
    new_results[VALUE] <- primal_val + data[OFFSET]
    new_results[PRIMAL] <- results_dict['x']
    new_results[EQ_DUAL] <- results_dict['y']
    new_results[INEQ_DUAL] <- results_dict['z']
  }
  new_results
})

setClass("SCS", contains = "Solver")
SCS <- function() { new("SCS") }

# SCS capabilities
setMethod("lp_capable", "SCS", function(solver) { TRUE })
setMethod("socp_capable", "SCS", function(solver) { TRUE })
setMethod("sdp_capable", "SCS", function(solver) { TRUE })
setMethod("exp_capable", "SCS", function(solver) { TRUE })
setMethod("mip_capable", "SCS", function(solver) { FALSE })

# Map of SCS status to CVXPY status.
setMethod("status_map", "SCS", function(solver, status) {
  if(status == "Solved") OPTIMAL
  else if(status == "Solved/Inaccurate") OPTIMAL_INACCURATE
  else if(status == "Unbounded") UNBOUNDED
  else if(status == "Unbounded/Inaccurate") UNBOUNDED_INACCURATE
  else if(status == "Infeasible") INFEASIBLE
  else if(status == "Infeasible/Inaccurate") INFEASIBLE_INACCURATE
  else if(status %in% c("Failure", "Indeterminate")) SOLVER_ERROR
  else stop("SCS status unrecognized: ", status)
})

setMethod("name", "SCS", function(object) { SCS_NAME })
setMethod("split_constr", "SCS", function(solver, constr_map) {
  list(eq_constr = c(constr_map[[EQ_MAP]], constr_map[[LEQ_MAP]]), ineq_constr = list(), nonlin_constr = list())
})

# TODO: Finish SCS interface

#'
#' Solver utilities
#'
# solver_intf <- list(ECOS(), ECOS_BB(), CVXOPT(), GLPK(), GLPK_MI(), CBC(), SCS(), GUROBI(), Elemental(), MOSEK(), LS())
solver_intf <- list(ECOS(), SCS())
SOLVERS <- solver_intf
names(SOLVERS) <- sapply(solver_intf, function(solver) { name(solver) })

installed_solvers <- function() {
  installed <- list()
  for(i in 1:length(SOLVERS)) {
    if(is_installed(SOLVERS[i]))
      installed <- c(installed, names(SOLVERS)[i])
  }
  installed
}

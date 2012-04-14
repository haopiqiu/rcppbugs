///////////////////////////////////////////////////////////////////////////
// Copyright (C) 2011  Whit Armstrong                                    //
//                                                                       //
// This program is free software: you can redistribute it and/or modify  //
// it under the terms of the GNU General Public License as published by  //
// the Free Software Foundation, either version 3 of the License, or     //
// (at your option) any later version.                                   //
//                                                                       //
// This program is distributed in the hope that it will be useful,       //
// but WITHOUT ANY WARRANTY; without even the implied warranty of        //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         //
// GNU General Public License for more details.                          //
//                                                                       //
// You should have received a copy of the GNU General Public License     //
// along with this program.  If not, see <http://www.gnu.org/licenses/>. //
///////////////////////////////////////////////////////////////////////////

#include <iostream>
#include <map>
#include <limits>
#include <stdexcept>
#include <boost/random.hpp>
#include <Rcpp.h>
#include <RcppArmadillo.h>
//#include <cppbugs/cppbugs.hpp>
#include <cppbugs/mcmc.deterministic.hpp>
#include <cppbugs/mcmc.normal.hpp>
#include <cppbugs/mcmc.uniform.hpp>
#include <cppbugs/mcmc.gamma.hpp>
#include <cppbugs/mcmc.binomial.hpp>
#include <cppbugs/mcmc.bernoulli.hpp>
//#include "interface.h"
#include "helpers.h"
#include "raw.address.h"
#include "distribution.types.h"
#include "arma.context.h"
#include "assign.normal.logp.h"
#include "assign.uniform.logp.h"
#include "r.deterministic.h"
#include "r.mcmc.model.h"

// map of memory address of Robject underlying data (void*) to wrapped Arma object (ArmaContext*)
typedef std::map<void*,ArmaContext*> sexpArmaMapT;
typedef std::map<void*,cppbugs::MCMCObject*> sexpMCMCMapT;

extern "C" SEXP logp(SEXP x);
extern "C" SEXP getRawAddr(SEXP x);
extern "C" SEXP testAttrAdd(SEXP x);
extern "C" SEXP attachArgs(SEXP args);
extern "C" SEXP createModel(SEXP args_sexp);
extern "C" SEXP run_model(SEXP m_, SEXP iterations, SEXP burn_in, SEXP adapt, SEXP thin);
ArmaContext* mapToArma(sexpArmaMapT& m, SEXP x);
cppbugs::MCMCObject* createMCMC(sexpArmaMapT& m, SEXP x);
cppbugs::MCMCObject* createNormal(sexpArmaMapT& m, SEXP x);
cppbugs::MCMCObject* createUniform(sexpArmaMapT& m, SEXP x);
cppbugs::MCMCObject* createDeterministic(sexpArmaMapT& m, SEXP x);
void appendHistory(sexpMCMCMapT& mcmc_map, sexpArmaMapT& arma_map, SEXP x);

class MCMCModelHolder {
public:
  ~MCMCModelHolder() {
    for(auto n : nodes) { delete n; }
  }
  sexpArmaMapT armaContextMap;
  sexpMCMCMapT mcmcContextMap;
  std::vector<cppbugs::MCMCObject*> nodes;
};

SEXP getRawAddr(SEXP x) {
  void* vp = rawAddress(x);
  Rprintf("%p\n",vp);
  return R_NilValue;
}

SEXP testAttrAdd(SEXP x) {
  SEXP attr;
  PROTECT(attr = Rf_allocVector(REALSXP, 1));
  REAL(attr)[0] = 100;
  Rf_setAttrib(x, Rf_install("test.atty"), attr);
  UNPROTECT(1);
  return R_NilValue;
}


static void modelFinalizer(SEXP m_) {
  MCMCModelHolder* m = reinterpret_cast<MCMCModelHolder*>(R_ExternalPtrAddr(m_));
  if(m) {
    delete m;
    R_ClearExternalPtr(m_);
  }
}

static void arglistFinalizer(SEXP a_) {
  arglistT* a = reinterpret_cast<arglistT*>(R_ExternalPtrAddr(a_));
  if(a) {
    delete a;
    R_ClearExternalPtr(a_);
  }
}

SEXP attachArgs(SEXP args) {
  args = CDR(args); /* skip 'name' */

  // pull off data object
  SEXP x = CAR(args); args = CDR(args);
  arglistT* arglist = new arglistT;

  // loop through rest of args
  for(; args != R_NilValue; args = CDR(args)) {
    arglist->push_back(CAR(args));
  }

  SEXP arglist_;
  PROTECT(arglist_ = R_MakeExternalPtr(reinterpret_cast<void*>(arglist),Rf_install("arglist"),R_NilValue));
  R_RegisterCFinalizerEx(arglist_, arglistFinalizer, TRUE);
  UNPROTECT(1);
  Rf_setAttrib(x, Rf_install("args"), arglist_);
  return R_NilValue;
}

SEXP logp(SEXP x) {
  double ans = std::numeric_limits<double>::quiet_NaN();
  sexpArmaMapT m;
  cppbugs::MCMCObject* node(NULL);
  try {
    node = createMCMC(m, x);
  } catch (std::logic_error &e) {
    REprintf("%s\n",e.what());
    delete node;
    return R_NilValue;
  }

  cppbugs::Stochastic* sp = dynamic_cast<cppbugs::Stochastic*>(node);
  if(sp) {
    ans = sp->loglik();
  } else {
    REprintf("ERROR: could not convert node to stochastic.\n");
  }
  delete node;
  return Rcpp::wrap(ans);
}

template<typename T>
void pmap(T& m) {
  for (typename T::iterator it=m.begin() ; it != m.end(); it++ ) {
    std::cout << it->first << "|" << it->second << std::endl;
  }
}

SEXP createModel(SEXP args_sexp) {
  SEXP ans;
  MCMCModelHolder* m = new MCMCModelHolder;
  try {
    //CDR(args_sexp); // function name
    //for(R_len_t i = 0; i < Rf_length(args_sexp); i++) {
    args_sexp = CDR(args_sexp); /* skip 'name' */
    for(int i = 0; args_sexp != R_NilValue; i++, args_sexp = CDR(args_sexp)) {
      SEXP this_sexp = CAR(args_sexp);
      Rprintf("adding object: %p\n",rawAddress(this_sexp));
      Rprintf("type %d\n",TYPEOF(this_sexp));
      cppbugs::MCMCObject* this_node = createMCMC(m->armaContextMap,this_sexp);
      pmap(m->armaContextMap);
      Rprintf("done adding\n");
      m->mcmcContextMap[rawAddress(this_sexp)] = this_node;
      m->nodes.push_back(this_node);
    }
  } catch (std::logic_error &e) {
    REprintf("%s\n",e.what());
    delete m;
    return R_NilValue;
  }
  PROTECT(ans = R_MakeExternalPtr(reinterpret_cast<void*>(m),Rf_install("MCMCModelHolder"),R_NilValue));
  R_RegisterCFinalizerEx(ans, modelFinalizer, TRUE);
  UNPROTECT(1);
  return ans;
}

SEXP run_model(SEXP mp_, SEXP iterations, SEXP burn_in, SEXP adapt, SEXP thin) {

  int iterations_ = Rcpp::as<int>(iterations);
  int burn_in_ = Rcpp::as<int>(burn_in);
  int adapt_ = Rcpp::as<int>(adapt);
  int thin_ = Rcpp::as<int>(thin);

  MCMCModelHolder* mp = reinterpret_cast<MCMCModelHolder*>(R_ExternalPtrAddr(mp_));
  if(!mp) { REprintf("bad model object.\n"); return R_NilValue; }

  try {
    cppbugs::MCModel<boost::minstd_rand> m(mp->nodes);
    m.sample(iterations_, burn_in_, adapt_, thin_);
    return Rcpp::wrap(m.acceptance_ratio());
  } catch (std::logic_error &e) {
    REprintf("%s\n",e.what());
    return R_NilValue;
  }
}

ArmaContext* mapToArma(sexpArmaMapT& m, SEXP x) {
  //void* ptr = static_cast<void*>(RAW(x));
  void* ptr = rawAddress(x);

  // if node is mapped, just return mapped pointer
  if(!m.count(ptr)) {
    switch(TYPEOF(x)) {
    case REALSXP:
      switch(getDims(x).size()) {
      case 0: m[ptr] = new ArmaDouble(x); break;
      case 1: m[ptr] = new ArmaVec(x); break;
      case 2: m[ptr] = new ArmaMat(x); break;
      default:
        throw std::logic_error("ERROR: tensor conversion not supported yet.");
      }
      break;
    case LGLSXP:
    case INTSXP:
      throw std::logic_error("ERROR: integer type conversion not supported yet.");
      break;
    default:
      throw std::logic_error("ERROR: conversion not supported.");
    }
  }
  return m[ptr];
}

cppbugs::MCMCObject* createMCMC(sexpArmaMapT& m, SEXP x) {
  SEXP distributed_sexp;
  distributed_sexp = Rf_getAttrib(x,Rf_install("distributed"));
  if(distributed_sexp == R_NilValue) {
    throw std::logic_error("ERROR: 'distributed' attribute not defined. Is this a stochastic variable?");
  }
  distT distributed = matchDistibution(std::string(CHAR(STRING_ELT(distributed_sexp,0))));

  cppbugs::MCMCObject* ans;

  switch(distributed) {
  case deterministicT:
    ans = createDeterministic(m, x);
    break;
  case normalDistT:
    ans = createNormal(m, x);
    break;
  case uniformDistT:
    ans = createUniform(m, x);
    break;
  case gammaDistT:
  case betaDistT:
  case binomialDistT:
  default:
    ans = NULL;
    throw std::logic_error("ERROR: distribution not supported yet.");
  }
  return ans;
}

cppbugs::MCMCObject* createDeterministic(sexpArmaMapT& m, SEXP x) {
  cppbugs::MCMCObject* ans(NULL);
  void* vp = rawAddress(x); Rprintf("deterministic addr: %p\n",vp);

  SEXP fun_sexp = Rf_getAttrib(x,Rf_install("fun"));
  SEXP args_sexp = Rf_getAttrib(x,Rf_install("args"));

  arglistT* args = reinterpret_cast<arglistT*>(R_ExternalPtrAddr(args_sexp));
  if(!args) {
    throw std::logic_error("ERROR: args attribute not defined.");
  }

  // map to arma types
  ArmaContext* x_arma = mapToArma(m, x);
  for(size_t i = 0; i < args->size(); i++) {
    if(TYPEOF(args->at(i)) != REALSXP && TYPEOF(args->at(i)) != INTSXP) {
      throw std::logic_error("ERROR: args must be int or real.");
    }
    mapToArma(m, args->at(i));
  }

  switch(x_arma->getArmaType()) {
  case doubleT:
    ans = new cppbugs::RDeterministic<double>(x_arma->getDouble(),fun_sexp,*args);
    break;
  case vecT:
    ans = new cppbugs::RDeterministic<arma::vec>(x_arma->getVec(),fun_sexp,*args);
    break;
  case matT:
    ans = new cppbugs::RDeterministic<arma::mat>(x_arma->getMat(),fun_sexp,*args);
    break;
  case intT:
  case ivecT:
  case imatT:
  default:
    throw std::logic_error("ERROR: deterministic must be a continuous variable type (double, vec, or mat) for now (under development).");
  }
  return ans;
}

cppbugs::MCMCObject* createNormal(sexpArmaMapT& m, SEXP x) {
  cppbugs::MCMCObject* ans;

  SEXP mu_sexp = Rf_getAttrib(x,Rf_install("mu"));
  SEXP tau_sexp = Rf_getAttrib(x,Rf_install("tau"));
  SEXP observed_sexp = Rf_getAttrib(x,Rf_install("observed"));

  if(mu_sexp == R_NilValue || tau_sexp == R_NilValue || observed_sexp == R_NilValue) {
    throw std::logic_error("ERROR: needed attribute not defined.");
  }

  //bool observed = LOGICAL(observed_sexp)[0];
  bool observed = Rcpp::as<bool>(observed_sexp);
  Rprintf("observed: %d\n",observed);

  // map to arma types
  ArmaContext* x_arma = mapToArma(m, x);
  ArmaContext* mu_arma = mapToArma(m, mu_sexp); void* vp = rawAddress(mu_sexp); Rprintf("mu addr: %p\n",vp);
  ArmaContext* tau_arma = mapToArma(m, tau_sexp);

  switch(x_arma->getArmaType()) {
  case doubleT:
    if(observed) {
      ans = assignNormalLogp<cppbugs::ObservedNormal>(x_arma->getDouble(),mu_arma,tau_arma);
    } else {
      ans = assignNormalLogp<cppbugs::Normal>(x_arma->getDouble(),mu_arma,tau_arma);
    }
    break;
  case vecT:
    if(observed) {
      ans = assignNormalLogp<cppbugs::ObservedNormal>(x_arma->getVec(),mu_arma,tau_arma);
    } else {
      ans = assignNormalLogp<cppbugs::Normal>(x_arma->getVec(),mu_arma,tau_arma);
    }
    break;
  case matT:
    if(observed) {
      ans = assignNormalLogp<cppbugs::ObservedNormal>(x_arma->getMat(),mu_arma,tau_arma);
    } else {
      ans = assignNormalLogp<cppbugs::Normal>(x_arma->getMat(),mu_arma,tau_arma);
    }
    break;
  case intT:
  case ivecT:
  case imatT:
  default:
    throw std::logic_error("ERROR: normal must be a continuous variable type (double, vec, or mat).");
  }
  return ans;
}

cppbugs::MCMCObject* createUniform(sexpArmaMapT& m, SEXP x) {
  cppbugs::MCMCObject* ans;

  SEXP lower_sexp = Rf_getAttrib(x,Rf_install("lower"));
  SEXP upper_sexp = Rf_getAttrib(x,Rf_install("upper"));
  SEXP observed_sexp = Rf_getAttrib(x,Rf_install("observed"));

  if(lower_sexp == R_NilValue || upper_sexp == R_NilValue || observed_sexp == R_NilValue) {
    throw std::logic_error("ERROR: needed attribute not defined.");
  }

  //bool observed = LOGICAL(observed_sexp)[0];
  bool observed = Rcpp::as<bool>(observed_sexp);

  // map to arma types
  ArmaContext* x_arma = mapToArma(m, x);
  ArmaContext* lower_arma = mapToArma(m, lower_sexp);
  ArmaContext* upper_arma = mapToArma(m, upper_sexp);

  switch(x_arma->getArmaType()) {
  case doubleT:
    if(observed) {
      ans = assignUniformLogp<cppbugs::ObservedUniform>(x_arma->getDouble(),lower_arma,upper_arma);
    } else {
      ans = assignUniformLogp<cppbugs::Uniform>(x_arma->getDouble(),lower_arma,upper_arma);
    }
    break;
  case vecT:
    if(observed) {
      ans = assignUniformLogp<cppbugs::ObservedUniform>(x_arma->getVec(),lower_arma,upper_arma);
    } else {
      ans = assignUniformLogp<cppbugs::Uniform>(x_arma->getVec(),lower_arma,upper_arma);
    }
    break;
  case matT:
    if(observed) {
      ans = assignUniformLogp<cppbugs::ObservedUniform>(x_arma->getMat(),lower_arma,upper_arma);
    } else {
      ans = assignUniformLogp<cppbugs::Uniform>(x_arma->getMat(),lower_arma,upper_arma);
    }
    break;
  case intT:
  case ivecT:
  case imatT:
  default:
    throw std::logic_error("ERROR: uniform must be a continuous variable type (double, vec, or mat).");
  }
  return ans;
}

template<typename T>
SEXP getHistory(cppbugs::MCMCObject* node) {
  //SEXP ans;
  cppbugs::MCMCSpecialized<T>* sp = dynamic_cast<cppbugs::MCMCSpecialized<T>*>(node);
  if(sp == nullptr) {
    throw std::logic_error("invalid node conversion.");
  }
  Rprintf("getHistory<T> history.size(): %d\n",sp->history.size());
  //PROTECT(ans = Rf_allocVector(VECSXP, sp->history.size()));
  //Rcpp::List ans(sp->history.size());
  //const size_t NC = sp->history.begin()->n_elem;
  const size_t NC = sp->history.begin()->n_cols;
  Rcpp::NumericMatrix ans(sp->history.size(),NC);
  R_len_t i = 0;
  for(typename std::list<T>::const_iterator it = sp->history.begin(); it != sp->history.end(); it++) {
    //SET_VECTOR_ELT(ans, i, Rcpp::wrap(*it)); ++i;
    //ans[i] = Rcpp::wrap(*it);
    //Rprintf("%d %d",i, it->n_cols);
    for(size_t j = 0; j < NC; j++) {
      ans(i,j) = it->at(j);
    }
    ++i;
  }
  //UNPROTECT(1);
  return Rcpp::wrap(ans);
}


template<> SEXP getHistory<arma::vec>(cppbugs::MCMCObject* node) {
  //SEXP ans;
  cppbugs::MCMCSpecialized<arma::vec>* sp = dynamic_cast<cppbugs::MCMCSpecialized<arma::vec>*>(node);
  if(sp == nullptr) {
    throw std::logic_error("invalid node conversion.");
  }
  Rprintf("getHistory<arma::vec> history.size(): %d\n",sp->history.size());
  const size_t NC = sp->history.begin()->n_elem;
  Rprintf("getHistory<arma::vec> history dim: %d\n",NC);
  Rcpp::NumericMatrix ans(sp->history.size(),NC);
  R_len_t i = 0;
  for(typename std::list<arma::vec>::const_iterator it = sp->history.begin(); it != sp->history.end(); it++) {
    for(size_t j = 0; j < NC; j++) {
      ans(i,j) = it->at(j);
    }
    ++i;
  }
  //UNPROTECT(1);
  return Rcpp::wrap(ans);
}

template<> SEXP getHistory<double>(cppbugs::MCMCObject* node) {
  cppbugs::MCMCSpecialized<double>* sp = dynamic_cast<cppbugs::MCMCSpecialized<double>*>(node);
  if(sp == nullptr) {
    throw std::logic_error("invalid node conversion.");
  }
  Rprintf("getHistory<double> history.size(): %d\n",sp->history.size());
  Rcpp::NumericVector ans(sp->history.size());
  R_len_t i = 0;
  for(typename std::list<double>::const_iterator it = sp->history.begin(); it != sp->history.end(); it++) {
    ans[i] =*it; ++i;
  }
  return Rcpp::wrap(ans);
}

void appendHistory(sexpMCMCMapT& mcmc_map, sexpArmaMapT& arma_map, SEXP x) {
  SEXP ans;
  void* sexp_data_ptr = rawAddress(x);

  if(arma_map.count(sexp_data_ptr) == 0) {
    throw std::logic_error("ERROR: getHistory, node not found in armaContextMap.");
  }

  if(mcmc_map.count(sexp_data_ptr) == 0) {
    throw std::logic_error("ERROR: getHistory, node not found in mcmcContextMap.");
  }

  ArmaContext* arma_ptr = arma_map[sexp_data_ptr];
  cppbugs::MCMCObject* mcmc_ptr = mcmc_map[sexp_data_ptr];

  switch(arma_ptr->getArmaType()) {
  case doubleT:
    PROTECT(ans = getHistory<double>(mcmc_ptr));
    break;
  case vecT:
    PROTECT(ans = getHistory<arma::vec>(mcmc_ptr));
    break;
  case matT:
    PROTECT(ans = getHistory<arma::mat>(mcmc_ptr));
    break;
  case intT:
  case ivecT:
  case imatT:
  default:
    throw std::logic_error("ERROR: history conversion not supported for this type.");
  }
  //Rprintf("returned length: %d",Rf_length(ans));
  Rf_setAttrib(x, Rf_install("history"), ans);
  UNPROTECT(1);
}

// -*- mode: C++; c-indent-level: 2; c-basic-offset: 2; tab-width: 8 -*-
///////////////////////////////////////////////////////////////////////////
// Copyright (C) 2012 Whit Armstrong                                     //
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

#ifndef R_DETERMINISTIC_H
#define R_DETERMINISTIC_H

//#include <R.h>
//#include <Rdefines.h>
#include <iostream>
#include <Rinternals.h>
#include <cppbugs/mcmc.dynamic.hpp>

typedef std::vector<SEXP> arglistT;

namespace cppbugs {

  template<typename T>
  class RDeterministic : public Dynamic<T> {
    SEXP fun_;
    arglistT& args_;

    static void updateFromSEXP(double& dest, SEXP x) {
      dest = REAL(x)[0];
    }

    static void updateFromSEXP(int& dest, SEXP x) {
      dest = INTEGER(x)[0];
    }

    static void updateFromSEXP(arma::vec& dest, SEXP x) {
      //Rprintf("dest size: %d\n",dest.n_elem);
      //Rprintf("src sizee: %d\n", Rf_length(x));
      memcpy(dest.memptr(),REAL(x),sizeof(double)*dest.n_elem);
    }

    static void updateFromSEXP(arma::mat& dest, SEXP x) {
      //Rprintf("dest size: %d\n",dest.n_elem);
      memcpy(dest.memptr(),REAL(x),sizeof(double)*dest.n_elem);
      std::cout << "address of updated mat:" << &dest << std::endl;
    }

  public:
    void jump(RngBase& rng) {
      SEXP r_call, ans;
      switch(args_.size()) {
      case 1:
        PROTECT(r_call = Rf_lang2(fun_, args_[0]));
        break;
      case 2:
        PROTECT(r_call = Rf_lang3(fun_, args_[0], args_[1]));
        break;
      case 3:
        PROTECT(r_call = Rf_lang4(fun_, args_[0], args_[1], args_[2]));
        break;
      default:
        throw std::logic_error("ERROR: too many arguments to deterministic function.");
      }
      PROTECT(ans = Rf_eval(r_call, R_GlobalEnv));
      updateFromSEXP(Dynamic<T>::value,ans);
      UNPROTECT(2);
      std::cout << "RDeterministic new value:"  << std::endl << Dynamic<T>::value << std::endl;
    }
    ~RDeterministic() { UNPROTECT(1); }
    RDeterministic(T& value, SEXP fun, arglistT& args): Dynamic<T>(value), fun_(fun), args_(args) {}

    void accept() {}
    void reject(){}
    void tune() {}
    // in Dynamic: void preserve()
    // in Dynamic: void revert()
    // in Dynamic: void tally()
    bool isDeterministc() const { return true; }
    bool isStochastic() const { return false; }
    bool isObserved() const { return false; }
    void setScale(const double scale) {}
  };

} // namespace cppbugs
#endif //R_DETERMINISTIC_H

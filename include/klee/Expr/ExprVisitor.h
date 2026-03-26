//===-- ExprVisitor.h -------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_EXPRVISITOR_H
#define KLEE_EXPRVISITOR_H

#include "ExprHashMap.h"

namespace klee {
  class ExprVisitor {
  protected:
    // typed variant, but non-virtual for efficiency
    class Action {
    public:
      enum Kind { SkipChildren, DoChildren, ChangeTo };

    private:
      //      Action() {}
      Action(Kind _kind) 
        : kind(_kind), argument(nullptr) {}
      Action(Kind _kind, const ref<Expr> &_argument) 
        : kind(_kind), argument(_argument) {}

      friend class ExprVisitor;

    public:
      Kind kind;
      ref<Expr> argument;

      static Action changeTo(const ref<Expr> &expr) { 
        return Action(ChangeTo,expr); 
      }
      static Action doChildren() { return Action(DoChildren); }
      static Action skipChildren() { return Action(SkipChildren); }
    };

  protected:
    explicit
    ExprVisitor(bool _recursive=false) : recursive(_recursive) {}
    virtual ~ExprVisitor() {}

    virtual Action visitExpr(const Expr&);
    virtual Action visitExprPost(const Expr&);

    virtual Action visitNotOptimized(const NotOptimizedExpr&);
    virtual Action visitRead(const ReadExpr&);
    virtual Action visitSelect(const SelectExpr&);
    virtual Action visitConcat(const ConcatExpr&);
    virtual Action visitExtract(const ExtractExpr&);
    virtual Action visitZExt(const ZExtExpr&);
    virtual Action visitSExt(const SExtExpr&);
    virtual Action visitAdd(const AddExpr&);
    virtual Action visitSub(const SubExpr&);
    virtual Action visitMul(const MulExpr&);
    virtual Action visitUDiv(const UDivExpr&);
    virtual Action visitSDiv(const SDivExpr&);
    virtual Action visitURem(const URemExpr&);
    virtual Action visitSRem(const SRemExpr&);
    virtual Action visitNot(const NotExpr&);
    virtual Action visitAnd(const AndExpr&);
    virtual Action visitOr(const OrExpr&);
    virtual Action visitXor(const XorExpr&);
    virtual Action visitShl(const ShlExpr&);
    virtual Action visitLShr(const LShrExpr&);
    virtual Action visitAShr(const AShrExpr&);
    virtual Action visitEq(const EqExpr&);
    virtual Action visitNe(const NeExpr&);
    virtual Action visitUlt(const UltExpr&);
    virtual Action visitUle(const UleExpr&);
    virtual Action visitUgt(const UgtExpr&);
    virtual Action visitUge(const UgeExpr&);
    virtual Action visitSlt(const SltExpr&);
    virtual Action visitSle(const SleExpr&);
    virtual Action visitSgt(const SgtExpr&);
    virtual Action visitSge(const SgeExpr&);

    // String operations
    virtual Action visitStrVar(const StrVarExpr&);
    virtual Action visitStrLiteral(const StrLiteralExpr&);
    virtual Action visitStrEq(const StrEqExpr&);
    virtual Action visitStrLen(const StrLenExpr&);
    virtual Action visitStrConcat(const StrConcatExpr&);
    virtual Action visitStrContains(const StrContainsExpr&);
    virtual Action visitStrIndexOf(const StrIndexOfExpr&);
    virtual Action visitStrCharAt(const StrCharAtExpr&);
    virtual Action visitStrSubstr(const StrSubstrExpr&);
    virtual Action visitStrMatchesRegex(const StrMatchesRegexExpr&);

    // Floating-point operations
    virtual Action visitFpAdd(const FpAddExpr&);
    virtual Action visitFpSub(const FpSubExpr&);
    virtual Action visitFpMul(const FpMulExpr&);
    virtual Action visitFpDiv(const FpDivExpr&);
    virtual Action visitFpRem(const FpRemExpr&);
    virtual Action visitFpNeg(const FpNegExpr&);
    virtual Action visitFpCmp(const FpCmpExpr&);
    virtual Action visitFpConv(const FpConvExpr&);

  private:
    typedef ExprHashMap< ref<Expr> > visited_ty;
    visited_ty visited;
    bool recursive;

    ref<Expr> visitActual(const ref<Expr> &e);
    
  public:
    // apply the visitor to the expression and return a possibly
    // modified new expression.
    ref<Expr> visit(const ref<Expr> &e);
  };

}

#endif /* KLEE_EXPRVISITOR_H */

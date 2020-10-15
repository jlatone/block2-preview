
/*
 * block2: Efficient MPO implementation of quantum chemistry DMRG
 * Copyright (C) 2020 Huanchen Zhai <hczhai@caltech.edu>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include "delayed_sparse_matrix.hpp"
#include "tensor_functions.hpp"

using namespace std;

namespace block2 {

// Operations for operator tensors
template <typename S> struct DelayedTensorFunctions : TensorFunctions<S> {
    using TensorFunctions<S>::opf;
    DelayedTensorFunctions(const shared_ptr<OperatorFunctions<S>> &opf)
        : TensorFunctions<S>(opf) {}
    const TensorFunctionsTypes get_type() const override {
        return TensorFunctionsTypes::Delayed;
    }
    // c = a
    void left_assign(const shared_ptr<OperatorTensor<S>> &a,
                     shared_ptr<OperatorTensor<S>> &c) const override {
        assert(a->lmat != nullptr);
        assert(a->lmat->get_type() == SymTypes::RVec);
        assert(c->lmat != nullptr);
        assert(c->lmat->get_type() == SymTypes::RVec);
        assert(a->lmat->data.size() == c->lmat->data.size());
        for (size_t i = 0; i < a->lmat->data.size(); i++) {
            if (a->lmat->data[i]->get_type() == OpTypes::Zero)
                c->lmat->data[i] = a->lmat->data[i];
            else {
                assert(a->lmat->data[i] == c->lmat->data[i]);
                auto pa = abs_value(a->lmat->data[i]),
                     pc = abs_value(c->lmat->data[i]);
                shared_ptr<SparseMatrix<S>> &mata = a->ops.at(pa);
                shared_ptr<SparseMatrix<S>> &matc = c->ops.at(pc);
                bool delayed = mata->get_type() == SparseMatrixTypes::Delayed;
                if (delayed) {
                    if (mata->info->n == matc->info->n)
                        matc =
                            dynamic_pointer_cast<DelayedSparseMatrix<S>>(mata)
                                ->copy();
                    else
                        matc =
                            dynamic_pointer_cast<DelayedSparseMatrix<S>>(mata)
                                ->selective_copy(matc->info);
                    matc->factor = mata->factor;
                } else {
                    matc->allocate(matc->info);
                    if (matc->info->n == mata->info->n) {
                        matc->copy_data_from(mata, true);
                    } else
                        matc->selective_copy_from(mata, true);
                    matc->factor = mata->factor;
                }
            }
        }
    }
    // c = a
    void right_assign(const shared_ptr<OperatorTensor<S>> &a,
                      shared_ptr<OperatorTensor<S>> &c) const override {
        assert(a->rmat != nullptr);
        assert(a->rmat->get_type() == SymTypes::CVec);
        assert(c->rmat != nullptr);
        assert(c->rmat->get_type() == SymTypes::CVec);
        assert(a->rmat->data.size() == c->rmat->data.size());
        for (size_t i = 0; i < a->rmat->data.size(); i++) {
            if (a->rmat->data[i]->get_type() == OpTypes::Zero)
                c->rmat->data[i] = a->rmat->data[i];
            else {
                assert(a->rmat->data[i] == c->rmat->data[i]);
                auto pa = abs_value(a->rmat->data[i]),
                     pc = abs_value(c->rmat->data[i]);
                shared_ptr<SparseMatrix<S>> &mata = a->ops.at(pa);
                shared_ptr<SparseMatrix<S>> &matc = c->ops.at(pc);
                bool delayed = mata->get_type() == SparseMatrixTypes::Delayed;
                if (delayed) {
                    if (mata->info->n == matc->info->n)
                        matc =
                            dynamic_pointer_cast<DelayedSparseMatrix<S>>(mata)
                                ->copy();
                    else
                        matc =
                            dynamic_pointer_cast<DelayedSparseMatrix<S>>(mata)
                                ->selective_copy(matc->info);
                    matc->factor = mata->factor;
                } else {
                    matc->allocate(matc->info);
                    if (matc->info->n == mata->info->n) {
                        matc->copy_data_from(mata, true);
                    } else
                        matc->selective_copy_from(mata, true);
                    matc->factor = mata->factor;
                }
            }
        }
    }
    // vmat = expr[L part | R part] x cmat (for perturbative noise)
    void tensor_product_partial_multiply(
        const shared_ptr<OpExpr<S>> &expr,
        const map<shared_ptr<OpExpr<S>>, shared_ptr<SparseMatrix<S>>,
                  op_expr_less<S>> &lop,
        const map<shared_ptr<OpExpr<S>>, shared_ptr<SparseMatrix<S>>,
                  op_expr_less<S>> &rop,
        bool trace_right, const shared_ptr<SparseMatrix<S>> &cmat,
        const vector<pair<uint8_t, S>> &psubsl,
        const vector<
            vector<shared_ptr<typename SparseMatrixInfo<S>::ConnectionInfo>>>
            &cinfos,
        const vector<S> &vdqs, const shared_ptr<SparseMatrixGroup<S>> &vmats,
        int &vidx) const override {
        const shared_ptr<OpElement<S>> i_op =
            make_shared<OpElement<S>>(OpNames::I, SiteIndex(), S());
        switch (expr->get_type()) {
        case OpTypes::Prod: {
            shared_ptr<OpString<S>> op =
                dynamic_pointer_cast<OpString<S>>(expr);
            assert(op->b != nullptr);
            shared_ptr<typename SparseMatrixInfo<S>::ConnectionInfo> old_cinfo =
                cmat->info->cinfo;
            if (trace_right) {
                assert(lop.count(op->a) != 0 && rop.count(i_op) != 0);
                shared_ptr<SparseMatrix<S>> lmat = lop.at(op->a);
                shared_ptr<SparseMatrix<S>> rmat = rop.at(i_op);
                bool dl = lmat->get_type() == SparseMatrixTypes::Delayed;
                bool dr = rmat->get_type() == SparseMatrixTypes::Delayed;
                lmat = dl ? dynamic_pointer_cast<DelayedSparseMatrix<S>>(lmat)
                                ->build()
                          : lmat;
                rmat = dr ? dynamic_pointer_cast<DelayedSparseMatrix<S>>(rmat)
                                ->build()
                          : rmat;
                S opdq = (op->conj & 1) ? -op->a->q_label : op->a->q_label;
                S pks = cmat->info->delta_quantum + opdq;
                int ij = lower_bound(psubsl.begin(), psubsl.end(),
                                     make_pair((uint8_t)(op->conj & 1), opdq)) -
                         psubsl.begin();
                for (int k = 0; k < pks.count(); k++) {
                    S vdq = pks[k];
                    int iv = lower_bound(vdqs.begin(), vdqs.end(), vdq) -
                             vdqs.begin();
                    shared_ptr<SparseMatrix<S>> vmat =
                        vidx == -1 ? (*vmats)[iv] : (*vmats)[vidx++];
                    cmat->info->cinfo = cinfos[ij][k];
                    opf->tensor_product_multiply(op->conj & 1, lmat, rmat, cmat,
                                                 vmat, opdq, op->factor);
                }
                if (dr)
                    rmat->deallocate();
                if (dl)
                    lmat->deallocate();
            } else {
                assert(lop.count(i_op) != 0 && rop.count(op->b) != 0);
                shared_ptr<SparseMatrix<S>> lmat = lop.at(i_op);
                shared_ptr<SparseMatrix<S>> rmat = rop.at(op->b);
                bool dl = lmat->get_type() == SparseMatrixTypes::Delayed;
                bool dr = rmat->get_type() == SparseMatrixTypes::Delayed;
                lmat = dl ? dynamic_pointer_cast<DelayedSparseMatrix<S>>(lmat)
                                ->build()
                          : lmat;
                rmat = dr ? dynamic_pointer_cast<DelayedSparseMatrix<S>>(rmat)
                                ->build()
                          : rmat;
                S opdq = (op->conj & 2) ? -op->b->q_label : op->b->q_label;
                S pks = cmat->info->delta_quantum + opdq;
                int ij =
                    lower_bound(psubsl.begin(), psubsl.end(),
                                make_pair((uint8_t)(!!(op->conj & 2)), opdq)) -
                    psubsl.begin();
                for (int k = 0; k < pks.count(); k++) {
                    S vdq = pks[k];
                    int iv = lower_bound(vdqs.begin(), vdqs.end(), vdq) -
                             vdqs.begin();
                    shared_ptr<SparseMatrix<S>> vmat =
                        vidx == -1 ? (*vmats)[iv] : (*vmats)[vidx++];
                    cmat->info->cinfo = cinfos[ij][k];
                    opf->tensor_product_multiply(op->conj & 2, lmat, rmat, cmat,
                                                 vmat, opdq, op->factor);
                }
                if (dr)
                    rmat->deallocate();
                if (dl)
                    lmat->deallocate();
            }
            cmat->info->cinfo = old_cinfo;
        } break;
        case OpTypes::Sum: {
            shared_ptr<OpSum<S>> op = dynamic_pointer_cast<OpSum<S>>(expr);
            for (auto &x : op->strings)
                tensor_product_partial_multiply(x, lop, rop, trace_right, cmat,
                                                psubsl, cinfos, vdqs, vmats,
                                                vidx);
        } break;
        case OpTypes::Zero:
            break;
        default:
            assert(false);
            break;
        }
    }
    // vmats = expr x cmats
    void tensor_product_multi_multiply(
        const shared_ptr<OpExpr<S>> &expr,
        const map<shared_ptr<OpExpr<S>>, shared_ptr<SparseMatrix<S>>,
                  op_expr_less<S>> &lop,
        const map<shared_ptr<OpExpr<S>>, shared_ptr<SparseMatrix<S>>,
                  op_expr_less<S>> &rop,
        const shared_ptr<SparseMatrixGroup<S>> &cmats,
        const shared_ptr<SparseMatrixGroup<S>> &vmats, S opdq,
        bool all_reduce) const override {
        for (int i = 0; i < cmats->n; i++)
            tensor_product_multiply(expr, lop, rop, (*cmats)[i], (*vmats)[i],
                                    opdq, false);
    }
    // vmat = expr x cmat
    void tensor_product_multiply(
        const shared_ptr<OpExpr<S>> &expr,
        const map<shared_ptr<OpExpr<S>>, shared_ptr<SparseMatrix<S>>,
                  op_expr_less<S>> &lop,
        const map<shared_ptr<OpExpr<S>>, shared_ptr<SparseMatrix<S>>,
                  op_expr_less<S>> &rop,
        const shared_ptr<SparseMatrix<S>> &cmat,
        const shared_ptr<SparseMatrix<S>> &vmat, S opdq,
        bool all_reduce) const override {
        switch (expr->get_type()) {
        case OpTypes::Prod: {
            shared_ptr<OpString<S>> op =
                dynamic_pointer_cast<OpString<S>>(expr);
            assert(op->b != nullptr);
            assert(!(lop.count(op->a) == 0 || rop.count(op->b) == 0));
            shared_ptr<SparseMatrix<S>> lmat = lop.at(op->a);
            shared_ptr<SparseMatrix<S>> rmat = rop.at(op->b);
            bool dl = lmat->get_type() == SparseMatrixTypes::Delayed;
            bool dr = rmat->get_type() == SparseMatrixTypes::Delayed;
            lmat =
                dl ? dynamic_pointer_cast<DelayedSparseMatrix<S>>(lmat)->build()
                   : lmat;
            rmat =
                dr ? dynamic_pointer_cast<DelayedSparseMatrix<S>>(rmat)->build()
                   : rmat;
            opf->tensor_product_multiply(op->conj, lmat, rmat, cmat, vmat, opdq,
                                         op->factor);
            if (dr)
                rmat->deallocate();
            if (dl)
                lmat->deallocate();
        } break;
        case OpTypes::Sum: {
            shared_ptr<OpSum<S>> op = dynamic_pointer_cast<OpSum<S>>(expr);
            for (auto &x : op->strings)
                tensor_product_multiply(x, lop, rop, cmat, vmat, opdq, false);
        } break;
        case OpTypes::Zero:
            break;
        default:
            assert(false);
            break;
        }
    }
    // mat = diag(expr)
    void tensor_product_diagonal(
        const shared_ptr<OpExpr<S>> &expr,
        const map<shared_ptr<OpExpr<S>>, shared_ptr<SparseMatrix<S>>,
                  op_expr_less<S>> &lop,
        const map<shared_ptr<OpExpr<S>>, shared_ptr<SparseMatrix<S>>,
                  op_expr_less<S>> &rop,
        shared_ptr<SparseMatrix<S>> &mat, S opdq) const override {
        switch (expr->get_type()) {
        case OpTypes::Prod: {
            shared_ptr<OpString<S>> op =
                dynamic_pointer_cast<OpString<S>>(expr);
            assert(op->b != nullptr);
            assert(!(lop.count(op->a) == 0 || rop.count(op->b) == 0));
            shared_ptr<SparseMatrix<S>> lmat = lop.at(op->a);
            shared_ptr<SparseMatrix<S>> rmat = rop.at(op->b);
            bool dl = lmat->get_type() == SparseMatrixTypes::Delayed;
            bool dr = rmat->get_type() == SparseMatrixTypes::Delayed;
            lmat =
                dl ? dynamic_pointer_cast<DelayedSparseMatrix<S>>(lmat)->build()
                   : lmat;
            rmat =
                dr ? dynamic_pointer_cast<DelayedSparseMatrix<S>>(rmat)->build()
                   : rmat;
            opf->tensor_product_diagonal(op->conj, lmat, rmat, mat, opdq,
                                         op->factor);
            if (dr)
                rmat->deallocate();
            if (dl)
                lmat->deallocate();
        } break;
        case OpTypes::Sum: {
            shared_ptr<OpSum<S>> op = dynamic_pointer_cast<OpSum<S>>(expr);
            for (auto &x : op->strings)
                tensor_product_diagonal(x, lop, rop, mat, opdq);
        } break;
        case OpTypes::Zero:
            break;
        default:
            assert(false);
            break;
        }
    }
    // mat = eval(expr)
    void
    tensor_product(const shared_ptr<OpExpr<S>> &expr,
                   const map<shared_ptr<OpExpr<S>>, shared_ptr<SparseMatrix<S>>,
                             op_expr_less<S>> &lop,
                   const map<shared_ptr<OpExpr<S>>, shared_ptr<SparseMatrix<S>>,
                             op_expr_less<S>> &rop,
                   shared_ptr<SparseMatrix<S>> &mat) const override {
        switch (expr->get_type()) {
        case OpTypes::Prod: {
            shared_ptr<OpString<S>> op =
                dynamic_pointer_cast<OpString<S>>(expr);
            assert(op->b != nullptr);
            assert(lop.count(op->a) != 0 && rop.count(op->b) != 0);
            shared_ptr<SparseMatrix<S>> lmat = lop.at(op->a);
            shared_ptr<SparseMatrix<S>> rmat = rop.at(op->b);
            bool dl = lmat->get_type() == SparseMatrixTypes::Delayed;
            bool dr = rmat->get_type() == SparseMatrixTypes::Delayed;
            lmat =
                dl ? dynamic_pointer_cast<DelayedSparseMatrix<S>>(lmat)->build()
                   : lmat;
            rmat =
                dr ? dynamic_pointer_cast<DelayedSparseMatrix<S>>(rmat)->build()
                   : rmat;
            opf->tensor_product(op->conj, lmat, rmat, mat, op->factor);
            if (dr)
                rmat->deallocate();
            if (dl)
                lmat->deallocate();
        } break;
        case OpTypes::SumProd: {
            shared_ptr<OpSumProd<S>> op =
                dynamic_pointer_cast<OpSumProd<S>>(expr);
            assert((op->a == nullptr) ^ (op->b == nullptr));
            assert(op->ops.size() != 0);
            shared_ptr<SparseMatrix<S>> tmp = make_shared<SparseMatrix<S>>();
            if (op->b == nullptr) {
                shared_ptr<OpExpr<S>> opb =
                    abs_value((shared_ptr<OpExpr<S>>)op->ops[0]);
                assert(lop.count(op->a) != 0 && rop.count(opb) != 0);
                tmp->allocate(rop.at(opb)->info);
                for (size_t i = 0; i < op->ops.size(); i++) {
                    shared_ptr<SparseMatrix<S>> rmat =
                        rop.at(abs_value((shared_ptr<OpExpr<S>>)op->ops[i]));
                    bool dr = rmat->get_type() == SparseMatrixTypes::Delayed;
                    rmat =
                        dr ? dynamic_pointer_cast<DelayedSparseMatrix<S>>(rmat)
                                 ->build()
                           : rmat;
                    opf->iadd(tmp, rmat, op->factor * op->ops[i]->factor,
                              op->conjs[i]);
                    if (opf->seq->mode == SeqTypes::Simple)
                        opf->seq->simple_perform();
                    if (dr)
                        rmat->deallocate();
                }
            } else {
                shared_ptr<OpExpr<S>> opa =
                    abs_value((shared_ptr<OpExpr<S>>)op->ops[0]);
                assert(lop.count(opa) != 0 && rop.count(op->b) != 0);
                tmp->allocate(lop.at(opa)->info);
                for (size_t i = 0; i < op->ops.size(); i++) {
                    shared_ptr<SparseMatrix<S>> lmat =
                        lop.at(abs_value((shared_ptr<OpExpr<S>>)op->ops[i]));
                    bool dl = lmat->get_type() == SparseMatrixTypes::Delayed;
                    lmat =
                        dl ? dynamic_pointer_cast<DelayedSparseMatrix<S>>(lmat)
                                 ->build()
                           : lmat;
                    opf->iadd(tmp, lmat, op->factor * op->ops[i]->factor,
                              op->conjs[i]);
                    if (opf->seq->mode == SeqTypes::Simple)
                        opf->seq->simple_perform();
                    if (dl)
                        lmat->deallocate();
                }
            }
            if (op->b == nullptr) {
                shared_ptr<SparseMatrix<S>> lmat = lop.at(op->a);
                bool dl = lmat->get_type() == SparseMatrixTypes::Delayed;
                lmat = dl ? dynamic_pointer_cast<DelayedSparseMatrix<S>>(lmat)
                                ->build()
                          : lmat;
                opf->tensor_product(op->conj, lmat, tmp, mat, 1.0);
                if (dl)
                    lmat->deallocate();
            } else {
                shared_ptr<SparseMatrix<S>> rmat = rop.at(op->b);
                bool dr = rmat->get_type() == SparseMatrixTypes::Delayed;
                rmat = dr ? dynamic_pointer_cast<DelayedSparseMatrix<S>>(rmat)
                                ->build()
                          : rmat;
                opf->tensor_product(op->conj, tmp, rmat, mat, 1.0);
                if (dr)
                    rmat->deallocate();
            }
            tmp->deallocate();
        } break;
        case OpTypes::Sum: {
            shared_ptr<OpSum<S>> op = dynamic_pointer_cast<OpSum<S>>(expr);
            for (auto &x : op->strings)
                tensor_product(x, lop, rop, mat);
        } break;
        case OpTypes::Zero:
            break;
        default:
            assert(false);
            break;
        }
    }
    // c = mpst_bra x a x mpst_ket
    void left_rotate(const shared_ptr<OperatorTensor<S>> &a,
                     const shared_ptr<SparseMatrix<S>> &mpst_bra,
                     const shared_ptr<SparseMatrix<S>> &mpst_ket,
                     shared_ptr<OperatorTensor<S>> &c) const override {
        for (auto &p : c->ops) {
            shared_ptr<OpElement<S>> op =
                dynamic_pointer_cast<OpElement<S>>(p.first);
            c->ops.at(op)->allocate(c->ops.at(op)->info);
        }
        for (size_t i = 0; i < a->lmat->data.size(); i++)
            if (a->lmat->data[i]->get_type() != OpTypes::Zero) {
                auto pa = abs_value(a->lmat->data[i]);
                shared_ptr<SparseMatrix<S>> mata = a->ops.at(pa);
                bool da = mata->get_type() == SparseMatrixTypes::Delayed;
                mata = da ? dynamic_pointer_cast<DelayedSparseMatrix<S>>(mata)
                                ->build()
                          : mata;
                shared_ptr<SparseMatrix<S>> matc = c->ops.at(pa);
                opf->tensor_rotate(mata, matc, mpst_bra, mpst_ket, false);
                if (da)
                    mata->deallocate();
            }
        if (opf->seq->mode == SeqTypes::Auto)
            opf->seq->auto_perform();
    }
    // c = mpst_bra x a x mpst_ket
    void right_rotate(const shared_ptr<OperatorTensor<S>> &a,
                      const shared_ptr<SparseMatrix<S>> &mpst_bra,
                      const shared_ptr<SparseMatrix<S>> &mpst_ket,
                      shared_ptr<OperatorTensor<S>> &c) const override {
        for (auto &p : c->ops) {
            shared_ptr<OpElement<S>> op =
                dynamic_pointer_cast<OpElement<S>>(p.first);
            c->ops.at(op)->allocate(c->ops.at(op)->info);
        }
        for (size_t i = 0; i < a->rmat->data.size(); i++)
            if (a->rmat->data[i]->get_type() != OpTypes::Zero) {
                auto pa = abs_value(a->rmat->data[i]);
                shared_ptr<SparseMatrix<S>> mata = a->ops.at(pa);
                bool da = mata->get_type() == SparseMatrixTypes::Delayed;
                mata = da ? dynamic_pointer_cast<DelayedSparseMatrix<S>>(mata)
                                ->build()
                          : mata;
                shared_ptr<SparseMatrix<S>> matc = c->ops.at(pa);
                opf->tensor_rotate(mata, matc, mpst_bra, mpst_ket, true);
                if (da)
                    mata->deallocate();
            }
        if (opf->seq->mode == SeqTypes::Auto)
            opf->seq->auto_perform();
    }
    // Numerical transform from normal operators
    // to complementary operators near the middle site
    void
    numerical_transform(const shared_ptr<OperatorTensor<S>> &a,
                        const shared_ptr<Symbolic<S>> &names,
                        const shared_ptr<Symbolic<S>> &exprs) const override {
        assert(names->data.size() == exprs->data.size());
        assert((a->lmat == nullptr) ^ (a->rmat == nullptr));
        if (a->lmat == nullptr)
            a->rmat = names;
        else
            a->lmat = names;
        for (size_t i = 0; i < a->ops.size(); i++) {
            bool found = false;
            for (size_t k = 0; k < names->data.size(); k++) {
                if (exprs->data[k]->get_type() == OpTypes::Zero)
                    continue;
                shared_ptr<OpExpr<S>> nop = abs_value(names->data[k]);
                shared_ptr<OpExpr<S>> expr =
                    exprs->data[k] *
                    (1 / dynamic_pointer_cast<OpElement<S>>(names->data[k])
                             ->factor);
                assert(a->ops.count(nop) != 0);
                switch (expr->get_type()) {
                case OpTypes::Sum: {
                    shared_ptr<OpSum<S>> op =
                        dynamic_pointer_cast<OpSum<S>>(expr);
                    found |= i < op->strings.size();
                    if (i < op->strings.size()) {
                        shared_ptr<OpElement<S>> nexpr =
                            op->strings[i]->get_op();
                        assert(a->ops.count(nexpr) != 0);
                        shared_ptr<SparseMatrix<S>> imat = a->ops.at(nexpr);
                        bool di =
                            imat->get_type() == SparseMatrixTypes::Delayed;
                        imat =
                            di ? dynamic_pointer_cast<DelayedSparseMatrix<S>>(
                                     imat)
                                     ->build()
                               : imat;
                        shared_ptr<SparseMatrix<S>> omat = a->ops.at(nop);
                        opf->iadd(omat, imat, op->strings[i]->factor,
                                  op->strings[i]->conj != 0);
                        if (opf->seq->mode == SeqTypes::Simple)
                            opf->seq->simple_perform();
                        if (di)
                            imat->deallocate();
                    }
                } break;
                case OpTypes::Zero:
                    break;
                default:
                    assert(false);
                    break;
                }
            }
            if (!found)
                break;
        }
        if (opf->seq->mode == SeqTypes::Auto)
            opf->seq->auto_perform();
    }
    // c = a x b (dot)
    void left_contract(
        const shared_ptr<OperatorTensor<S>> &a,
        const shared_ptr<OperatorTensor<S>> &b,
        shared_ptr<OperatorTensor<S>> &c,
        const shared_ptr<Symbolic<S>> &cexprs = nullptr) const override {
        if (a == nullptr)
            left_assign(b, c);
        else {
            shared_ptr<Symbolic<S>> exprs =
                cexprs == nullptr ? a->lmat * b->lmat : cexprs;
            assert(exprs->data.size() == c->lmat->data.size());
            for (size_t i = 0; i < exprs->data.size(); i++) {
                shared_ptr<OpElement<S>> cop =
                    dynamic_pointer_cast<OpElement<S>>(c->lmat->data[i]);
                shared_ptr<OpExpr<S>> op = abs_value(c->lmat->data[i]);
                shared_ptr<OpExpr<S>> expr = exprs->data[i] * (1 / cop->factor);
                c->ops.at(op)->allocate(c->ops.at(op)->info);
                tensor_product(expr, a->ops, b->ops, c->ops.at(op));
            }
            if (opf->seq->mode == SeqTypes::Auto)
                opf->seq->auto_perform();
        }
    }
    // c = b (dot) x a
    void right_contract(
        const shared_ptr<OperatorTensor<S>> &a,
        const shared_ptr<OperatorTensor<S>> &b,
        shared_ptr<OperatorTensor<S>> &c,
        const shared_ptr<Symbolic<S>> &cexprs = nullptr) const override {
        if (a == nullptr)
            right_assign(b, c);
        else {
            shared_ptr<Symbolic<S>> exprs =
                cexprs == nullptr ? b->rmat * a->rmat : cexprs;
            assert(exprs->data.size() == c->rmat->data.size());
            for (size_t i = 0; i < exprs->data.size(); i++) {
                shared_ptr<OpElement<S>> cop =
                    dynamic_pointer_cast<OpElement<S>>(c->rmat->data[i]);
                shared_ptr<OpExpr<S>> op = abs_value(c->rmat->data[i]);
                shared_ptr<OpExpr<S>> expr = exprs->data[i] * (1 / cop->factor);
                c->ops.at(op)->allocate(c->ops.at(op)->info);
                tensor_product(expr, b->ops, a->ops, c->ops.at(op));
            }
            if (opf->seq->mode == SeqTypes::Auto)
                opf->seq->auto_perform();
        }
    }
};

} // namespace block2


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

#include "integral.hpp"
#include "mps.hpp"
#include "mps_unfused.hpp"
#include <algorithm>
#include <array>
#include <set>
#include <stack>
#include <tuple>
#include <vector>

using namespace std;

namespace block2 {

// Prefix trie structure
// can be used as map<DET, double>
// memory complexity:
//    (n_dets << 4^n_sites) : (4 * n_sites + 1) * n_dets * sizeof(int)
//    (n_dets  ~ 4^n_sites) : (19 / 3) * n_dets * sizeof(int)
// time complexity: (D = MPS bond dimension)
//    (n_dets << 4^n_sites) : n_sites * n_dets * D * D
//    (n_dets  ~ 4^n_sites) : (4 / 3) * n_dets * D * D
template <typename D, uint8_t L = 4> struct TRIE {
    vector<array<int, L>> data;
    vector<int> dets, invs;
    vector<double> vals;
    int n_sites;
    bool enable_look_up;
    TRIE(int n_sites, bool enable_look_up = false)
        : n_sites(n_sites), enable_look_up(enable_look_up) {
        data.reserve(n_sites + 1);
        data.push_back(array<int, L>());
    }
    // empty trie
    void clear() {
        data.clear(), dets.clear(), invs.clear(), vals.clear();
        data.push_back(array<int, L>());
    }
    // deep copy
    shared_ptr<D> copy() {
        shared_ptr<D> dett = make_shared<D>(n_sites, enable_look_up);
        dett->data = vector<array<int, L>>(data.begin(), data.end());
        dett->dets = vector<int>(dets.begin(), dets.end());
        dett->invs = vector<int>(invs.begin(), invs.end());
        dett->vals = vector<double>(vals.begin(), vals.end());
        return dett;
    }
    // number of determinants
    size_t size() const noexcept { return dets.size(); }
    // add a determinant to trie
    void push_back(const vector<uint8_t> &det) {
        assert((int)det.size() == n_sites);
        int cur = 0;
        for (int i = 0; i < n_sites; i++) {
            uint8_t j = det[i];
            if (data[cur][j] == 0) {
                data[cur][j] = (int)data.size();
                data.push_back(array<int, L>());
            }
            cur = data[cur][j];
        }
        // cannot push_back repeated determinants
        assert(dets.size() == 0 || cur > dets.back());
        dets.push_back(cur);
        if (enable_look_up) {
            invs.resize(data.size());
            for (int i = 0, cur = 0; i < n_sites; i++) {
                uint8_t j = det[i];
                invs[data[cur][j]] = cur;
                cur = data[cur][j];
            }
        }
    }
    // find the index of a determinant
    int find(const vector<uint8_t> &det) {
        assert((int)det.size() == n_sites);
        int cur = 0;
        for (int i = 0; i < n_sites; i++) {
            uint8_t j = det[i];
            if (data[cur][j] == 0)
                return -1;
            cur = data[cur][j];
        }
        int idx =
            (int)(lower_bound(dets.begin(), dets.end(), cur) - dets.begin());
        return idx < (int)dets.size() && dets[idx] == cur ? idx : -1;
    }
    // get a determinant in trie
    vector<uint8_t> operator[](int idx) const {
        assert(enable_look_up && idx < dets.size());
        vector<uint8_t> r(n_sites, 0);
        for (int cur = dets[idx], i = n_sites - 1, ir; i >= 0; i--, cur = ir) {
            ir = invs[cur];
            for (uint8_t j = 0; j < (uint8_t)data[ir].size(); j++)
                if (data[ir][j] == cur) {
                    r[i] = j;
                    break;
                }
        }
        return r;
    }
    vector<double> get_state_occupation() const {
        int ntg = threading->activate_global();
        vector<vector<double>> pop(ntg);
#pragma omp parallel num_threads(ntg)
        {
            int tid = threading->get_thread_id();
            pop[tid].resize(n_sites * L);
            vector<double> &ipop = pop[tid];
#pragma omp for schedule(static)
            for (int i = 0; i < dets.size(); i++) {
                double vsq = vals[i] * vals[i];
                vector<uint8_t> det = (*this)[i];
                for (int j = 0; j < n_sites; j++)
                    ipop[j * L + det[j]] += vsq;
            }
        }
        vector<double> rpop(n_sites * L, 0);
        for (int itg = 0; itg < ntg; itg++)
            for (int k = 0; k < n_sites * L; k++)
                rpop[k] += pop[itg][k];
        return rpop;
    }
};

template <typename, typename = void> struct DeterminantTRIE;

// Prefix trie structure of determinants (non-spin-adapted)
// det[i] = 0 (empty) 1 (alpha) 2 (beta) 3 (double)
template <typename S>
struct DeterminantTRIE<S, typename S::is_sz_t> : TRIE<DeterminantTRIE<S>> {
    using TRIE<DeterminantTRIE<S>>::data;
    using TRIE<DeterminantTRIE<S>>::dets;
    using TRIE<DeterminantTRIE<S>>::vals;
    using TRIE<DeterminantTRIE<S>>::invs;
    using TRIE<DeterminantTRIE<S>>::n_sites;
    using TRIE<DeterminantTRIE<S>>::enable_look_up;
    DeterminantTRIE(int n_sites, bool enable_look_up = false)
        : TRIE<DeterminantTRIE<S>>(n_sites, enable_look_up) {}
    // set the value for each determinant to the overlap between mps
    void evaluate(const shared_ptr<UnfusedMPS<S>> &mps, double cutoff = 0) {
        vals.resize(dets.size());
        memset(vals.data(), 0, sizeof(double) * vals.size());
        bool has_dets = dets.size() != 0;
        stack<tuple<int, int, int>> ptrs;
        vector<map<S, vector<double>>> partials;
        vector<uint8_t> det(n_sites);
        if (!has_dets) {
            for (uint8_t j = 0; j < (uint8_t)data[0].size(); j++)
                if (data[0][j] == 0) {
                    data[0][j] = (int)data.size();
                    data.push_back(array<int, 4>{0, 0, 0, 0});
                }
        }
        for (uint8_t j = 0; j < (int)data[0].size(); j++)
            if (data[0][j] != 0)
                ptrs.push(make_tuple(data[0][j], j, 0));
        map<S, vector<double>> mp;
        mp[mps->info->vacuum] = vector<double>{1.0};
        partials.push_back(mp);
        threading->activate_global_mkl();
        // depth-first traverse of trie
        while (!ptrs.empty()) {
            check_signal_()();
            auto &p = ptrs.top();
            int cur = get<0>(p), j = get<1>(p), d = get<2>(p);
            det[d] = j;
            partials.resize(d + 1);
            partials.push_back(map<S, vector<double>>());
            map<S, vector<double>> &pmp = partials[d], &cmp = partials[d + 1];
            vector<S> vcmp;
            vcmp.reserve(mps->tensors[d]->data[j].size());
            unordered_map<S, vector<size_t>> kcmp;
            kcmp.reserve(vcmp.size());
            for (size_t im = 0; im < mps->tensors[d]->data[j].size(); im++) {
                auto &m = mps->tensors[d]->data[j][im];
                S bra = m.first.first, ket = m.first.second;
                if (pmp.count(bra)) {
                    if (!cmp.count(ket)) {
                        vcmp.push_back(ket);
                        MatrixRef mat = m.second->ref();
                        cmp[ket] = vector<double>(mat.n, 0);
                    }
                    kcmp[ket].push_back(im);
                }
            }
            for (int i = 0; i < (int)vcmp.size(); i++) {
                for (auto &im : kcmp.at(vcmp[i])) {
                    auto &m = mps->tensors[d]->data[j][im];
                    S bra = m.first.first, ket = m.first.second;
                    MatrixRef mat = m.second->ref();
                    MatrixFunctions::multiply(
                        MatrixRef(&pmp[bra][0], 1, mat.m), false, mat, false,
                        MatrixRef(&cmp[ket][0], 1, mat.n), 1.0, 1.0);
                }
            }
            ptrs.pop();
            if (cmp.size() == 0)
                continue;
            if (cutoff != 0) {
                double sqsum = 0;
                for (int i = 0; i < (int)vcmp.size(); i++) {
                    double m_norm = MatrixFunctions::norm(
                        MatrixRef(&cmp.at(vcmp[i])[0],
                                  (MKL_INT)cmp.at(vcmp[i]).size(), 1));
                    sqsum += m_norm * m_norm;
                }
                if (sqrt(sqsum) < cutoff)
                    continue;
            }
            if (d == n_sites - 1) {
                assert(cmp.size() == 1 && cmp.count(mps->info->target) != 0);
                assert(cmp[mps->info->target].size() == 1);
                if (!has_dets) {
                    dets.push_back(cur);
                    vals.push_back(cmp[mps->info->target][0]);
                    if (enable_look_up) {
                        invs.resize(data.size());
                        for (int i = 0, curx = 0; i < n_sites; i++) {
                            uint8_t jj = det[i];
                            invs[data[curx][jj]] = curx;
                            curx = data[curx][jj];
                        }
                    }
                } else
                    vals[lower_bound(dets.begin(), dets.end(), cur) -
                         dets.begin()] = cmp[mps->info->target][0];
            } else {
                if (!has_dets) {
                    for (uint8_t jj = 0; jj < (uint8_t)data[cur].size(); jj++)
                        if (data[cur][jj] == 0) {
                            data[cur][jj] = (int)data.size();
                            data.push_back(array<int, 4>{0, 0, 0, 0});
                        }
                }
                for (uint8_t jj = 0; jj < (uint8_t)data[cur].size(); jj++)
                    if (data[cur][jj] != 0)
                        ptrs.push(make_tuple(data[cur][jj], jj, d + 1));
            }
        }
        threading->activate_normal();
    }
};

// Prefix trie structure of Configuration State Functions (CSFs) (spin-adapted)
// det[i] = 0 (empty) 1 (increase) 2 (decrease) 3 (double)
template <typename S>
struct DeterminantTRIE<S, typename S::is_su2_t> : TRIE<DeterminantTRIE<S>> {
    using TRIE<DeterminantTRIE<S>>::data;
    using TRIE<DeterminantTRIE<S>>::dets;
    using TRIE<DeterminantTRIE<S>>::vals;
    using TRIE<DeterminantTRIE<S>>::invs;
    using TRIE<DeterminantTRIE<S>>::n_sites;
    using TRIE<DeterminantTRIE<S>>::enable_look_up;
    DeterminantTRIE(int n_sites, bool enable_look_up = false)
        : TRIE<DeterminantTRIE<S>>(n_sites, enable_look_up) {}
    // set the value for each CSF to the overlap between mps
    void evaluate(const shared_ptr<UnfusedMPS<S>> &mps, double cutoff = 0) {
        vals.resize(dets.size());
        memset(vals.data(), 0, sizeof(double) * vals.size());
        bool has_dets = dets.size() != 0;
        stack<tuple<int, int, int>> ptrs;
        vector<map<S, vector<double>>> partials;
        vector<uint8_t> det(n_sites);
        if (!has_dets) {
            for (uint8_t j = 0; j < (uint8_t)data[0].size(); j++)
                if (data[0][j] == 0) {
                    data[0][j] = (int)data.size();
                    data.push_back(array<int, 4>{0, 0, 0, 0});
                }
        }
        for (uint8_t j = 0; j < (int)data[0].size(); j++)
            if (data[0][j] != 0)
                ptrs.push(make_tuple(data[0][j], j, 0));
        map<S, vector<double>> mp;
        mp[mps->info->vacuum] = vector<double>{1.0};
        partials.push_back(mp);
        threading->activate_global_mkl();
        // depth-first traverse of trie
        while (!ptrs.empty()) {
            check_signal_()();
            auto &p = ptrs.top();
            int cur = get<0>(p), j = get<1>(p), d = get<2>(p);
            int jd = j >= 2 ? j - 1 : j;
            det[d] = j;
            partials.resize(d + 1);
            partials.push_back(map<S, vector<double>>());
            map<S, vector<double>> &pmp = partials[d], &cmp = partials[d + 1];
            vector<S> vcmp;
            vcmp.reserve(mps->tensors[d]->data[jd].size());
            unordered_map<S, vector<size_t>> kcmp;
            kcmp.reserve(vcmp.size());
            for (size_t im = 0; im < mps->tensors[d]->data[jd].size(); im++) {
                auto &m = mps->tensors[d]->data[jd][im];
                S bra = m.first.first, ket = m.first.second;
                if (jd == 1 && !((j == 1 && ket.twos() > bra.twos()) ||
                                 (j == 2 && ket.twos() < bra.twos())))
                    continue;
                if (pmp.count(bra)) {
                    if (!cmp.count(ket)) {
                        vcmp.push_back(ket);
                        MatrixRef mat = m.second->ref();
                        cmp[ket] = vector<double>(mat.n, 0);
                    }
                    kcmp[ket].push_back(im);
                }
            }
            for (int i = 0; i < (int)vcmp.size(); i++) {
                for (auto &im : kcmp.at(vcmp[i])) {
                    auto &m = mps->tensors[d]->data[jd][im];
                    S bra = m.first.first, ket = m.first.second;
                    MatrixRef mat = m.second->ref();
                    MatrixFunctions::multiply(
                        MatrixRef(&pmp[bra][0], 1, mat.m), false, mat, false,
                        MatrixRef(&cmp[ket][0], 1, mat.n), 1.0, 1.0);
                }
            }
            ptrs.pop();
            if (cmp.size() == 0)
                continue;
            if (cutoff != 0) {
                double sqsum = 0;
                for (int i = 0; i < (int)vcmp.size(); i++) {
                    double m_norm = MatrixFunctions::norm(
                        MatrixRef(&cmp.at(vcmp[i])[0],
                                  (MKL_INT)cmp.at(vcmp[i]).size(), 1));
                    sqsum += m_norm * m_norm;
                }
                if (sqrt(sqsum) < cutoff)
                    continue;
            }
            if (d == n_sites - 1) {
                assert(cmp.size() == 1 && cmp.count(mps->info->target) != 0);
                assert(cmp[mps->info->target].size() == 1);
                if (!has_dets) {
                    dets.push_back(cur);
                    vals.push_back(cmp[mps->info->target][0]);
                    if (enable_look_up) {
                        invs.resize(data.size());
                        for (int i = 0, curx = 0; i < n_sites; i++) {
                            uint8_t jj = det[i];
                            invs[data[curx][jj]] = curx;
                            curx = data[curx][jj];
                        }
                    }
                } else
                    vals[lower_bound(dets.begin(), dets.end(), cur) -
                         dets.begin()] = cmp[mps->info->target][0];
            } else {
                if (!has_dets) {
                    for (uint8_t jj = 0; jj < (uint8_t)data[cur].size(); jj++)
                        if (data[cur][jj] == 0) {
                            data[cur][jj] = (int)data.size();
                            data.push_back(array<int, 4>{0, 0, 0, 0});
                        }
                }
                for (uint8_t jj = 0; jj < (uint8_t)data[cur].size(); jj++)
                    if (data[cur][jj] != 0)
                        ptrs.push(make_tuple(data[cur][jj], jj, d + 1));
            }
        }
        threading->activate_normal();
    }
};

template <typename S> struct DeterminantQC {
    vector<uint8_t> hf_occ, orb_sym;
    vector<double> h1e_energy;
    int n_trials = 20, n_outer_trials = 50000;
    DeterminantQC(const vector<uint8_t> &hf_occ, const vector<uint8_t> &orb_sym,
                  const vector<double> &h1e_energy)
        : hf_occ(hf_occ), orb_sym(orb_sym), h1e_energy(h1e_energy) {}
    struct det_less {
        bool operator()(const vector<uint8_t> &a,
                        const vector<uint8_t> &b) const {
            assert(a.size() == b.size());
            for (size_t i = 0; i < a.size(); i++)
                if (a[i] != b[i])
                    return a[i] < b[i];
            return false;
        }
    };
    S det_quantum(const vector<uint8_t> &det, int i_begin, int i_end) const {
        int n_block_sites = i_end - i_begin;
        assert(det.size() == n_block_sites);
        int n = 0, twos = 0, ipg = 0;
        for (int i = 0; i < n_block_sites; i++) {
            n += det[i];
            if (det[i] == 1)
                ipg ^= orb_sym[i + i_begin], twos++;
        }
        return S(n, twos, ipg);
    }
    // generate determinants for quantum number q for block [i_begin, i_end)
    vector<vector<uint8_t>> distribute(S q, int i_begin, int i_end) const {
        int n_block_sites = i_end - i_begin;
        vector<uint8_t> idx(n_block_sites, 0);
        for (int i = 0; i < n_block_sites; i++)
            idx[i] = i_begin + i;
        sort(idx.begin(), idx.end(), [this](int i, int j) {
            return hf_occ[i] != hf_occ[j] ? (hf_occ[i] > hf_occ[j])
                                          : (h1e_energy[i] < h1e_energy[j]);
        });
        int n_alpha = (q.n() + q.twos()) >> 1, n_beta = (q.n() - q.twos()) >> 1;
        int n_docc = min(n_alpha, n_beta);
        assert(n_alpha >= 0 && n_beta >= 0 && n_alpha <= n_block_sites &&
               n_beta <= n_block_sites);
        vector<bool> mask(n_block_sites, true);
        for (int i = 0; i < max(n_alpha, n_beta); i++)
            mask[i] = false;
        vector<vector<uint8_t>> r;
        for (int jt = 0; jt < n_outer_trials && r.empty(); jt++)
            for (int it = 0; it < n_trials; it++) {
                next_permutation(mask.begin(), mask.end());
                vector<uint8_t> iocc(n_block_sites, 0);
                for (int i = 0, j = 0; i < n_block_sites; i++)
                    !mask[i] && (iocc[idx[i] - i_begin] = j++ < n_docc ? 2 : 1);
                if (det_quantum(iocc, i_begin, i_end).pg() == q.pg())
                    r.push_back(iocc);
            }
        return r;
    }
};

// Quantum number infomation in a MPS
// Generated from determinant, used for warm-up sweep
template <typename S> struct DeterminantMPSInfo : MPSInfo<S> {
    using MPSInfo<S>::basis;
    shared_ptr<FCIDUMP> fcidump;
    shared_ptr<DeterminantQC<S>> det;
    vector<uint8_t> iocc;
    ubond_t n_det_states = 2; // number of states for each determinant
    DeterminantMPSInfo(int n_sites, S vacuum, S target,
                       const vector<shared_ptr<StateInfo<S>>> &basis,
                       const vector<uint8_t> &orb_sym, uint8_t n_syms,
                       const vector<uint8_t> &iocc,
                       const shared_ptr<FCIDUMP> &fcidump)
        : iocc(iocc), fcidump(fcidump),
          det(make_shared<DeterminantQC<S>>(iocc, orb_sym,
                                            fcidump->h1e_energy())),
          MPSInfo<S>(n_sites, vacuum, target, basis, n_syms) {}
    void set_bond_dimension(ubond_t m) override {
        this->bond_dim = m;
        this->left_dims[0] = make_shared<StateInfo<S>>(this->vacuum);
        this->right_dims[this->n_sites] =
            make_shared<StateInfo<S>>(this->vacuum);
    }
    WarmUpTypes get_warm_up_type() const override {
        return WarmUpTypes::Determinant;
    }
    void set_left_bond_dimension(int i,
                                 const vector<vector<vector<uint8_t>>> &dets) {
        this->left_dims[0] = make_shared<StateInfo<S>>(this->vacuum);
        for (int j = 0; j < i; j++) {
            set<vector<uint8_t>, typename DeterminantQC<S>::det_less> mp;
            for (auto &idets : dets)
                for (auto &jdet : idets)
                    mp.insert(
                        vector<uint8_t>(jdet.begin(), jdet.begin() + j + 1));
            this->left_dims[j + 1]->allocate((int)mp.size());
            auto it = mp.begin();
            for (int k = 0; k < this->left_dims[j + 1]->n; k++, it++) {
                this->left_dims[j + 1]->quanta[k] =
                    det->det_quantum(*it, 0, j + 1);
                this->left_dims[j + 1]->n_states[k] = 1;
            }
            this->left_dims[j + 1]->sort_states();
            this->left_dims[j + 1]->collect();
        }
        this->left_dims[i + 1]->allocate((int)dets.size());
        for (int k = 0; k < this->left_dims[i + 1]->n; k++) {
            this->left_dims[i + 1]->quanta[k] =
                det->det_quantum(dets[k][0], 0, i + 1);
            this->left_dims[i + 1]->n_states[k] = (ubond_t)dets[k].size();
        }
        this->left_dims[i + 1]->sort_states();
        for (int k = i + 1; k < this->n_sites; k++)
            this->left_dims[k + 1]->n = 0;
    }
    void set_right_bond_dimension(int i,
                                  const vector<vector<vector<uint8_t>>> &dets) {
        this->right_dims[this->n_sites] =
            make_shared<StateInfo<S>>(this->vacuum);
        for (int j = this->n_sites - 1; j > i; j--) {
            set<vector<uint8_t>, typename DeterminantQC<S>::det_less> mp;
            for (auto &idets : dets)
                for (auto &jdet : idets)
                    mp.insert(
                        vector<uint8_t>(jdet.begin() + (j - i), jdet.end()));
            this->right_dims[j]->allocate((int)mp.size());
            auto it = mp.begin();
            for (int k = 0; k < this->right_dims[j]->n; k++, it++) {
                this->right_dims[j]->quanta[k] =
                    det->det_quantum(*it, j, this->n_sites);
                this->right_dims[j]->n_states[k] = 1;
            }
            this->right_dims[j]->sort_states();
            this->right_dims[j]->collect();
        }
        this->right_dims[i]->allocate((int)dets.size());
        for (int k = 0; k < this->right_dims[i]->n; k++) {
            this->right_dims[i]->quanta[k] =
                det->det_quantum(dets[k][0], i, this->n_sites);
            this->right_dims[i]->n_states[k] = (ubond_t)dets[k].size();
        }
        this->right_dims[i]->sort_states();
        for (int k = i - 1; k >= 0; k--)
            this->right_dims[k]->n = 0;
    }
    vector<vector<vector<uint8_t>>> get_determinants(StateInfo<S> &st,
                                                     int i_begin, int i_end) {
        vector<vector<vector<uint8_t>>> dets;
        dets.reserve(st.n);
        for (int j = 0; j < st.n; j++) {
            vector<vector<uint8_t>> dd =
                det->distribute(st.quanta[j], i_begin, i_end);
            if (dd.size() == 0)
                continue;
            int n_states = min((int)dd.size(), (int)st.n_states[j]);
            vector<double> dd_energies(dd.size());
            vector<int> dd_idx(dd.size());
            for (size_t k = 0; k < dd.size(); k++)
                dd_energies[k] = fcidump->det_energy(dd[k], i_begin, i_end),
                dd_idx[k] = (int)k;
            sort(dd_idx.begin(), dd_idx.end(), [&dd_energies](int ii, int jj) {
                return dd_energies[ii] < dd_energies[jj];
            });
            dets.push_back(vector<vector<uint8_t>>());
            for (int k = 0; k < n_states; k++)
                dets.back().push_back(dd[dd_idx[k]]);
        }
        st.deallocate();
        return dets;
    }
    // generate quantum numbers based on determinant for left block [0, i]
    // right bond dimension at site i_right_ref is used as reference
    StateInfo<S> get_complementary_left_dims(int i, int i_right_ref,
                                             bool match_prev = false) {
        this->load_right_dims(i_right_ref);
        StateInfo<S> rref = *this->right_dims[i_right_ref];
        for (int k = i_right_ref - 1; k >= i + 1; k--) {
            StateInfo<S> rr = StateInfo<S>::tensor_product(
                *basis[k], rref, *this->right_dims_fci[k]);
            rref = rr;
        }
        // get complementary quantum numbers
        map<S, ubond_t> qs;
        for (int i = 0; i < rref.n; i++) {
            S qls = this->target - rref.quanta[i];
            for (int k = 0; k < qls.count(); k++)
                qs[qls[k]] += rref.n_states[i];
        }
        rref.deallocate();
        if (match_prev) {
            this->load_left_dims(i + 1);
            for (int l = 0; l < this->left_dims[i + 1]->n; l++) {
                S q = this->left_dims[i + 1]->quanta[l];
                if (qs.count(q) == 0)
                    qs[q] = this->left_dims[i + 1]->n_states[l];
                else
                    qs[q] = max(qs[q], this->left_dims[i + 1]->n_states[l]);
            }
            this->left_dims[i + 1]->deallocate();
        }
        StateInfo<S> lref;
        lref.allocate((int)qs.size());
        int k = 0;
        for (auto &q : qs) {
            lref.quanta[k] = q.first;
            lref.n_states[k] = min(q.second, n_det_states);
            k++;
        }
        lref.sort_states();
        return lref;
    }
    // generate quantum numbers based on determinant for right block [i,
    // n_sites) left bond dimension at site i_left_ref is used as reference
    StateInfo<S> get_complementary_right_dims(int i, int i_left_ref,
                                              bool match_prev = false) {
        this->load_left_dims(i_left_ref + 1);
        StateInfo<S> lref = *this->left_dims[i_left_ref + 1];
        for (int k = i_left_ref + 1; k < i; k++) {
            StateInfo<S> ll = StateInfo<S>::tensor_product(
                lref, *basis[k], *this->left_dims_fci[k + 1]);
            lref = ll;
        }
        // get complementary quantum numbers
        map<S, ubond_t> qs;
        for (int i = 0; i < lref.n; i++) {
            S qrs = this->target - lref.quanta[i];
            for (int k = 0; k < qrs.count(); k++)
                qs[qrs[k]] += lref.n_states[i];
        }
        lref.deallocate();
        if (match_prev) {
            this->load_right_dims(i);
            for (int l = 0; l < this->right_dims[i]->n; l++) {
                S q = this->right_dims[i]->quanta[l];
                if (qs.count(q) == 0)
                    qs[q] = this->right_dims[i]->n_states[l];
                else
                    qs[q] = max(qs[q], this->right_dims[i]->n_states[l]);
            }
            this->right_dims[i]->deallocate();
        }
        StateInfo<S> rref;
        rref.allocate((int)qs.size());
        int k = 0;
        for (auto &q : qs) {
            rref.quanta[k] = q.first;
            rref.n_states[k] = min(q.second, n_det_states);
            k++;
        }
        rref.sort_states();
        return rref;
    }
};

} // namespace block2

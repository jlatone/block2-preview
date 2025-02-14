
/*
 * block2: Efficient MPO implementation of quantum chemistry DMRG
 * Copyright (C) 2020 Henrik R. Larsson <larsson@caltech.edu>
 * Copyright (C) 2020-2021 Huanchen Zhai <hczhai@caltech.edu>
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

/** Symbolic algebra using Wick's theorem. */

#pragma once

#include "../core/threading.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

using namespace std;

namespace block2 {

enum struct WickIndexTypes : uint8_t {
    None = 0,
    Inactive = 1,
    Active = 2,
    External = 4,
    Alpha = 8,
    Beta = 16
};

inline string to_str(const WickIndexTypes c) {
    const static string repr[] = {"N", "I", "A", "IA", "E", "EI", "EA", "EIA",
                                  "A", "",  "",  "",   "",  "",   "",   "",
                                  "B", "",  "",  "",   "",  "",   "",   ""};
    return repr[(uint8_t)c];
}

inline WickIndexTypes operator|(WickIndexTypes a, WickIndexTypes b) {
    return WickIndexTypes((uint8_t)a | (uint8_t)b);
}

inline WickIndexTypes operator&(WickIndexTypes a, WickIndexTypes b) {
    return WickIndexTypes((uint8_t)a & (uint8_t)b);
}

inline WickIndexTypes operator~(WickIndexTypes a) {
    return WickIndexTypes(~(uint8_t)a);
}

enum struct WickTensorTypes : uint8_t {
    CreationOperator = 0,
    DestroyOperator = 1,
    SpinFreeOperator = 2,
    KroneckerDelta = 3,
    Tensor = 4
};

struct WickIndex {
    string name;
    WickIndexTypes types;
    WickIndex() : WickIndex("") {}
    WickIndex(const char name[]) : name(name), types(WickIndexTypes::None) {}
    WickIndex(const string &name) : name(name), types(WickIndexTypes::None) {}
    WickIndex(const string &name, WickIndexTypes types)
        : name(name), types(types) {}
    bool operator==(const WickIndex &other) const noexcept {
        return name == other.name && types == other.types;
    }
    bool operator!=(const WickIndex &other) const noexcept {
        return name != other.name || types != other.types;
    }
    bool operator<(const WickIndex &other) const noexcept {
        return types == other.types ? name < other.name : types < other.types;
    }
    size_t hash() const noexcept { return std::hash<string>{}(name); }
    friend ostream &operator<<(ostream &os, const WickIndex &wi) {
        os << wi.name;
        return os;
    }
    bool has_types() const { return types != WickIndexTypes::None; }
    bool is_short() const { return name.length() == 1; }
    WickIndex with_no_types() const { return WickIndex(name); }
    static vector<WickIndex> parse(const string &x) {
        size_t index = x.find_first_of(" ", 0);
        vector<WickIndex> r;
        if (index == string::npos) {
            r.resize(x.size());
            for (int i = 0; i < (int)x.length(); i++)
                r[i] = WickIndex(string(1, x[i]));
        } else {
            size_t last = 0;
            while (index != string::npos) {
                if (index > last)
                    r.push_back(WickIndex(x.substr(last, index - last)));
                last = index + 1;
                index = x.find_first_of(" ", last);
            }
            if (x.length() > last)
                r.push_back(WickIndex(x.substr(last, x.length() - last)));
        }
        return r;
    }
    static vector<WickIndex>
    add_types(vector<WickIndex> r,
              const map<WickIndexTypes, set<WickIndex>> &type_map) {
        for (auto &rr : r)
            for (auto &m : type_map)
                if (m.second.count(rr.with_no_types()))
                    rr.types = rr.types | m.first;
        return r;
    }
    static vector<WickIndex>
    parse_with_types(const string &x,
                     const map<WickIndexTypes, set<WickIndex>> &type_map) {
        return add_types(parse(x), type_map);
    }
    static set<WickIndex> parse_set(const string &x) {
        vector<WickIndex> r = parse(x);
        sort(r.begin(), r.end());
        return set<WickIndex>(r.begin(), r.end());
    }
    static set<WickIndex>
    parse_set_with_types(const string &x,
                         const map<WickIndexTypes, set<WickIndex>> &type_map) {
        vector<WickIndex> r = parse_with_types(x, type_map);
        sort(r.begin(), r.end());
        return set<WickIndex>(r.begin(), r.end());
    }
};

struct WickPermutation {
    vector<int16_t> data;
    bool negative;
    WickPermutation() : negative(false) {}
    WickPermutation(const vector<int16_t> &data, bool negative = false)
        : data(data), negative(negative) {}
    bool operator==(const WickPermutation &other) const noexcept {
        return negative == other.negative && data == other.data;
    }
    bool operator!=(const WickPermutation &other) const noexcept {
        return negative != other.negative || data != other.data;
    }
    bool operator<(const WickPermutation &other) const noexcept {
        return negative == other.negative ? data < other.data
                                          : negative < other.negative;
    }
    WickPermutation operator*(const WickPermutation &other) const noexcept {
        vector<int16_t> r(data.size());
        for (int i = 0; i < (int)data.size(); i++)
            r[i] = data[other.data[i]];
        return WickPermutation(r, negative ^ other.negative);
    }
    friend ostream &operator<<(ostream &os, const WickPermutation &wp) {
        os << "< " << (wp.negative ? "- " : "+ ");
        for (int i = 0; i < (int)wp.data.size(); i++)
            os << wp.data[i] << " ";
        os << ">";
        return os;
    }
    size_t hash() const noexcept {
        size_t h = std::hash<bool>{}(negative);
        h ^= data.size() + 0x9E3779B9 + (h << 6) + (h >> 2);
        for (int i = 0; i < data.size(); i++)
            h ^= (std::hash<int16_t>{}(data[i])) + 0x9E3779B9 + (h << 6) +
                 (h >> 2);
        return h;
    }
    static vector<WickPermutation>
    complete_set(int n, const vector<WickPermutation> &def) {
        vector<int16_t> ident(n);
        for (int i = 0; i < n; i++)
            ident[i] = i;
        auto hx = [](const WickPermutation &wp) { return wp.hash(); };
        unordered_set<WickPermutation, decltype(hx)> swp(def.size(), hx);
        vector<WickPermutation> vwp;
        vwp.push_back(WickPermutation(ident, false));
        swp.insert(vwp[0]);
        for (int k = 0; k < vwp.size(); k++) {
            WickPermutation g = vwp[k];
            for (auto &d : def) {
                WickPermutation h = g * d;
                if (!swp.count(h))
                    vwp.push_back(h), swp.insert(h);
            }
        }
        return vwp;
    }
    static vector<WickPermutation> non_symmetric() {
        return vector<WickPermutation>();
    }
    static vector<WickPermutation> two_symmetric() {
        return vector<WickPermutation>{WickPermutation({1, 0}, false)};
    }
    static vector<WickPermutation> qc_chem() {
        return vector<WickPermutation>{WickPermutation({2, 3, 0, 1}, false),
                                       WickPermutation({1, 0, 2, 3}, false),
                                       WickPermutation({0, 1, 3, 2}, false)};
    }
    static vector<WickPermutation> qc_phys() {
        return vector<WickPermutation>{WickPermutation({0, 3, 2, 1}, false),
                                       WickPermutation({2, 1, 0, 3}, false),
                                       WickPermutation({1, 0, 3, 2}, false)};
    }
    static vector<WickPermutation> four_anti() {
        return vector<WickPermutation>{WickPermutation({1, 0, 2, 3}, true),
                                       WickPermutation({0, 1, 3, 2}, true)};
    }
    static vector<WickPermutation> pair_symmetric(int n,
                                                  bool hermitian = false) {
        vector<WickPermutation> r(n - 1);
        vector<int16_t> x(n * 2);
        for (int i = 1; i < n; i++) {
            for (int j = 0; j < n; j++) {
                x[j] = j == 0 ? i : (j == i ? 0 : j);
                x[j + n] = j == 0 ? i + n : (j == i ? n : j + n);
            }
            r[i - 1] = WickPermutation(x, false);
        }
        if (hermitian) {
            for (int j = 0; j < n; j++)
                x[j] = j + n, x[j + n] = j;
            r.push_back(WickPermutation(x, false));
        }
        return r;
    }
};

struct WickTensor {
    string name;
    vector<WickIndex> indices;
    vector<WickPermutation> perms;
    WickTensorTypes type;
    WickTensor() : name(""), type(WickTensorTypes::Tensor) {}
    WickTensor(
        const string &name, const vector<WickIndex> &indices,
        const vector<WickPermutation> &perms = WickPermutation::non_symmetric(),
        WickTensorTypes type = WickTensorTypes::Tensor)
        : name(name), indices(indices),
          perms(reset_permutations(indices, WickPermutation::complete_set(
                                                (int)indices.size(), perms))),
          type(type) {}
    static vector<WickPermutation>
    reset_permutations(const vector<WickIndex> &indices,
                       const vector<WickPermutation> &perms) {
        vector<WickPermutation> rperms;
        for (auto &perm : perms) {
            bool valid = true;
            for (int i = 0; i < (int)indices.size() && valid; i++)
                if ((indices[perm.data[i]].types & indices[i].types) ==
                        WickIndexTypes::None &&
                    indices[perm.data[i]].types != WickIndexTypes::None &&
                    indices[i].types != WickIndexTypes::None)
                    valid = false;
            if (valid)
                rperms.push_back(perm);
        }
        return rperms;
    }
    static WickTensor
    parse(const string &tex_expr,
          const map<WickIndexTypes, set<WickIndex>> &idx_map,
          const map<pair<string, int>, vector<WickPermutation>> &perm_map) {
        string name, indices;
        bool is_name = true;
        for (char c : tex_expr)
            if (c == '_' || c == '[')
                is_name = false;
            else if (c == ',' || c == ' ')
                continue;
            else if (string("{}]").find(c) == string::npos && is_name)
                name.push_back(c);
            else if (string("{}]").find(c) == string::npos && !is_name)
                indices.push_back(c);
        vector<WickPermutation> perms;
        if (perm_map.count(make_pair(name, (int)indices.size())))
            perms = perm_map.at(make_pair(name, (int)indices.size()));
        WickTensorTypes tensor_type = WickTensorTypes::Tensor;
        if (name == "C" && indices.size() == 1)
            tensor_type = WickTensorTypes::CreationOperator;
        else if (name == "D" && indices.size() == 1)
            tensor_type = WickTensorTypes::DestroyOperator;
        else if (name[0] == 'E' && name.length() == 2 &&
                 indices.size() == (int)(name[1] - '0') * 2) {
            tensor_type = WickTensorTypes::SpinFreeOperator;
            perms = WickPermutation::pair_symmetric((int)(name[1] - '0'));
        } else if (name[0] == 'R' && name.length() == 2 &&
                   indices.size() == (int)(name[1] - '0') * 2) {
            tensor_type = WickTensorTypes::SpinFreeOperator;
            perms = WickPermutation::pair_symmetric((int)(name[1] - '0'), true);
        } else if (name == "delta" && indices.size() == 2) {
            tensor_type = WickTensorTypes::KroneckerDelta;
            perms = WickPermutation::two_symmetric();
        }
        return WickTensor(name, WickIndex::parse_with_types(indices, idx_map),
                          perms, tensor_type);
    }
    bool operator==(const WickTensor &other) const noexcept {
        return type == other.type && name == other.name &&
               indices == other.indices;
    }
    bool operator!=(const WickTensor &other) const noexcept {
        return type != other.type || name != other.name ||
               indices != other.indices;
    }
    bool operator<(const WickTensor &other) const noexcept {
        const WickIndexTypes mask = WickIndexTypes::Inactive |
                                    WickIndexTypes::Active |
                                    WickIndexTypes::External;
        WickIndexTypes x_type = indices.size() == 0 ? WickIndexTypes::None
                                                    : indices[0].types & mask;
        WickIndexTypes y_type = other.indices.size() == 0
                                    ? WickIndexTypes::None
                                    : other.indices[0].types & mask;
        WickIndexTypes occ_type =
            WickIndexTypes(min((uint8_t)x_type, (uint8_t)y_type));
        WickIndexTypes max_type =
            WickIndexTypes(max((uint8_t)x_type, (uint8_t)y_type));
        if (occ_type == WickIndexTypes::None ||
            occ_type == WickIndexTypes::External ||
            (occ_type == WickIndexTypes::Active &&
             max_type == WickIndexTypes::Active))
            occ_type = WickIndexTypes::Inactive;
        return fermi_type(occ_type) != other.fermi_type(occ_type)
                   ? fermi_type(occ_type) < other.fermi_type(occ_type)
                   : (name != other.name
                          ? name < other.name
                          : (type == other.type ? indices < other.indices
                                                : type < other.type));
    }
    WickTensor operator*(const WickPermutation &perm) const noexcept {
        vector<WickIndex> xindices(indices.size());
        for (int i = 0; i < (int)indices.size(); i++)
            xindices[i] = indices[perm.data[i]];
        return WickTensor(name, xindices, perms, type);
    }
    // Ca [00] < Di [01] < Ci [10] < Da [11]
    // Ca [00] < Du [01] < Cu [10] < Da [11]
    // Cu [00] < Di [01] < Ci [10] < Du [11]
    int fermi_type(WickIndexTypes occ_type) const noexcept {
        const int x = type == WickTensorTypes::DestroyOperator;
        const int y = indices.size() != 0 &&
                      (indices[0].types & occ_type) != WickIndexTypes::None;
        return x | ((x ^ y) << 1);
    }
    string to_str(const WickPermutation &perm) const {
        string d = " ";
        if (all_of(indices.begin(), indices.end(),
                   [](const WickIndex &idx) { return idx.is_short(); }))
            d = "";
        stringstream ss;
        ss << (perm.negative ? "-" : "") << name << "[" << d;
        for (int i = 0; i < (int)indices.size(); i++) {
            if (type == WickTensorTypes::SpinFreeOperator &&
                i * 2 == (int)indices.size())
                ss << "," << d;
            ss << indices[perm.data[i]] << d;
        }
        ss << "]";
        return ss.str();
    }
    friend ostream &operator<<(ostream &os, const WickTensor &wt) {
        os << wt.to_str(wt.perms[0]);
        return os;
    }
    static WickTensor kronecker_delta(const vector<WickIndex> &indices) {
        assert(indices.size() == 2);
        return WickTensor("delta", indices, WickPermutation::two_symmetric(),
                          WickTensorTypes::KroneckerDelta);
    }
    // GUGA book P66 EQ21 E[ij] = x_{i\sigma}^\dagger x_{j\sigma}
    // e[ik,jl] = E[ij]E[kl] - delta[kj]E[il] = e[ki,lj] ==> e[ij,kl] in P66
    // e[ijk...abc...] = SUM <stu...> C[is] C[jt] C[ku] ... D[cu] D[bt] D[as]
    // ...
    static WickTensor spin_free(const vector<WickIndex> &indices) {
        assert(indices.size() % 2 == 0);
        const int k = (int)(indices.size() / 2);
        stringstream name;
        name << "E" << k;
        return WickTensor(name.str(), indices,
                          WickPermutation::pair_symmetric(k),
                          WickTensorTypes::SpinFreeOperator);
    }
    // with additional pq,rs -> rs,pq symmetry
    static WickTensor
    spin_free_density_matrix(const vector<WickIndex> &indices) {
        assert(indices.size() % 2 == 0);
        const int k = (int)(indices.size() / 2);
        stringstream name;
        name << "R" << k;
        return WickTensor(name.str(), indices,
                          WickPermutation::pair_symmetric(k, true),
                          WickTensorTypes::SpinFreeOperator);
    }
    static WickTensor cre(const WickIndex &index, const string &name = "C") {
        return WickTensor(name, vector<WickIndex>{index},
                          WickPermutation::non_symmetric(),
                          WickTensorTypes::CreationOperator);
    }
    static WickTensor cre(const WickIndex &index,
                          const map<WickIndexTypes, set<WickIndex>> &idx_map,
                          const string &name = "C") {
        return WickTensor(
            name, WickIndex::add_types(vector<WickIndex>{index}, idx_map),
            WickPermutation::non_symmetric(),
            WickTensorTypes::CreationOperator);
    }
    static WickTensor des(const WickIndex &index, const string &name = "D") {
        return WickTensor(name, vector<WickIndex>{index},
                          WickPermutation::non_symmetric(),
                          WickTensorTypes::DestroyOperator);
    }
    static WickTensor des(const WickIndex &index,
                          const map<WickIndexTypes, set<WickIndex>> &idx_map,
                          const string &name = "D") {
        return WickTensor(
            name, WickIndex::add_types(vector<WickIndex>{index}, idx_map),
            WickPermutation::non_symmetric(), WickTensorTypes::DestroyOperator);
    }
    WickTensor sort(double &factor) const {
        WickTensor x = *this;
        bool neg = false;
        for (auto &perm : perms) {
            WickTensor z = *this * perm;
            if (z.indices < x.indices)
                x = z, neg = perm.negative;
        }
        if (neg)
            factor = -factor;
        return x;
    }
    vector<pair<map<WickIndex, int>, int>>
    sort_gen_maps(const WickTensor &ref, const set<WickIndex> &ctr_idxs,
                  const vector<pair<map<WickIndex, int>, int>> &ctr_maps,
                  int new_idx) {
        set<pair<map<WickIndex, int>, int>> new_maps;
        assert(perms.size() != 0);
        for (auto &perm : perms) {
            WickTensor zz = *this * perm;
            for (auto &ctr_map : ctr_maps) {
                WickTensor z = zz;
                map<WickIndex, int> new_map;
                int kidx = new_idx;
                for (auto &wi : z.indices)
                    if (ctr_idxs.count(wi)) {
                        if (!ctr_map.first.count(wi) && !new_map.count(wi))
                            new_map[wi] = kidx++;
                        wi.name = string(1, (ctr_map.first.count(wi)
                                                 ? ctr_map.first.at(wi)
                                                 : new_map.at(wi)) +
                                                '0');
                    }
                if (z.indices == ref.indices) {
                    new_map.insert(ctr_map.first.begin(), ctr_map.first.end());
                    new_maps.insert(make_pair(new_map, perm.negative
                                                           ? -ctr_map.second
                                                           : ctr_map.second));
                }
            }
        }
        return vector<pair<map<WickIndex, int>, int>>(new_maps.begin(),
                                                      new_maps.end());
    }
    WickTensor sort(const set<WickIndex> &ctr_idxs,
                    const vector<pair<map<WickIndex, int>, int>> &ctr_maps,
                    int &new_idx) const {
        int kidx = new_idx;
        WickTensor x = *this;
        map<WickIndex, int> new_map;
        assert(ctr_maps.size() != 0);
        for (auto &wi : x.indices)
            if (ctr_idxs.count(wi)) {
                if (!ctr_maps[0].first.count(wi) && !new_map.count(wi))
                    new_map[wi] = kidx++;
                wi.name = string(1, (ctr_maps[0].first.count(wi)
                                         ? ctr_maps[0].first.at(wi)
                                         : new_map.at(wi)) +
                                        '0');
            }
        for (auto &perm : perms) {
            WickTensor zz = *this * perm;
            for (auto &ctr_map : ctr_maps) {
                WickTensor z = zz;
                new_map.clear();
                kidx = new_idx;
                for (auto &wi : z.indices)
                    if (ctr_idxs.count(wi)) {
                        if (!ctr_map.first.count(wi) && !new_map.count(wi))
                            new_map[wi] = kidx++;
                        wi.name = string(1, (ctr_map.first.count(wi)
                                                 ? ctr_map.first.at(wi)
                                                 : new_map.at(wi)) +
                                                '0');
                    }
                if (z.indices < x.indices)
                    x = z;
            }
        }
        new_idx = kidx;
        return x;
    }
    string get_permutation_rules() const {
        stringstream ss;
        for (int i = 0; i < (int)perms.size(); i++)
            ss << to_str(perms[i])
               << (i == (int)perms.size() - 1 ? "" : " == ");
        return ss.str();
    }
};

struct WickString {
    vector<WickTensor> tensors;
    set<WickIndex> ctr_indices;
    double factor;
    WickString() : factor(0.0) {}
    WickString(const WickTensor &tensor, double factor = 1.0)
        : factor(factor), tensors({tensor}), ctr_indices() {}
    WickString(const vector<WickTensor> &tensors)
        : factor(1.0), tensors(tensors), ctr_indices() {}
    WickString(const vector<WickTensor> &tensors,
               const set<WickIndex> &ctr_indices, double factor = 1.0)
        : factor(factor), tensors(tensors), ctr_indices(ctr_indices) {}
    bool abs_equal_to(const WickString &other) const noexcept {
        return tensors.size() == other.tensors.size() &&
               ctr_indices.size() == other.ctr_indices.size() &&
               tensors == other.tensors && ctr_indices == other.ctr_indices;
    }
    bool operator==(const WickString &other) const noexcept {
        return factor == other.factor && tensors == other.tensors &&
               ctr_indices == other.ctr_indices;
    }
    bool operator!=(const WickString &other) const noexcept {
        return factor != other.factor || tensors != other.tensors ||
               ctr_indices != other.ctr_indices;
    }
    bool operator<(const WickString &other) const noexcept {
        if (tensors.size() != other.tensors.size())
            return tensors.size() < other.tensors.size();
        else if (ctr_indices.size() != other.ctr_indices.size())
            return ctr_indices.size() < other.ctr_indices.size();
        else if (tensors != other.tensors)
            return tensors < other.tensors;
        else if (ctr_indices != other.ctr_indices)
            return ctr_indices < other.ctr_indices;
        else
            return factor < other.factor;
    }
    static WickString
    parse(const string &tex_expr,
          const map<WickIndexTypes, set<WickIndex>> &idx_map,
          const map<pair<string, int>, vector<WickPermutation>> &perm_map) {
        vector<WickTensor> tensors;
        set<WickIndex> ctr_idxs;
        string sum_expr, fac_expr, tensor_expr;
        int idx = 0;
        for (; idx < tex_expr.length(); idx++) {
            char c = tex_expr[idx];
            if (c == ' ' || c == '(')
                continue;
            else if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+')
                fac_expr.push_back(c);
            else
                break;
        }
        for (; idx < tex_expr.length() &&
               (tex_expr[idx] == ')' || tex_expr[idx] == ' ');
             idx++)
            ;
        bool has_sum = false;
        if (tex_expr.substr(idx, 6) == "\\sum_{")
            idx += 6, has_sum = true;
        else if (tex_expr.substr(idx, 5) == "SUM <")
            idx += 5, has_sum = true;
        for (; idx < tex_expr.length() && has_sum; idx++) {
            char c = tex_expr[idx];
            if (c == '}' || c == '|' || c == '>')
                break;
            else if (c == ' ')
                continue;
            else
                sum_expr.push_back(c);
        }
        if (idx < tex_expr.length() && tex_expr[idx] == '|') {
            for (; idx < tex_expr.length(); idx++)
                if (tex_expr[idx] == '>')
                    break;
        }
        if (idx < tex_expr.length() &&
            (tex_expr[idx] == '}' || tex_expr[idx] == '>'))
            idx++;
        for (; idx < tex_expr.length(); idx++) {
            char c = tex_expr[idx];
            if (c == ' ')
                continue;
            else if (c == '}' || c == ']') {
                tensor_expr.push_back(c);
                tensors.push_back(
                    WickTensor::parse(tensor_expr, idx_map, perm_map));
                tensor_expr = "";
            } else
                tensor_expr.push_back(c);
        }
        if (sum_expr != "")
            ctr_idxs = WickIndex::parse_set_with_types(sum_expr, idx_map);
        if (tensor_expr != "")
            tensors.push_back(
                WickTensor::parse(tensor_expr, idx_map, perm_map));
        double xfac = 1.0;
        if (fac_expr == "-")
            xfac = -1;
        else if (fac_expr != "" && fac_expr != "+")
            xfac = atof(fac_expr.c_str());
        return WickString(tensors, ctr_idxs, xfac);
    }
    vector<WickString> substitute(
        const map<string, pair<WickTensor, vector<WickString>>> &defs) const {
        vector<WickString> r = {*this};
        r[0].tensors.clear();
        for (auto &wt : tensors) {
            if (!defs.count(wt.name)) {
                for (auto &rr : r)
                    rr.tensors.push_back(wt);
            } else {
                auto &p = defs.at(wt.name);
                vector<WickString> rx;
                for (auto &rr : r) {
                    for (auto &dx : p.second) {
                        WickString rg = rr;
                        set<WickIndex> used_idxs = rr.used_indices();
                        used_idxs.insert(wt.indices.begin(), wt.indices.end());
                        map<WickIndex, WickIndex> idx_map;
                        assert(p.first.indices.size() == wt.indices.size());
                        for (int i = 0; i < (int)wt.indices.size(); i++)
                            idx_map[p.first.indices[i]] = wt.indices[i];
                        for (auto &wi : dx.ctr_indices) {
                            WickIndex g = wi;
                            for (int i = 0; i < 100; i++) {
                                g.name[0] = wi.name[0] + i;
                                if (!used_idxs.count(g))
                                    break;
                            }
                            rg.ctr_indices.insert(g);
                            used_idxs.insert(g);
                            idx_map[wi] = g;
                        }
                        for (auto wx : dx.tensors) {
                            for (auto &wi : wx.indices)
                                wi = idx_map.at(wi);
                            rg.tensors.push_back(wx);
                        }
                        rg.factor *= dx.factor;
                        rx.push_back(rg);
                    }
                }
                r = rx;
            }
        }
        return r;
    }
    set<WickIndex> used_indices() const {
        set<WickIndex> r;
        for (auto &ts : tensors)
            r.insert(ts.indices.begin(), ts.indices.end());
        return r;
    }
    WickString operator*(const WickString &other) const noexcept {
        vector<WickTensor> xtensors = tensors;
        xtensors.insert(xtensors.end(), other.tensors.begin(),
                        other.tensors.end());
        set<WickIndex> xctr_indices = ctr_indices;
        xctr_indices.insert(other.ctr_indices.begin(), other.ctr_indices.end());
        // resolve conflicts in summation indices
        set<WickIndex> a_idxs = used_indices(), b_idxs = other.used_indices();
        vector<WickIndex> used_idxs_v(a_idxs.size() + b_idxs.size());
        auto it = set_union(a_idxs.begin(), a_idxs.end(), b_idxs.begin(),
                            b_idxs.end(), used_idxs_v.begin());
        set<WickIndex> used_idxs(used_idxs_v.begin(), it);
        vector<WickIndex> a_rep(ctr_indices.size()),
            b_rep(other.ctr_indices.size()), c_rep(ctr_indices.size());
        it = set_intersection(ctr_indices.begin(), ctr_indices.end(),
                              b_idxs.begin(), b_idxs.end(), a_rep.begin());
        a_rep.resize(it - a_rep.begin());
        it =
            set_intersection(other.ctr_indices.begin(), other.ctr_indices.end(),
                             a_idxs.begin(), a_idxs.end(), b_rep.begin());
        b_rep.resize(it - b_rep.begin());
        it = set_intersection(ctr_indices.begin(), ctr_indices.end(),
                              other.ctr_indices.begin(),
                              other.ctr_indices.end(), c_rep.begin());
        c_rep.resize(it - c_rep.begin());
        set<WickIndex> xa_rep(a_rep.begin(), a_rep.end()),
            xb_rep(b_rep.begin(), b_rep.end()),
            xc_rep(c_rep.begin(), c_rep.end());
        map<WickIndex, WickIndex> mp_idxs;
        for (auto &idx : used_idxs)
            if (xa_rep.count(idx) || xb_rep.count(idx))
                for (int i = 1; i < 100; i++) {
                    WickIndex g = idx;
                    g.name[0] += i;
                    if (!used_idxs.count(g)) {
                        used_idxs.insert(g);
                        mp_idxs[idx] = g;
                        break;
                    }
                }
        // change contraction index in a, if it is also free index in b
        for (int i = 0; i < tensors.size(); i++)
            for (auto &wi : xtensors[i].indices)
                if (mp_idxs.count(wi) && xa_rep.count(wi) && !xc_rep.count(wi))
                    wi = mp_idxs[wi];
        // change contraction index in b,
        // if it is also free index or contraction index in a
        for (int i = tensors.size(); i < (int)xtensors.size(); i++)
            for (auto &wi : xtensors[i].indices)
                if (mp_idxs.count(wi) && xb_rep.count(wi))
                    wi = mp_idxs[wi];
        xctr_indices.clear();
        for (auto &wi : ctr_indices)
            if (mp_idxs.count(wi) && xa_rep.count(wi) && !xc_rep.count(wi))
                xctr_indices.insert(mp_idxs[wi]);
            else
                xctr_indices.insert(wi);
        for (auto &wi : other.ctr_indices)
            if (mp_idxs.count(wi) && xb_rep.count(wi))
                xctr_indices.insert(mp_idxs[wi]);
            else
                xctr_indices.insert(wi);
        return WickString(xtensors, xctr_indices, factor * other.factor);
    }
    WickString operator*(double d) const noexcept {
        return WickString(tensors, ctr_indices, factor * d);
    }
    WickString abs() const { return WickString(tensors, ctr_indices); }
    bool group_less(const WickString &other) const noexcept {
        const static vector<WickTensorTypes> wtts = {
            WickTensorTypes::KroneckerDelta, WickTensorTypes::Tensor,
            WickTensorTypes::CreationOperator, WickTensorTypes::DestroyOperator,
            WickTensorTypes::SpinFreeOperator};
        if (tensors.size() != other.tensors.size())
            return tensors.size() < other.tensors.size();
        if (ctr_indices.size() != other.ctr_indices.size())
            return ctr_indices.size() < other.ctr_indices.size();
        for (auto &wtt : wtts) {
            int xi = 0, xii = 0, xj = 0, xjj = 0;
            for (auto &wt : tensors)
                if (wt.type == wtt)
                    xi += (int)wt.indices.size(), xii++;
            for (auto &wt : other.tensors)
                if (wt.type == wtt)
                    xj += (int)wt.indices.size(), xjj++;
            if (xi != xj)
                return xi < xj;
            if (xii != xjj)
                return xii < xjj;
        }
        return false;
    }
    bool has_external_ops() const {
        for (auto &wt : tensors)
            if (wt.type == WickTensorTypes::SpinFreeOperator ||
                wt.type == WickTensorTypes::CreationOperator ||
                wt.type == WickTensorTypes::DestroyOperator)
                for (auto &wi : wt.indices)
                    if ((uint8_t)(wi.types & WickIndexTypes::External))
                        return true;
        return false;
    }
    WickString simple_sort() const {
        vector<WickTensor> cd_tensors, ot_tensors;
        double xfactor = factor;
        map<WickIndex, int> ctr_map;
        int ip = 0;
        for (auto &wt : tensors)
            if (wt.type == WickTensorTypes::KroneckerDelta ||
                wt.type == WickTensorTypes::Tensor)
                ot_tensors.push_back(wt.sort(xfactor));
            else
                cd_tensors.push_back(wt.sort(xfactor));
        vector<WickTensor> f_tensors = ot_tensors;
        f_tensors.insert(f_tensors.end(), cd_tensors.begin(), cd_tensors.end());
        std::sort(f_tensors.begin(), f_tensors.begin() + ot_tensors.size());
        return WickString(f_tensors, ctr_indices, xfactor);
    }
    WickString quick_sort() const {
        vector<WickTensor> cd_tensors, ot_tensors;
        double xfactor = factor;
        for (auto &wt : tensors)
            if (wt.type == WickTensorTypes::KroneckerDelta ||
                wt.type == WickTensorTypes::Tensor)
                ot_tensors.push_back(wt.sort(xfactor));
            else
                cd_tensors.push_back(wt.sort(xfactor));
        std::sort(ot_tensors.begin(), ot_tensors.end(),
                  [](const WickTensor &a, const WickTensor &b) {
                      return a.name != b.name
                                 ? a.name < b.name
                                 : a.indices.size() < b.indices.size();
                  });
        vector<int> ot_tensor_groups;
        for (int i = 0; i < (int)ot_tensors.size(); i++)
            if (i == 0 || (ot_tensors[i].name != ot_tensors[i - 1].name ||
                           ot_tensors[i].indices.size() !=
                               ot_tensors[i - 1].indices.size()))
                ot_tensor_groups.push_back(i);
        ot_tensor_groups.push_back(ot_tensors.size());
        int kidx = 0;
        vector<WickTensor> ot_sorted(ot_tensors.size());
        vector<pair<map<WickIndex, int>, int>> ctr_maps = {
            make_pair(map<WickIndex, int>(), 1)};
        for (int ig = 0; ig < (int)ot_tensor_groups.size() - 1; ig++) {
            vector<int> wta(ot_tensor_groups[ig + 1] - ot_tensor_groups[ig]);
            for (int j = 0; j < (int)wta.size(); j++)
                wta[j] = ot_tensor_groups[ig] + j;
            WickTensor *wtb = ot_sorted.data() + ot_tensor_groups[ig];
            for (int j = 0; j < (int)wta.size(); j++) {
                int jxx = -1, jixx = -1;
                for (int k = j; k < (int)wta.size(); k++) {
                    int jidx = kidx;
                    wtb[k] =
                        ot_tensors[wta[k]].sort(ctr_indices, ctr_maps, jidx);
                    if (k == j || wtb[k].indices < wtb[j].indices)
                        wtb[j] = wtb[k], jxx = k, jixx = jidx;
                }
                ctr_maps = ot_tensors[wta[jxx]].sort_gen_maps(
                    wtb[j], ctr_indices, ctr_maps, kidx);
                kidx = jixx;
                if (jxx != j)
                    swap(wta[jxx], wta[j]);
            }
        }
        for (auto &wt : cd_tensors) {
            int jidx = kidx;
            ot_sorted.push_back(wt.sort(ctr_indices, ctr_maps, kidx));
            ctr_maps =
                wt.sort_gen_maps(ot_sorted.back(), ctr_indices, ctr_maps, jidx);
        }
        assert(kidx == (int)ctr_maps[0].first.size() &&
               kidx == (int)ctr_indices.size());
        set<WickIndex> xctr_idxs;
        for (auto wi : ctr_indices) {
            wi.name = string(1, ctr_maps[0].first.at(wi) + '0');
            xctr_idxs.insert(wi);
        }
        return WickString(ot_sorted, xctr_idxs, xfactor * ctr_maps[0].second);
    }
    WickString old_sort() const {
        vector<WickTensor> cd_tensors, ot_tensors;
        map<WickIndex, int> ctr_map;
        double xfactor = factor;
        int ip = 0;
        for (auto &wt : tensors)
            if (wt.type == WickTensorTypes::KroneckerDelta ||
                wt.type == WickTensorTypes::Tensor)
                ot_tensors.push_back(wt.sort(xfactor));
            else
                cd_tensors.push_back(wt.sort(xfactor));
        for (auto &wt : ot_tensors)
            for (auto &wi : wt.indices)
                if (ctr_indices.count(wi) && !ctr_map.count(wi))
                    ctr_map[wi] = ip++;
        for (auto &wt : cd_tensors)
            for (auto &wi : wt.indices)
                if (ctr_indices.count(wi) && !ctr_map.count(wi))
                    ctr_map[wi] = ip++;
        vector<WickTensor> f_tensors = ot_tensors;
        f_tensors.insert(f_tensors.end(), cd_tensors.begin(), cd_tensors.end());
        WickString ex(f_tensors, set<WickIndex>(), xfactor);
        for (auto &wt : ex.tensors) {
            for (auto &wi : wt.indices)
                if (ctr_indices.count(wi))
                    wi.name = string(1, ctr_map[wi] + '0');
            wt = wt.sort(ex.factor);
        }
        vector<WickIndex> ex_ctr(ctr_indices.begin(), ctr_indices.end());
        for (auto &wi : ex_ctr)
            wi.name = string(1, ctr_map[wi] + '0');
        std::sort(ex.tensors.begin(), ex.tensors.begin() + ot_tensors.size());
        vector<int> ip_map(ip);
        for (int i = 0; i < ip; i++)
            ip_map[i] = i;
        while (next_permutation(ip_map.begin(), ip_map.end())) {
            WickString ez(f_tensors, set<WickIndex>(), xfactor);
            for (auto &wt : ez.tensors) {
                for (auto &wi : wt.indices)
                    if (ctr_indices.count(wi))
                        wi.name = string(1, ip_map[ctr_map[wi]] + '0');
                wt = wt.sort(ez.factor);
            }
            std::sort(ez.tensors.begin(),
                      ez.tensors.begin() + ot_tensors.size());
            if (ez < ex) {
                ex = ez;
                ex_ctr =
                    vector<WickIndex>(ctr_indices.begin(), ctr_indices.end());
                for (auto &wi : ex_ctr)
                    wi.name = string(1, ip_map[ctr_map[wi]] + '0');
            }
        }
        return WickString(ex.tensors,
                          set<WickIndex>(ex_ctr.begin(), ex_ctr.end()),
                          ex.factor);
    }
    friend ostream &operator<<(ostream &os, const WickString &ws) {
        os << "(" << fixed << setprecision(10) << setw(16) << ws.factor << ") ";
        if (ws.ctr_indices.size() != 0) {
            string d = " ";
            if (all_of(ws.ctr_indices.begin(), ws.ctr_indices.end(),
                       [](const WickIndex &idx) { return idx.is_short(); }))
                d = "";
            os << "SUM <" << d;
            for (auto &ci : ws.ctr_indices)
                os << ci << d;
            if (any_of(ws.ctr_indices.begin(), ws.ctr_indices.end(),
                       [](const WickIndex &wi) { return wi.has_types(); })) {
                os << "|";
                for (auto &ci : ws.ctr_indices)
                    os << to_str(ci.types)
                       << (to_str(ci.types).length() > 1 ? " " : "");
            }
            os << "> ";
        }
        for (int i = 0; i < (int)ws.tensors.size(); i++)
            os << ws.tensors[i] << (i == (int)ws.tensors.size() - 1 ? "" : " ");
        return os;
    }
    WickString simplify_delta() const {
        vector<WickTensor> xtensors = tensors;
        set<WickIndex> xctr_indices = ctr_indices;
        double xfactor = factor;
        vector<int> xidxs;
        for (int i = 0; i < (int)xtensors.size(); i++)
            if (xtensors[i].type == WickTensorTypes::KroneckerDelta) {
                const WickIndex &ia = xtensors[i].indices[0],
                                &ib = xtensors[i].indices[1];
                if (ia != ib) {
                    if ((ia.types != WickIndexTypes::None ||
                         ib.types != WickIndexTypes::None) &&
                        (ia.types & ib.types) == WickIndexTypes::None)
                        xfactor = 0;
                    else if (!xctr_indices.count(ia) &&
                             !xctr_indices.count(ib)) {
                        bool found = false;
                        for (int j = 0; j < (int)xidxs.size() && !found; j++)
                            if (xtensors[xidxs[j]].type ==
                                    WickTensorTypes::KroneckerDelta &&
                                ((xtensors[xidxs[j]].indices[0] == ia &&
                                  xtensors[xidxs[j]].indices[1] == ib) ||
                                 (xtensors[xidxs[j]].indices[0] == ib &&
                                  xtensors[xidxs[j]].indices[1] == ia)))
                                found = true;
                        if (!found)
                            xidxs.push_back(i);
                    } else {
                        WickIndex ic;
                        if (xctr_indices.count(ia)) {
                            ic = WickIndex(ib.name, ia.types & ib.types);
                            xctr_indices.erase(ia);
                        } else {
                            ic = WickIndex(ia.name, ia.types & ib.types);
                            xctr_indices.erase(ib);
                        }
                        for (int j = 0; j < (int)xtensors.size(); j++)
                            if (j != i)
                                for (int k = 0;
                                     k < (int)xtensors[j].indices.size(); k++)
                                    if (xtensors[j].indices[k] == ia ||
                                        xtensors[j].indices[k] == ib)
                                        xtensors[j].indices[k] = ic;
                    }
                }
            } else
                xidxs.push_back(i);
        for (int i = 0; i < (int)xidxs.size(); i++)
            xtensors[i] = xtensors[xidxs[i]];
        xtensors.resize(xidxs.size());
        return WickString(xtensors, xctr_indices, xfactor);
    }
};

struct WickExpr {
    vector<WickString> terms;
    WickExpr() {}
    WickExpr(const WickString &term) : terms(vector<WickString>{term}) {}
    WickExpr(const vector<WickString> &terms) : terms(terms) {}
    bool operator==(const WickExpr &other) const noexcept {
        return terms == other.terms;
    }
    bool operator!=(const WickExpr &other) const noexcept {
        return terms != other.terms;
    }
    bool operator<(const WickExpr &other) const noexcept {
        return terms < other.terms;
    }
    WickExpr operator*(const WickExpr &other) const noexcept {
        vector<WickString> xterms;
        xterms.reserve(terms.size() * other.terms.size());
        for (auto &ta : terms)
            for (auto &tb : other.terms)
                xterms.push_back(ta * tb);
        return WickExpr(xterms);
    }
    WickExpr operator+(const WickExpr &other) const noexcept {
        vector<WickString> xterms = terms;
        xterms.insert(xterms.end(), other.terms.begin(), other.terms.end());
        return WickExpr(xterms);
    }
    WickExpr operator-(const WickExpr &other) const noexcept {
        vector<WickString> xterms = terms;
        WickExpr mx = other * (-1.0);
        xterms.insert(xterms.end(), mx.terms.begin(), mx.terms.end());
        return WickExpr(xterms);
    }
    WickExpr operator*(double d) const noexcept {
        vector<WickString> xterms = terms;
        for (auto &term : xterms)
            term = term * d;
        return WickExpr(xterms);
    }
    friend ostream &operator<<(ostream &os, const WickExpr &we) {
        os << "EXPR /" << we.terms.size() << "/";
        if (we.terms.size() != 0)
            os << endl;
        for (int i = 0; i < (int)we.terms.size(); i++)
            os << we.terms[i] << endl;
        return os;
    }
    string to_einsum(const WickTensor &x) const {
        stringstream ss;
        bool first = true;
        for (auto &term : terms) {
            map<WickIndex, string> mp;
            set<string> pstr;
            for (int i = 0; i < (int)term.tensors.size(); i++)
                for (auto &wi : term.tensors[i].indices)
                    if (!term.ctr_indices.count(wi) && !mp.count(wi)) {
                        string x = wi.name;
                        while (pstr.count(x))
                            x[0]++;
                        mp[wi] = x, pstr.insert(x);
                    }
            for (int i = 0; i < (int)term.tensors.size(); i++)
                for (auto &wi : term.tensors[i].indices)
                    if (!mp.count(wi)) {
                        string x = wi.name;
                        while (pstr.count(x))
                            x[0]++;
                        mp[wi] = x, pstr.insert(x);
                    }
            for (auto &wi : x.indices)
                if (!mp.count(wi)) {
                    string x = wi.name;
                    while (pstr.count(x))
                        x[0]++;
                    mp[wi] = x, pstr.insert(x);
                }
            ss << x.name << (first ? " += " : " += ");
            first = false;
            if (term.factor != 1.0)
                ss << term.factor << " * ";
            ss << "np.einsum('";
            for (int i = 0; i < (int)term.tensors.size(); i++) {
                for (auto &wi : term.tensors[i].indices)
                    ss << mp[wi];
                ss << (i == (int)term.tensors.size() - 1 ? "->" : ",");
            }
            for (auto &wi : x.indices)
                ss << mp[wi];
            ss << "'";
            for (auto &wt : term.tensors) {
                ss << ", " << wt.name;
                if (wt.type == WickTensorTypes::KroneckerDelta ||
                    wt.type == WickTensorTypes::Tensor)
                    for (auto &wi : wt.indices)
                        ss << to_str(wi.types);
            }
            ss << ")\n";
        }
        return ss.str();
    }
    static WickExpr
    parse(const string &tex_expr,
          const map<WickIndexTypes, set<WickIndex>> &idx_map,
          const map<pair<string, int>, vector<WickPermutation>> &perm_map =
              map<pair<string, int>, vector<WickPermutation>>()) {
        vector<WickString> terms;
        size_t index = tex_expr.find_first_of("\n\r", 0);
        size_t last = 0;
        while (index != string::npos) {
            if (index > last)
                terms.push_back(WickString::parse(
                    tex_expr.substr(last, index - last), idx_map, perm_map));
            last = index + 1;
            index = tex_expr.find_first_of("\n\r", last);
        }
        if (tex_expr.length() > last)
            terms.push_back(WickString::parse(
                tex_expr.substr(last, tex_expr.length() - last), idx_map,
                perm_map));
        return terms;
    }
    static pair<WickTensor, WickExpr>
    parse_def(const string &tex_expr,
              const map<WickIndexTypes, set<WickIndex>> &idx_map,
              const map<pair<string, int>, vector<WickPermutation>> &perm_map =
                  map<pair<string, int>, vector<WickPermutation>>()) {
        size_t index = tex_expr.find_first_of("=", 0);
        assert(index != string::npos);
        WickTensor name =
            WickTensor::parse(tex_expr.substr(0, index), idx_map, perm_map);
        WickExpr expr =
            WickExpr::parse(tex_expr.substr(index + 1), idx_map, perm_map);
        return make_pair(name, expr);
    }
    WickExpr
    substitute(const map<string, pair<WickTensor, WickExpr>> &defs) const {
        WickExpr r;
        map<string, pair<WickTensor, vector<WickString>>> xdefs;
        for (auto &dd : defs)
            xdefs[dd.first] =
                make_pair(dd.second.first, dd.second.second.terms);
        for (auto &ws : terms) {
            vector<WickString> rws = ws.substitute(xdefs);
            r.terms.insert(r.terms.end(), rws.begin(), rws.end());
        }
        return r;
    }
    static WickExpr split_index_types(const WickString &x) {
        vector<WickIndex> vidxs(x.ctr_indices.begin(), x.ctr_indices.end());
        vector<vector<WickIndex>> xctr_idxs = {vidxs};
        WickIndexTypes check_mask = WickIndexTypes::Inactive |
                                    WickIndexTypes::Active |
                                    WickIndexTypes::External;
        vector<WickIndexTypes> check_types = {WickIndexTypes::Inactive,
                                              WickIndexTypes::Active,
                                              WickIndexTypes::External};
        for (int i = 0; i < (int)vidxs.size(); i++) {
            int k = 0, nk = xctr_idxs.size();
            for (int j = 0; j < (int)check_types.size(); j++)
                if ((vidxs[i].types & check_types[j]) != WickIndexTypes::None &&
                    (vidxs[i].types & check_mask) != check_types[j]) {
                    if (k != 0) {
                        xctr_idxs.reserve(xctr_idxs.size() + nk);
                        for (int l = 0; l < nk; l++)
                            xctr_idxs.push_back(xctr_idxs[l]);
                    }
                    for (int l = 0; l < nk; l++) {
                        xctr_idxs[k * nk + l][i].types =
                            xctr_idxs[k * nk + l][i].types & (~check_mask);
                        xctr_idxs[k * nk + l][i].types =
                            xctr_idxs[k * nk + l][i].types | check_types[j];
                    }
                    k++;
                }
        }
        WickExpr r;
        for (int i = 0; i < (int)xctr_idxs.size(); i++) {
            r.terms.push_back(WickString(
                x.tensors,
                set<WickIndex>(xctr_idxs[i].begin(), xctr_idxs[i].end()),
                x.factor));
            for (auto &wt : r.terms.back().tensors) {
                for (auto &wi : wt.indices)
                    for (auto &wii : xctr_idxs[i])
                        if (wi.with_no_types() == wii.with_no_types() &&
                            (wi.types & wii.types) != WickIndexTypes::None)
                            wi = wii;
                if (wt.perms.size() == 0)
                    r.terms.back().factor = 0;
            }
            if (r.terms.back().factor == 0)
                r.terms.pop_back();
        }
        return r;
    }
    WickExpr split_index_types() const {
        WickExpr r;
        for (auto &term : terms) {
            WickExpr rr = split_index_types(term);
            r.terms.insert(r.terms.end(), rr.terms.begin(), rr.terms.end());
        }
        return r;
    }
    WickExpr expand(int max_unctr = -1, bool no_ctr = false) const {
        return split_index_types().normal_order_impl(max_unctr, no_ctr);
    }
    WickExpr normal_order_impl(int max_unctr = -1, bool no_ctr = false) const {
        int ntg = threading->activate_global();
        vector<WickExpr> r(ntg);
#pragma omp parallel for schedule(static) num_threads(ntg)
        for (int k = 0; k < (int)terms.size(); k++) {
            int tid = threading->get_thread_id();
            WickExpr rr = normal_order_impl_new(terms[k], max_unctr, no_ctr);
            r[tid].terms.insert(r[tid].terms.end(), rr.terms.begin(),
                                rr.terms.end());
        }
        threading->activate_normal();
        WickExpr rx;
        size_t nr = 0;
        for (auto &rr : r)
            nr += rr.terms.size();
        rx.terms.reserve(nr);
        for (auto &rr : r)
            rx.terms.insert(rx.terms.end(), rr.terms.begin(), rr.terms.end());
        return rx;
    }
    static WickExpr normal_order_impl(const WickString &x, int max_unctr = -1,
                                      bool no_ctr = false) {
        WickExpr r;
        bool cd_type = any_of(
            x.tensors.begin(), x.tensors.end(), [](const WickTensor &wt) {
                return wt.type == WickTensorTypes::CreationOperator ||
                       wt.type == WickTensorTypes::DestroyOperator;
            });
        bool sf_type = any_of(
            x.tensors.begin(), x.tensors.end(), [](const WickTensor &wt) {
                return wt.type == WickTensorTypes::SpinFreeOperator;
            });
        assert(!cd_type || !sf_type);
        vector<WickTensor> cd_tensors, ot_tensors;
        vector<int> cd_idx_map;
        cd_tensors.reserve(x.tensors.size());
        ot_tensors.reserve(x.tensors.size());
        for (auto &wt : x.tensors)
            if (wt.type == WickTensorTypes::CreationOperator ||
                wt.type == WickTensorTypes::DestroyOperator)
                cd_tensors.push_back(wt);
            else if (wt.type == WickTensorTypes::SpinFreeOperator) {
                int sf_n = wt.indices.size() / 2;
                for (int i = 0; i < sf_n; i++) {
                    cd_tensors.push_back(WickTensor::cre(wt.indices[i]));
                    cd_idx_map.push_back(cd_idx_map.size() + sf_n);
                }
                for (int i = 0; i < sf_n; i++) {
                    cd_tensors.push_back(WickTensor::des(wt.indices[i + sf_n]));
                    cd_idx_map.push_back(cd_idx_map.size() - sf_n);
                }
            } else
                ot_tensors.push_back(wt);
        int ot_count = (int)ot_tensors.size();
        // all possible pairs
        vector<pair<int, int>> ctr_idxs;
        // starting index in ctr_idxs for the given first index in the pair
        vector<int> ctr_cd_idxs(cd_tensors.size() + 1);
        for (int i = 0; i < (int)cd_tensors.size(); i++) {
            ctr_cd_idxs[i] = (int)ctr_idxs.size();
            if (sf_type) {
                for (int j = i + 1; j < (int)cd_tensors.size(); j++)
                    if (cd_tensors[j].type < cd_tensors[i].type)
                        ctr_idxs.push_back(make_pair(i, j));
            } else {
                for (int j = i + 1; j < (int)cd_tensors.size(); j++)
                    if (cd_tensors[i].type != cd_tensors[j].type &&
                        cd_tensors[j] < cd_tensors[i])
                        ctr_idxs.push_back(make_pair(i, j));
            }
        }
        ctr_cd_idxs[cd_tensors.size()] = (int)ctr_idxs.size();
        vector<pair<int, int>> que;
        vector<pair<int, int>> cur_idxs(cd_tensors.size());
        vector<int8_t> cur_idxs_mask(cd_tensors.size(), 0);
        vector<int> tensor_idxs(cd_tensors.size());
        vector<int> cd_idx_map_rev(cd_tensors.size());
        vector<int> acc_sign(cd_tensors.size() + 1);
        if (max_unctr != 0 || cd_tensors.size() % 2 == 0) {
            que.push_back(make_pair(-1, -1));
            acc_sign[0] = 0; // even
            for (int i = 0; i < (int)cd_tensors.size(); i++)
                tensor_idxs[i] = i;
            if (sf_type) {
                stable_sort(tensor_idxs.begin(), tensor_idxs.end(),
                            [&cd_tensors](int i, int j) {
                                return cd_tensors[i].type < cd_tensors[j].type;
                            });
                assert(all_of(tensor_idxs.begin(),
                              tensor_idxs.begin() + tensor_idxs.size() / 2,
                              [&cd_tensors](int i) {
                                  return cd_tensors[i].type ==
                                         WickTensorTypes::CreationOperator;
                              }));
            } else {
                // sign for reordering tensors to the normal order
                for (int i = 0; i < (int)cd_tensors.size(); i++)
                    for (int j = i + 1; j < (int)cd_tensors.size(); j++)
                        acc_sign[0] ^= (cd_tensors[j] < cd_tensors[i]);
                // arg sort of tensors in the normal order
                stable_sort(tensor_idxs.begin(), tensor_idxs.end(),
                            [&cd_tensors](int i, int j) {
                                return cd_tensors[i] < cd_tensors[j];
                            });
            }
        }
        // depth-first tree traverse
        while (!que.empty()) {
            int l = que.back().first, j = que.back().second, k = 0;
            que.pop_back();
            int a, b, c, d;
            if (l != -1) {
                cur_idxs[l] = ctr_idxs[j];
                k = ctr_cd_idxs[ctr_idxs[j].first + 1];
            }
            acc_sign[l + 2] = acc_sign[l + 1];
            ot_tensors.resize(ot_count + l + 1);
            memset(cur_idxs_mask.data(), 0,
                   sizeof(int8_t) * cur_idxs_mask.size());
            if (sf_type)
                memcpy(cd_idx_map_rev.data(), cd_idx_map.data(),
                       sizeof(int) * cd_idx_map.size());
            if (l != -1) {
                tie(c, d) = cur_idxs[l];
                bool skip = false;
                acc_sign[l + 2] ^= ((c ^ d) & 1) ^ 1;
                // add contraction crossing sign from c/d
                for (int i = 0; i < l && !skip; i++) {
                    tie(a, b) = cur_idxs[i];
                    skip |= (b == d || b == c || a == d);
                    cur_idxs_mask[a] = cur_idxs_mask[b] = 1;
                    acc_sign[l + 2] ^= ((a < c && b > c && b < d) ||
                                        (a > c && a < d && b > d));
                }
                if (skip)
                    continue;
                cur_idxs_mask[c] = cur_idxs_mask[d] = 1;
                if (sf_type) {
                    for (int i = 0; i < l; i++) {
                        tie(a, b) = cur_idxs[i];
                        cd_idx_map_rev[cd_idx_map_rev[a]] = cd_idx_map_rev[b];
                        cd_idx_map_rev[cd_idx_map_rev[b]] = cd_idx_map_rev[a];
                    }
                    cd_idx_map_rev[cd_idx_map_rev[c]] = cd_idx_map_rev[d];
                    cd_idx_map_rev[cd_idx_map_rev[d]] = cd_idx_map_rev[c];
                    acc_sign[l + 2] = 0;
                } else {
                    // remove tensor reorder sign for c/d
                    acc_sign[l + 2] ^= (cd_tensors[d] < cd_tensors[c]);
                    for (int i = 0; i < (int)cd_tensors.size(); i++)
                        if (!cur_idxs_mask[i]) {
                            acc_sign[l + 2] ^=
                                (cd_tensors[max(c, i)] < cd_tensors[min(c, i)]);
                            acc_sign[l + 2] ^=
                                (cd_tensors[max(d, i)] < cd_tensors[min(d, i)]);
                        }
                }
                ot_tensors[ot_count + l] =
                    WickTensor::kronecker_delta(vector<WickIndex>{
                        cd_tensors[c].indices[0], cd_tensors[d].indices[0]});
            }
            // push next contraction order to queue
            if (!no_ctr)
                for (; k < (int)ctr_idxs.size(); k++)
                    que.push_back(make_pair(l + 1, k));
            if (max_unctr != -1 && cd_tensors.size() - (l + l + 2) > max_unctr)
                continue;
            if (sf_type) {
                int sf_n = cd_tensors.size() / 2, tn = sf_n - l - 1;
                vector<WickIndex> wis(tn * 2);
                for (int i = 0, k = 0; i < (int)tensor_idxs.size(); i++)
                    if (!cur_idxs_mask[tensor_idxs[i]] &&
                        cd_tensors[tensor_idxs[i]].type ==
                            WickTensorTypes::CreationOperator) {
                        wis[k] = cd_tensors[tensor_idxs[i]].indices[0];
                        wis[k + tn] = cd_tensors[cd_idx_map_rev[tensor_idxs[i]]]
                                          .indices[0];
                        k++;
                    }
                ot_tensors.push_back(WickTensor::spin_free(wis));
            } else {
                for (int i = 0; i < (int)tensor_idxs.size(); i++)
                    if (!cur_idxs_mask[tensor_idxs[i]])
                        ot_tensors.push_back(cd_tensors[tensor_idxs[i]]);
            }
            r.terms.push_back(
                WickString(ot_tensors, x.ctr_indices,
                           acc_sign[l + 2] ? -x.factor : x.factor));
        }
        return r;
    }
    static WickExpr normal_order_impl_new(const WickString &x,
                                          int max_unctr = -1,
                                          bool no_ctr = false) {
        WickExpr r;
        bool cd_type = any_of(
            x.tensors.begin(), x.tensors.end(), [](const WickTensor &wt) {
                return wt.type == WickTensorTypes::CreationOperator ||
                       wt.type == WickTensorTypes::DestroyOperator;
            });
        bool sf_type = any_of(
            x.tensors.begin(), x.tensors.end(), [](const WickTensor &wt) {
                return wt.type == WickTensorTypes::SpinFreeOperator;
            });
        assert(!cd_type || !sf_type);
        vector<WickTensor> cd_tensors, ot_tensors;
        vector<int> cd_idx_map, n_inactive_idxs;
        int init_sign = 0, final_sign = 0;
        cd_tensors.reserve(x.tensors.size());
        ot_tensors.reserve(x.tensors.size());
        for (auto &wt : x.tensors)
            if (wt.type == WickTensorTypes::CreationOperator ||
                wt.type == WickTensorTypes::DestroyOperator)
                cd_tensors.push_back(wt);
            else if (wt.type == WickTensorTypes::SpinFreeOperator) {
                int sf_n = wt.indices.size() / 2;
                // sign from reverse destroy operator
                init_sign ^= ((sf_n - 1) & 1) ^ (((sf_n - 1) & 2) >> 1);
                for (int i = 0; i < sf_n; i++) {
                    cd_tensors.push_back(WickTensor::cre(wt.indices[i]));
                    cd_idx_map.push_back(cd_idx_map.size() + sf_n);
                }
                for (int i = 0; i < sf_n; i++) {
                    cd_tensors.push_back(WickTensor::des(wt.indices[i + sf_n]));
                    cd_idx_map.push_back(cd_idx_map.size() - sf_n);
                }
            } else
                ot_tensors.push_back(wt);
        int ot_count = (int)ot_tensors.size();
        // all possible pairs
        vector<pair<int, int>> ctr_idxs;
        // starting index in ctr_idxs for the given first index in the pair
        vector<int> ctr_cd_idxs(cd_tensors.size() + 1);
        if (sf_type)
            n_inactive_idxs.resize(cd_tensors.size() + 1, 0);
        for (int i = 0; i < (int)cd_tensors.size(); i++) {
            ctr_cd_idxs[i] = (int)ctr_idxs.size();
            if (sf_type) {
                for (int j = i + 1; j < (int)cd_tensors.size(); j++) {
                    const bool ti =
                        (cd_tensors[i].indices[0].types &
                         WickIndexTypes::Inactive) != WickIndexTypes::None;
                    const bool tj =
                        (cd_tensors[j].indices[0].types &
                         WickIndexTypes::Inactive) != WickIndexTypes::None;
                    if (ti || tj) {
                        if (cd_tensors[i].type < cd_tensors[j].type && ti &&
                            tj) {
                            ctr_idxs.push_back(make_pair(i, j));
                            n_inactive_idxs[i] = 1;
                        }
                    } else if (cd_tensors[j].type < cd_tensors[i].type)
                        ctr_idxs.push_back(make_pair(i, j));
                }
            } else {
                for (int j = i + 1; j < (int)cd_tensors.size(); j++)
                    if (cd_tensors[i].type != cd_tensors[j].type &&
                        cd_tensors[j] < cd_tensors[i])
                        ctr_idxs.push_back(make_pair(i, j));
            }
        }
        ctr_cd_idxs[cd_tensors.size()] = (int)ctr_idxs.size();
        for (int i = (int)n_inactive_idxs.size() - 2; i >= 0; i--)
            n_inactive_idxs[i] += n_inactive_idxs[i + 1];
        vector<pair<int, int>> que;
        vector<pair<int, int>> cur_idxs(cd_tensors.size());
        vector<int8_t> cur_idxs_mask(cd_tensors.size(), 0);
        vector<int8_t> inactive_mask(cd_tensors.size(), 0);
        vector<int> tensor_idxs(cd_tensors.size()), rev_idxs(cd_tensors.size());
        vector<int> cd_idx_map_rev(cd_tensors.size());
        vector<int> acc_sign(cd_tensors.size() + 1);
        if (max_unctr != 0 || cd_tensors.size() % 2 == 0) {
            que.push_back(make_pair(-1, -1));
            acc_sign[0] = init_sign; // even
            for (int i = 0; i < (int)cd_tensors.size(); i++)
                tensor_idxs[i] = i;
            // arg sort of tensors in the normal order
            if (sf_type) {
                stable_sort(tensor_idxs.begin(), tensor_idxs.end(),
                            [&cd_tensors](int i, int j) {
                                return cd_tensors[i].type < cd_tensors[j].type;
                            });
                assert(all_of(tensor_idxs.begin(),
                              tensor_idxs.begin() + tensor_idxs.size() / 2,
                              [&cd_tensors](int i) {
                                  return cd_tensors[i].type ==
                                         WickTensorTypes::CreationOperator;
                              }));
            } else {
                stable_sort(tensor_idxs.begin(), tensor_idxs.end(),
                            [&cd_tensors](int i, int j) {
                                return cd_tensors[i] < cd_tensors[j];
                            });
                // sign for reordering tensors to the normal order
                for (int i = 0; i < (int)tensor_idxs.size(); i++)
                    rev_idxs[tensor_idxs[i]] = i;
                for (int i = 0; i < (int)rev_idxs.size(); i++)
                    for (int j = i + 1; j < (int)rev_idxs.size(); j++)
                        acc_sign[0] ^= (rev_idxs[j] < rev_idxs[i]);
            }
        }
        // depth-first tree traverse
        while (!que.empty()) {
            int l = que.back().first, j = que.back().second, k = 0;
            que.pop_back();
            int a, b, c, d, n_inact = 0;
            double inact_fac = 1.0;
            if (l != -1) {
                cur_idxs[l] = ctr_idxs[j];
                k = ctr_cd_idxs[ctr_idxs[j].first + 1];
            }
            acc_sign[l + 2] = acc_sign[l + 1];
            ot_tensors.resize(ot_count + l + 1);
            memset(cur_idxs_mask.data(), 0,
                   sizeof(int8_t) * cur_idxs_mask.size());
            if (sf_type) {
                memcpy(cd_idx_map_rev.data(), cd_idx_map.data(),
                       sizeof(int) * cd_idx_map.size());
                memset(inactive_mask.data(), 0,
                       sizeof(int8_t) * inactive_mask.size());
            }
            if (l != -1) {
                tie(c, d) = cur_idxs[l];
                bool skip = false;
                acc_sign[l + 2] ^= ((c ^ d) & 1) ^ 1;
                // add contraction crossing sign from c/d
                for (int i = 0; i < l && !skip; i++) {
                    tie(a, b) = cur_idxs[i];
                    skip |= (b == d || b == c || a == d);
                    cur_idxs_mask[a] = cur_idxs_mask[b] = 1;
                    acc_sign[l + 2] ^= ((a < c && b > c && b < d) ||
                                        (a > c && a < d && b > d));
                }
                if (skip)
                    continue;
                cur_idxs_mask[c] = cur_idxs_mask[d] = 1;
                if (sf_type) {
                    n_inact = 0;
                    for (int i = 0; i < l; i++) {
                        tie(a, b) = cur_idxs[i];
                        inactive_mask[a] |=
                            n_inactive_idxs[a] - n_inactive_idxs[a + 1];
                        inactive_mask[b] |=
                            n_inactive_idxs[b] - n_inactive_idxs[b + 1];
                        inactive_mask[cd_idx_map_rev[a]] |= inactive_mask[a];
                        inactive_mask[cd_idx_map_rev[b]] |= inactive_mask[b];
                        n_inact += n_inactive_idxs[a] - n_inactive_idxs[a + 1];
                        inact_fac *=
                            1 << ((cd_idx_map_rev[a] == b) & inactive_mask[a]);
                        cd_idx_map_rev[cd_idx_map_rev[a]] = cd_idx_map_rev[b];
                        cd_idx_map_rev[cd_idx_map_rev[b]] = cd_idx_map_rev[a];
                    }
                    inactive_mask[c] |=
                        n_inactive_idxs[c] - n_inactive_idxs[c + 1];
                    inactive_mask[d] |=
                        n_inactive_idxs[d] - n_inactive_idxs[d + 1];
                    inactive_mask[cd_idx_map_rev[c]] |= inactive_mask[c];
                    inactive_mask[cd_idx_map_rev[d]] |= inactive_mask[d];
                    n_inact += n_inactive_idxs[c] - n_inactive_idxs[c + 1];
                    // inactive must be all contracted
                    if (n_inact + n_inactive_idxs[c + 1] < n_inactive_idxs[0])
                        continue;
                    inact_fac *=
                        1 << ((cd_idx_map_rev[c] == d) & inactive_mask[c]);
                    cd_idx_map_rev[cd_idx_map_rev[c]] = cd_idx_map_rev[d];
                    cd_idx_map_rev[cd_idx_map_rev[d]] = cd_idx_map_rev[c];
                } else {
                    // remove tensor reorder sign for c/d
                    acc_sign[l + 2] ^= (rev_idxs[d] < rev_idxs[c]);
                    for (int i = 0; i < (int)rev_idxs.size(); i++)
                        if (!cur_idxs_mask[i]) {
                            acc_sign[l + 2] ^=
                                (rev_idxs[max(c, i)] < rev_idxs[min(c, i)]);
                            acc_sign[l + 2] ^=
                                (rev_idxs[max(d, i)] < rev_idxs[min(d, i)]);
                        }
                }
                ot_tensors[ot_count + l] =
                    WickTensor::kronecker_delta(vector<WickIndex>{
                        cd_tensors[c].indices[0], cd_tensors[d].indices[0]});
            }
            // push next contraction order to queue
            if (!no_ctr)
                for (; k < (int)ctr_idxs.size(); k++)
                    que.push_back(make_pair(l + 1, k));
            if (max_unctr != -1 && cd_tensors.size() - (l + l + 2) > max_unctr)
                continue;
            if (sf_type) {
                if (n_inact < n_inactive_idxs[0])
                    continue;
                int sf_n = cd_tensors.size() / 2, tn = sf_n - l - 1;
                vector<WickIndex> wis(tn * 2);
                for (int i = 0, k = 0; i < (int)tensor_idxs.size(); i++)
                    if (!cur_idxs_mask[tensor_idxs[i]] &&
                        cd_tensors[tensor_idxs[i]].type ==
                            WickTensorTypes::CreationOperator) {
                        rev_idxs[k] = tensor_idxs[i];
                        rev_idxs[k + tn] = cd_idx_map_rev[tensor_idxs[i]];
                        k++;
                    }
                for (int i = 0; i < tn + tn; i++)
                    wis[i] = cd_tensors[rev_idxs[i]].indices[0];
                // sign for reversing destroy operator
                final_sign = ((tn - 1) & 1) ^ (((tn - 1) & 2) >> 1);
                // sign for reordering tensors to the normal order
                for (int i = 0; i < (int)(tn + tn); i++)
                    for (int j = i + 1; j < (int)(tn + tn); j++)
                        final_sign ^= (rev_idxs[j] < rev_idxs[i]);
                if (wis.size() != 0)
                    ot_tensors.push_back(WickTensor::spin_free(wis));
            } else {
                for (int i = 0; i < (int)tensor_idxs.size(); i++)
                    if (!cur_idxs_mask[tensor_idxs[i]])
                        ot_tensors.push_back(cd_tensors[tensor_idxs[i]]);
            }
            r.terms.push_back(WickString(
                ot_tensors, x.ctr_indices,
                inact_fac *
                    ((acc_sign[l + 2] ^ final_sign) ? -x.factor : x.factor)));
        }
        return r;
    }
    WickExpr simple_sort() const {
        WickExpr r = *this;
        for (auto &rr : r.terms)
            rr = rr.simple_sort();
        return r;
    }
    WickExpr simplify_delta() const {
        WickExpr r = *this;
        for (auto &rr : r.terms)
            rr = rr.simplify_delta();
        return r;
    }
    WickExpr simplify_zero() const {
        WickExpr r;
        for (auto &rr : terms)
            if (abs(rr.factor) > 1E-12 && rr.tensors.size() != 0)
                r.terms.push_back(rr);
        return r;
    }
    WickExpr remove_external() const {
        WickExpr r;
        for (auto &rr : terms)
            if (!rr.has_external_ops())
                r.terms.push_back(rr);
        return r;
    }
    // when there is only one spin free operator
    // it can be considered as density matrix
    // on the ref state with trans symmetry
    WickExpr add_spin_free_trans_symm() const {
        WickExpr r = *this;
        for (auto &rr : r.terms) {
            int found = 0;
            WickTensor *xwt;
            for (auto &wt : rr.tensors)
                if (wt.type == WickTensorTypes::SpinFreeOperator)
                    found++, xwt = &wt;
            if (found == 1)
                xwt->perms = WickPermutation::complete_set(
                    (int)xwt->indices.size(),
                    WickPermutation::pair_symmetric(
                        (int)xwt->indices.size() / 2, true));
        }
        return r;
    }
    WickExpr conjugate() const {
        WickExpr r = *this;
        for (auto &rr : r.terms) {
            vector<WickTensor> tensors;
            for (auto &wt : rr.tensors)
                if (wt.type == WickTensorTypes::SpinFreeOperator) {
                    int k = (int)wt.indices.size() / 2;
                    for (int i = 0; i < k; i++)
                        swap(wt.indices[i], wt.indices[i + k]);
                    tensors.push_back(wt);
                } else if (wt.type == WickTensorTypes::CreationOperator) {
                    wt.type = WickTensorTypes::DestroyOperator;
                    wt.name = wt.name == "C" ? "D" : wt.name;
                    tensors.push_back(wt);
                } else if (wt.type == WickTensorTypes::DestroyOperator) {
                    wt.type = WickTensorTypes::CreationOperator;
                    wt.name = wt.name == "D" ? "C" : wt.name;
                    tensors.push_back(wt);
                }
            for (auto &wt : rr.tensors)
                if (wt.type == WickTensorTypes::SpinFreeOperator ||
                    wt.type == WickTensorTypes::CreationOperator ||
                    wt.type == WickTensorTypes::DestroyOperator)
                    wt = tensors.back(), tensors.pop_back();
        }
        return r;
    }
    WickExpr simplify_merge() const {
        vector<WickString> sorted(terms.size());
        vector<pair<int, double>> ridxs;
        int ntg = threading->activate_global();
#pragma omp parallel for schedule(static) num_threads(ntg)
        for (int k = 0; k < (int)terms.size(); k++)
            sorted[k] = terms[k].abs().quick_sort();
        threading->activate_normal();
        for (int i = 0; i < (int)terms.size(); i++) {
            bool found = false;
            for (int j = 0; j < (int)ridxs.size() && !found; j++)
                if (sorted[i].abs_equal_to(sorted[ridxs[j].first])) {
                    found = true;
                    ridxs[j].second += terms[i].factor * sorted[i].factor *
                                       sorted[ridxs[j].first].factor;
                }
            if (!found)
                ridxs.push_back(make_pair(i, terms[i].factor));
        }
        WickExpr r;
        for (auto &m : ridxs) {
            r.terms.push_back(terms[m.first]);
            r.terms.back().factor = m.second;
        }
        r = r.simplify_zero();
        sort(r.terms.begin(), r.terms.end());
        return r;
    }
    WickExpr simplify() const {
        return simplify_delta().simplify_zero().simplify_merge();
    }
};

inline WickExpr operator+(const WickString &a, const WickString &b) noexcept {
    return WickExpr({a, b});
}

inline WickExpr operator*(double d, const WickExpr &x) noexcept {
    return x * d;
}

// commutator
inline WickExpr operator^(const WickExpr &a, const WickExpr &b) noexcept {
    return a * b + b * a * (-1.0);
}

// multiply and contract all
inline WickExpr operator&(const WickExpr &a, const WickExpr &b) noexcept {
    WickExpr c = a * b;
    for (auto &ws : c.terms)
        for (auto &wt : ws.tensors)
            for (auto &wi : wt.indices)
                ws.ctr_indices.insert(wi);
    return c;
}

struct WickGHF {
    map<WickIndexTypes, set<WickIndex>> idx_map[4]; // aa, bb, ab, ba
    map<pair<string, int>, vector<WickPermutation>> perm_map;
    WickGHF() {
        idx_map[0][WickIndexTypes::Alpha] = WickIndex::parse_set("ijkl");
        idx_map[1][WickIndexTypes::Beta] = WickIndex::parse_set("ijkl");
        idx_map[2][WickIndexTypes::Alpha] = WickIndex::parse_set("ij");
        idx_map[2][WickIndexTypes::Beta] = WickIndex::parse_set("kl");
        idx_map[3][WickIndexTypes::Beta] = WickIndex::parse_set("ij");
        idx_map[3][WickIndexTypes::Alpha] = WickIndex::parse_set("kl");
        perm_map[make_pair("v", 4)] = WickPermutation::qc_chem();
    }
    WickExpr make_h1b() const {
        WickExpr expr =
            WickExpr::parse("SUM <ij> h[ij] D[i] C[j]", idx_map[1], perm_map);
        return expr.expand().simplify();
    }
    WickExpr make_h2aa() const {
        WickExpr expr =
            0.5 * WickExpr::parse("SUM <ijkl> v[ijkl] C[i] C[k] D[l] D[j]",
                                  idx_map[0], perm_map);
        return expr.expand().simplify();
    }
    WickExpr make_h2bb() const {
        WickExpr expr =
            0.5 * WickExpr::parse("SUM <ijkl> v[ijkl] D[i] D[k] C[l] C[j]",
                                  idx_map[1], perm_map);
        return expr.expand().simplify();
    }
    WickExpr make_h2ab() const {
        WickExpr expr =
            0.5 * WickExpr::parse("SUM <ijkl> v[ijkl] C[i] D[k] C[l] D[j]",
                                  idx_map[2], perm_map);
        return expr.expand().simplify();
    }
    WickExpr make_h2ba() const {
        WickExpr expr =
            0.5 * WickExpr::parse("SUM <ijkl> v[ijkl] D[i] C[k] D[l] C[j]",
                                  idx_map[3], perm_map);
        return expr.expand().simplify();
    }
};

struct WickCCSD {
    map<WickIndexTypes, set<WickIndex>> idx_map;
    map<pair<string, int>, vector<WickPermutation>> perm_map;
    WickExpr h1, h2, h, t1, t2, t, ex1, ex2;
    WickCCSD(bool anti_integral = true) {
        idx_map[WickIndexTypes::Inactive] = WickIndex::parse_set("pqrsijklmno");
        idx_map[WickIndexTypes::External] = WickIndex::parse_set("pqrsabcdefg");
        perm_map[make_pair("v", 4)] = anti_integral
                                          ? WickPermutation::four_anti()
                                          : WickPermutation::qc_phys();
        perm_map[make_pair("t", 2)] = WickPermutation::non_symmetric();
        perm_map[make_pair("t", 4)] = WickPermutation::four_anti();
        h1 = WickExpr::parse("SUM <pq> h[pq] C[p] D[q]", idx_map, perm_map);
        h2 = (anti_integral ? 0.25 : 0.5) *
             WickExpr::parse("SUM <pqrs> v[pqrs] C[p] C[q] D[s] D[r]", idx_map,
                             perm_map);
        t1 = WickExpr::parse("SUM <ai> t[ai] C[a] D[i]", idx_map, perm_map);
        t2 = 0.25 * WickExpr::parse("SUM <abij> t[abij] C[a] C[b] D[j] D[i]",
                                    idx_map, perm_map);
        ex1 = WickExpr::parse("C[i] D[a]", idx_map, perm_map);
        ex2 = WickExpr::parse("C[i] C[j] D[b] D[a]", idx_map, perm_map);
        h = (h1 + h2).expand(-1, true).simplify();
        t = (t1 + t2).expand(-1, true).simplify();
    }
    // ex1 * (h + [h, t] + 0.5 [[h, t], t] + (1/6) [[[h2, t1], t1], t1])
    WickExpr t1_equations(int order = 4) const {
        vector<WickExpr> hx(5, h);
        WickExpr amp = h;
        for (int i = 0; i < order; amp = amp + hx[++i])
            hx[i + 1] = (1.0 / (i + 1)) *
                        (hx[i] ^ t).expand((order - i) * 2).simplify();
        return (ex1 * amp).expand(0).simplify();
    }
    // MEST Eq. (5.7.16)
    WickExpr t2_equations(int order = 4) const {
        vector<WickExpr> hx(5, h);
        WickExpr amp = h;
        for (int i = 0; i < order; amp = amp + hx[++i])
            hx[i + 1] = (1.0 / (i + 1)) *
                        (hx[i] ^ t).expand((order - i) * 4).simplify();
        return (ex2 * amp).expand(0).simplify();
    }
};

struct WickNEVPT2 {
    map<WickIndexTypes, set<WickIndex>> idx_map;
    map<pair<string, int>, vector<WickPermutation>> perm_map;
    WickExpr fi, fa, fe, hw, hd;
    WickNEVPT2() {
        idx_map[WickIndexTypes::Inactive] = WickIndex::parse_set("mnopijkl");
        idx_map[WickIndexTypes::Active] = WickIndex::parse_set("mnoprstugh");
        idx_map[WickIndexTypes::External] = WickIndex::parse_set("mnopabcd");
        perm_map[make_pair("w", 4)] = WickPermutation::qc_phys();
        fi = WickExpr::parse("SUM <ij> f[ij] E1[i,j]", idx_map, perm_map);
        fa = WickExpr::parse("SUM <rs> f[rs] E1[r,s]", idx_map, perm_map);
        fe = WickExpr::parse("SUM <ab> f[ab] E1[a,b]", idx_map, perm_map);
        hw = WickExpr::parse("0.5 SUM <turs> w[turs] E2[tu,rs]", idx_map,
                             perm_map);
        hd = fi + fa + hw + fe;
    }
    WickExpr aavv_equations() const {
        WickExpr x = WickExpr::parse("x[cdtu]", idx_map, perm_map);
        WickExpr v = WickExpr::parse("0.5 w[cdtu]", idx_map, perm_map);
        WickExpr bra = WickExpr::parse("E1[r,a] E1[s,b]", idx_map);
        WickExpr ket = WickExpr::parse("E1[c,t] E1[d,u]", idx_map);
        WickExpr lhs = bra * (hd ^ (ket & x)).expand().simplify();
        WickExpr rhs = bra * (ket & v).expand().simplify();
        return (rhs - lhs).expand().remove_external().simplify();
    }
    WickExpr ccvv_equations() const {
        WickExpr x = WickExpr::parse("x[cdkl]", idx_map, perm_map);
        WickExpr v = WickExpr::parse("0.5 w[cdkl]", idx_map, perm_map);
        WickExpr bra = WickExpr::parse("E1[i,a] E1[j,b]", idx_map);
        WickExpr ket = WickExpr::parse("E1[c,k] E1[d,l]", idx_map);
        WickExpr lhs = bra * (hd ^ (ket & x)).expand().simplify();
        WickExpr rhs = bra * (ket & v).expand().simplify();
        return (rhs - lhs).expand().remove_external().simplify();
    }
    WickExpr cavv_equations() const {
        WickExpr x = WickExpr::parse("x[cdjs]", idx_map, perm_map);
        WickExpr v = WickExpr::parse("0.5 w[cdjs]", idx_map, perm_map);
        WickExpr bra = WickExpr::parse("E1[r,a] E1[i,b]", idx_map);
        WickExpr ket = WickExpr::parse("E1[c,j] E1[d,s]", idx_map);
        WickExpr lhs = bra * (hd ^ (ket & x)).expand().simplify();
        WickExpr rhs = bra * (ket & v).expand().simplify();
        return (rhs - lhs).expand().remove_external().simplify();
    }
    WickExpr ccav_equations() const {
        WickExpr x = WickExpr::parse("x[sbkl]", idx_map, perm_map);
        WickExpr v = WickExpr::parse("0.5 w[sbkl]", idx_map, perm_map);
        WickExpr bra = WickExpr::parse("E1[i,a] E1[j,r]", idx_map);
        WickExpr ket = WickExpr::parse("E1[s,k] E1[b,l]", idx_map);
        WickExpr lhs = bra * (hd ^ (ket & x)).expand().simplify();
        WickExpr rhs = bra * (ket & v).expand().simplify();
        return (rhs - lhs).expand().remove_external().simplify();
    }
    WickExpr ccaa_equations() const {
        WickExpr x = WickExpr::parse("x[tukl]", idx_map, perm_map);
        WickExpr v = WickExpr::parse("0.5 w[tukl]", idx_map, perm_map);
        WickExpr bra = WickExpr::parse("E1[i,r] E1[j,s]", idx_map);
        WickExpr ket = WickExpr::parse("E1[t,k] E1[u,l]", idx_map);
        WickExpr lhs = bra * (hd ^ (ket & x)).expand().simplify();
        WickExpr rhs = bra * (ket & v).expand().simplify();
        return (rhs - lhs).expand().remove_external().simplify();
    }
    WickExpr cava_equations() const {
        WickExpr x = WickExpr::parse("x[btju]", idx_map, perm_map);
        WickExpr v = WickExpr::parse("0.5 w[btju]", idx_map, perm_map);
        WickExpr bra = WickExpr::parse("E1[r,s] E1[i,a]", idx_map);
        WickExpr ket = WickExpr::parse("E1[b,j] E1[t,u]", idx_map);
        WickExpr lhs = bra * (hd ^ (ket & x)).expand().simplify();
        WickExpr rhs = bra * (ket & v).expand().simplify();
        return (rhs - lhs).expand().remove_external().simplify();
    }
    WickExpr cvaa_equations() const {
        WickExpr x = WickExpr::parse("x[utbj]", idx_map, perm_map);
        WickExpr v = WickExpr::parse("0.5 w[utbj]", idx_map, perm_map);
        WickExpr bra = WickExpr::parse("E1[i,s] E1[r,a]", idx_map)
                           .expand(-1, true)
                           .simplify();
        WickExpr ket = WickExpr::parse("E1[b,u] E1[t,j]", idx_map)
                           .expand(-1, true)
                           .simplify();
        WickExpr lhs = bra * (hd ^ (ket & x)).expand().simplify();
        WickExpr rhs = bra * (ket & v).expand().simplify();
        cout << (ket & v) << endl;
        cout << (ket & v).expand() << endl;
        cout << rhs << endl;
        cout << rhs.expand() << endl;
        return (rhs - lhs).expand().remove_external().simplify();
    }
};

struct WickSCNEVPT2 {
    map<WickIndexTypes, set<WickIndex>> idx_map;
    map<pair<string, int>, vector<WickPermutation>> perm_map;
    map<string, pair<WickTensor, WickExpr>> defs;
    vector<pair<string, string>> sub_spaces;
    WickExpr heff, hw, hd;
    WickSCNEVPT2() {
        idx_map[WickIndexTypes::Inactive] = WickIndex::parse_set("mnxyijkl");
        idx_map[WickIndexTypes::Active] =
            WickIndex::parse_set("mnxyabcdefghpq");
        idx_map[WickIndexTypes::External] = WickIndex::parse_set("mnxyrstu");
        perm_map[make_pair("w", 4)] = WickPermutation::qc_phys();
        heff = WickExpr::parse("SUM <ab> h[ab] E1[a,b]", idx_map, perm_map);
        hw = WickExpr::parse("0.5 SUM <abcd> w[abcd] E2[ab,cd]", idx_map,
                             perm_map);
        hd = heff + hw;
        sub_spaces = {{"ijrs", "gamma[ij] gamma[rs] w[rsij] E1[r,i] E1[s,j] \n"
                               "gamma[ij] gamma[rs] w[rsji] E1[s,i] E1[r,j]"},
                      {"rsi", "SUM <a> gamma[rs] w[rsia] E1[r,i] E1[s,a] \n"
                              "SUM <a> gamma[rs] w[sria] E1[s,i] E1[r,a]"},
                      {"ijr", "SUM <a> gamma[ij] w[raji] E1[r,j] E1[a,i] \n"
                              "SUM <a> gamma[ij] w[raij] E1[r,i] E1[a,j]"},
                      {"rs", "SUM <ab> gamma[rs] w[rsba] E1[r,b] E1[s,a]"},
                      {"ij", "SUM <ab> gamma[ij] w[baij] E1[b,i] E1[a,j]"},
                      {"ir", "SUM <ab> w[raib] E1[r,i] E1[a,b] \n"
                             "SUM <ab> w[rabi] E1[a,i] E1[r,b] \n"
                             "h[ri] E1[r,i]"},
                      {"r", "SUM <abc> w[rabc] E1[r,b] E1[a,c] \n"
                            "SUM <a> h[ra] E1[r,a] \n"
                            "- SUM <ab> w[rbba] E1[r,a]"},
                      {"i", "SUM <abc> w[baic] E1[b,i] E1[a,c] \n"
                            "SUM <a> h[ai] E1[a,i]"}};
        defs["gamma"] = WickExpr::parse_def(
            "gamma[mn] = 1.0 \n - 0.5 delta[mn]", idx_map, perm_map);
        defs["hbar"] = WickExpr::parse_def(
            "hbar[ab] = h[ab] \n - 0.5 SUM <c> w[accb]", idx_map, perm_map);
        defs["hp"] = WickExpr::parse_def(
            "hp[mn] = h[mn] \n - 1.0 SUM <b> w[mbbn]", idx_map, perm_map);
        defs["E1T"] = WickExpr::parse_def(
            "E1T[a,b] = 2.0 delta[ab] \n - E1[b,a]", idx_map, perm_map);
        defs["E2TX"] = WickExpr::parse_def(
            "E2TX[pq,ab] = E2[ab,pq] \n + delta[pb] E1[a,q] \n"
            "delta[qa] E1[b,p] \n - 2.0 delta[pa] E1[b,q] \n"
            "- 2.0 delta[qb] E1[a,p] \n - 2.0 delta[pb] delta[qa] \n"
            "+ 4.0 delta[ap] delta[bq]",
            idx_map, perm_map);
        defs["E2T"] = WickExpr::parse_def(
            "E2T[pq,ab] = E1T[p,a] E1T[q,b] \n - delta[qa] E1T[p,b]", idx_map,
            perm_map);
        defs["E2T"].second = defs["E2T"].second.substitute(defs);
        assert((defs["E2T"].second - defs["E2TX"].second)
                   .expand()
                   .simplify()
                   .terms.size() == 0);
        defs["E3T"] = WickExpr::parse_def(
            "E3T[pqg,abc] = E1T[p,a] E1T[q,b] E1T[g,c] \n"
            " - delta[ag] E2T[pq,cb] \n - delta[aq] E2T[pg,bc] \n"
            " - delta[bg] E2T[pq,ac] \n - delta[aq] delta[bg] E1T[p,c]",
            idx_map, perm_map);
        defs["E3T"].second = defs["E3T"].second.substitute(defs);
    }
    WickExpr build_communicator(const string &bra, const string &ket,
                                bool do_sum = true) const {
        WickExpr xbra = WickExpr::parse(bra, idx_map, perm_map)
                            .substitute(defs)
                            .expand()
                            .simplify();
        WickExpr xket = WickExpr::parse(ket, idx_map, perm_map)
                            .substitute(defs)
                            .expand()
                            .simplify();
        WickExpr expr =
            do_sum ? (xbra.conjugate() & (hd ^ xket).expand().simplify())
                   : (xbra.conjugate() * (hd ^ xket).expand().simplify());
        return expr.expand()
            .remove_external()
            .add_spin_free_trans_symm()
            .simplify();
    }
    WickExpr build_communicator(const string &ket, bool do_sum = true) const {
        WickExpr xket = WickExpr::parse(ket, idx_map, perm_map)
                            .substitute(defs)
                            .expand()
                            .simplify();
        WickExpr expr =
            do_sum ? (xket.conjugate() & (hd ^ xket).expand().simplify())
                   : (xket.conjugate() * (hd ^ xket).expand().simplify());
        return expr.expand()
            .remove_external()
            .add_spin_free_trans_symm()
            .simplify();
    }
    WickExpr build_norm(const string &ket, bool do_sum = true) const {
        WickExpr xket = WickExpr::parse(ket, idx_map, perm_map)
                            .substitute(defs)
                            .expand()
                            .simplify();
        WickExpr expr =
            do_sum ? (xket.conjugate() & xket) : (xket.conjugate() * xket);
        return expr.expand()
            .add_spin_free_trans_symm()
            .remove_external()
            .simplify();
    }
    string to_einsum_orb_energies(const WickTensor &tensor) const {
        stringstream ss;
        ss << tensor.name << " = ";
        for (int i = 0; i < (int)tensor.indices.size(); i++) {
            auto &wi = tensor.indices[i];
            if (wi.types == WickIndexTypes::Inactive)
                ss << "(-1) * ";
            ss << "orbe" << to_str(wi.types);
            ss << "[";
            for (int j = 0; j < (int)tensor.indices.size(); j++) {
                ss << (i == j ? ":" : "None");
                if (j != (int)tensor.indices.size() - 1)
                    ss << ", ";
            }
            ss << "]";
            if (i != (int)tensor.indices.size() - 1)
                ss << " + ";
        }
        return ss.str();
    }
    string to_einsum_sum_restriction(const WickTensor &tensor) const {
        stringstream ss, sr;
        ss << "grid = np.indices((";
        for (int i = 0; i < (int)tensor.indices.size(); i++) {
            auto &wi = tensor.indices[i];
            ss << (wi.types == WickIndexTypes::Inactive ? "ncore" : "nvirt");
            if (i != (int)tensor.indices.size() - 1 || i == 0)
                ss << ", ";
            if (i != 0 &&
                tensor.indices[i].types == tensor.indices[i - 1].types)
                sr << "idx &= grid[" << i - 1 << "] <= grid[" << i << "]"
                   << endl;
        }
        ss << "))" << endl;
        return ss.str() + sr.str();
    }
    string to_einsum_add_indent(const string &x, int indent = 4) const {
        stringstream ss;
        for (size_t i = 0, j = 0; j != string::npos; i = j + 1) {
            ss << string(indent, ' ');
            j = x.find_first_of("\n", i);
            if (j > i)
                ss << x.substr(i, j - i);
            ss << endl;
        }
        return ss.str();
    }
    string to_einsum() const {
        stringstream ss;
        WickTensor norm, ener, deno;
        for (int i = 0; i < (int)sub_spaces.size(); i++) {
            string key = sub_spaces[i].first, expr = sub_spaces[i].second;
            stringstream sr;
            ss << "def compute_" << key << "():" << endl;
            norm = WickTensor::parse("norm[" + key + "]", idx_map, perm_map);
            ener = WickTensor::parse("hexp[" + key + "]", idx_map, perm_map);
            deno = WickTensor::parse("deno[" + key + "]", idx_map, perm_map);
            sr << to_einsum_orb_energies(deno) << endl;
            sr << "norm = np.zeros_like(deno)" << endl;
            sr << build_norm(expr, false).to_einsum(norm) << endl;
            sr << "hexp = np.zeros_like(deno)" << endl;
            sr << build_communicator(expr, false).to_einsum(ener) << endl;
            sr << "idx = abs(norm) > 1E-14" << endl;
            if (key.length() >= 2)
                sr << to_einsum_sum_restriction(deno) << endl;
            sr << "hexp[idx] = deno[idx] + hexp[idx] / norm[idx]" << endl;
            sr << "xener = -(norm[idx] / hexp[idx]).sum()" << endl;
            sr << "xnorm = norm[idx].sum()" << endl;
            sr << "return xnorm, xener" << endl;
            ss << to_einsum_add_indent(sr.str()) << endl;
        }
        return ss.str();
    }
    // Eq (3) ijrs
    WickExpr make_x11(bool do_sum = true) const {
        return build_norm("gamma[ij] gamma[rs] w[rsij] E1[r,i] E1[s,j] \n"
                          "gamma[ij] gamma[rs] w[rsji] E1[s,i] E1[r,j]",
                          do_sum);
    }
    // Eq (4) rsi
    WickExpr make_x12(bool do_sum = true) const {
        return build_norm("SUM <a> gamma[rs] w[rsia] E1[r,i] E1[s,a] \n"
                          "SUM <a> gamma[rs] w[sria] E1[s,i] E1[r,a]",
                          do_sum);
    }
    // Eq (5) ijr
    WickExpr make_x13(bool do_sum = true) const {
        return build_norm("SUM <a> gamma[ij] w[raji] E1[r,j] E1[a,i] \n"
                          "SUM <a> gamma[ij] w[raij] E1[r,i] E1[a,j]",
                          do_sum);
    }
    // Eq (6) rs
    WickExpr make_x14(bool do_sum = true) const {
        return build_norm("SUM <ab> gamma[rs] w[rsba] E1[r,b] E1[s,a]", do_sum);
    }
    // Eq (7) ij
    WickExpr make_x15(bool do_sum = true) const {
        return build_norm("SUM <ab> gamma[ij] w[baij] E1[b,i] E1[a,j]", do_sum);
    }
    // Eq (8) ir
    WickExpr make_x16(bool do_sum = true) const {
        return build_norm("SUM <ab> w[raib] E1[r,i] E1[a,b] \n"
                          "SUM <ab> w[rabi] E1[a,i] E1[r,b] \n"
                          "h[ri] E1[r,i]",
                          do_sum);
    }
    // Eq (9) r
    WickExpr make_x17(bool do_sum = true) const {
        return build_norm("SUM <abc> w[rabc] E1[r,b] E1[a,c] \n"
                          "SUM <a> h[ra] E1[r,a] \n"
                          "- SUM <ab> w[rbba] E1[r,a]",
                          do_sum);
    }
    // Eq (10) i
    WickExpr make_x18(bool do_sum = true) const {
        return build_norm("SUM <abc> w[baic] E1[b,i] E1[a,c] \n"
                          "SUM <a> h[ai] E1[a,i]",
                          do_sum);
    }
    WickExpr make_ax15(bool do_sum = true) const {
        return build_communicator("SUM <ab> gamma[ij] w[baij] E1[b,i] E1[a,j]",
                                  do_sum);
    }
    WickExpr make_ax16(bool do_sum = true) const {
        return build_communicator("SUM <ab> w[raib] E1[r,i] E1[a,b] \n"
                                  "SUM <ab> w[rabi] E1[a,i] E1[r,b] \n"
                                  "h[ri] E1[r,i]",
                                  do_sum);
    }
    WickExpr make_ax17(bool do_sum = true) const {
        return build_communicator("SUM <abc> w[rabc] E1[r,b] E1[a,c] \n"
                                  "SUM <a> h[ra] E1[r,a] \n"
                                  "- SUM <ab> w[rbba] E1[r,a]",
                                  do_sum);
    }
    WickExpr make_ax18(bool do_sum = true) const {
        return build_communicator("SUM <abc> w[baic] E1[b,i] E1[a,c] \n"
                                  "SUM <a> h[ai] E1[a,i]",
                                  do_sum);
    }
    WickExpr make_a1(bool do_sum = true) const {
        return build_communicator(
            "gamma[ij] gamma[rs] w[rsij] E1[r,i] E1[s,j] \n"
            "gamma[ij] gamma[rs] w[rsji] E1[s,i] E1[r,j]",
            do_sum);
    }
    WickExpr make_a3(bool do_sum = true) const {
        return build_communicator("SUM <a> gamma[ij] w[raji] E1[r,j] E1[a,i] \n"
                                  "SUM <a> gamma[ij] w[raij] E1[r,i] E1[a,j]",
                                  do_sum);
    }
    WickExpr make_a3k() const {
        return build_communicator("0.5 E1[i,p] E1[j,r]", "E1[r,j] E1[a,i]");
    }
    WickExpr make_a7(bool do_sum = true) const {
        return build_communicator("SUM <ab> gamma[rs] w[rsba] E1[r,b] E1[s,a]",
                                  do_sum);
    }
    WickExpr make_a7k() const {
        return build_communicator("E1[p,s] E1[q,r]", "E1[r,b] E1[s,a]");
    }
    WickExpr make_ax25(bool do_sum = true) const {
        return build_communicator("SUM <a> gamma[rs] w[rsia] E1[r,i] E1[s,a] \n"
                                  "SUM <a> gamma[rs] w[sria] E1[s,i] E1[r,a]",
                                  do_sum);
    }
    WickExpr make_a9k() const {
        return build_communicator("E1[i,q] E1[j,p]", "E1[a,j] E1[b,i]");
    }
    WickExpr make_a12() const {
        return build_communicator("0.5 E1[q,p] E1[i,r]", "E1[r,i] E1[a,b]");
    }
    WickExpr make_a13() const {
        return build_communicator("E1[q,r] E1[i,p]", "E1[a,i] E1[r,b]");
    }
    WickExpr make_a16() const {
        return build_communicator("E1[g,p] E1[q,r]", "E1[r,b] E1[a,c]");
    }
    WickExpr make_a17() const {
        return build_communicator("E1[g,p] E1[q,r]", "E1[r,a]");
    }
    WickExpr make_a18() const {
        return build_communicator("E1[p,r]", "E1[r,b] E1[a,c]");
    }
    WickExpr make_a19() const {
        return build_communicator("E1[p,r]", "E1[r,a]");
    }
    WickExpr make_a22() const {
        return build_communicator("E1[g,p] E1[i,q]", "E1[b,i] E1[a,c]");
    }
    WickExpr make_a23() const {
        return build_communicator("E1[g,p] E1[i,q]", "E1[a,i]");
    }
    WickExpr make_a24() const {
        return build_communicator("E1[i,p]", "E1[b,i] E1[a,c]");
    }
    WickExpr make_a25() const {
        return build_communicator("E1[i,p]", "E1[a,i]");
    }
};

} // namespace block2

namespace std {

template <> struct hash<block2::WickPermutation> {
    size_t operator()(const block2::WickPermutation &x) const noexcept {
        return x.hash();
    }
};

} // namespace std

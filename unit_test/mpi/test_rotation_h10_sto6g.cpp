
#include "block2_core.hpp"
#include "block2_dmrg.hpp"
#include <gtest/gtest.h>

using namespace block2;

// suppress googletest output for non-root mpi procs
struct MPITest {
    shared_ptr<testing::TestEventListener> tel;
    testing::TestEventListener *def_tel;
    MPITest() {
        if (block2::MPI::rank() != 0) {
            testing::TestEventListeners &tels =
                testing::UnitTest::GetInstance()->listeners();
            def_tel = tels.Release(tels.default_result_printer());
            tel = make_shared<testing::EmptyTestEventListener>();
            tels.Append(tel.get());
        }
    }
    ~MPITest() {
        if (block2::MPI::rank() != 0) {
            testing::TestEventListeners &tels =
                testing::UnitTest::GetInstance()->listeners();
            assert(tel.get() == tels.Release(tel.get()));
            tel = nullptr;
            tels.Append(def_tel);
        }
    }
    static bool okay() {
        static MPITest _mpi_test;
        return _mpi_test.tel != nullptr;
    }
};

class TestRotationH10STO6G : public ::testing::Test {
    static bool _mpi;

  protected:
    size_t isize = 1L << 22;
    size_t dsize = 1L << 30;

    template <typename S>
    void test_dmrg(S target, const shared_ptr<HamiltonianQC<S>> &hamil,
                   const shared_ptr<HamiltonianQC<S>> &hamil_rot,
                   const shared_ptr<HamiltonianQC<S>> &hamil_c1,
                   const string &name, int dot, TETypes te_type, double tol);
    void SetUp() override {
        Random::rand_seed(0);
        frame_() = make_shared<DataFrame>(isize, dsize, "nodex");
        frame_()->minimal_disk_usage = true;
        frame_()->use_main_stack = false;
        threading_() = make_shared<Threading>(
            ThreadingTypes::OperatorBatchedGEMM | ThreadingTypes::Global, 4, 4,
            1);
        threading_()->seq_type = SeqTypes::Tasked;
        cout << *threading_() << endl;
    }
    void TearDown() override {
        frame_()->activate(0);
        assert(ialloc_()->used == 0 && dalloc_()->used == 0);
        frame_() = nullptr;
    }
};

bool TestRotationH10STO6G::_mpi = MPITest::okay();
template <typename S>
void TestRotationH10STO6G::test_dmrg(
    S target, const shared_ptr<HamiltonianQC<S>> &hamil,
    const shared_ptr<HamiltonianQC<S>> &hamil_rot,
    const shared_ptr<HamiltonianQC<S>> &hamil_c1, const string &name, int dot,
    TETypes te_type, double tol) {

    double energy_std = -5.424385375684663;

#ifdef _HAS_MPI
    shared_ptr<ParallelCommunicator<S>> para_comm =
        make_shared<MPICommunicator<S>>();
#else
    shared_ptr<ParallelCommunicator<S>> para_comm =
        make_shared<ParallelCommunicator<S>>(1, 0, 0);
#endif
    shared_ptr<ParallelRule<S>> para_rule =
        make_shared<ParallelRuleQC<S>>(para_comm);

    Timer t;
    t.get_time();
    // MPO construction
    cout << "MPO start" << endl;
    shared_ptr<MPO<S>> mpo =
        make_shared<MPOQC<S>>(hamil, QCTypes::Conventional);
    cout << "MPO end .. T = " << t.get_time() << endl;

    // MPO simplification
    cout << "MPO simplification start" << endl;
    mpo = make_shared<SimplifiedMPO<S>>(mpo, make_shared<RuleQC<S>>(), true);
    cout << "MPO simplification end .. T = " << t.get_time() << endl;

    // MPO parallelization
    cout << "MPO parallelization start" << endl;
    mpo = make_shared<ParallelMPO<S>>(mpo, para_rule);
    cout << "MPO parallelization end .. T = " << t.get_time() << endl;

    ubond_t ket_bond_dim = 500, bra_bond_dim = 1000;
    vector<ubond_t> bra_bdims = {bra_bond_dim}, ket_bdims = {ket_bond_dim};
    vector<double> noises = {1E-6, 1E-8, 1E-10, 0};

    t.get_time();

    shared_ptr<MPSInfo<S>> mps_info = make_shared<MPSInfo<S>>(
        hamil->n_sites, hamil->vacuum, target, hamil->basis);
    mps_info->set_bond_dimension(ket_bond_dim);
    mps_info->tag = "KET";

    // MPS
    Random::rand_seed(0);

    shared_ptr<MPS<S>> mps = make_shared<MPS<S>>(hamil->n_sites, 0, dot);
    mps->initialize(mps_info);
    mps->random_canonicalize();

    // MPS/MPSInfo save mutable
    mps->save_mutable();
    mps->deallocate();
    mps_info->save_mutable();
    mps_info->deallocate_mutable();

    // ME
    shared_ptr<MovingEnvironment<S>> me =
        make_shared<MovingEnvironment<S>>(mpo, mps, mps, "DMRG");
    me->init_environments(false);

    // DMRG
    shared_ptr<DMRG<S>> dmrg = make_shared<DMRG<S>>(me, ket_bdims, noises);
    dmrg->noise_type = NoiseTypes::ReducedPerturbative;
    dmrg->decomp_type = DecompositionTypes::SVD;
    double energy = dmrg->solve(20, mps->center == 0, 1E-12);

    cout << "== " << name << " (DMRG) ==" << setw(20) << target
         << " E = " << fixed << setw(22) << setprecision(12) << energy
         << " error = " << scientific << setprecision(3) << setw(10)
         << (energy - energy_std) << " T = " << fixed << setw(10)
         << setprecision(3) << t.get_time() << endl;

    // 1-site can be unstable
    EXPECT_LT(abs(energy - energy_std), dot == 1 ? 1E-4 : 1E-7);

    // MPO construction
    cout << "MPO ROT start" << endl;
    shared_ptr<MPO<S>> mpo_rot = make_shared<MPOQC<S>>(hamil_rot, QCTypes::NC);
    cout << "MPO ROT end .. T = " << t.get_time() << endl;

    // MPO simplification
    cout << "MPO ROT simplification start" << endl;
    mpo_rot = make_shared<SimplifiedMPO<S>>(
        mpo_rot, make_shared<AntiHermitianRuleQC<S>>(make_shared<RuleQC<S>>()),
        true);
    cout << "MPO ROT simplification end .. T = " << t.get_time() << endl;

    // MPO parallelization
    cout << "MPO parallelization start" << endl;
    mpo_rot = make_shared<ParallelMPO<S>>(mpo_rot, para_rule);
    cout << "MPO parallelization end .. T = " << t.get_time() << endl;

    double dt = 0.02;
    int n_steps = (int)(1.0 / dt + 0.1);
    shared_ptr<MovingEnvironment<S>> rme =
        make_shared<MovingEnvironment<S>>(mpo_rot, mps, mps, "ROT");
    rme->init_environments();
    shared_ptr<TimeEvolution<S>> te =
        make_shared<TimeEvolution<S>>(rme, bra_bdims, te_type);
    te->hermitian = false;
    te->iprint = 2;
    te->n_sub_sweeps = te->mode == TETypes::TangentSpace ? 1 : 2;
    te->normalize_mps = false;
    for (int i = 0; i < n_steps; i++) {
        if (te->mode == TETypes::TangentSpace)
            te->solve(2, -dt / 2, mps->center == 0);
        else
            te->solve(1, -dt, mps->center == 0);
        cout << setprecision(12);
        cout << i * dt << " " << te->energies.back() << " "
             << te->normsqs.back() << endl;
    }

    // MPO construction
    cout << "MPO MO start" << endl;
    shared_ptr<MPO<S>> mpo_c1 =
        make_shared<MPOQC<S>>(hamil_c1, QCTypes::Conventional);
    cout << "MPO MO end .. T = " << t.get_time() << endl;

    // MPO simplification
    cout << "MPO MO simplification start" << endl;
    mpo_c1 =
        make_shared<SimplifiedMPO<S>>(mpo_c1, make_shared<RuleQC<S>>(), true);
    cout << "MPO MO simplification end .. T = " << t.get_time() << endl;

    // MPO parallelization
    cout << "MPO parallelization start" << endl;
    mpo_c1 = make_shared<ParallelMPO<S>>(mpo_c1, para_rule);
    cout << "MPO parallelization end .. T = " << t.get_time() << endl;

    // ME
    shared_ptr<MovingEnvironment<S>> me_c1 =
        make_shared<MovingEnvironment<S>>(mpo_c1, mps, mps, "DMRG");
    me_c1->init_environments(false);

    shared_ptr<Expect<S>> ex =
        make_shared<Expect<S>>(me_c1, bra_bond_dim, bra_bond_dim);
    double ener_c1 = ex->solve(false);

    cout << "== " << name << " (DMRG) ==" << setw(20) << target
         << " E = " << fixed << setw(22) << setprecision(12) << ener_c1
         << " error = " << scientific << setprecision(3) << setw(10)
         << (ener_c1 - energy_std) << " T = " << fixed << setw(10)
         << setprecision(3) << t.get_time() << endl;

    EXPECT_LT(abs(ener_c1 - energy_std), tol);

    mpo_c1->deallocate();
    mpo_rot->deallocate();
    mps_info->deallocate();
    mpo->deallocate();
}

TEST_F(TestRotationH10STO6G, TestSU2) {
    shared_ptr<FCIDUMP> fcidump = make_shared<FCIDUMP>();
    PGTypes pg = PGTypes::C1;
    string filename = "data/H10.STO6G.R1.8.FCIDUMP.LOWDIN";
    string filename_c1 = "data/H10.STO6G.R1.8.FCIDUMP.C1";
    string filename_rot = "data/H10.STO6G.R1.8.ROTATION.LOWDIN";
    fcidump->read(filename);
    vector<uint8_t> orbsym = fcidump->orb_sym<uint8_t>();
    transform(orbsym.begin(), orbsym.end(), orbsym.begin(),
              PointGroup::swap_pg(pg));

    SU2 vacuum(0);
    SU2 target(fcidump->n_elec(), fcidump->twos(),
               PointGroup::swap_pg(pg)(fcidump->isym()));

    int norb = fcidump->n_sites();
    shared_ptr<HamiltonianQC<SU2>> hamil =
        make_shared<HamiltonianQC<SU2>>(vacuum, norb, orbsym, fcidump);

    shared_ptr<FCIDUMP> fcidump_rot = make_shared<FCIDUMP>();
    fcidump_rot->read(filename_rot);
    shared_ptr<HamiltonianQC<SU2>> hamil_rot =
        make_shared<HamiltonianQC<SU2>>(vacuum, norb, orbsym, fcidump_rot);

    shared_ptr<FCIDUMP> fcidump_c1 = make_shared<FCIDUMP>();
    fcidump_c1->read(filename_c1);
    shared_ptr<HamiltonianQC<SU2>> hamil_c1 =
        make_shared<HamiltonianQC<SU2>>(vacuum, norb, orbsym, fcidump_c1);

    test_dmrg<SU2>(target, hamil, hamil_rot, hamil_c1, "SU2/2-site/TS", 2,
                   TETypes::TangentSpace, 1E-7);
    test_dmrg<SU2>(target, hamil, hamil_rot, hamil_c1, "SU2/2-site/RK", 2,
                   TETypes::RK4, 1E-7);
    // test_dmrg<SU2>(target, hamil, hamil_rot, hamil_c1, "SU2/1-site", 1,
    //                TETypes::TangentSpace);

    hamil_rot->deallocate();
    hamil->deallocate();
    fcidump->deallocate();
}

TEST_F(TestRotationH10STO6G, TestSZ) {
    shared_ptr<FCIDUMP> fcidump = make_shared<FCIDUMP>();
    PGTypes pg = PGTypes::C1;
    string filename = "data/H10.STO6G.R1.8.FCIDUMP.LOWDIN";
    string filename_c1 = "data/H10.STO6G.R1.8.FCIDUMP.C1";
    string filename_rot = "data/H10.STO6G.R1.8.ROTATION.LOWDIN";
    fcidump->read(filename);
    vector<uint8_t> orbsym = fcidump->orb_sym<uint8_t>();
    transform(orbsym.begin(), orbsym.end(), orbsym.begin(),
              PointGroup::swap_pg(pg));

    SZ vacuum(0);
    SZ target(fcidump->n_elec(), fcidump->twos(),
              PointGroup::swap_pg(pg)(fcidump->isym()));

    int norb = fcidump->n_sites();
    shared_ptr<HamiltonianQC<SZ>> hamil =
        make_shared<HamiltonianQC<SZ>>(vacuum, norb, orbsym, fcidump);

    shared_ptr<FCIDUMP> fcidump_rot = make_shared<FCIDUMP>();
    fcidump_rot->read(filename_rot);
    shared_ptr<HamiltonianQC<SZ>> hamil_rot =
        make_shared<HamiltonianQC<SZ>>(vacuum, norb, orbsym, fcidump_rot);

    shared_ptr<FCIDUMP> fcidump_c1 = make_shared<FCIDUMP>();
    fcidump_c1->read(filename_c1);
    shared_ptr<HamiltonianQC<SZ>> hamil_c1 =
        make_shared<HamiltonianQC<SZ>>(vacuum, norb, orbsym, fcidump_c1);

    test_dmrg<SZ>(target, hamil, hamil_rot, hamil_c1, "SZ/2-site/TS", 2,
                  TETypes::TangentSpace, 1E-5);
    test_dmrg<SZ>(target, hamil, hamil_rot, hamil_c1, "SZ/2-site/RK", 2,
                  TETypes::RK4, 1E-5);
    // test_dmrg<SZ>(target, hamil, hamil_rot, hamil_c1, "SZ/1-site", 1,
    //               TETypes::TangentSpace);

    hamil_rot->deallocate();
    hamil->deallocate();
    fcidump->deallocate();
}

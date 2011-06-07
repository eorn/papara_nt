#include "ivymike/multiple_alignment.h"
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <vector>
#include <deque>
#include <map>
#include <functional>
#include <cstring>
#include <boost/io/ios_state.hpp>
#include <boost/numeric/ublas/matrix.hpp>
#include <boost/numeric/ublas/banded.hpp>
#include <boost/numeric/ublas/io.hpp>
#include <boost/numeric/ublas/lu.hpp>
#include <boost/numeric/ublas/triangular.hpp>

#include <EigenvalueDecomposition.hpp>


#include "parsimony.h"
#include "pars_align_vec.h"
#include "pars_align_seq.h"
#include "fasta.h"


#include "ivymike/tree_parser.h"
#include "ivymike/time.h"
#include "ivymike/getopt.h"
#include "ivymike/thread.h"
#include "ivymike/demangle.h"
#include "ivymike/stupid_ptr.h"

using namespace ivy_mike;
using namespace ivy_mike::tree_parser_ms;

using namespace boost::numeric;

class ostream_test {
    std::ostream &m_os;

public:
    ostream_test( std::ostream &os ) : m_os(os) {}
    void operator()(int i) {
        m_os << i;
    }
};

typedef int parsimony_state;


// this class is so strange, we could name a new design-pattern after it.
// it does something like a static {} block in java...
class dna_parsimony_mapping_real {
    std::vector<uint8_t> m_p2d;
    std::vector<parsimony_state> m_d2p;

public:
    dna_parsimony_mapping_real() : m_p2d(16), m_d2p(256, -1)
    {
        const uint8_t  pd[18] =  {'_', 'A', 'C', 'M', 'G', 'R', 'S', 'V', 'T', 'U', 'W', 'Y', 'H', 'K', 'D', 'B', '-', 'N'};
        const uint32_t pds[18] = { 0,   1,   2,   3,   4,   5,   6,   7,   8,    8,   9,  10,  11,  12,  13,  14,  15,  15};
        //m_p2d.assign( pd, pd + 16 );


        // this is some weird code, which is supposed tp setup the dna->pstate and pstate->dna maps from ps and pds...
        for( int i = 0; i < 18; i++ ) {
            if( pds[i] >= m_p2d.size() ) {
                throw std::runtime_error( "dna/parsimony map initialization fsck'ed up" );
            }

            m_d2p[pd[i]] = pds[i];
            m_d2p[std::tolower(pd[i])] = pds[i];

            if( m_p2d[pds[i]] == 0 ) {
                m_p2d[pds[i]] = pd[i];
            }
        }

    }

    static dna_parsimony_mapping_real s_pdm;

    static uint8_t p2d( parsimony_state c ) {
        if( c < 0 || c > 15 ) {
            throw std::runtime_error( "illegal parsimony state" );
        }

        return s_pdm.m_p2d[c];
    }

    static parsimony_state d2p( uint8_t c ) {
        if( s_pdm.m_d2p[c] == -1 ) {

//             std::cerr << "illegal: " << c << "\n";
//             throw std::runtime_error( "illegal dna character" );
            return 0xf; // default to undefined pars state
        }

        return s_pdm.m_d2p[c];
    }

    static int d2aux( uint8_t c ) {
        parsimony_state ps = d2p(c);
        if( ps == 0xf ) {
            return AUX_CGAP;
        } else {
            return 0;
        }
    }

    static bool is_gap( uint8_t c ) {
        return d2p(c) == 0xf;
    }
};

dna_parsimony_mapping_real dna_parsimony_mapping_real::s_pdm;


struct dna_parsimony_mapping_simple {
    static parsimony_state d2p( uint8_t c ) {

        switch( c ) {
        case 'A':
        case 'a':
            return 0x1;

        case 'C':
        case 'c':
            return 0x2;

        case 'G':
        case 'g':
            return 0x4;

        case 'U':
        case 'u':
        case 'T':
        case 't':
            return 0x8;

        default:
            return 0xf;
        };
    }

    static uint8_t p2d( parsimony_state c ) {
        switch( c ) {
        case 0x1:
            return 'A';
        case 0x2:
            return 'C';
        case 0x4:
            return 'G';
        case 0x8:
            return 'T';
        case 0xf:
            return '-';

        default:
            return 'X';
        };
    }
    static int d2aux( uint8_t c ) {
        parsimony_state ps = d2p(c);
        if( ps == 0xf ) {
            return AUX_CGAP;
        } else {
            return 0;
        }
    }
};

typedef dna_parsimony_mapping_real dna_parsimony_mapping;

// static inline parsimony_state dna_to_parsimony_state( uint8_t c ) {
//     switch( c ) {
//         case 'A':
//         case 'a':
//             return 0x1;
//
//         case 'C':
//         case 'c':
//             return 0x2;
//
//         case 'G':
//         case 'g':
//             return 0x4;
//
//         case 'U':
//         case 'u':
//         case 'T':
//         case 't':
//             return 0x8;
//
//         default:
//             return 0xf;
//     };
//
// }
//
//
// static inline int dna_to_cgap( uint8_t c ) {
//     switch( c ) {
//     case 'A':
//     case 'a':
//     case 'C':
//     case 'c':
//     case 'G':
//     case 'g':
//     case 'T':
//     case 't':
//     case 'U':
//     case 'u':
//         return 0x0;
//
//     default:
//         return AUX_CGAP;
//     };
// }

static bool g_dump_aux = false;

class pvec_cgap {
    //     aligned_buffer<parsimony_state> v;
    std::vector<parsimony_state> v;
    std::vector<int> auxv;

public:
    void init( const std::vector<uint8_t> &seq ) {
        assert( v.size() == 0 );
        v.resize(seq.size());
        auxv.resize( seq.size() );
        std::transform( seq.begin(), seq.end(), v.begin(), dna_parsimony_mapping::d2p );
        std::transform( seq.begin(), seq.end(), auxv.begin(), dna_parsimony_mapping::d2aux );
    }

    static void newview( pvec_cgap &p, pvec_cgap &c1, pvec_cgap &c2, double /*z1*/, double /*z2*/, tip_case tc ) {
        assert( c1.v.size() == c2.v.size() );

//         p.v.resize(0);
        p.v.resize(c1.v.size());
        p.auxv.resize(c1.auxv.size());

        if( g_dump_aux ) {
            std::cout << "1:";
            std::copy( c1.auxv.begin(), c1.auxv.end(), std::ostream_iterator<int>(std::cout) );
            std::cout << "\n2:";
            std::copy( c2.auxv.begin(), c2.auxv.end(), std::ostream_iterator<int>(std::cout) );
            std::cout << "\n";
        }

        for( size_t i = 0; i < c1.v.size(); i++ ) {
            parsimony_state ps = c1.v[i] & c2.v[i];

            if( ps == 0 ) {
                ps = c1.v[i] | c2.v[i];
            }

            //p.v.push_back( ps );
            p.v[i] = ps;



            const int a1 = c1.auxv[i];
            const int a2 = c2.auxv[i];

            const bool cgap1 = (a1 & AUX_CGAP) != 0;
            const bool cgap2 = (a2 & AUX_CGAP) != 0;

//             const bool open1 = (a1 & AUX_OPEN) != 0;
//             const bool open2 = (a2 & AUX_OPEN) != 0;

            p.auxv[i] = 0;

            if( tc == TIP_TIP ) {
                if( cgap1 && cgap2 ) {
                    p.auxv[i] = AUX_CGAP;
                } else if( cgap1 != cgap2 ) {
                    p.auxv[i] = AUX_CGAP | AUX_OPEN;
                }
            } else if( tc == TIP_INNER ) {
                if( cgap1 && cgap2 ) {
                    p.auxv[i] = AUX_CGAP;
                } else if( cgap1 != cgap2 ) {
                    p.auxv[i] = AUX_CGAP | AUX_OPEN;
                }
            } else {
                if( a1 == AUX_CGAP && a2 == AUX_CGAP ) {
                    p.auxv[i] = AUX_CGAP;
                } else if( a1 == AUX_CGAP || a2 == AUX_CGAP ) {
                    p.auxv[i] = AUX_CGAP | AUX_OPEN;
                }
            }

        }
    }

    inline size_t size() {
        return v.size();
    }

    inline void to_int_vec( std::vector<int> &outv ) {

        outv.resize( v.size() );

        std::copy( v.begin(), v.end(), outv.begin() );
    }

    inline void to_aux_vec( std::vector<unsigned int> &outv ) {
//         std::cout << "v: " << v.size() << "\n";

        outv.resize( v.size() );
       std::copy( auxv.begin(), auxv.end(), outv.begin() );


//         std::for_each( auxv.begin(), auxv.end(), ostream_test(std::cout) );


    }
};

class probgap_model {
    ublas::matrix<double> m_evecs;
    ublas::vector<double> m_evals;
    ublas::matrix<double> m_evecs_inv;

    ublas::diagonal_matrix<double> m_evals_diag; // used temporarily.
    ublas::matrix<double> m_prob_matrix;
    ublas::matrix<double> m_temp_matrix;
    double calc_gap_freq ( const std::vector< std::vector< uint8_t > > &seqs ) {
        size_t ngaps = 0;
        size_t nres = 0;

        for( std::vector< std::vector< uint8_t > >::const_iterator it = seqs.begin(); it != seqs.end(); ++it ) {
            nres += it->size();
            ngaps += std::count_if( it->begin(), it->end(), dna_parsimony_mapping::is_gap );
        }

        double rgap = double(ngaps) / nres;
        std::cout << "gap rate: " << ngaps << " " << nres << "\n";
        std::cout << "gap rate: " << rgap << "\n";
        return rgap;
    }

public:
    probgap_model( const std::vector< std::vector<uint8_t> > &seqs ) : m_evals_diag(2), m_prob_matrix(2,2), m_temp_matrix(2,2) {
        // initialize probgap model from input sequences

        double gap_freq = calc_gap_freq( seqs );

        double f[2] = {1-gap_freq, gap_freq};

        ublas::matrix<double> rate_matrix(2,2);
        rate_matrix(0,0) = -f[0];
        rate_matrix(0,1) = f[0];
        rate_matrix(1,0) = f[1];
        rate_matrix(1,1) = -f[1];

        ublas::EigenvalueDecomposition ed(rate_matrix);

        m_evecs = ed.getV();
        m_evals = ed.getRealEigenvalues();

        // use builtin ublas lu-factorization rather than jama
        {
            ublas::matrix<double> A(m_evecs);
            ublas::permutation_matrix<size_t> pm( A.size1() );
            size_t res = ublas::lu_factorize(A,pm);
            if( res != 0 ) {
                throw std::runtime_error( " ublas::lu_factorize failed" );
            }
            m_evecs_inv = ublas::identity_matrix<double>( A.size1());

            ublas::lu_substitute(A,pm,m_evecs_inv);
        }
//         ublas::LUDecomposition lud(m_evecs);
//         m_evecs_inv = lud.pseudoinverse();
//         std::cout << "inv jama: " << m_evecs_inv << "\n";
    }

    const ublas::matrix<double> &setup_pmatrix( double t ) {

        for( int i = 0; i < 2; i++ ) {
            m_evals_diag(i,i) = exp( t * m_evals(i));
        }

//         m_prob_matrix = ublas::prod( m_evecs, m_evals_diag );
//         m_prob_matrix = ublas::prod( m_prob_matrix, m_evecs_inv );
        m_prob_matrix = ublas::prod( ublas::prod( m_evecs, m_evals_diag, m_temp_matrix ), m_evecs_inv );


//         std::cout << "pmatrix: " << m_prob_matrix << "\n";



//         throw std::runtime_error( "xxx" );

        return m_prob_matrix;
    }




};

class pvec_pgap {
    //     aligned_buffer<parsimony_state> v;
    std::vector<parsimony_state> v;
    ublas::matrix<double> gap_prob;

public:
    static stupid_ptr<probgap_model> pgap_model;

    void init( const std::vector<uint8_t> &seq ) {
        assert( v.size() == 0 );
        v.resize(seq.size());

        std::transform( seq.begin(), seq.end(), v.begin(), dna_parsimony_mapping::d2p );
        //std::transform( seq.begin(), seq.end(), auxv.begin(), dna_parsimony_mapping::d2aux );

        gap_prob.resize(2, seq.size());

        for( size_t i = 0; i < seq.size(); i++ ) {
            // ( 0 )
            // ( 1 ) means gap


            if( v[i] == 0xf ) {
                gap_prob( 0, i ) = 0.0;
                gap_prob( 1, i ) = 1.0;
            } else {
                gap_prob( 0, i ) = 1.0;
                gap_prob( 1, i ) = 0.0;
            }
        }

    }

    static void newview( pvec_pgap &p, pvec_pgap &c1, pvec_pgap &c2, double z1, double z2, tip_case tc ) {
        assert( c1.v.size() == c2.v.size() );

//         p.v.resize(0);
        p.v.resize(c1.v.size());
        //p.gap_prob.resize(2, c1.v.size());

        assert( pgap_model.is_valid_ptr() );

        ublas::matrix<double> p1 = pgap_model->setup_pmatrix(z1);
        ublas::matrix<double> p2 = pgap_model->setup_pmatrix(z2);

        ublas::matrix<double> t1 = ublas::prod(p1, c1.gap_prob);
        ublas::matrix<double> t2 = ublas::prod(p2, c2.gap_prob);

        p.gap_prob = ublas::element_prod( t1, t2 );
        //ublas::matrix< double >::const_iterator1 xxx = p.gap_prob.begin1();
       // xxx.

        //std::cout << "pvec: " << *xxx << "\n";

//         ublas::matrix< double >::iterator1 tit1 = p.gap_prob.begin1();
//
//
//         boost::io::ios_all_saver ioss(std::cout);
//         std::cout << std::setprecision(2);
//
//         std::cout << std::left;
//
// //         std::transform( tit1.begin(), tit1.end(), tit1.begin(), log );
//         std::copy( tit1.begin(), tit1.end(), std::ostream_iterator<double>(std::cout, " "));
//         std::cout << "\n";
//         ++tit1;
// //         std::transform( tit1.begin(), tit1.end(), tit1.begin(), log );
//         std::copy( tit1.begin(), tit1.end() + 4, std::ostream_iterator<double>(std::cout, " "));
//         std::cout << "\n";


        for( size_t i = 0; i < c1.v.size(); i++ ) {
            parsimony_state ps = c1.v[i] & c2.v[i];

            if( ps == 0 ) {
                ps = c1.v[i] | c2.v[i];
            }

            //p.v.push_back( ps );
            p.v[i] = ps;
        }
    }

    inline size_t size() {
        return v.size();
    }
        inline void to_int_vec( std::vector<int> &outv ) {

        outv.resize( v.size() );

        std::copy( v.begin(), v.end(), outv.begin() );
    }

    inline void to_aux_vec( std::vector<unsigned int> &outv ) {
//         std::cout << "v: " << v.size() << "\n";

        outv.resize( v.size() );
        std::fill( outv.begin(), outv.end(), 0 );

//         std::for_each( auxv.begin(), auxv.end(), ostream_test(std::cout) );



        ublas::matrix<double> t = get_pgap();

        // yeah! metaprogramming massacre!

        ublas::matrix< double >::iterator1 tit1 = t.begin1();
        std::vector<double> odds;


        for( ublas::matrix< double >::iterator2 tit = t.begin2(); tit != t.end2(); ++tit ) {
            odds.push_back( *tit / *(tit.begin()+1));
        }
        std::transform( odds.begin(), odds.end(), odds.begin(), std::ptr_fun<double>(log) );

        std::vector<bool> bias(odds.size());
        std::transform( odds.begin(), odds.end(), bias.begin(), std::bind1st( std::greater<double>(), 0.0 ) );
        //std::transform( bias.begin(), bias.end(), outv.begin(), std::bind1st( std::multiplies<int>(), 3 ) );
        std::copy( bias.begin(), bias.end(), outv.begin());
//         std::vector<uint8_t> lomag(odds.size());
//         std::transform( odds.begin(), odds.end(), lomag.begin(), to_hex );


#if 0
//         std::transform( t.begin2(), t.end2(), odds.begin(), calc_odds<ublas::matrix< double > > );

        //boost::io::ios_all_saver ioss(std::cout);
        std::cout << std::setprecision(0);
//         std::cout << std::setw(5);
        std::cout << std::fixed;
        std::cout << std::left;

//         std::cout << t(0,0) << t(0,1) << t(0,2) <<t(0,3) <<t(0,4) << "\n";


        //std::copy( odds.begin(), odds.end(), std::ostream_iterator<double>(std::cout, " "));
        std::copy( bias.begin(), bias.end(), std::ostream_iterator<int>(std::cout));
        std::cout << "\n";

        std::copy( lomag.begin(), lomag.end(), std::ostream_iterator<unsigned char>(std::cout));
        std::cout << "\n";



        std::transform( tit1.begin(), tit1.end(), tit1.begin(), log );
        std::copy( tit1.begin(), tit1.end(), std::ostream_iterator<double>(std::cout, " "));
        std::cout << "\n";
        ++tit1;
        std::transform( tit1.begin(), tit1.end(), tit1.begin(), log );
        std::copy( tit1.begin(), tit1.end(), std::ostream_iterator<double>(std::cout, " "));
        std::cout << "\n";
#endif

    }
    const ublas::matrix<double> get_pgap() {
        return gap_prob;
    }


};
stupid_ptr<probgap_model> pvec_pgap::pgap_model;


// probgap_model *pvec_pgap::pgap_model = 0;

// class pvec_ugly {
//     parsimony_state *m_v;
//     size_t m_size;
//
// public:
//     pvec_ugly() : m_v(0), m_size(0) {}
//     ~pvec_ugly() {
//         delete[] m_v;
//     }
//     void init( const std::vector<uint8_t> &seq ) {
//         assert( m_v == 0 );
//         delete[] m_v;
//         m_size = seq.size(0);
//         m_v = new parsimony_state[m_size];
//
//         std::transform( seq.begin(), seq.end(), m_v, dna_to_parsimony_state );
//     }
//
//     static void newview( pvec_ugly &p, pvec_ugly &c1, pvec_ugly &c2 ) {
//         assert( c1.m_size() == c2.m_size() );
//
// //         p.v.resize(0);
//         //p.v.resize(c1.v.size());
// #error continue here!
//
//         for( size_t i = 0; i < c1.v.size(); i++ ) {
//             parsimony_state ps = c1.v[i] & c2.v[i];
//
//             if( ps == 0 ) {
//                 ps = c1.v[i] | c2.v[i];
//             }
//
//             //p.v.push_back( ps );
//             p.v[i] = ps;
//
//         }
//     }
//
// };


template<class pvec_t>
class my_adata_gen : public ivy_mike::tree_parser_ms::adata {
//     static int ct;
    //std::vector<parsimony_state> m_pvec;
    pvec_t m_pvec;

public:
//     int m_ct;
    my_adata_gen() {

//         std::cout << "my_adata\n";

    }

    virtual ~my_adata_gen() {

//         std::cout << "~my_adata\n";

    }

    virtual void visit() {
//         std::cout << "tr: " << m_ct << "\n";
    }
    void init_pvec(const std::vector< uint8_t >& seq) {


        m_pvec.init( seq );
//         std::cout << "init_pvec: " << m_pvec.size() << "\n";
//                 m_pvec.reserve(seq.size());
//         for( std::vector< uint8_t >::const_iterator it = seq.begin(); it != seq.end(); ++it ) {
//             m_pvec.push_back(dna_to_parsimony_state(*it));
//
//         }
    }
    pvec_t &get_pvec() {
        return m_pvec;
    }


};

// inline void newview_parsimony( std::vector<parsimony_state> &p, const std::vector<parsimony_state> &c1, const std::vector<parsimony_state> &c2 ) {
//
// }



// inline std::ostream &operator<<( std::ostream &os, const my_adata &rb ) {
//
//     os << "my_adata: " << rb.m_ct;
// }

template<class ndata_t>
class my_fact : public ivy_mike::tree_parser_ms::node_data_factory {

    virtual ndata_t *alloc_adata() {

        return new ndata_t;
    }

};



template<class lnode>
void traverse_rec( lnode *n ) {

    n->m_data->visit();

    if( n->next->back != 0 ) {
        traverse_rec(n->next->back);
    }

    if( n->next->next->back != 0 ) {
        traverse_rec(n->next->next->back);
    }
}

// template<class lnode>
// void traverse( lnode *n ) {
//     n->m_data->visit();
//
//     if( n->back != 0 ) {
//         traverse_rec(n->back);
//     }
//
//     if( n->next->back != 0 ) {
//         traverse_rec(n->next->back);
//     }
//
//     if( n->next->next->back != 0 ) {
//         traverse_rec(n->next->next->back);
//     }
//
//
// }


uint8_t to_hex( double v ) {
    int vi = int(fabs(v));

    vi = std::min( vi, 15 );

    if( vi <= 9 ) {
        return '0' + vi;
    } else {
        return 'a' + (vi - 10);
    }

}


template<class pvec_t>
void do_newview( pvec_t &root_pvec, lnode *n1, lnode *n2, bool incremental ) {
    typedef my_adata_gen<pvec_t> my_adata;

    std::deque<rooted_bifurcation<lnode> > trav_order;

    //std::cout << "traversal for branch: " << *(n1->m_data) << " " << *(n2->m_data) << "\n";

    rooted_traveral_order( n1, n2, trav_order, incremental );
//     std::cout << "traversal: " << trav_order.size() << "\n";

    for( std::deque< rooted_bifurcation< ivy_mike::tree_parser_ms::lnode > >::iterator it = trav_order.begin(); it != trav_order.end(); ++it ) {
//         std::cout << *it << "\n";

        my_adata *p = dynamic_cast<my_adata *>( it->parent->m_data.get());
        my_adata *c1 = dynamic_cast<my_adata *>( it->child1->m_data.get());
        my_adata *c2 = dynamic_cast<my_adata *>( it->child2->m_data.get());
//         rooted_bifurcation<ivy_mike::tree_parser_ms::lnode>::tip_case tc = it->tc;

//         std::cout << "tip case: " << (*it) << "\n";
        pvec_t::newview(p->get_pvec(), c1->get_pvec(), c2->get_pvec(), it->child1->backLen, it->child2->backLen, it->tc);

    }





    {
        my_adata *c1 = dynamic_cast<my_adata *>( n1->m_data.get());
        my_adata *c2 = dynamic_cast<my_adata *>( n2->m_data.get());

//         tip_case tc;

        if( c1->isTip && c2->isTip ) {
//                 std::cout << "root: TIP TIP\n";
            pvec_t::newview(root_pvec, c1->get_pvec(), c2->get_pvec(), n1->backLen, n2->backLen, TIP_TIP );
        } else if( c1->isTip && !c2->isTip ) {
//                 std::cout << "root: TIP INNER\n";
            pvec_t::newview(root_pvec, c1->get_pvec(), c2->get_pvec(), n1->backLen, n2->backLen, TIP_INNER );
//             root_pvec = c2->get_pvec();
        } else if( !c1->isTip && c2->isTip ) {
//                 std::cout << "root: INNER TIP\n";
            pvec_t::newview(root_pvec, c2->get_pvec(), c1->get_pvec(), n1->backLen, n2->backLen, TIP_INNER );
//             root_pvec = c1->get_pvec();
        } else {
//                 std::cout << "root: INNER INNER\n";
            pvec_t::newview(root_pvec, c1->get_pvec(), c2->get_pvec(), n1->backLen, n2->backLen, INNER_INNER );
        }


    }
//     std::cout << std::hex;
//     for( std::vector< parsimony_state >::const_iterator it = root_pvec.begin(); it != root_pvec.end(); ++it ) {
//         std::cout << *it;
//     }
//
//     std::cout << std::dec << std::endl;

}


static void seq_to_nongappy_pvec( std::vector<uint8_t> &seq, std::vector<uint8_t> &pvec ) {
    pvec.resize( 0 );

    for( unsigned int i = 0; i < seq.size(); i++ ) {
        uint8_t ps = dna_parsimony_mapping::d2p(seq[i]);

        if( ps == 0x1 || ps == 0x2 || ps == 0x4 || ps == 0x8 ) {
            pvec.push_back(ps);
        }

    }

}

void pairwise_seq_distance( std::vector< std::vector<uint8_t> > &seq );

class papara_nt_i {
public:
    virtual ~papara_nt_i() {}

    virtual void calc_scores( size_t ) = 0;
    virtual void print_best_scores( std::ostream & ) = 0;
    virtual void write_result_phylip( std::ostream &, std::ostream &) = 0;
};


template<typename pvec_t>
class papara_nt : public papara_nt_i {

    //typedef pvec_pgap pvec_t;
    typedef my_adata_gen<pvec_t> my_adata;


    const static size_t VW = pars_align_vec::WIDTH;

    struct block_t {
        block_t() {
            memset( this, 0, sizeof( block_t )); // FIXME: hmm, this is still legal?
        }

        // WARNING: these are pointers into m_ref_pvecs and m_ref_aux
        // make sure they stay valid!
        const int *seqptrs[VW];
        const unsigned int *auxptrs[VW];
        size_t ref_len;
        int edges[VW];
        int num_valid;
    };

    papara_nt( const papara_nt &other );
    papara_nt & operator=( const papara_nt &other );



    //multiple_alignment m_ref_ma;

    std::vector <std::string > m_ref_names;
    std::vector <std::vector<uint8_t> > m_ref_seqs;
    std::auto_ptr<ivy_mike::tree_parser_ms::ln_pool> m_ln_pool;
    edge_collector<lnode> m_ec;

    std::vector <std::string> m_qs_names;
    std::vector <std::vector<uint8_t> > m_qs_seqs;

    std::vector<std::vector <uint8_t> > m_qs_pvecs;

    std::vector<std::vector <int> > m_ref_pvecs;
    std::vector<std::vector <unsigned int> > m_ref_aux;

    ivy_mike::mutex m_qmtx; // mutex for the block queue and the qs best score/edge arrays
    std::deque<block_t> m_blockqueue;
    std::vector <int> m_qs_bestscore;
    std::vector <int> m_qs_bestedge;




    class worker {
        papara_nt &m_pnt;
        size_t m_rank;
    public:
        worker( papara_nt & pnt, size_t rank ) : m_pnt(pnt), m_rank(rank) {}
        void operator()() {

            pars_align_vec::arrays<VW> arrays;
            pars_align_seq::arrays seq_arrays(true);

            ivy_mike::timer tstatus;
            double last_tstatus = 0.0;
            while( true ) {
                block_t block;

                {
                    ivy_mike::lock_guard<ivy_mike::mutex> lock( m_pnt.m_qmtx );
                    if( m_pnt.m_blockqueue.empty() ) {
                        break;
                    }
                    block = m_pnt.m_blockqueue.front();
                    m_pnt.m_blockqueue.pop_front();

                    if( m_rank == 0 && (tstatus.elapsed() - last_tstatus) > 10.0 ) {
                        std::cerr << tstatus.elapsed() << " " << m_pnt.m_blockqueue.size() << " blocks remaining\n";
                        last_tstatus = tstatus.elapsed();
                    }
                }




                for( unsigned int i = 0; i < m_pnt.m_qs_names.size(); i++ ) {

                    size_t stride = 1;
                    size_t aux_stride = 1;
                    pars_align_vec pa( block.seqptrs, m_pnt.m_qs_pvecs[i].data(), block.ref_len, m_pnt.m_qs_pvecs[i].size(), stride, block.auxptrs, aux_stride, arrays, 0 );


                    pars_align_vec::score_t *score_vec = pa.align_freeshift();

                    {
                        ivy_mike::lock_guard<ivy_mike::mutex> lock( m_pnt.m_qmtx );

                        for( int k = 0; k < block.num_valid; k++ ) {



                            if( score_vec[k] < m_pnt.m_qs_bestscore[i] || (score_vec[k] == m_pnt.m_qs_bestscore[i] && block.edges[k] < m_pnt.m_qs_bestedge[i] )) {
                                const bool validate = false;
                                if( validate ) {
                                    const int *seqptr = block.seqptrs[k];
                                    const unsigned int *auxptr = block.auxptrs[k];

                                    pars_align_seq pas( seqptr, m_pnt.m_qs_pvecs[i].data(), block.ref_len, m_pnt.m_qs_pvecs[i].size(), stride, auxptr, aux_stride, seq_arrays, 0 );
                                    int res = pas.alignFreeshift(INT_MAX);

                                    if( res != score_vec[k] ) {


                                        std::cout << "meeeeeeep! score: " << score_vec[k] << " " << res << "\n";
                                    }
                                }

                                m_pnt.m_qs_bestscore[i] = score_vec[k];
                                m_pnt.m_qs_bestedge[i] = block.edges[k];
                            }
                        }
                    }
                }
            }
        }
    };

    void build_block_queue() {
        // creates the list of ref-block to be consumed by the worker threads.  A ref-block onsists of N ancestral state sequences, where N='width of the vector unit'.
        // The vectorized alignment implementation will align a QS against a whole ref-block at a time, rather than a single ancestral state sequence as in the
        // sequencial algorithm.

        assert( m_blockqueue.empty() );
        size_t n_groups = (m_ec.m_edges.size() / VW);
        if( (m_ec.m_edges.size() % VW) != 0 ) {
            n_groups++;
        }


//         std::vector<int> seqlist[VW];
//         const int *seqptrs[VW];
//         std::vector<unsigned int> auxlist[VW];
//         const unsigned int *auxptrs[VW];



        for ( size_t j = 0; j < n_groups; j++ ) {
            int num_valid = 0;



            block_t block;

            for( unsigned int i = 0; i < VW; i++ ) {

                unsigned int edge = j * VW + i;
                if( edge < m_ec.m_edges.size()) {
                    block.edges[i] = edge;
                    block.num_valid++;

                    block.seqptrs[i] = m_ref_pvecs[edge].data();
                    block.auxptrs[i] = m_ref_aux[edge].data();
                    block.ref_len = m_ref_pvecs[edge].size();
                    //                     do_newview( root_pvec, m_ec.m_edges[edge].first, m_ec.m_edges[edge].second, true );
//                     root_pvec.to_int_vec(seqlist[i]);
//                     root_pvec.to_aux_vec(auxlist[i]);
//
//                     seqptrs[i] = seqlist[i].data();
//                     auxptrs[i] = auxlist[i].data();

                    num_valid++;
                } else {
                    if( i < 1 ) {
                        std::cout << "edge: " << edge << " " << m_ec.m_edges.size() << std::endl;

                        throw std::runtime_error( "bad integer mathematics" );
                    }
                    block.edges[i] = block.edges[i-1];

                    block.seqptrs[i] = block.seqptrs[i-1];
                    block.auxptrs[i] = block.auxptrs[i-1];
                }

            }
            m_blockqueue.push_back(block);
        }
    }

    void build_ref_vecs() {
        // pre-create the ancestral state vectors. This step is necessary for the threaded version, because otherwise, each
        // thread would need an independent copy of the tree to do concurrent newviews. Anyway, having a copy of the tree
        // in each thread will most likely use more memory than storing the pre-calculated vectors.

        // TODO: maybe try lazy create/cache of the asv's in the threads

        assert( m_ref_aux.empty() && m_ref_pvecs.empty() );

        m_ref_pvecs.reserve( m_ec.m_edges.size() );
        m_ref_aux.reserve( m_ec.m_edges.size() );


        for( size_t i = 0; i < m_ec.m_edges.size(); i++ ) {
            pvec_t root_pvec;

            std::cout << "newview for branch " << i << ": " << *(m_ec.m_edges[i].first->m_data) << " " << *(m_ec.m_edges[i].second->m_data) << "\n";

            if( i == 340 ) {
                g_dump_aux = true;
            }

            do_newview( root_pvec, m_ec.m_edges[i].first, m_ec.m_edges[i].second, true );

            g_dump_aux = false;
            // TODO: try something fancy with rvalue refs...

            m_ref_pvecs.push_back( std::vector<int>() );
            m_ref_aux.push_back( std::vector<unsigned int>() );

            root_pvec.to_int_vec(m_ref_pvecs.back());
            root_pvec.to_aux_vec(m_ref_aux.back());


        }

    }

public:



    papara_nt( const char* opt_tree_name, const char *opt_alignment_name, const char *opt_qs_name )
      : m_ln_pool(new ivy_mike::tree_parser_ms::ln_pool( sptr::shared_ptr<my_fact<my_adata> >( new my_fact<my_adata> )))
    {

            //std::cerr << "papara_nt instantiated as: " << typeid(*this).name() << "\n";
        std::cerr << "papara_nt instantiated as: " << ivy_mike::demangle(typeid(*this).name()) << "\n";

        // load input data: ref-tree, ref-alignment and query sequences

        //
        // parse the reference tree
        //


        ln_pool &pool = *m_ln_pool;
        tree_parser_ms::parser tp( opt_tree_name, pool );
        tree_parser_ms::lnode * n = tp.parse();

        n = towards_tree( n );
        //
        // create map from tip names to tip nodes
        //
        typedef tip_collector<lnode> tc_t;
        tc_t tc;

        visit_lnode( n, tc );

        std::map<std::string, sptr::shared_ptr<lnode> > name_to_lnode;

        for( std::vector< sptr::shared_ptr<lnode> >::iterator it = tc.m_nodes.begin(); it != tc.m_nodes.end(); ++it ) {
            std::cout << (*it)->m_data->tipName << "\n";
            name_to_lnode[(*it)->m_data->tipName] = *it;
        }


        {
            //
            // read reference alignment: store the ref-seqs in the tips of the ref-tree
            //
            multiple_alignment ref_ma;
            ref_ma.load_phylip( opt_alignment_name );



            for( unsigned int i = 0; i < ref_ma.names.size(); i++ ) {

                std::map< std::string, sptr::shared_ptr<lnode> >::iterator it = name_to_lnode.find(ref_ma.names[i]);

                if( it != name_to_lnode.end() ) {
                    sptr::shared_ptr< lnode > ln = it->second;
                    //      adata *ad = ln->m_data.get();

                    assert( typeid(*ln->m_data.get()) == typeid(my_adata ) );
                    my_adata *adata = static_cast<my_adata *> (ln->m_data.get());

                    m_ref_names.push_back(std::string() );
                    m_ref_seqs.push_back(std::vector<uint8_t>() );

                    m_ref_names.back().swap( ref_ma.names[i] );
                    m_ref_seqs.back().swap( ref_ma.data[i] );

                    // WARNING: make sure not to keep references to elements of m_ref_seqs at this point!
                    adata->init_pvec( m_ref_seqs.back() );
                } else {
                    m_qs_names.push_back(std::string() );
                    m_qs_seqs.push_back(std::vector<uint8_t>() );

                    m_qs_names.back().swap( ref_ma.names[i] );
                    m_qs_seqs.back().swap( ref_ma.data[i] );
                }
            }
        }
        probgap_model pm( m_ref_seqs );

        std::cout << "p: " << pm.setup_pmatrix(0.1) << "\n";

        stupid_ptr_guard<probgap_model> spg( pvec_pgap::pgap_model, &pm );

        //
        // collect list of edges
        //

        visit_edges( n, m_ec );

        std::cout << "edges: " << m_ec.m_edges.size() << "\n";

//         std::vector< pvec_t > m_parsvecs;
//         m_parsvecs.resize( m_ec.m_edges.size() );


        //
        // read query sequences
        //

        if( opt_qs_name != 0 ) {
            std::ifstream qsf( opt_qs_name );

            // mix them with the qs from the ref alignment
            read_fasta( qsf, m_qs_names, m_qs_seqs);
        }

        if( m_qs_names.empty() ) {
            throw std::runtime_error( "no qs" );
        }

        //
        // setup qs best-score/best-edge lists
        //


        m_qs_pvecs.resize( m_qs_names.size() );

        m_qs_bestscore.resize(m_qs_names.size());
        std::fill( m_qs_bestscore.begin(), m_qs_bestscore.end(), 32000);
        m_qs_bestedge.resize(m_qs_names.size());

        //
        // pre-create the reference pvecs/auxvecs
        //

        build_ref_vecs();


        //
        // preprocess query sequences
        //

        for( size_t i = 0; i < m_qs_seqs.size(); i++ ) {
            seq_to_nongappy_pvec( m_qs_seqs[i], m_qs_pvecs[i] );
        }


    }


    void calc_scores( const size_t n_threads ) {

        //
        // build the alignment blocks
        //


        build_block_queue();

        //
        // work
        //
        ivy_mike::timer t1;
        ivy_mike::thread_group tg;



        while( tg.size() < n_threads ) {
            tg.create_thread(worker(*this, tg.size()));
        }
        tg.join_all();

        std::cerr << "scoring finished: " << t1.elapsed() << "\n";

    }

    void print_best_scores( std::ostream &os ) {
        boost::io::ios_all_saver ioss(os);
        os << std::setfill ('0');
        for( unsigned int i = 0; i < m_qs_names.size(); i++ ) {
            os << m_qs_names[i] << " "  << std::setw (4) << m_qs_bestedge[i] << " " << std::setw(5) << m_qs_bestscore[i] << "\n";

        }
    }


    void gapstream_to_alignment( const std::vector<uint8_t> &gaps, const std::vector<uint8_t> &raw, std::vector<uint8_t> &out, uint8_t gap_char ) {

        std::vector<uint8_t>::const_reverse_iterator rit = raw.rbegin();

        for ( std::vector<uint8_t>::const_iterator git = gaps.begin(); git != gaps.end(); ++git ) {

            if ( *git == 1) {
                out.push_back(gap_char);
            } else if ( *git == 0 ) {

                out.push_back(*rit);
                ++rit;
            } else {
                ++rit; // just consume one QS character
            }
        }

        std::reverse(out.begin(), out.end());
    }


    static uint8_t normalize_dna( uint8_t c ) {
        c = std::toupper(c);

        if( c == 'U' ) {
            c = 'T';
        }

        return c;
    }

    static char num_to_ascii( int n ) {
        if( n >= 0 && n <= 9 ) {
            return '0' + n;
        } else if( n >= 0xa && n <= 0xf ) {
            return 'a' + n;
        } else {
            throw std::runtime_error( "not a single digit (hex) number" );
        }
    }

    void align_best_scores( std::ostream &os, std::ostream &os_quality ) {
        // create the actual alignments for the best scoring insertion position (=do the traceback)

        pars_align_seq::arrays seq_arrays(true);

        double mean_quality = 0.0;
        double n_quality = 0.0;

        for( unsigned int i = 0; i < m_qs_names.size(); i++ ) {
            int best_edge = m_qs_bestedge[i];

            assert( best_edge >= 0 && size_t(best_edge) < m_ref_pvecs.size() );

            const int *seqptr = m_ref_pvecs[best_edge].data();
            const unsigned int *auxptr = m_ref_aux[best_edge].data();

            const size_t ref_len = m_ref_pvecs[best_edge].size();

            const size_t stride = 1;
            const size_t aux_stride = 1;
            pars_align_seq pas( seqptr, m_qs_pvecs[i].data(), ref_len, m_qs_pvecs[i].size(), stride, auxptr, aux_stride, seq_arrays, 0 );
            int res = pas.alignFreeshift(INT_MAX);


            std::vector<uint8_t> tbv;
            pas.tracebackCompressed(tbv);

            std::vector<uint8_t> out_qs;


            gapstream_to_alignment(tbv, m_qs_pvecs[i], out_qs, 0xf);

            //
            // in place transform out_qs from pstate to dna form. WATCH OUT!
            //

            std::transform( out_qs.begin(), out_qs.end(), out_qs.begin(), dna_parsimony_mapping::p2d );

            os << m_qs_names[i] << "\t";
            std::copy( out_qs.begin(), out_qs.end(), std::ostream_iterator<char>(os));
            os << "\n";

            const bool dump_auxvec = false;
            if( dump_auxvec )
            {
                std::string auxv;
                auxv.resize(m_ref_aux[best_edge].size());

                std::transform( m_ref_aux[best_edge].begin(), m_ref_aux[best_edge].end(), auxv.begin(), num_to_ascii );
                os << m_qs_names[i] << "\t" << auxv << "\n";
            }


            if( res != m_qs_bestscore[i] ) {
                std::cout << "meeeeeeep! score: " << m_qs_bestscore[i] << " " << res << "\n";
            }

            if( os_quality.good() && out_qs.size() == m_qs_seqs[i].size() ) {
                bool debug = (m_qs_names[i] == "Species187_04");



                double score = alignment_quality( out_qs, m_qs_seqs[i], debug );

                os_quality << m_qs_names[i] << " " << score << "\n";

                mean_quality += score;
                n_quality += 1;
            }


        }

        std::cerr << "mean quality: " << mean_quality / n_quality << "\n";

    }

    ~papara_nt() {

        ivy_mike::timer t2;
        m_ln_pool->clear();
        //   pool.mark(n);
        m_ln_pool->sweep();

        std::cout << t2.elapsed() << std::endl;

    }
    void dump_ref_seqs ( std::ostream &os ) {
        for( size_t i = 0; i < m_ref_seqs.size(); i++ ) {
            std::string outs;
            outs.resize(m_ref_seqs[i].size() );

            std::transform( m_ref_seqs[i].begin(), m_ref_seqs[i].end(), outs.begin(), normalize_dna );

            os << m_ref_names[i] << "\t" << outs << "\n";
        }
    }


    void write_result_phylip( std::ostream &os, std::ostream &os_quality ) {
        os << " 1 2\n";
        dump_ref_seqs(os);
        align_best_scores(os, os_quality);

    }
    double alignment_quality_very_strict ( const std::vector< uint8_t > &s1, const std::vector< uint8_t >& s2, bool debug = false ) {
        size_t nident = 0;
        size_t ngap1 = 0;
        size_t ngap2 = 0;


        for( std::vector< uint8_t >::const_iterator it1 = s1.begin(), it2 = s2.begin(); it1 != s1.end(); ++it1, ++it2 ) {

            if( dna_parsimony_mapping::is_gap( *it1 ) ) {
                ngap1++;
            }

            if( dna_parsimony_mapping::is_gap( *it2 ) ) {
                ngap2++;
            }
            if( debug ) {
                std::cerr << ngap1 << " " << ngap2 << " " << *it1 << " " << *it2 << "\n";
            }

            if( ngap1 == ngap2 ) {
                nident++;
            }
        }

        return double(nident) / s1.size();

    }
    double alignment_quality ( const std::vector< uint8_t > &s1, const std::vector< uint8_t >& s2, bool debug = false ) {
        size_t nident = 0;

//         size_t nident_nongap = 0;
//         size_t n_nongap = 0;

        for( std::vector< uint8_t >::const_iterator it1 = s1.begin(), it2 = s2.begin(); it1 != s1.end(); ++it1, ++it2 ) {
            if( dna_parsimony_mapping::d2p(*it1) == dna_parsimony_mapping::d2p(*it2) ) {
                nident++;
            }
        }

        return double(nident) / s1.size();

    }

};

int main( int argc, char *argv[] ) {

//     aligned_buffer<int> xxx(1024);
    
    
    
    
    namespace igo = ivy_mike::getopt;

    ivy_mike::getopt::parser igp;

    std::string opt_tree_name;
    std::string opt_alignment_name;
    std::string opt_qs_name;
    bool opt_use_cgap;

    igp.add_opt( 't', igo::value<std::string>(opt_tree_name) );
    igp.add_opt( 's', igo::value<std::string>(opt_alignment_name) );
    igp.add_opt( 'q', igo::value<std::string>(opt_qs_name) );
    igp.add_opt( 'c', igo::value<bool>(opt_use_cgap, true).set_default(false) );

    igp.parse(argc,argv);

    if( igp.opt_count('t') != 1 || igp.opt_count('s') != 1  ) {
        std::cerr << "missing options -t and/or -s (-q is optional)\n";
        return 0;
    }
    ivy_mike::timer t;

    const char *qs_name = 0;
    if( !opt_qs_name.empty() ) {
        qs_name = opt_qs_name.c_str();
    }

    std::auto_ptr<papara_nt_i> pnt_ptr;

    if( !opt_use_cgap ) {
        pnt_ptr.reset( new papara_nt<pvec_pgap>( opt_tree_name.c_str(), opt_alignment_name.c_str(), qs_name ));
    } else {
        pnt_ptr.reset( new papara_nt<pvec_cgap>( opt_tree_name.c_str(), opt_alignment_name.c_str(), qs_name ));
    }

    papara_nt_i &pnt = *pnt_ptr;
    pnt.calc_scores( 4 );

    {
        std::ofstream os( "papara_scores.txt" );
        pnt.print_best_scores(os);
    }

    {
        std::ofstream os( "papara_ali.phy" );
        std::ofstream os_quality( "papara_quality.phy" );

        //         pnt.dump_ref_seqs(os);
        //         pnt.align_best_scores(os);
        pnt.write_result_phylip(os, os_quality);
    }

    //ivymike::LN *n = tp.parse();

//     getchar();
    //ivymike::LN::free( n );
//     delete n;


    std::cout << t.elapsed() << std::endl;
    return 0;
//     getchar();
}


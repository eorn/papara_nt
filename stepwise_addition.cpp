/*
 * Copyright (C) 2011 Simon A. Berger
 *
 *  This program is free software; you may redistribute it and/or modify its
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 */
#include "stepwise_align.h"
#include <functional>
#include <iomanip>
#include <numeric>
#include <deque>


#include <boost/dynamic_bitset.hpp>
#include <boost/thread.hpp>
//#include <boost/thread/future.hpp>
#include <boost/thread/barrier.hpp>

#include <boost/array.hpp>

#ifndef WIN32
// there is some strange linker error on widows. can't be bothered now... visual c++ will probably do better whole program optimization than gcc anyway...
#define PWDIST_INLINE
#endif
#include "pairwise_seq_distance.h"

#include "ivymike/tdmatrix.h"
#include "ivymike/algorithm.h"
// #include "ivymike/cycle.h"
#include "tree_utils.h"
#include "parsimony.h"
#include "pvec.h"
#include "fasta.h"
#include "ivymike/tree_parser.h"
#include "pars_align_seq.h"
#include "tree_similarity.h"
#include <ivymike/time.h>

using namespace std;
using namespace ivy_mike::tree_parser_ms;




template<class pvec_t>
class my_adata_gen : public ivy_mike::tree_parser_ms::adata {
//     static int ct;
    //vector<parsimony_state> m_pvec;
  
    vector<uint8_t> m_raw_seq;
    pvec_t m_pvec;
public:
//     int m_ct;
    my_adata_gen() {

//         cout << "my_adata\n";

    }

    virtual ~my_adata_gen() {

//         cout << "~my_adata\n";

    }

    virtual void visit() {
//         cout << "tr: " << m_ct << "\n";
    }
    void init_pvec(const vector< uint8_t >& seq) {
        m_raw_seq = seq;
        reset_pvec();
//        m_pvec.init( seq );
    }
    vector<uint8_t>&get_raw_seq() {
        return m_raw_seq;
    }
    
    void reset_pvec() {
        m_pvec.init( m_raw_seq );
    }
    
    pvec_t &get_pvec() {
        return m_pvec;
    }


};

template<typename pvec_t>
class my_ldata_gen : public ivy_mike::tree_parser_ms::ldata {
    pvec_t m_pvec;
    int m_generation;
public:
    my_ldata_gen() : m_generation(-1) {
//         std::cout << "hello ldata " << sizeof(*this) << "\n";
    }
    
    virtual ~my_ldata_gen() {
//         std::cout << "bye ldata\n";
    }
    pvec_t &get_pvec() {
        return m_pvec;
    }
    
    int get_generation() {
        return m_generation;
    }
    
    void set_generation( int gen ) {
        m_generation = gen;
    }
    
    
};

template<class ndata_t, class ldata_t>
class my_fact_gen : public ivy_mike::tree_parser_ms::node_data_factory {

    virtual ndata_t *alloc_adata() {

        return new ndata_t;
    }
    
    virtual ldata_t *alloc_ldata() {
        return new ldata_t;
    }

};


template<class pvec_t>
size_t do_newview( pvec_t &root_pvec, lnode *n1, lnode *n2, bool incremental ) {
    typedef my_adata_gen<pvec_t> my_adata;

    deque<rooted_bifurcation<lnode> > trav_order;

//     cout << "traversal for branch: " << *(n1->m_data) << " " << *(n2->m_data) << "\n";

    rooted_traveral_order( n1, n2, trav_order, incremental );
//     cout << "traversal: " << trav_order.size() << "\n";

    for( deque< rooted_bifurcation< ivy_mike::tree_parser_ms::lnode > >::iterator it = trav_order.begin(); it != trav_order.end(); ++it ) {
//         cout << *it << "\n";

        my_adata *p = dynamic_cast<my_adata *>( it->parent->m_data.get());
        my_adata *c1 = dynamic_cast<my_adata *>( it->child1->m_data.get());
        my_adata *c2 = dynamic_cast<my_adata *>( it->child2->m_data.get());
//         rooted_bifurcation<ivy_mike::tree_parser_ms::lnode>::tip_case tc = it->tc;

//         cout << "tip case: " << (*it) << "\n";
        pvec_t::newview(p->get_pvec(), c1->get_pvec(), c2->get_pvec(), it->child1->backLen, it->child2->backLen, it->tc);

    }


    {
        my_adata *c1 = dynamic_cast<my_adata *>( n1->m_data.get());
        my_adata *c2 = dynamic_cast<my_adata *>( n2->m_data.get());

//         tip_case tc;

        if( c1->isTip && c2->isTip ) {
//                 cout << "root: TIP TIP\n";
            pvec_t::newview(root_pvec, c1->get_pvec(), c2->get_pvec(), n1->backLen, n2->backLen, TIP_TIP );
        } else if( c1->isTip && !c2->isTip ) {
//                 cout << "root: TIP INNER\n";
            pvec_t::newview(root_pvec, c1->get_pvec(), c2->get_pvec(), n1->backLen, n2->backLen, TIP_INNER );
//             root_pvec = c2->get_pvec();
        } else if( !c1->isTip && c2->isTip ) {
//                 cout << "root: INNER TIP\n";
            pvec_t::newview(root_pvec, c2->get_pvec(), c1->get_pvec(), n1->backLen, n2->backLen, TIP_INNER );
//             root_pvec = c1->get_pvec();
        } else {
//                 cout << "root: INNER INNER\n";
            pvec_t::newview(root_pvec, c1->get_pvec(), c2->get_pvec(), n1->backLen, n2->backLen, INNER_INNER );
        }
        
        

    }
    
    return trav_order.size();
    
//     cout << hex;
//     for( vector< parsimony_state >::const_iterator it = root_pvec.begin(); it != root_pvec.end(); ++it ) {
//         cout << *it;
//     }
//
//     cout << dec << endl;

}

template<class pvec_t>
void do_newview_from_ldata( pvec_t &root_pvec, lnode *n1, lnode *n2 ) {
    // assume that the pvecs in the ldata are already valid
    
    
    typedef my_adata_gen<pvec_t> my_adata;
    typedef my_ldata_gen<pvec_t> my_ldata;
    
    {
        my_adata *c1 = dynamic_cast<my_adata *>( n1->m_data.get());
        my_adata *c2 = dynamic_cast<my_adata *>( n2->m_data.get());

        my_ldata *ldc1 = dynamic_cast<my_ldata *>( n1->m_ldata.get());
        my_ldata *ldc2 = dynamic_cast<my_ldata *>( n2->m_ldata.get());
        
//         tip_case tc;

        if( c1->isTip && c2->isTip ) {
//                 cout << "root: TIP TIP\n";
            pvec_t::newview(root_pvec, ldc1->get_pvec(), ldc2->get_pvec(), n1->backLen, n2->backLen, TIP_TIP );
        } else if( c1->isTip && !c2->isTip ) {
//                 cout << "root: TIP INNER\n";
            pvec_t::newview(root_pvec, ldc1->get_pvec(), ldc2->get_pvec(), n1->backLen, n2->backLen, TIP_INNER );
//             root_pvec = c2->get_pvec();
        } else if( !c1->isTip && c2->isTip ) {
//                 cout << "root: INNER TIP\n";
            pvec_t::newview(root_pvec, ldc2->get_pvec(), ldc1->get_pvec(), n1->backLen, n2->backLen, TIP_INNER );
//             root_pvec = c1->get_pvec();
        } else {
//                 cout << "root: INNER INNER\n";
            pvec_t::newview(root_pvec, ldc1->get_pvec(), ldc2->get_pvec(), n1->backLen, n2->backLen, INNER_INNER );
        }
        
        

    }
    
    
    
//     cout << hex;
//     for( vector< parsimony_state >::const_iterator it = root_pvec.begin(); it != root_pvec.end(); ++it ) {
//         cout << *it;
//     }
//
//     cout << dec << endl;

}



typedef pvec_cgap pvec_t;
    
typedef my_adata_gen<pvec_t> my_adata;
typedef my_ldata_gen<pvec_t> my_ldata;

typedef my_fact_gen<my_adata, my_ldata> my_fact;





class step_add {
    //auto_ptr<ivy_mike::tree_parser_ms::ln_pool> m_ln_pool;
    
    const static int score_match = 3;
    const static int score_match_cgap = -4;
    const static int score_gap_open = -3;
    const static int score_gap_extend = -1;
    const static bool align_global = true;
    
    sptr::shared_ptr<ln_pool> m_ln_pool;
    string m_seq_file_name;
    vector<string> m_qs_names;
    vector<vector<uint8_t> > m_qs_seqs;
//    vector<vector<uint8_t> > m_qs_seqs_mapped; // the content of m_qs_seqs, but mapped by m_pw_scoring_matrix
    
    vector<vector <uint8_t> > m_qs_nongappy;
    
    ivy_mike::tdmatrix<float> m_pw_dist;
    scoring_matrix m_pw_scoring_matrix;
    boost::dynamic_bitset<> m_used_seqs;
    lnode *m_tree_root;
    pars_align_seq::arrays m_seq_arrays;
        
    vector<lnode *> m_leafs;
    align_arrays_traceback<int> m_align_arrays_traceback;
    
    std::ofstream m_inc_ali;
    
    const size_t m_num_threads;

    size_t m_sum_ncup;
    int m_pvec_gen;
    
    
    static void seq_to_nongappy_pvec( vector<uint8_t> &seq, vector<uint8_t> &pvec ) {
        pvec.resize( 0 );
        
        for( unsigned int i = 0; i < seq.size(); i++ ) {
            uint8_t ps = dna_parsimony_mapping::d2p(seq[i]);
            
            if( ps == 0x1 || ps == 0x2 || ps == 0x4 || ps == 0x8 ) {
                pvec.push_back(ps);
            }
            
        }
        
    }
    void lnode_newview( lnode *n ) {
        my_adata *ad = dynamic_cast<my_adata *>( n->m_data.get());
        my_ldata *ld = dynamic_cast<my_ldata *>( n->m_ldata.get());
        
        //my_adata *c2 = dynamic_cast<my_adata *>( it->child2->m_data.get());
        if( ld->get_generation() < m_pvec_gen ) {
            
            if( ad->isTip ) {
                ld->get_pvec() = ad->get_pvec();
            } else {
                lnode *c1 = n->next->back;
                lnode *c2 = n->next->next->back;
                
                lnode_newview( c1 );
                lnode_newview( c2 );
                
                
                
                
                tip_case tc;
                if( c1->m_data->isTip && c2->m_data->isTip ) {
                    tc = TIP_TIP;
                } else if( c1->m_data->isTip && !c2->m_data->isTip ) {
                    tc = TIP_INNER;
                } else if( !c1->m_data->isTip && c2->m_data->isTip ) {
                    tc = TIP_INNER;
                    std::swap( c1, c2 );
                } else {
                    tc = INNER_INNER;
                }
                
                my_ldata *ldc1 = dynamic_cast<my_ldata *>( c1->m_ldata.get());
                my_ldata *ldc2 = dynamic_cast<my_ldata *>( c2->m_ldata.get());
                
                pvec_t &pvc1 = ldc1->get_pvec();
                pvec_t &pvc2 = ldc2->get_pvec();
                
             //   std::cout << "size: " << pvc1.size() << " " << pvc2.size() << "\n";
                
                pvec_t::newview( ld->get_pvec(), pvc1, pvc2, n->next->backLen, n->next->next->backLen, tc);
            }
            
            ld->set_generation(m_pvec_gen);
            //pvec_t::newview(p->get_pvec(), c1->get_pvec(), c2->get_pvec(), it->child1->backLen, it->child2->backLen, it->tc);
            
      
            
//             if( n->towards_root ) {
//                 if (ad->get_pvec() != ld->get_pvec()) {
//                     throw std::runtime_error( "ad != ld pvec\n" );
//                 }          
//                 
//                // std::cout << ad->get_pvec().size() << " " << ld->get_pvec().size() << " " <<  << "\n";
//             }
                
        }
    }
    
    struct ali_task {
        pair< ivy_mike::tree_parser_ms::lnode*, ivy_mike::tree_parser_ms::lnode* > m_edge;
        
        // WARNINIG: the next two members are most likely references to local objects, which are kept in scope between bar1 and bar2, but not longer
        const vector<uint8_t> &m_qs_pvec; 
        int &m_result;
        
        ali_task( pair< ivy_mike::tree_parser_ms::lnode*, ivy_mike::tree_parser_ms::lnode* > edge, const vector<uint8_t> &qs_pvec, int &result ) 
        : m_edge(edge), m_qs_pvec(qs_pvec), m_result(result) {
            
        }
        
        void work() {
            
        }
        
    };
    
    
    template<size_t W>
    static void copy_to_profile( vector<pvec_t> &pvecs, aligned_buffer<short> &prof, aligned_buffer<short> &aux_prof ) {
        size_t reflen = pvecs[0].size();
        
        assert( W == pvecs.size() );
        assert( reflen * W == prof.size() );
        assert( reflen * W == aux_prof.size() );
        
        aligned_buffer<short>::iterator it = prof.begin();
        aligned_buffer<short>::iterator ait = aux_prof.begin();
//         std::cout << "reflen: " << reflen << " " << size_t(&(*it)) << "\n";
        
        for( size_t i = 0; i < reflen; ++i ) {
            for( size_t j = 0; j < W; ++j ) {
//                 std::cout << "ij: " << i << " " << j << " " << pvecs[j].size() <<  "\n";
                
                
                *it = short(pvecs[j].get_v()[i]);
                *ait = short( (pvecs[j].get_auxv()[i] == AUX_CGAP) ? 0xFFFF : 0x0 );
                
                ++it;
                ++ait;
            }
        }
        
        
        assert( it == prof.end());
        assert( ait == aux_prof.end());
        
    }
    
    static void ali_work_vec_nt( step_add *sa, int rank ) {
        // TODO: code review! This went to smoothly, I don' trust it...
        
        const size_t W = 8;
        typedef short score_t;
        
        
        
        
        vector<uint8_t> seq_tmp;
        vector<uint8_t> aux_tmp; 
        
        
        
        vector<ali_task *> tasks;
        vector<pvec_t> root_pvecs(W);
        aligned_buffer<short> a_prof;//(seq_tmp.size() * W);
        aligned_buffer<short> a_aux_prof; //(seq_tmp.size() * W);
        aligned_buffer<short> out_score(W);
        align_vec_arrays<score_t> arr;
        
        ivy_mike::perf_timer pt;
        
        
        
        while( true ) {
            
            sa->m_bar1.wait();
//             std::cout << sa->m_par_queue[rank].size() << "\n";
            
            while( !sa->m_par_queue[rank].empty() ) {

                ivy_mike::perf_timer lpt;


                tasks.clear();
                
                while( tasks.size() < W && !sa->m_par_queue[rank].empty() ) {
                    tasks.push_back(sa->m_par_queue[rank].back());
                    sa->m_par_queue[rank].pop_back();
                }
                assert( !tasks.empty() );
                
                size_t ref_len = 0;
                
                
                //                 std::cout << "tasks: " << tasks.size() << "\n";
                lpt.add_int();
                for( size_t i = 0; i < tasks.size(); ++i ) {
                    sa->m_num_newview += 1;
                    do_newview_from_ldata( root_pvecs[i], tasks[i]->m_edge.first, tasks[i]->m_edge.second );
                    
                    
                    if( i == 0 ) {
                        ref_len = root_pvecs[i].size();
                        if( a_prof.size() < ref_len * W ) {
                            // zero initialize on resize, so that valgrind will not complain about uninitialized reads if tasks.size() < W after resize...
                            a_prof.resize( ref_len * W, 0 );
                            a_aux_prof.resize( ref_len * W, 0 );
                        }
                    } else {
                        if( ref_len != root_pvecs[i].size() ) {
                            throw std::runtime_error( "quirk: ref_len != root_pvecs[i].size()" );
                        }
                        
                    }
                    
                    
                    
                    // FIXME: to_aux_vec sets a_aux_prof to 0xFFFF for cgap columns. The correct value is dependent on the width of the vector unit.
                    /*                    root_pvecs[i].to_int_vec_strided<aligned_buffer<short>::iterator,W>(a_prof.begin() + i);
                        *        root_pvecs[i].to_aux_vec_strided<aligned_buffer<short>::iterator,W>(a_aux_prof.begin() + i);*/
                    
                    
                }
                for( size_t i = tasks.size(); i < W; ++i ) {
                    root_pvecs[i] = root_pvecs[0];
                }
                lpt.add_int();    
                copy_to_profile<W>( root_pvecs, a_prof, a_aux_prof );
                
                lpt.add_int();
                
                align_pvec_score_vec<short,W,align_global>(a_prof, a_aux_prof, tasks[0]->m_qs_pvec, score_match, score_match_cgap, score_gap_open, score_gap_extend, out_score, arr );            
                
                sa->m_thread_ncup.at(rank) += uint64_t(ref_len) * tasks.size() * tasks[0]->m_qs_pvec.size();
                
                for( size_t i = 0; i < tasks.size(); ++i ) {
                    tasks[i]->m_result = out_score[i];
                }
                
                for( std::vector< ali_task* >::iterator it = tasks.begin(); it != tasks.end(); ++it ) {
                    delete *it;
                }
                tasks.clear();
                
                lpt.add_int();
                pt += lpt;
            }
            
            
            if( sa->m_queue_exit ) {
                boost::lock_guard<boost::mutex> tree_lock( sa->m_gen_mutex );
                sa->m_align_pt += pt;   
                break;
            }
            
            sa->m_bar2.wait();
            
        }


    }
    
    
    //
    // regarding the aligner threads, bar1 and bar2 separate the program into two sections:
    // basic data access policies for the sections:
    // between bar1 and bar2:
    //  the aligner threads are working. the sequential part is sleeping.
    //  the shared state is read-only, except for:
    //  m_par_queue[rank] and contained ali_task instances are r/w for the respective thread.
    //  some additional shared stuff secured by m_gen_mutex is r/w (iff m_gen_mutex is held)
    // between bar2 and bar1:
    //  the aligner threads are sleeping. the sequential part is running.
    //  the whole shared state is r/w for the sequential part.
    //
    bool m_queue_exit;

    std::vector<std::vector<ali_task *> > m_par_queue;
    boost::barrier m_bar1;
    boost::barrier m_bar2;
    
    
    // additional shared stuff, secured by m_gen_mutex
    boost::mutex m_gen_mutex;
    volatile size_t m_num_newview;
    ivy_mike::perf_timer m_align_pt;
    // end of additional shared stuff
    
    boost::thread_group m_thread_group;
    
    std::vector<uint64_t> m_thread_ncup;
    ivy_mike::perf_timer m_insert_timer;
public:
    step_add( const char *seq_name, sptr::shared_ptr<ln_pool> ln_pool ) 
    : m_ln_pool( ln_pool ),
    m_seq_file_name(seq_name),
    m_pw_scoring_matrix(3,0),
    m_seq_arrays(true),
    m_num_threads(boost::thread::hardware_concurrency()),
    m_sum_ncup(0),
    m_pvec_gen(0),
    m_queue_exit(false),
    m_par_queue(m_num_threads),
    m_bar1(m_num_threads + 1),
    m_bar2(m_num_threads + 1)
    {
        {
            ifstream qsf( m_seq_file_name.c_str() );
            
            if( !qsf.good() ) {
                throw std::runtime_error( "cannot read input fasta" );
            }
            
            read_fasta( qsf, m_qs_names, m_qs_seqs);
        } 
        m_used_seqs.resize( m_qs_names.size() );
        
        
        
        for( size_t i = 0; i < m_num_threads; i++ ) {
            int rank = int(i);
            m_thread_group.create_thread( boost::bind( &step_add::ali_work_vec_nt, this, rank ) );
            m_thread_ncup.push_back(0);
        }
        
        if( !true ) {
            m_inc_ali.open( "inc_ali.txt" );
        }
        
    }
    
    ~step_add() {
        // cleanup the ali_task worker threads
        // when m_queue_exit = true, the worker threads will exit directly after bar1

        m_queue_exit = true;
        m_bar1.wait();
        
        
        
        m_thread_group.join_all();
        
        std::cout << "align perf_timer: \n";
        m_align_pt.print();
        
        std::cout << "overall perf_timer: \n";
        m_insert_timer.print();
            
    }
    pair<size_t,size_t> calc_dist_matrix_from_msa( std::map<std::string,std::vector<uint8_t> > &msa ) {
        typedef std::map<std::string,std::vector<uint8_t> > msa_t;
        
        m_pw_dist.init_size(m_qs_names.size(), m_qs_names.size());
    
        vector<vector<uint8_t> > qs_mapped;
        qs_mapped.reserve(m_qs_names.size() );
               
        
        ivy_mike::tdmatrix<int> out_scores(m_qs_names.size(), m_qs_names.size());
        
        
        if( msa.size() != m_qs_names.size() ) {
            throw std::runtime_error( "msa.size() != m_qs_names.size()" );
        }
        for( size_t i = 0; i < m_qs_names.size(); ++i ) {
            out_scores[i][i] = 0;
            
            msa_t::iterator seq_i = msa.find(m_qs_names[i] );
            
            if( seq_i == msa.end() ) {
                throw std::runtime_error( "missing sequence in ref msa" );
            }
            
            // TODO: maybe cache the seq_i iterators in a vector to get rid of the map.finds in the j-loop...
            
            
            for( size_t j = 0; j < i; ++j ) {
                msa_t::iterator seq_j = msa.find(m_qs_names[j] );
                
                if( seq_j == msa.end() ) {
                    throw std::runtime_error( "missing sequence in ref msa" );
                }
                
                int score = score_for_aligned_pair( seq_i->second, seq_j->second ); 
                
//                 std::cout << "score " << i << " " << j << " " << score << "\n";
                
                out_scores[i][j] = score;
                out_scores[j][i] = score;
            }
            
        }
        
        return init_pw_dist_from_msa_score_matrix(out_scores);
    }
    
    
    pair<size_t,size_t> init_pw_dist_from_msa_score_matrix( ivy_mike::tdmatrix<int> &out_scores ) {
        size_t li = -1, lj = -1;
        float lowest_dist = 1e8;
        int min = *(std::min_element( out_scores.begin(), out_scores.end() ));
        int max = *(std::max_element( out_scores.begin(), out_scores.end() ));
        
        for( size_t i = 0; i < out_scores.size(); i++ ) {
            
            for( size_t j = 0; j < out_scores[i].size(); j++ ) {
            
                // three modes for normalizing: min, max and mean
                //const float norm = min( ma[i][i], ma[j][j] );
                //             const float norm = max( ma[i][i], ma[j][j] );
                const float norm = (out_scores[i][j] - min) / float(max-min);
                
              
                const float dist = 1.0 - norm;
                m_pw_dist[i][j] = dist;
                
                if( i != j && dist < lowest_dist ) {
                    lowest_dist = dist;
                    li = i;
                    lj = j;
                }
                
            }
        
        }
        
        return pair<size_t,size_t>(li,lj);
    }


    pair<size_t,size_t> calc_dist_matrix() {
        m_pw_dist.init_size(m_qs_names.size(), m_qs_names.size());
    
        vector<vector<uint8_t> > qs_mapped;
        qs_mapped.reserve(m_qs_names.size() );
        
        // pre-map raw qs seqs to 'state numbers' (=scoring matrix rows/columns)
        for( vector< vector< uint8_t > >::iterator it = m_qs_seqs.begin(); it != m_qs_seqs.end(); ++it) 
        {
            qs_mapped.push_back(vector< uint8_t >());//(it->size()));
            qs_mapped.back().reserve(it->size());
            
            for_each( it->begin(), it->end(), scoring_matrix::valid_state_appender<vector< uint8_t > >(m_pw_scoring_matrix, qs_mapped.back() ));
        }
        
        
        
        ivy_mike::tdmatrix<int> out_scores(m_qs_names.size(), m_qs_names.size());
        
        
        if( false ) {
            ifstream is( "out_scores.bin" );
            
            is.seekg(0, ios_base::end);
            size_t size = is.tellg();
            is.seekg(0, ios_base::beg);
            if( size != out_scores.num_elements() * sizeof(int)) {
                throw runtime_error( "bad external outscores\n" );
            }
            is.read((char*)out_scores.begin(), sizeof(int) * out_scores.num_elements() );
        } else {
            pairwise_seq_distance(qs_mapped, out_scores, m_pw_scoring_matrix, -5, -2, m_num_threads, 64);
            ofstream os( "out_scores.bin" );
            os.write((char*)out_scores.begin(), sizeof(int) * out_scores.num_elements() );
        }
        
        return init_pw_dist_from_local_score_matrix(out_scores);
    }
    
    pair<size_t,size_t> init_pw_dist_from_local_score_matrix( ivy_mike::tdmatrix<int> &out_scores ) {
        size_t li = -1, lj = -1;
        float lowest_dist = 1e8;
        
        
        for( size_t i = 0; i < out_scores.size(); i++ ) {
            
            for( size_t j = 0; j < out_scores[i].size(); j++ ) {
            
                // three modes for normalizing: min, max and mean
                //const float norm = min( ma[i][i], ma[j][j] );
                //             const float norm = max( ma[i][i], ma[j][j] );
                const float norm = (out_scores[i][i] + out_scores[j][j]) * 0.5;
                
                
                int mae;
                if( i <= j ) {
                    mae = out_scores[i][j];
                    //                 mae = ma[j][i];
                } else {
                    mae = out_scores[j][i];
                    
                }
                
                const float dist = 1.0 - (mae / norm);
                m_pw_dist[i][j] = dist;
                
                if( i != j && dist < lowest_dist ) {
                    lowest_dist = dist;
                    li = i;
                    lj = j;
                }
                
            }
        
        }
        
        return pair<size_t,size_t>(li,lj);
    }
    void start_tree( size_t a, size_t b ) {
        lnode *na = lnode::create( *m_ln_pool );
        lnode *nb = lnode::create( *m_ln_pool );
        
        m_leafs.push_back(na);
        m_leafs.push_back(nb);
        
        m_used_seqs[a] = true;
        m_used_seqs[b] = true;
        
        assert( ivy_mike::isa<my_adata>( na->m_data.get() ) );
        assert( ivy_mike::isa<my_adata>( nb->m_data.get() ) );
        
        my_adata *da = na->m_data.get()->get_as<my_adata>();
        my_adata *db = nb->m_data.get()->get_as<my_adata>();
        
        vector<uint8_t> atmp = m_qs_seqs.at(a);
        vector<uint8_t> btmp = m_qs_seqs.at(b);
        scoring_matrix sm( 3, 0 );
        
        align_freeshift(sm, atmp, btmp, -5, -3 );
        
        da->init_pvec(atmp);
        db->init_pvec(btmp);
        
        parser::twiddle(na, nb, 1.0, "I1", 0 );
        na->m_data->setTipName(m_qs_names.at(a));
        nb->m_data->setTipName(m_qs_names.at(b));
        na->m_data->isTip = true;
        nb->m_data->isTip = true;
        
        
        
        
        m_tree_root = na;
    }
    
    size_t find_next_candidate() {
        size_t f = m_used_seqs.find_first();
        
        vector<float> dist_sum;
        
        while( f != m_used_seqs.npos ) {
            ivy_mike::odmatrix<float> slice = m_pw_dist[f];
            if( dist_sum.empty() ) {
                dist_sum.assign( slice.begin(), slice.end() );
            } else {
                ivy_mike::binary_twizzle( dist_sum.begin(), dist_sum.end(), slice.begin(), dist_sum.begin(), plus<float>() );
            }
            
            f = m_used_seqs.find_next(f);
        }
        
        float min_dist = 1e8;
        size_t min_element = size_t(-1);
        for( size_t i = 0; i < dist_sum.size(); i++ ) {
            if( !m_used_seqs[i] && dist_sum[i] < min_dist ) {
                min_dist = dist_sum[i];
                min_element = i;
            }
        }
        
        return min_element;
    }
    void gapstream_to_alignment( const std::vector<uint8_t> &gaps, const std::vector<uint8_t> &raw, std::vector<uint8_t> &out, uint8_t gap_char, bool upper ) {

        std::vector<uint8_t>::const_reverse_iterator rit = raw.rbegin();


        // 'gap indicator': if upper is set, insert gap into reference (=if gaps[i] == 2).
        const uint8_t gap_ind = upper ? 2 : 1;
        
        
        
        for ( std::vector<uint8_t>::const_iterator git = gaps.begin(); git != gaps.end(); ++git ) {

            
            
            if ( *git == gap_ind ) {
                out.push_back(gap_char);
            } else {
                out.push_back(*rit);
                ++rit;
            } 
        }

        std::reverse(out.begin(), out.end());
    }
    bool insertion_step() {
        size_t candidate = find_next_candidate();
        
//         cout << "candidate: " << candidate << "\n";
        
        bool valid = candidate != size_t(-1);
        
        if( !valid ) {
            return false;
        }
        
        
        m_used_seqs[candidate] = true;
        
        
        
//         copy( m_qs_seqs[candidate].begin(), m_qs_seqs[candidate].end(), ostream_iterator<char>(cout) );
//         cout << "\n";
        
        edge_collector<lnode> ec;
        visit_edges( m_tree_root, ec);
        
//         cout << "edges: " << ec.m_edges.size() << "\n";
        
        
        
        vector<uint8_t> qs_pvec;
        seq_to_nongappy_pvec(m_qs_seqs.at(candidate), qs_pvec);
        
        //
        // search for best insertion edge
        //
        pair< ivy_mike::tree_parser_ms::lnode*, ivy_mike::tree_parser_ms::lnode* > best_edge;
        int best_score = INT_MIN;
        vector<uint8_t> best_tb;
        
        ivy_mike::timer t1;
        vector<uint8_t> seq_tmp;
        vector<uint8_t> aux_tmp;    
        
        
//         
        const size_t W = 8;
        aligned_buffer<short> out_score(W);
        
        ivy_mike::perf_timer perf_timer(true);
        
        
        deque<ali_task *> tasks;
        vector<int> results;
        results.reserve(ec.m_edges.size());
        
        m_pvec_gen++;
        size_t circ_ptr = 0;
        for( vector< pair< ivy_mike::tree_parser_ms::lnode*, ivy_mike::tree_parser_ms::lnode* > >::iterator it = ec.m_edges.begin(); it != ec.m_edges.end(); ++it ) {
            lnode_newview( it->first );
            lnode_newview( it->second );
            
            //tasks.push_back( new ali_task(*it, qs_pvec) );
            //futures.push_back( boost::shared_future<int>(tasks.back()->m_prom.get_future()) );
            results.push_back(-1);
            m_par_queue[circ_ptr % m_num_threads].push_back(new ali_task(*it, qs_pvec, results.back()));
            circ_ptr++;
            
        }
        
        
        perf_timer.add_int();

//         std::cout << "wait1\n";
        
        m_bar1.wait();
        
//         std::cout << "wait2\n";
        m_bar2.wait();
//         std::cout << "done wait\n";
        
        perf_timer.add_int();
        
        
        for( size_t i = 0; i < ec.m_edges.size(); ++i ) {
            int res = results[i];
            //cout << "result: " << i << " = " << res << "\n";
            if( res > best_score ) {

                best_score = res;
                best_edge = ec.m_edges[i];
            }
        }
        //double eticks2 = getticks();
        perf_timer.add_int();
        
        //
        // at this point all worker threads must be blocking on the empty queue (TODO: maybe add explicit check).
        // it is now safe again to modify the tree
        //
        // print timing stats
        
        {
            //             cout << "ticks: " << eticks1 << " " << eticks2 << " " << eticks3 << "\n";
//             double dt = thread_timer.elapsed();
            size_t sum_ncup = std::accumulate( m_thread_ncup.begin(), m_thread_ncup.end(), uint64_t(0) );
            std::fill( m_thread_ncup.begin(), m_thread_ncup.end(), 0 );
//             cout << sum_ncup << " in " << dt << "s : " << sum_ncup / (dt * 1e9) << " GNCUP/s\n";
        
            m_sum_ncup += sum_ncup;
     //       std::cout << "newview: " << m_num_newview << " " << ec.m_edges.size() << " " << m_num_newview / double(ec.m_edges.size()) << "\n";
        }
        
        //
        // generate traceback for best insertion position
        //
        {
            vector<uint8_t> seq_tmp;
            vector<uint8_t> aux_tmp;    
            
            pvec_t root_pvec;
            
            //do_newview( root_pvec, best_edge.first, best_edge.second, true );
        
            do_newview_from_ldata( root_pvec, best_edge.first, best_edge.second );
            
            root_pvec.to_int_vec(seq_tmp);
            root_pvec.to_aux_vec(aux_tmp);
            
            
            
//             size_t stride = 1;
//             size_t aux_stride = 1;
//             pars_align_seq pas( &seq_tmp[0], &qs_pvec[0], seq_tmp.size(), qs_pvec.size(), stride, &aux_tmp[0], aux_stride, m_seq_arrays, 0 );
           // int res = pas.alignFreeshift(INT_MAX);

//             cout << "size: " << seq_tmp.size() << " " << qs_pvec.size() << "\n";
            
            int res2;
            if( align_global ) {
                res2 = align_global_pvec<int>(seq_tmp, aux_tmp, qs_pvec, score_match, score_match_cgap, score_gap_open, score_gap_extend, best_tb, m_align_arrays_traceback );
            } else {
                res2 = align_freeshift_pvec<int>(seq_tmp, aux_tmp, qs_pvec, score_match, score_match_cgap, score_gap_open, score_gap_extend, best_tb, m_align_arrays_traceback );
            }
            
            if( res2 != best_score ) {
                cerr << "res2 != best_score: " << res2 << " " << best_score << "\n";
               throw runtime_error( "alignment error" );
            }
        }
        perf_timer.add_int();
        
        
            
            
//             throw std::runtime_error( "xxx.\n" );
        
        
        //
        // apply traceback to reference sequences
        //
        
        for( std::vector< ivy_mike::tree_parser_ms::lnode* >::iterator it = m_leafs.begin(); it != m_leafs.end(); ++it ) {
            my_adata *adata = (*it)->m_data->get_as<my_adata>();

            // directly alter the 'raw_seq' stored in the my_adata ojects of the tip-node
            
            vector< uint8_t > &raw_seq = adata->get_raw_seq();
            vector< uint8_t > new_seq;
            // update old raw_seq with new gaps and swap in the new sequence
            gapstream_to_alignment(best_tb, raw_seq, new_seq, '-', true );
            raw_seq.swap(new_seq);
//             cout << "len: " << raw_seq.size() << " " << best_tb.size() << "\n";
            // re-create internal pvec
            adata->reset_pvec();
        }
        
        //
        // apply traceback to query sequence and insert new tip into tree
        //
        
        vector<uint8_t> tip_seq;
        gapstream_to_alignment(best_tb, m_qs_seqs[candidate], tip_seq, '-', false );
//         cout << "tip len: " << tip_seq.size() << "\n";
        
        
        lnode *nc = lnode::create( *m_ln_pool );
        nc->m_data->setTipName(m_qs_names.at(candidate));
        nc->m_data->isTip = true;
        
        nc->m_data->get_as<my_adata>()->init_pvec(tip_seq);
        
        lnode *nn = lnode::create( *m_ln_pool );
        parser::twiddle(nc, nn, 1.0, "I1", 0 );
        
        
        best_edge.first->back = 0;
        best_edge.second->back = 0;
        parser::twiddle( best_edge.first, nn->next, 1.0, "I1", 0 );
        parser::twiddle( best_edge.second, nn->next->next, 1.0, "I1", 0 );
        
        
        m_leafs.push_back(nc);
        perf_timer.add_int();
#if 0
        {
            stringstream ss;
            ss << "tree_" << setw(5) << setfill('0') << m_leafs.size();
            
            ofstream os( ss.str().c_str() );
//         cout << ">>>>>>>>>>>>>>\n";
            print_newick( next_non_tip( towards_tree( m_tree_root )), os);
//         cout << "<<<<<<<<<<<<<<\n";
        }
        {
            stringstream ss;
            ss << "ali_" << setw(5) << setfill('0') << m_leafs.size();
            
            ofstream os( ss.str().c_str() );
//         cout << ">>>>>>>>>>>>>>\n";
            //print_newick( next_non_tip( towards_tree( m_tree_root )), os);
//         cout << "<<<<<<<<<<<<<<\n";
            
            
            
            for( std::vector< ivy_mike::tree_parser_ms::lnode* >::const_iterator it = m_leafs.begin(); it != m_leafs.end(); ++it ) {
                my_adata *adata = (*it)->m_data->get_as<my_adata>();
                
                copy( adata->get_raw_seq().begin(), adata->get_raw_seq().end(), ostream_iterator<char>(os) );
            
                os << "\n";
            }
            
            
            
        }
#endif   
        
        if( m_inc_ali.good() ) {
            m_inc_ali << m_leafs.size() << "\n";
            for( std::vector< ivy_mike::tree_parser_ms::lnode* >::const_iterator it = m_leafs.begin(); it != m_leafs.end(); ++it ) {
                my_adata *adata = (*it)->m_data->get_as<my_adata>();
                
                copy( adata->get_raw_seq().begin(), adata->get_raw_seq().end(), ostream_iterator<char>(m_inc_ali) );
                
                m_inc_ali << "\n";
            }
            
            m_inc_ali << "\n\n";
            
        }
        
        perf_timer.add_int();
        perf_timer.print();
        
        m_insert_timer += perf_timer;
        
        
//         std::cout << "ticks: " << eticks2 - eticks1 << " " << eticks3 - eticks2 << " " << eticks4 - eticks3 << "\n";
        
        
        return valid;
           
    }
    void write_phylip( ostream &os ) {
        //
        // write sequences in dfs-ordering, so that topologically close sequences cluster in the output phylip file
        //
        
        tip_collector<lnode>tc;
        
        visit_lnode(m_tree_root, tc );
        
        if( tc.m_nodes.empty() ) {
            throw runtime_error( "tip_collector: empty" );
        }
        
        size_t max_name_len = 0;
        for( vector< sptr::shared_ptr< ivy_mike::tree_parser_ms::lnode > >::iterator it = tc.m_nodes.begin(); it != tc.m_nodes.end(); ++it ) {
            max_name_len = max(max_name_len, (*it)->m_data->tipName.size());
        }
        size_t seq_len = tc.m_nodes.front()->m_data->get_as<my_adata>()->get_raw_seq().size();
        os << tc.m_nodes.size() << " " << seq_len << "\n";
        for( vector< sptr::shared_ptr< ivy_mike::tree_parser_ms::lnode > >::iterator it = tc.m_nodes.begin(); it != tc.m_nodes.end(); ++it ) {
            my_adata *adata = (*it)->m_data->get_as<my_adata>();

            
            
            os << setw(max_name_len + 1) << left << setfill( ' ' ) << adata->tipName;
            copy( adata->get_raw_seq().begin(), adata->get_raw_seq().end(), ostream_iterator<char>(os) );
            
            os << "\n";
        }
    }
    void write_newick( ostream &os ) {
        print_newick( next_non_tip( towards_tree( m_tree_root )), os); 
    }
    
    
    // move raw sequence data from tree to std::map (leaving the tree in an undefined state)
    void move_raw_seq_data_to_map( std::map<std::string,std::vector<uint8_t> > &msa ) {
        tip_collector<lnode>tc;
        
        visit_lnode(m_tree_root, tc );
        
        for( vector< sptr::shared_ptr< ivy_mike::tree_parser_ms::lnode > >::const_iterator it = tc.m_nodes.begin(); it != tc.m_nodes.end(); ++it ) {
            my_adata *adata = (*it)->m_data->get_as<my_adata>();
            
            assert( msa.find( adata->tipName ) == msa.end() );
            
            std::vector<uint8_t> &seq = msa[adata->tipName];
            
            seq.swap( adata->get_raw_seq() );
            
        }
    }
    
    static int score_for_aligned_pair( const std::vector<uint8_t> &a, const std::vector<uint8_t> &b ) {
        if( a.size() != b.size() ) {
            throw std::runtime_error( "a.size() != b.size()" );
        }
        bool gap_a = false;
        bool gap_b = false;
        
        const int gap_open = -5;
        const int gap_extend = -2;
        const int score_mismatch = 0;
        const int score_match = 3;
        
        
        int score = 0;
        for( size_t i = 0; i < a.size(); ++i ) {
            bool ga = a[i] == '-';
            bool gb = b[i] == '-';
            
            if( ga && gb ) {
                continue;
            }
            
            if( ga ) {
                if( !gap_a ) {
                    score += gap_open;
                } else {
                    score += gap_extend;
                }
            } else if( gb ) {
                if( !gap_b ) {
                    score += gap_open;
                } else {
                    score += gap_extend;
                }
            } else {
                if( a[i] == b[i] ) {
                    score += score_match;
                } else {
                    score += score_mismatch;
                }
            }
            gap_a = ga;
            gap_b = gb;
        }
        
        
        return score;
    }
    
    lnode *get_tree() {
        return m_tree_root;
    }
};

int main( int argc, char **argv ) {
    //mapped_file qsf( "test_1604/1604.fa" );
    
    //     string ta = "GATTACAGATTACA";
    //     string tb = "GATTACAGATTA";
    //     
    //     vector<uint8_t> a(ta.begin(), ta.end());
    //     vector<uint8_t> b(tb.begin(), tb.end());
    //     
    //     scoring_matrix sm(3,0);
    //     
    //     
    //   
    //     
    //     align_freeshift( sm, a, b, -5, -3);
    //     return 0;
    
    
    //     const char *filename = (argc == 2) ? argv[1] : "test_150/150.fa";
    const char *filename = (argc == 2) ? argv[1] : "test_218/218.fa";
    
    
    
    std::map<std::string, std::vector<uint8_t> >out_msa1;
    sptr::shared_ptr<ln_pool> pool(new ln_pool(std::auto_ptr<node_data_factory>(new my_fact()) ));

    lnode *last_tree;
    {
        step_add sa(filename, pool);
        pair<size_t,size_t> start_pair = sa.calc_dist_matrix();
        
//         cout << "start: " << start_pair.first << " " << start_pair.second << "\n";
        sa.start_tree( start_pair.first, start_pair.second );
        
        
        ivy_mike::timer t1;
        
        
        
        while( sa.insertion_step() ) {
            
        }
        
        {
            ofstream os_ali( "sa_alignment.phy" );
            sa.write_phylip( os_ali );
            
            ofstream os_tree( "sa_tree.phy" );
            sa.write_newick( os_tree );
        }
        
        sa.move_raw_seq_data_to_map(out_msa1);
        last_tree = sa.get_tree();
        cout << "time: " << t1.elapsed() << "\n";
    }
    
    
    std::vector<split_set_t> split_history;
    
    for( int i = 0; i < 100; ++i )
    {
        step_add sa(filename,pool);
        pair<size_t,size_t> start_pair = sa.calc_dist_matrix_from_msa(out_msa1);
        
//         cout << "start: " << start_pair.first << " " << start_pair.second << "\n";
        sa.start_tree( start_pair.first, start_pair.second );
        
        
        ivy_mike::timer t1;
        
        
        
        while( sa.insertion_step() ) {
            
        }
        
        {
            std::stringstream suffix;
            suffix << std::setfill('0') << std::setw(3) << i << ".phy";
            
            ofstream os_ali( ("sa_alignment" + suffix.str()).c_str() );
            sa.write_phylip( os_ali );
            
            ofstream os_tree( ("sa_tree" + suffix.str()).c_str() );
            sa.write_newick( os_tree );
        }
        
        
//         cout << "time2: " << t1.elapsed() << "\n";
    
        out_msa1.clear();
        sa.move_raw_seq_data_to_map(out_msa1);
        
//         std::cout << "out msa: " << out_msa1.size() << "\n";
        
        lnode *tree2 = sa.get_tree();
        
        split_history.push_back( split_set_t() );
        double v = compare_trees( last_tree, tree2, split_history.back() );
        
        size_t period = size_t(-1);
        
        for( std::vector<split_set_t>::reverse_iterator it = split_history.rbegin() + 1; it != split_history.rend(); ++it ) {
            if( split_sets_equal( split_history.back(), *it ) ) {
                period = it - split_history.rbegin();
                
                std::cout << "found cycle: " << period << "\n";
                
                
            }
        }
        
        
        
        
        std::cout << "tree dist: " << v << "\n";
        
        last_tree = tree2;
        
        pool->clear();
        pool->mark(last_tree);
        pool->sweep();
        
        
        if( period != size_t(-1) ) {
            std::cout << "convergence. period " << period << "\n";
            break;
        }
        
    }
    
  
}

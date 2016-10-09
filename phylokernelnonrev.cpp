/*
 * phylokernelnonrev.cpp
 * likelihood kernel for non-reversible models
 *
 *  Created on: Mar 30, 2016
 *      Author: minh
 */



#include "phylotree.h"

void PhyloTree::computeNonrevPartialLikelihood(PhyloNeighbor *dad_branch, PhyloNode *dad) {

    // don't recompute the likelihood
	assert(dad);
    if (dad_branch->partial_lh_computed & 1)
        return;
    dad_branch->partial_lh_computed |= 1;
    PhyloNode *node = (PhyloNode*)(dad_branch->node);

    assert(dad_branch->direction != UNDEFINED_DIRECTION);

    size_t nstates = aln->num_states;
    size_t nptn = aln->size()+model_factory->unobserved_ptns.size();

	if (node->isLeaf()) {
	    dad_branch->lh_scale_factor = 0.0;

		if (!tip_partial_lh_computed)
			computeTipPartialLikelihood();
		return;
	}
    
    assert(node->degree() >= 3);
    
    size_t ptn, c;
    size_t orig_ntn = aln->size();
    size_t ncat = site_rate->getNRate();
    const size_t nstatesqr=nstates*nstates;
    size_t i, x;
    size_t block = nstates * ncat;

    dad_branch->lh_scale_factor = 0.0;

	// internal node
	PhyloNeighbor *left = NULL, *right = NULL; // left & right are two neighbors leading to 2 subtrees
	FOR_NEIGHBOR_IT(node, dad, it) {
        PhyloNeighbor *nei = (PhyloNeighbor*)*it;
		if (!left) left = (PhyloNeighbor*)(*it); else right = (PhyloNeighbor*)(*it);
        if ((nei->partial_lh_computed & 1) == 0)
            computeNonrevPartialLikelihood(nei, node);
        dad_branch->lh_scale_factor += nei->lh_scale_factor;
	}

    if (params->lh_mem_save == LM_PER_NODE && !dad_branch->partial_lh) {
        // re-orient partial_lh
        bool done = false;
        FOR_NEIGHBOR_IT(node, dad, it2) {
            PhyloNeighbor *backnei = ((PhyloNeighbor*)(*it2)->node->findNeighbor(node));
            if (backnei->partial_lh) {
                dad_branch->partial_lh = backnei->partial_lh;
                dad_branch->scale_num = backnei->scale_num;
                backnei->partial_lh = NULL;
                backnei->scale_num = NULL;
                backnei->partial_lh_computed &= ~1; // clear bit
                done = true;
                break;
            }
        }
        assert(done && "partial_lh is not re-oriented");
    }

    // precompute buffer to save times
    double *echildren = new double[block*nstates*(node->degree()-1)];
    double *partial_lh_leaves = new double[(aln->STATE_UNKNOWN+1)*block*(node->degree()-1)];
    double *echild = echildren;
    double *partial_lh_leaf = partial_lh_leaves;

    FOR_NEIGHBOR_IT(node, dad, it) {
        PhyloNeighbor *child = (PhyloNeighbor*)*it;
        // precompute information buffer
        if (child->direction == TOWARD_ROOT) {
            // tranpose probability matrix
            double mat[nstatesqr];
            for (c = 0; c < ncat; c++) {
                double len_child = site_rate->getRate(c) * child->length;
                model_factory->computeTransMatrix(len_child, mat);
                double *echild_ptr = &echild[c*nstatesqr];
                for (i = 0; i < nstates; i++)
                    for (x = 0; x < nstates; x++)
                        echild_ptr[i*nstates+x] = mat[x*nstates+i];
            }
        } else {
            for (c = 0; c < ncat; c++) {
                double len_child = site_rate->getRate(c) * child->length;
                model_factory->computeTransMatrix(len_child, &echild[c*nstatesqr]);
            }
        }

        // pre compute information for tip
        if (child->node == root) {
            model->getStateFrequency(partial_lh_leaf);
            for (c = 1; c < ncat; c++)
                memcpy(partial_lh_leaf+c*nstates, partial_lh_leaf, nstates*sizeof(double));
            partial_lh_leaf += (aln->STATE_UNKNOWN+1)*block;
        } else if (child->node->isLeaf()) {
            vector<int>::iterator it;
            for (it = aln->seq_states[child->node->id].begin(); it != aln->seq_states[child->node->id].end(); it++) {
                double *this_tip_partial_lh = &tip_partial_lh[(*it)*nstates];
                for (x = 0; x < block; x++) {
                    double vchild = 0.0;
                    for (i = 0; i < nstates; i++) {
                        vchild += echild[x*nstates+i] * this_tip_partial_lh[i];
                    }
                    partial_lh_leaf[(*it)*block+x] = vchild;
                }
            }
            for (x = 0; x < block; x++) {
                size_t addr = aln->STATE_UNKNOWN * block;
                partial_lh_leaf[addr+x] = 1.0;
            }
            partial_lh_leaf += (aln->STATE_UNKNOWN+1)*block;
        }
        echild += block*nstates;
    }
    
    
    double sum_scale = 0.0;
    
        
    double *eleft = echildren, *eright = echildren + block*nstates;
    
	if ((!left->node->isLeaf() && right->node->isLeaf())) {
		PhyloNeighbor *tmp = left;
		left = right;
		right = tmp;
        double *etmp = eleft;
        eleft = eright;
        eright = etmp;
    }

    if (node->degree() > 3) {

        /*--------------------- multifurcating node ------------------*/
    
        // now for-loop computing partial_lh over all site-patterns
#ifdef _OPENMP
#pragma omp parallel for reduction(+: sum_scale) private(ptn, c, x, i) schedule(static)
#endif
        for (ptn = 0; ptn < nptn; ptn++) {
            double *partial_lh_all = dad_branch->partial_lh + ptn*block;
            for (i = 0; i < block; i++)
                partial_lh_all[i] = 1.0;
            dad_branch->scale_num[ptn] = 0;
                
            partial_lh_leaf = partial_lh_leaves;
            double *echild = echildren;

            FOR_NEIGHBOR_IT(node, dad, it) {
                PhyloNeighbor *child = (PhyloNeighbor*)*it;
                if (child->node->isLeaf()) {
                    // external node
                    int state_child;
                    if (child->node == root) 
                        state_child = 0;
                    else state_child = (ptn < orig_ntn) ? (aln->at(ptn))[child->node->id] : model_factory->unobserved_ptns[ptn-orig_ntn];
                    double *child_lh = partial_lh_leaf + state_child*block;
                    for (c = 0; c < block; c++) {
                        // compute real partial likelihood vector
                        partial_lh_all[c] *= child_lh[c];
                    }
                    partial_lh_leaf += (aln->STATE_UNKNOWN+1)*block;
                } else {
                    // internal node
                    double *partial_lh = partial_lh_all;
                    double *partial_lh_child = child->partial_lh + ptn*block;
                    dad_branch->scale_num[ptn] += child->scale_num[ptn];

                    double *echild_ptr = echild;
                    for (c = 0; c < ncat; c++) {
                        // compute real partial likelihood vector
                        for (x = 0; x < nstates; x++) {
                            double vchild = 0.0;
                            for (i = 0; i < nstates; i++) {
                                vchild += echild_ptr[i] * partial_lh_child[i];
                            }
                            echild_ptr += nstates;
                            partial_lh[x] *= vchild;
                        }
                        partial_lh += nstates;
                        partial_lh_child += nstates;
                    }
                } // if
                echild += block*nstates;
            } // FOR_NEIGHBOR
            
        
            double lh_max = partial_lh_all[0];
            for (i = 1; i < block; i++)
                lh_max = max(lh_max, partial_lh_all[i]);

            assert(lh_max > 0.0);
            // check if one should scale partial likelihoods
            if (lh_max == 0.0) {
                // for very shitty data
                for (c = 0; c < ncat; c++)
                    memcpy(&partial_lh_all[c*nstates], &tip_partial_lh[aln->STATE_UNKNOWN*nstates], nstates*sizeof(double));
                sum_scale += LOG_SCALING_THRESHOLD* 4 * ptn_freq[ptn];
                //sum_scale += log(lh_max) * ptn_freq[ptn];
                dad_branch->scale_num[ptn] += 4;
//                int nsite = aln->getNSite();
//                for (i = 0, x = 0; i < nsite && x < ptn_freq[ptn]; i++)
//                    if (aln->getPatternID(i) == ptn) {
//                        outWarning((string)"Numerical underflow for site " + convertIntToString(i+1));
//                        x++;
//                    }
            } else if (lh_max < SCALING_THRESHOLD) {
                // now do the likelihood scaling
                for (i = 0; i < block; i++) {
                    partial_lh_all[i] *= SCALING_THRESHOLD_INVER;
                    //partial_lh[i] /= lh_max;
                }
                // unobserved const pattern will never have underflow
                sum_scale += LOG_SCALING_THRESHOLD * ptn_freq[ptn];
                //sum_scale += log(lh_max) * ptn_freq[ptn];
                dad_branch->scale_num[ptn] += 1;
            }

        } // for ptn
        dad_branch->lh_scale_factor += sum_scale;               
                
        // end multifurcating treatment
    } else if (left->node->isLeaf() && right->node->isLeaf()) {

        /*--------------------- TIP-TIP (cherry) case ------------------*/

        double *partial_lh_left = partial_lh_leaves;
        double *partial_lh_right = partial_lh_leaves + (aln->STATE_UNKNOWN+1)*block;

        if (right->node == root) {
            // swap so that left node is the root
            PhyloNeighbor *tmp = left;
            left = right;
            right = tmp;
            double *etmp = eleft;
            eleft = eright;
            eright = etmp;
            etmp = partial_lh_left;
            partial_lh_left = partial_lh_right;
            partial_lh_right = etmp;
        }
    
		// scale number must be ZERO
	    memset(dad_branch->scale_num, 0, nptn * sizeof(UBYTE));
#ifdef _OPENMP
#pragma omp parallel for private(ptn, i) schedule(static)
#endif
		for (ptn = 0; ptn < nptn; ptn++) {
			double *partial_lh = dad_branch->partial_lh + ptn*block;
			int state_left;
            if (left->node == root)
                state_left = 0;
            else
                state_left = (ptn < orig_ntn) ? (aln->at(ptn))[left->node->id] : model_factory->unobserved_ptns[ptn-orig_ntn];
			int state_right = (ptn < orig_ntn) ? (aln->at(ptn))[right->node->id] : model_factory->unobserved_ptns[ptn-orig_ntn];
            double *vleft = partial_lh_left + (state_left*block);
            double *vright = partial_lh_right + (state_right*block);
            for (i = 0; i < block; i++)
                partial_lh[i] = vleft[i] * vright[i];
		}
	} else if (left->node->isLeaf() && !right->node->isLeaf()) {

        /*--------------------- TIP-INTERNAL NODE case ------------------*/

		// only take scale_num from the right subtree
		memcpy(dad_branch->scale_num, right->scale_num, nptn * sizeof(UBYTE));


        double *partial_lh_left = partial_lh_leaves;

#ifdef _OPENMP
#pragma omp parallel for reduction(+: sum_scale) private(ptn, c, x, i) schedule(static)
#endif
		for (ptn = 0; ptn < nptn; ptn++) {
			double *partial_lh = dad_branch->partial_lh + ptn*block;
			double *partial_lh_right = right->partial_lh + ptn*block;
			int state_left;
            if (left->node == root)
                state_left = 0;
            else
                state_left = (ptn < orig_ntn) ? (aln->at(ptn))[left->node->id] : model_factory->unobserved_ptns[ptn-orig_ntn];
            double *vleft = partial_lh_left + state_left*block;
            double lh_max = 0.0;
            
            double *eright_ptr = eright;
			for (c = 0; c < ncat; c++) {
				// compute real partial likelihood vector
				for (x = 0; x < nstates; x++) {
					double vright = 0.0;
					for (i = 0; i < nstates; i++) {
						vright += eright_ptr[i] * partial_lh_right[i];
					}
                    eright_ptr += nstates;
                    lh_max = max(lh_max, (partial_lh[c*nstates+x] = vleft[x]*vright));
				}
                vleft += nstates;
                partial_lh_right += nstates;
			}
            assert(lh_max > 0.0);
            // check if one should scale partial likelihoods
            if (lh_max == 0.0) {
                // for very shitty data
                for (c = 0; c < ncat; c++)
                    memcpy(&partial_lh[c*nstates], &tip_partial_lh[aln->STATE_UNKNOWN*nstates], nstates*sizeof(double));
                sum_scale += LOG_SCALING_THRESHOLD* 4 * ptn_freq[ptn];
                //sum_scale += log(lh_max) * ptn_freq[ptn];
                dad_branch->scale_num[ptn] += 4;
//                int nsite = aln->getNSite();
//                for (i = 0, x = 0; i < nsite && x < ptn_freq[ptn]; i++)
//                    if (aln->getPatternID(i) == ptn) {
//                        outWarning((string)"Numerical underflow for site " + convertIntToString(i+1));
//                        x++;
//                    }
            } else if (lh_max < SCALING_THRESHOLD) {
                // now do the likelihood scaling
                for (i = 0; i < block; i++) {
                    partial_lh[i] *= SCALING_THRESHOLD_INVER;
                    //partial_lh[i] /= lh_max;
                }
                // unobserved const pattern will never have underflow
                sum_scale += LOG_SCALING_THRESHOLD * ptn_freq[ptn];
                //sum_scale += log(lh_max) * ptn_freq[ptn];
                dad_branch->scale_num[ptn] += 1;
            }
		}
		dad_branch->lh_scale_factor += sum_scale;
//		delete [] partial_lh_left;

	} else {

        /*--------------------- INTERNAL-INTERNAL NODE case ------------------*/

#ifdef _OPENMP
#pragma omp parallel for reduction(+: sum_scale) private(ptn, c, x, i) schedule(static)
#endif
		for (ptn = 0; ptn < nptn; ptn++) {
			double *partial_lh = dad_branch->partial_lh + ptn*block;
			double *partial_lh_left = left->partial_lh + ptn*block;
			double *partial_lh_right = right->partial_lh + ptn*block;
            double lh_max = 0.0;
			dad_branch->scale_num[ptn] = left->scale_num[ptn] + right->scale_num[ptn];

            double *eleft_ptr = eleft;
            double *eright_ptr = eright;

			for (c = 0; c < ncat; c++) {
				// compute real partial likelihood vector
				for (x = 0; x < nstates; x++) {
					double vleft = 0.0, vright = 0.0;
					for (i = 0; i < nstates; i++) {
						vleft += eleft_ptr[i] * partial_lh_left[i];
						vright += eright_ptr[i] * partial_lh_right[i];
					}
                    eleft_ptr += nstates;
                    eright_ptr += nstates;
					lh_max=max(lh_max, (partial_lh[c*nstates+x] = vleft*vright));
				}
                partial_lh_left += nstates;
                partial_lh_right += nstates;
            }

            assert(lh_max > 0.0);
            // check if one should scale partial likelihoods
            if (lh_max == 0.0) {
                // for very shitty data
                for (c = 0; c < ncat; c++)
                    memcpy(&partial_lh[c*nstates], &tip_partial_lh[aln->STATE_UNKNOWN*nstates], nstates*sizeof(double));
                sum_scale += LOG_SCALING_THRESHOLD* 4 * ptn_freq[ptn];
                //sum_scale += log(lh_max) * ptn_freq[ptn];
                dad_branch->scale_num[ptn] += 4;
//                int nsite = aln->getNSite();
//                for (i = 0, x = 0; i < nsite && x < ptn_freq[ptn]; i++)
//                    if (aln->getPatternID(i) == ptn) {
//                        outWarning((string)"Numerical underflow for site " + convertIntToString(i+1));
//                        x++;
//                    }
            } else if (lh_max < SCALING_THRESHOLD) {
                // now do the likelihood scaling
                for (i = 0; i < block; i++) {
                    partial_lh[i] *= SCALING_THRESHOLD_INVER;
                    //partial_lh[i] /= lh_max;
                }
                // unobserved const pattern will never have underflow
                sum_scale += LOG_SCALING_THRESHOLD * ptn_freq[ptn];
                //sum_scale += log(lh_max) * ptn_freq[ptn];
                dad_branch->scale_num[ptn] += 1;
            }

		}
		dad_branch->lh_scale_factor += sum_scale;

	}

    delete [] partial_lh_leaves;
    delete [] echildren;
}

//template <const int nstates>
void PhyloTree::computeNonrevLikelihoodDerv(PhyloNeighbor *dad_branch, PhyloNode *dad, double &df, double &ddf) {

    assert(rooted);

    PhyloNode *node = (PhyloNode*) dad_branch->node;
    PhyloNeighbor *node_branch = (PhyloNeighbor*) node->findNeighbor(dad);
    if (!central_partial_lh)
        initializeAllPartialLh();
    if (node->isLeaf() || (dad_branch->direction == AWAYFROM_ROOT && dad != root)) {
    	PhyloNode *tmp_node = dad;
    	dad = node;
    	node = tmp_node;
    	PhyloNeighbor *tmp_nei = dad_branch;
    	dad_branch = node_branch;
    	node_branch = tmp_nei;
    }
    if ((dad_branch->partial_lh_computed & 1) == 0)
        computeNonrevPartialLikelihood(dad_branch, dad);
    if ((node_branch->partial_lh_computed & 1) == 0)
        computeNonrevPartialLikelihood(node_branch, node);
    size_t nstates = aln->num_states;
    size_t nstatesqr = nstates*nstates;
    size_t ncat = site_rate->getNRate();

    size_t block = ncat * nstates;
    size_t ptn; // for big data size > 4GB memory required
    size_t c, i, x;
    size_t orig_nptn = aln->size();
    size_t nptn = aln->size()+model_factory->unobserved_ptns.size();

    double *trans_mat = new double[block*nstates*3];
    double *trans_derv1 = trans_mat + block*nstates;
    double *trans_derv2 = trans_derv1 + block*nstates;
    
	for (c = 0; c < ncat; c++) {
		double len = site_rate->getRate(c)*dad_branch->length;
		double prop = site_rate->getProp(c);
        double *this_trans_mat = &trans_mat[c*nstatesqr];
        double *this_trans_derv1 = &trans_derv1[c*nstatesqr];
        double *this_trans_derv2 = &trans_derv2[c*nstatesqr];
        model->computeTransDerv(len, this_trans_mat, this_trans_derv1, this_trans_derv2);
        double prop_rate = prop*site_rate->getRate(c);
        double prop_rate_2 = prop_rate * site_rate->getRate(c); 
		for (i = 0; i < nstatesqr; i++) {
			this_trans_mat[i] *= prop;
            this_trans_derv1[i] *= prop_rate;
            this_trans_derv2[i] *= prop_rate_2;
        }
	}

    double my_df = 0.0, my_ddf = 0.0, prob_const = 0.0, df_const = 0.0, ddf_const = 0.0;

    if (dad->isLeaf()) {
         // make sure that we do not estimate the virtual branch length from the root
        assert(dad != root);
    	// special treatment for TIP-INTERNAL NODE case
    	double *partial_lh_node = new double[(aln->STATE_UNKNOWN+1)*block*3];
        double *partial_lh_derv1 = partial_lh_node + (aln->STATE_UNKNOWN+1)*block; 
        double *partial_lh_derv2 = partial_lh_derv1 + (aln->STATE_UNKNOWN+1)*block; 
        IntVector states_dad = aln->seq_states[dad->id];
        states_dad.push_back(aln->STATE_UNKNOWN);
        // precompute information from one tip
        for (IntVector::iterator it = states_dad.begin(); it != states_dad.end(); it++) {
            double *lh_node = partial_lh_node +(*it)*block;
            double *lh_derv1 = partial_lh_derv1 +(*it)*block;
            double *lh_derv2 = partial_lh_derv2 +(*it)*block;
            double *lh_tip = tip_partial_lh + (*it)*nstates;
            double *trans_mat_tmp = trans_mat;
            double *trans_derv1_tmp = trans_derv1;
            double *trans_derv2_tmp = trans_derv2;
            for (c = 0; c < ncat; c++) {
                for (i = 0; i < nstates; i++) {
                    lh_node[i] = 0.0;
                    lh_derv1[i] = 0.0;
                    lh_derv2[i] = 0.0;
                    for (x = 0; x < nstates; x++) {
                        lh_node[i] += trans_mat_tmp[x] * lh_tip[x];
                        lh_derv1[i] += trans_derv1_tmp[x] * lh_tip[x];
                        lh_derv2[i] += trans_derv2_tmp[x] * lh_tip[x];
                    }
                    trans_mat_tmp += nstates;
                    trans_derv1_tmp += nstates;
                    trans_derv2_tmp += nstates;
                }
                lh_node += nstates;
                lh_derv1 += nstates;
                lh_derv2 += nstates;
            }
        }

    	// now do the real computation
#ifdef _OPENMP
#pragma omp parallel for reduction(+: my_df, my_ddf, prob_const, df_const, ddf_const) private(ptn, i, c) schedule(static)
#endif
    	for (ptn = 0; ptn < nptn; ptn++) {
			double lh_ptn = ptn_invar[ptn], df_ptn = 0.0, ddf_ptn = 0.0;
			double *partial_lh_dad = dad_branch->partial_lh + ptn*block;
			int state_dad;
            state_dad = (ptn < orig_nptn) ? (aln->at(ptn))[dad->id] : model_factory->unobserved_ptns[ptn-orig_nptn];
			double *lh_node = partial_lh_node + state_dad*block;
			double *lh_derv1 = partial_lh_derv1 + state_dad*block;
			double *lh_derv2 = partial_lh_derv2 + state_dad*block;
			for (c = 0; c < ncat; c++) {
				for (i = 0; i < nstates; i++) {
					lh_ptn += lh_node[i] * partial_lh_dad[i];
					df_ptn += lh_derv1[i] * partial_lh_dad[i];
					ddf_ptn += lh_derv2[i] * partial_lh_dad[i];
				}
				lh_node += nstates;
				lh_derv1 += nstates;
				lh_derv2 += nstates;
				partial_lh_dad += nstates;
			}
			assert(lh_ptn > 0.0);
            if (ptn < orig_nptn) {
                double df_frac = df_ptn / lh_ptn;
                double ddf_frac = ddf_ptn / lh_ptn;
                double freq = ptn_freq[ptn];
                double tmp1 = df_frac * freq;
                double tmp2 = ddf_frac * freq;
                my_df += tmp1;
                my_ddf += tmp2 - tmp1 * df_frac;
			} else {
                // bugfix 2016-01-21, prob_const can be rescaled
                if (dad_branch->scale_num[ptn] + node_branch->scale_num[ptn] >= 1)
                    lh_ptn *= SCALING_THRESHOLD;
//				_pattern_lh[ptn] = lh_ptn;
				prob_const += lh_ptn;
                df_const += df_ptn;
                ddf_const += ddf_ptn;
			}
        }
		delete [] partial_lh_node;
    } else {
    	// both dad and node are internal nodes
#ifdef _OPENMP
#pragma omp parallel for reduction(+: my_df, my_ddf, prob_const, df_const, ddf_const) private(ptn, i, c) schedule(static)
#endif
    	for (ptn = 0; ptn < nptn; ptn++) {
			double lh_ptn = ptn_invar[ptn], df_ptn = 0.0, ddf_ptn = 0.0;
			double *partial_lh_dad = dad_branch->partial_lh + ptn*block;
			double *partial_lh_node = node_branch->partial_lh + ptn*block;
			double *trans_mat_tmp = trans_mat;
			double *trans_derv1_tmp = trans_derv1;
			double *trans_derv2_tmp = trans_derv2;
			for (c = 0; c < ncat; c++) {
				for (i = 0; i < nstates; i++) {
                    double lh_state = 0.0;
                    double lh_derv1 = 0.0;
                    double lh_derv2 = 0.0;
                    for (x = 0; x < nstates; x++) {
                        lh_state += trans_mat_tmp[x] * partial_lh_node[x];
                        lh_derv1 += trans_derv1_tmp[x] * partial_lh_node[x];
                        lh_derv2 += trans_derv2_tmp[x] * partial_lh_node[x];
                    }
                    lh_ptn += partial_lh_dad[i] * lh_state;
                    df_ptn += partial_lh_dad[i] * lh_derv1;
                    ddf_ptn += partial_lh_dad[i] * lh_derv2;
                    trans_mat_tmp += nstates;
                    trans_derv1_tmp += nstates;
                    trans_derv2_tmp += nstates;
				}
				partial_lh_node += nstates;
				partial_lh_dad += nstates;
			}

			assert(lh_ptn > 0.0);
            if (ptn < orig_nptn) {
                double df_frac = df_ptn / lh_ptn;
                double ddf_frac = ddf_ptn / lh_ptn;
                double freq = ptn_freq[ptn];
                double tmp1 = df_frac * freq;
                double tmp2 = ddf_frac * freq;
                my_df += tmp1;
                my_ddf += tmp2 - tmp1 * df_frac;
			} else {
                // bugfix 2016-01-21, prob_const can be rescaled
                if (dad_branch->scale_num[ptn] + node_branch->scale_num[ptn] >= 1)
                    lh_ptn *= SCALING_THRESHOLD;
//				_pattern_lh[ptn] = lh_ptn;
				prob_const += lh_ptn;
                df_const += df_ptn;
                ddf_const += ddf_ptn;
			}
		}
    }

	df = my_df;
	ddf = my_ddf;
    assert(!isnan(df) && !isinf(df));

	if (orig_nptn < nptn) {
    	// ascertainment bias correction
    	prob_const = 1.0 - prob_const;
    	double df_frac = df_const / prob_const;
    	double ddf_frac = ddf_const / prob_const;
    	int nsites = aln->getNSite();
    	df += nsites * df_frac;
    	ddf += nsites *(ddf_frac + df_frac*df_frac);
    }

    delete [] trans_mat;
}

//template <const int nstates>
double PhyloTree::computeNonrevLikelihoodBranch(PhyloNeighbor *dad_branch, PhyloNode *dad) {

    assert(rooted);

    PhyloNode *node = (PhyloNode*) dad_branch->node;
    PhyloNeighbor *node_branch = (PhyloNeighbor*) node->findNeighbor(dad);
    if (!central_partial_lh)
        initializeAllPartialLh();
    if (node->isLeaf() || (dad_branch->direction == AWAYFROM_ROOT && dad != root)) {
    	PhyloNode *tmp_node = dad;
    	dad = node;
    	node = tmp_node;
    	PhyloNeighbor *tmp_nei = dad_branch;
    	dad_branch = node_branch;
    	node_branch = tmp_nei;
    }
    if ((dad_branch->partial_lh_computed & 1) == 0)
        computeNonrevPartialLikelihood(dad_branch, dad);
    if ((node_branch->partial_lh_computed & 1) == 0)
        computeNonrevPartialLikelihood(node_branch, node);
    double tree_lh = node_branch->lh_scale_factor + dad_branch->lh_scale_factor;
    size_t nstates = aln->num_states;
    size_t nstatesqr = nstates*nstates;
    size_t ncat = site_rate->getNRate();

    size_t block = ncat * nstates;
    size_t ptn; // for big data size > 4GB memory required
    size_t c, i, x;
    size_t orig_nptn = aln->size();
    size_t nptn = aln->size()+model_factory->unobserved_ptns.size();

    double *trans_mat = new double[block*nstates];
	for (c = 0; c < ncat; c++) {
		double len = site_rate->getRate(c)*dad_branch->length;
		double prop = site_rate->getProp(c);
        double *this_trans_mat = &trans_mat[c*nstatesqr];
        model->computeTransMatrix(len, this_trans_mat);
		for (i = 0; i < nstatesqr; i++)
			this_trans_mat[i] *= prop;
	}

	double prob_const = 0.0;
	memset(_pattern_lh_cat, 0, nptn*ncat*sizeof(double));

    if (dad->isLeaf()) {
    	// special treatment for TIP-INTERNAL NODE case
    	double *partial_lh_node = new double[(aln->STATE_UNKNOWN+1)*block];
        if (dad == root) {
            for (c = 0; c < ncat; c++) {
                double *lh_node = partial_lh_node + c*nstates;
                model->getStateFrequency(lh_node);
                double prop = site_rate->getProp(c);
                for (i = 0; i < nstates; i++)
                    lh_node[i] *= prop;
            }
        } else {
            IntVector states_dad = aln->seq_states[dad->id];
            states_dad.push_back(aln->STATE_UNKNOWN);
            // precompute information from one tip
            for (IntVector::iterator it = states_dad.begin(); it != states_dad.end(); it++) {
                double *lh_node = partial_lh_node +(*it)*block;
                double *lh_tip = tip_partial_lh + (*it)*nstates;
                double *trans_mat_tmp = trans_mat;
                for (c = 0; c < ncat; c++) {
                    for (i = 0; i < nstates; i++) {
                        lh_node[i] = 0.0;
                        for (x = 0; x < nstates; x++)
                            lh_node[i] += trans_mat_tmp[x] * lh_tip[x];
                        trans_mat_tmp += nstates;
                    }
                    lh_node += nstates;
                }
            }
        }

    	// now do the real computation
#ifdef _OPENMP
#pragma omp parallel for reduction(+: tree_lh, prob_const) private(ptn, i, c) schedule(static)
#endif
    	for (ptn = 0; ptn < nptn; ptn++) {
			double lh_ptn = ptn_invar[ptn];
			double *lh_cat = _pattern_lh_cat + ptn*ncat;
			double *partial_lh_dad = dad_branch->partial_lh + ptn*block;
			int state_dad;
            if (dad == root) 
                state_dad = 0;
            else
                state_dad = (ptn < orig_nptn) ? (aln->at(ptn))[dad->id] : model_factory->unobserved_ptns[ptn-orig_nptn];
			double *lh_node = partial_lh_node + state_dad*block;
			for (c = 0; c < ncat; c++) {
				for (i = 0; i < nstates; i++) {
					lh_cat[c] += lh_node[i] * partial_lh_dad[i];
				}
				lh_node += nstates;
				partial_lh_dad += nstates;
				lh_ptn += lh_cat[c];
//				lh_cat++;
			}
			assert(lh_ptn > 0.0);
			if (ptn < orig_nptn) {
				lh_ptn = log(fabs(lh_ptn));
				_pattern_lh[ptn] = lh_ptn;
				tree_lh += lh_ptn * ptn_freq[ptn];
			} else {
                // bugfix 2016-01-21, prob_const can be rescaled
                if (dad_branch->scale_num[ptn] >= 1)
                    lh_ptn *= SCALING_THRESHOLD;
//				_pattern_lh[ptn] = lh_ptn;
				prob_const += lh_ptn;
			}
		}
		delete [] partial_lh_node;
    } else {
    	// both dad and node are internal nodes
#ifdef _OPENMP
#pragma omp parallel for reduction(+: tree_lh, prob_const) private(ptn, i, c) schedule(static)
#endif
    	for (ptn = 0; ptn < nptn; ptn++) {
			double lh_ptn = ptn_invar[ptn];
			double *lh_cat = _pattern_lh_cat + ptn*ncat;
			double *partial_lh_dad = dad_branch->partial_lh + ptn*block;
			double *partial_lh_node = node_branch->partial_lh + ptn*block;
			double *trans_mat_tmp = trans_mat;
			for (c = 0; c < ncat; c++) {
				for (i = 0; i < nstates; i++) {
                    double lh_state = 0.0;
                    for (x = 0; x < nstates; x++)
                        lh_state += trans_mat_tmp[x] * partial_lh_node[x];
                    *lh_cat += partial_lh_dad[i] * lh_state;
                    trans_mat_tmp += nstates;
				}
				lh_ptn += *lh_cat;
				partial_lh_node += nstates;
				partial_lh_dad += nstates;
				lh_cat++;
			}

			assert(lh_ptn > 0.0);
            if (ptn < orig_nptn) {
				lh_ptn = log(fabs(lh_ptn));
				_pattern_lh[ptn] = lh_ptn;
				tree_lh += lh_ptn * ptn_freq[ptn];
			} else {
                // bugfix 2016-01-21, prob_const can be rescaled
                if (dad_branch->scale_num[ptn] + node_branch->scale_num[ptn] >= 1)
                    lh_ptn *= SCALING_THRESHOLD;
//				_pattern_lh[ptn] = lh_ptn;
				prob_const += lh_ptn;
			}
		}
    }

    if (isnan(tree_lh) || isinf(tree_lh)) {
        cout << "WARNING: Numerical underflow caused by alignment sites";
        i = aln->getNSite();
        int j;
        for (j = 0, c = 0; j < i; j++) {
            ptn = aln->getPatternID(j);
            if (isnan(_pattern_lh[ptn]) || isinf(_pattern_lh[ptn])) {
                cout << " " << j+1;
                c++;
                if (c >= 10) {
                    cout << " ...";
                    break;
                }
            }
        }
        cout << endl;
        tree_lh = current_it->lh_scale_factor + current_it_back->lh_scale_factor;
        for (ptn = 0; ptn < orig_nptn; ptn++) {
            if (isnan(_pattern_lh[ptn]) || isinf(_pattern_lh[ptn])) {
                _pattern_lh[ptn] = LOG_SCALING_THRESHOLD*4; // log(2^(-1024))
            }
            tree_lh += _pattern_lh[ptn] * ptn_freq[ptn];
        }
    }

    if (orig_nptn < nptn) {
    	// ascertainment bias correction
        if (prob_const >= 1.0 || prob_const < 0.0) {
            printTree(cout, WT_TAXON_ID + WT_BR_LEN + WT_NEWLINE);
            model->writeInfo(cout);
        }
        assert(prob_const < 1.0 && prob_const >= 0.0);

        // BQM 2015-10-11: fix this those functions using _pattern_lh_cat
//        double inv_const = 1.0 / (1.0-prob_const);
//        size_t nptn_cat = orig_nptn*ncat;
//    	for (ptn = 0; ptn < nptn_cat; ptn++)
//            _pattern_lh_cat[ptn] *= inv_const;
        
    	prob_const = log(1.0 - prob_const);
    	for (ptn = 0; ptn < orig_nptn; ptn++)
    		_pattern_lh[ptn] -= prob_const;
    	tree_lh -= aln->getNSite()*prob_const;
		assert(!isnan(tree_lh) && !isinf(tree_lh));
    }

	assert(!isnan(tree_lh) && !isinf(tree_lh));

    delete [] trans_mat;
    return tree_lh;
}
//
//  phylotreemixlen.h
//  iqtree
//
//  Created by Minh Bui on 24/08/15.
//
//

#ifndef __iqtree__phylotreemixlen__
#define __iqtree__phylotreemixlen__

#include <stdio.h>
#include "iqtree.h"


/**
    Phylogenetic tree with mixture of branch lengths
    Started within joint project with Stephen Crotty
*/
class PhyloTreeMixlen : public IQTree {

    friend class ModelFactoryMixlen;

public:

    /**
            default constructor
     */
    PhyloTreeMixlen();

    PhyloTreeMixlen(Alignment *aln, int mixlen);

    virtual ~PhyloTreeMixlen();

    /**
            allocate a new node. Override this if you have an inherited Node class.
            @param node_id node ID
            @param node_name node name
            @return a new node
     */
    virtual Node* newNode(int node_id = -1, const char* node_name = NULL);

    /**
            allocate a new node. Override this if you have an inherited Node class.
            @param node_id node ID
            @param node_name node name issued by an interger
            @return a new node
     */
    virtual Node* newNode(int node_id, int node_name);

    /**
        @return true if this is a tree with mixture branch lengths, default: false
    */
    virtual bool isMixlen() { return true; }

    /**
        @return number of mixture branch lengths, default: 1
    */
    virtual int getMixlen() { return mixlen; }

    /**
        set number of mixture branch lengths
    */
    void setMixlen(int mixlen);

    /**
            @param[out] lenvec tree lengths for each class in mixlen model
            @param node the starting node, NULL to start from the root
            @param dad dad of the node, used to direct the search
     */
    virtual void treeLengths(DoubleVector &lenvec, Node *node = NULL, Node *dad = NULL);

    /**
     * assign branch length as mean over all branch lengths of categories
     */
    void assignMeanMixBranches(Node *node = NULL, Node *dad = NULL);

    /**
     *  internal function called by printTree to print branch length
     *  @param out output stream
     *  @param length_nei target Neighbor to print
     */
    virtual void printBranchLength(ostream &out, int brtype, bool print_slash, Neighbor *length_nei);

    /**
            print tree to .treefile
            @param params program parameters, field root is taken
     */
    virtual void printResultTree(string suffix = "");

    /**
        initialize mixture branch lengths
    */
    void initializeMixBranches(PhyloNode *node = NULL, PhyloNode *dad = NULL);

    /** initialize parameters if necessary */
    void initializeMixlen(double tolerance);

    /**
     * IMPORTANT: semantic change: this function does not return score anymore, for efficiency purpose
            optimize one branch length by ML
            @param node1 1st end node of the branch
            @param node2 2nd end node of the branch
            @param clearLH true to clear the partial likelihood, otherwise false
            @param maxNRStep maximum number of Newton-Raphson steps
            @return likelihood score
     */
    virtual void optimizeOneBranch(PhyloNode *node1, PhyloNode *node2, bool clearLH = true, int maxNRStep = 100);

    /**
            optimize all branch lengths of the tree
            @param iterations number of iterations to loop through all branches
            @return the likelihood of the tree
     */
    virtual double optimizeAllBranches(int my_iterations = 100, double tolerance = TOL_LIKELIHOOD, int maxNRStep = 100);

	/**
		This function calculate f(value), first derivative f'(value) and 2nd derivative f''(value).
		used by Newton raphson method to minimize the function.
		Please always override this function to adapt to likelihood or parsimony score.
		The default is for function f(x) = x^2.
		@param value x-value of the function
		@param df (OUT) first derivative
		@param ddf (OUT) second derivative
	*/
//	virtual void computeFuncDervMulti(double *value, double *df, double *ddf);

    /**
            Inherited from Optimization class.
            This function calculate f(value), first derivative f'(value) and 2nd derivative f''(value).
            used by Newton raphson method to minimize the function.
            @param value current branch length
            @param df (OUT) first derivative
            @param ddf (OUT) second derivative
            @return negative of likelihood (for minimization)
     */
//    virtual void computeFuncDerv(double value, double &df, double &ddf);

	/**
		return the number of dimensions
	*/
	virtual int getNDim();


	/**
		the target function which needs to be optimized
		@param x the input vector x
		@return the function value at x
	*/
	virtual double targetFunk(double x[]);

	/**
		the approximated derivative function
		@param x the input vector x
		@param dfx the derivative at x
		@return the function value at x
	*/
	virtual double derivativeFunk(double x[], double dfx[]);

    /** number of mixture categories */
    int mixlen;

    /** current category, for optimizing branch length */
    int cur_mixture;

protected:
    
    /** relative rate, used to initialize branch lengths */
    RateHeterogeneity *relative_rate;

};

#endif /* defined(__iqtree__phylotreemixlen__) */
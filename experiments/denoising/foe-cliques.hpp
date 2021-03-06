#ifndef _FOE_CLIQUES_HPP_
#define _FOE_CLIQUES_HPP_
/*
 * foe-cliques.hpp
 *
 * Copyright 2012 Alexander Fix
 * See LICENSE.txt for license information
 *
 * Example of deriving from CliqueEnergy (defined in clique.hpp) to implement 
 * the Field of Experts energy used for the denoising algorithm
 */

#include "multilabel-energy.hpp"
#include <math.h>
#include <iostream>

const double DoubleToREAL = 10000;

/*
 * The Field of Experts energy, defined for a 2x2 patch of the image.
 * Note that the only thing we really need to override from the abstract 
 * base class is operator(), which actually calculates the FoE energy of a 
 * 2x2 patch.
 */
class FoEEnergy : public Clique {
    public:
        FoEEnergy(const int* nodes) {
            for (int i = 0; i < 4; ++i)
                m_nodes[i] = nodes[i];
        }

        virtual REAL energy(const Label buf[]) const override;
        virtual const VarId* nodes() const override { return m_nodes; }
        virtual size_t size() const override { return 4; }

    protected:
        VarId m_nodes[4];
};

/*
 * The unary energy penalizes the squared distance from the original 
 * observed value. 
 */
REAL FoEUnaryEnergy(unsigned char orig, unsigned char label, double sigma);

void AddFoEGrad(const Clique& clique, const std::vector<Clique::Label>& current,
        std::vector<double>& grad);
double FoEUnaryGrad(Clique::Label orig, Clique::Label current, double sigma);

#endif

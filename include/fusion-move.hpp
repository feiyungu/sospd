#ifndef _FUSION_MOVE_HPP_
#define _FUSION_MOVE_HPP_

/*
 * fusion-move.hpp
 *
 * Copyright 2012 Alexander Fix
 * See LICENSE.txt for license information
 *
 * Computes a fusion move between the current and proposed image.
 *
 * A fusion move takes two images (current and proposed) and tries to perform
 * the optimal move where each pixel is allowed to either stay at its current
 * value, or switch to its label in the proposed image. This is a 
 * generalization of alpha-expansion, where in alpha-expansion each pixel is 
 * allowed to either stay the same, or change to a fixed value alpha. That is,
 * alpha expansion is a fusion move where the proposed image is just the flat
 * image with value alpha at all pixels.
 */

#include <iostream>
#include <sstream>
#include <boost/foreach.hpp>
#include <functional>
#include <vector>
#include "higher-order-energy.hpp"
#include "HOCR.h"
#include "clique.hpp"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wall"
#pragma clang diagnostic ignored "-Wdeprecated-writable-strings"
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#include "QPBO.h"
#pragma clang diagnostic pop
#include "generic-higher-order.hpp"

template <int MaxDegree>
class FusionMove {
    public:
        typedef MultilabelEnergy::NodeId NodeId;
        typedef MultilabelEnergy::Label Label;
        typedef std::vector<Label> LabelVec;
        typedef std::function<void(int, const LabelVec&, LabelVec&)> ProposalCallback;
        FusionMove(const MultilabelEnergy* energy, const ProposalCallback& pc)
            : m_energy(energy), m_pc(pc), m_labels(energy->NumNodes(), 0), m_iter(0), m_hocr(false) { }
        FusionMove(const MultilabelEnergy* energy, const ProposalCallback& pc, const LabelVec& current)
            : m_energy(energy), m_pc(pc), m_labels(current), m_iter(0), m_hocr(false) { }

        void Solve(int niters);
        Label GetLabel(NodeId i) const { return m_labels[i]; }
        void SetHOCR(bool b) { m_hocr = b; }

    protected:
        template <typename HO>
        void SetupFusionEnergy(const LabelVec& proposed,
                HO& hoe) const;
        void GetFusedImage(const LabelVec& proposed, QPBO<REAL>& qr);
        void FusionStep();
    
        const MultilabelEnergy* m_energy;
        ProposalCallback m_pc;
        LabelVec m_labels;
        int m_iter;
        bool m_hocr;
};

template <int MaxDegree>
void FusionMove<MaxDegree>::Solve(int niters) {
    for (int i = 0; i < niters; ++i)
        FusionStep();
}

template <int MaxDegree>
void FusionMove<MaxDegree>::FusionStep() {
    if (m_hocr) {
        PBF<REAL, MaxDegree> pbf;
        LabelVec proposed(m_labels.size());
        m_pc(m_iter, m_labels, proposed);
        SetupFusionEnergy(proposed, pbf);
        PBF<REAL, 2> qr;
        pbf.toQuadratic(qr);
        pbf.clear();
        int numvars = qr.maxID();
        QPBO<REAL> qpbo(numvars, numvars*4);
        convert(qpbo, qr);
        qpbo.AddNode(m_labels.size());
        qr.clear();
        qpbo.MergeParallelEdges();
        qpbo.Solve();
        qpbo.ComputeWeakPersistencies();
        GetFusedImage(proposed, qpbo);
    } else {
        HigherOrderEnergy<REAL, MaxDegree> hoe;
        QPBO<REAL> qr(m_labels.size(), 0);
        LabelVec proposed(m_labels.size());
        m_pc(m_iter, m_labels, proposed);
        SetupFusionEnergy(proposed, hoe);
        hoe.ToQuadratic(qr);
        qr.MergeParallelEdges();
        qr.Solve();
        qr.ComputeWeakPersistencies();
        GetFusedImage(proposed, qr);
    }
    m_iter++;
}

template <int MaxDegree>
void FusionMove<MaxDegree>::GetFusedImage(const LabelVec& proposed, QPBO<REAL>& qr) {
    for (size_t i = 0; i < m_labels.size(); ++i) {
        int label = qr.GetLabel(i);
        if (label == 1) {
            m_labels[i] = proposed[i];
        }
    }
}

template <int MaxDegree>
template <typename HO>
void FusionMove<MaxDegree>::SetupFusionEnergy(const LabelVec& proposed, HO& hoe) const {
    AddVars(hoe,m_energy->NumNodes());
    for (NodeId i = 0; i < m_energy->NumNodes(); ++i)
        hoe.AddUnaryTerm(i, m_energy->Unary(i, m_labels[i]), m_energy->Unary(i, proposed[i]));

    std::vector<REAL> energy_table;
    for (const auto& cp : m_energy->Cliques()) {
        const Clique& c = *cp;
        NodeId size = c.Size();
        ASSERT(size > 1);

        uint32_t numAssignments = 1 << size;
        energy_table.resize(numAssignments);
        
        // For each boolean assignment, get the clique energy at the 
        // corresponding labeling
        std::vector<Label> cliqueLabels(size);
        for (uint32_t assignment = 0; assignment < numAssignments; ++assignment) {
            for (NodeId i = 0; i < size; ++i) {
                if (assignment & (1 << i)) { 
                    cliqueLabels[i] = proposed[c.Nodes()[i]];
                } else {
                    cliqueLabels[i] = m_labels[c.Nodes()[i]];
                }
            }
            energy_table[assignment] = c.Energy(cliqueLabels.data());
        }
        std::vector<NodeId> nodes(c.Nodes(), c.Nodes() + c.Size());
        AddClique(hoe, int(c.Size()), energy_table.data(), nodes.data());
    }
}

#endif

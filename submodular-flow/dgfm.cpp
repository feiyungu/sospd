#include "dgfm.hpp"
#include "clique.hpp"

DualGuidedFusionMove::DualGuidedFusionMove(Label max_label)
    : m_num_labels(max_label),
    m_constant_term(0),
    m_cliques(),
    m_unary_cost(),
    m_labels(),
    m_fusion_labels()
{ }

DualGuidedFusionMove::NodeId DualGuidedFusionMove::AddNode(int n) {
    NodeId ret = m_labels.size();
    for (int i = 0; i < n; ++i) {
        m_labels.push_back(0);
        m_fusion_labels.push_back(0);
        UnaryCost uc(m_num_labels, 0);
        m_unary_cost.push_back(uc);
    }
    return ret;
}

int DualGuidedFusionMove::GetLabel(NodeId i) const {
    return m_labels[i];
}

void DualGuidedFusionMove::AddConstantTerm(REAL c) {
    m_constant_term += c;
}

void DualGuidedFusionMove::AddUnaryTerm(NodeId i, const std::vector<REAL>& coeffs) {
    ASSERT(coeffs.size() == m_num_labels);
    for (size_t j = 0; j < m_num_labels; ++j) {
        m_unary_cost[i][j] += coeffs[j];
    }
}

void DualGuidedFusionMove::AddClique(const CliquePtr& cp) {
    m_cliques.push_back(cp);
}

void DualGuidedFusionMove::InitialLabeling() {
    const NodeId n = m_unary_cost.size();
    for (NodeId i = 0; i < n; ++i) {
        REAL best_cost = std::numeric_limits<REAL>::max();
        for (size_t l = 0; l < m_num_labels; ++l) {
            if (m_unary_cost[i][l] < best_cost) {
                best_cost = m_unary_cost[i][l];
                m_labels[i] = l;
            }
        }
    }
}

void DualGuidedFusionMove::InitialDual() {
    m_dual.clear();
    std::vector<Label> labelBuf;
    for (const CliquePtr& cp : m_cliques) {
        const Clique& c = *cp;
		const std::vector<NodeId>& nodes = c.Nodes();
		int k = nodes.size();
        labelBuf.resize(k);
		for (int i = 0; i < k; ++i) {
            labelBuf[i] = m_labels[nodes[i]];
		}
		REAL energy = c.Energy(labelBuf);
        m_dual.push_back(Dual(k, std::vector<REAL>(m_num_labels, 0)));
		Dual& newDual = m_dual.back();
        
        ASSERT(energy >= 0);
        REAL avg = energy / k;
        int remainder = energy % k;
        for (int i = 0; i < k; ++i) {
            newDual[i][m_labels[nodes[i]]] = avg;
            if (i < remainder) // Have to distribute remainder to maintain average
                newDual[i][m_labels[nodes[i]]] += 1;
        }
    }
}

void DualGuidedFusionMove::InitialNodeCliqueList() {
    size_t n = m_labels.size();
    m_node_clique_list.clear();
    m_node_clique_list.resize(n);

    int clique_index = 0;
    for (const CliquePtr& cp : m_cliques) {
        const Clique& c = *cp;
        const std::vector<NodeId>& nodes = c.Nodes();
        const size_t k = nodes.size();
        for (size_t i = 0; i < k; ++i) {
            m_node_clique_list[nodes[i]].push_back(std::make_pair(clique_index, i));
        }
        ++clique_index;
    }
}

void DualGuidedFusionMove::PreEditDual(SubmodularIBFS& crf) {
    // Allocate all the buffers we need in one place, resize as necessary
    std::vector<Label> label_buf;
    std::vector<Label> current_labels;
    std::vector<Label> fusion_labels;
    std::vector<REAL> psi;
    std::vector<REAL> current_lambda;
    std::vector<REAL> fusion_lambda;

    SubmodularIBFS::CliqueVec& ibfs_cliques = crf.GetCliques();
    int clique_index = 0;
    for (const CliquePtr& cp : m_cliques) {
        const Clique& c = *cp;
        const size_t k = c.Nodes().size();
        ASSERT(k < 32);

        auto& ibfs_c = *ibfs_cliques[clique_index];
        ASSERT(k == ibfs_c.Size());
        std::vector<REAL>& energy_table = ibfs_c.EnergyTable();
        Assgn max_assgn = 1 << k;
        ASSERT(energy_table.size() == max_assgn);

        psi.resize(k);
        label_buf.resize(k);
        current_labels.resize(k);
        fusion_labels.resize(k);
        current_lambda.resize(k);
        fusion_lambda.resize(k);
        Assgn fusion_equals_current = 0;
        for (size_t i = 0; i < k; ++i) {
            current_labels[i] = m_labels[c.Nodes()[i]];
            fusion_labels[i] = m_fusion_labels[c.Nodes()[i]];
            current_lambda[i] = m_dual[clique_index][i][current_labels[i]];
            fusion_lambda[i] = m_dual[clique_index][i][fusion_labels[i]];
            if (current_labels[i] == fusion_labels[i])
                fusion_equals_current |= (1 << i);
        }
        
        // Compute costs of all fusion assignments
        for (Assgn a = 0; a < max_assgn; ++a) {
            for (size_t i_idx = 0; i_idx < k; ++i_idx) {
                if (a & (1 << i_idx)) 
                    label_buf[i_idx] = fusion_labels[i_idx];
                else 
                    label_buf[i_idx] = current_labels[i_idx];
            }
            energy_table[a] = c.Energy(label_buf);
            ASSERT(energy_table[a] >= 0);
        }

        // Find g with g(S) >= f(S) and g submodular. Also want to make sure
        // that g(S | T) == g(S) where T is the set of nodes with 
        // current[i] == fusion[i]
        std::vector<REAL> upper_bound = SubmodularUpperBound(k, energy_table);
        upper_bound = ZeroMarginalSet(k, upper_bound, fusion_equals_current);
        ASSERT(CheckUpperBoundInvariants(k, energy_table, upper_bound));
        energy_table = upper_bound;

        // Compute the residual function 
        // g(S) - lambda_fusion(S) - lambda_current(C\S)
        SubtractLinear(k, energy_table, fusion_lambda, current_lambda);
        ASSERT(energy_table[0] == 0); // Check tightness of current labeling

        // Modify g, find psi so that g(S) + psi(S) >= 0
        Normalize(k, energy_table, psi);

        // Update lambda_fusion[i] so that 
        // g(S) - lambda_fusion(S) - lambda_current(C\S) >= 0
        for (size_t i = 0; i < k; ++i) {
            m_dual[clique_index][i][fusion_labels[i]] -= psi[i];
        }

        ++clique_index;
    }
}

REAL DualGuidedFusionMove::ComputeHeight(NodeId i, Label x) {
    REAL ret = m_unary_cost[i][x];
    for (const auto& p : m_node_clique_list[i]) {
        ret += m_dual[p.first][p.second][x];
    }
    return ret;
}

REAL DualGuidedFusionMove::ComputeHeightDiff(NodeId i, Label l1, Label l2) const {
    REAL ret = m_unary_cost[i][l1] - m_unary_cost[i][l2];
    for (const auto& p : m_node_clique_list[i]) {
        const auto& lambda_Ci = m_dual[p.first][p.second];
        ret += lambda_Ci[l1] - lambda_Ci[l2];
    }
    return ret;
}

void DualGuidedFusionMove::SetupGraph(SubmodularIBFS& crf) {
    typedef int32_t Assgn;
    const size_t n = m_labels.size();
    crf.AddNode(n);

    size_t clique_index = 0;
    for (const CliquePtr& cp : m_cliques) {
        const Clique& c = *cp;
        const size_t k = c.Size();
        ASSERT(k < 32);
        const Assgn max_assgn = 1 << k;
        crf.AddClique(c.Nodes(), std::vector<REAL>(max_assgn, 0), false);
        ++clique_index;
    }

    crf.GraphInit();
}

void DualGuidedFusionMove::SetupAlphaEnergy(SubmodularIBFS& crf) {
    typedef int32_t Assgn;
    const size_t n = m_labels.size();
    crf.ClearUnaries();
    crf.AddConstantTerm(-crf.GetConstantTerm());
    for (size_t i = 0; i < n; ++i) {
        REAL height_diff = ComputeHeightDiff(i, m_labels[i], m_fusion_labels[i]);
        if (height_diff > 0) {
            crf.AddUnaryTerm(i, height_diff, 0);
        }
        else {
            crf.AddUnaryTerm(i, 0, -height_diff);
        }
    }
}

bool DualGuidedFusionMove::UpdatePrimalDual(SubmodularIBFS& crf) {
    bool ret = false;
    SetupAlphaEnergy(crf);
    crf.Solve();
    NodeId n = m_labels.size();
    for (NodeId i = 0; i < n; ++i) {
        int crf_label = crf.GetLabel(i);
        if (crf_label == 1) {
            Label alpha = m_fusion_labels[i];
            if (m_labels[i] != alpha) ret = true;
            m_labels[i] = alpha;
        }
    }
    SubmodularIBFS::CliqueVec clique = crf.GetCliques();
    size_t i = 0;
    for (const CliquePtr& cp : m_cliques) {
        const Clique& c = *cp;
        SubmodularIBFS::CliquePtr ibfs_c = clique[i];
        const std::vector<REAL>& phiCi = ibfs_c->AlphaCi();
        for (size_t j = 0; j < phiCi.size(); ++j) {
            m_dual[i][j][m_fusion_labels[c.Nodes()[j]]] += phiCi[j];
        }
        ++i;
    }
    return ret;
}

void DualGuidedFusionMove::PostEditDual() {
    std::vector<Label> labelBuf;
    int clique_index = 0;
    for (const CliquePtr& cp : m_cliques) {
        const Clique& c = *cp;
        const std::vector<NodeId>& nodes = c.Nodes();
        int k = nodes.size();
        labelBuf.resize(k);
		for (int i = 0; i < k; ++i) {
            labelBuf[i] = m_labels[nodes[i]];
		}
		REAL energy = c.Energy(labelBuf);
        REAL avg = energy / k;
        int remainder = energy % k;
		for (int i = 0; i < k; ++i) {
		    m_dual[clique_index][i][labelBuf[i]] = avg;
            if (i < remainder)
                m_dual[clique_index][i][labelBuf[i]] += 1;
		}
		++clique_index;
    }
}

void DualGuidedFusionMove::DualFit() {
    // FIXME: This is the only function that doesn't work with integer division.
    // It's also not really used for anything at the moment
    /*
	for (size_t i = 0; i < m_dual.size(); ++i)
		for (size_t j = 0; j < m_dual[i].size(); ++j)
			for (size_t k = 0; k < m_dual[i][j].size(); ++k)
				m_dual[i][j][k] /= (m_mu * m_rho);
                */
}

bool DualGuidedFusionMove::InitialFusionLabeling() {
    const size_t n = m_labels.size();
    bool different = false;
    for (size_t i = 0; i < n; ++i) {
        m_fusion_labels[i] = m_labels[i];
        REAL h = ComputeHeight(i, m_labels[i]);
        for (Label l = 0; l < m_num_labels; ++l) {
            REAL newH = ComputeHeight(i, l);
            if (newH < h) {
                different = true;
                m_fusion_labels[i] = l;
                h = newH;
            }
        }
    }
    return different;
}

void DualGuidedFusionMove::Solve() {
	#ifdef PROGRESS_DISPLAY
		std::cout << "(" << std::endl;
	#endif
	m_num_cliques = m_cliques.size();
	ComputeRho();
    SubmodularIBFS crf;
    SetupGraph(crf);
	InitialLabeling();
	InitialDual();
	InitialNodeCliqueList();
	#ifdef PROGRESS_DISPLAY
		size_t num_round = 0;
		REAL energy = ComputeEnergy();
		std::cout << "Iteration " << num_round << ": " << energy << std::endl;
	#endif
	#ifdef CHECK_INVARIANTS
        ASSERT(CheckLabelInvariant());
        ASSERT(CheckDualBoundInvariant());
        ASSERT(CheckActiveInvariant());
	#endif
	bool labelChanged = true;
	while (labelChanged){
        labelChanged = InitialFusionLabeling();
        if (!labelChanged) break;
	    PreEditDual(crf);
		#ifdef CHECK_INVARIANTS
            ASSERT(CheckLabelInvariant());
            ASSERT(CheckDualBoundInvariant());
            ASSERT(CheckActiveInvariant());
	    #endif
        UpdatePrimalDual(crf);
		PostEditDual();
		#ifdef CHECK_INVARIANTS
            ASSERT(CheckLabelInvariant());
            ASSERT(CheckDualBoundInvariant());
            ASSERT(CheckActiveInvariant());
	    #endif
		#ifdef PROGRESS_DISPLAY
			energy = ComputeEnergy();
			num_round++;
			std::cout << "Iteration " << num_round << ": " << energy << std::endl;
		#endif
	}
	#ifdef CHECK_INVARIANTS
	    ASSERT(CheckHeightInvariant());
	#endif
	DualFit();
    #ifdef PROGRESS_DISPLAY
	    std::cout << ")" << std::endl;
    #endif
}

REAL DualGuidedFusionMove::ComputeEnergy() const {
    return ComputeEnergy(m_labels);
}

REAL DualGuidedFusionMove::ComputeEnergy(const std::vector<Label>& labels) const {
    REAL energy = m_constant_term;
    std::vector<Label> labelBuf;
    for (const CliquePtr& cp : m_cliques) {
        const Clique& c = *cp;
        labelBuf.clear();
        for (NodeId i : c.Nodes()) 
            labelBuf.push_back(m_labels[i]);
        energy += c.Energy(labelBuf);
    }
    const NodeId n = m_labels.size();
    for (NodeId i = 0; i < n; ++i)
        energy += m_unary_cost[i][labels[i]];
    return energy;
}

void DualGuidedFusionMove::ComputeRho() {
    m_rho = 1;
    for (const CliquePtr& cp : m_cliques) {
        Clique& c = *cp;
        m_rho = std::max(m_rho, c.Rho());
    }
}

double DualGuidedFusionMove::GetRho() {
    return m_rho;
}

bool DualGuidedFusionMove::CheckHeightInvariant() {
    size_t m = m_labels.size();
    for (size_t i = 0; i < m; ++i) {
        REAL hx = ComputeHeight(i, m_labels[i]);
        for (Label alpha = 0; alpha < m_num_labels; ++alpha) {
            if (alpha == m_labels[i]) continue;
            REAL halpha = ComputeHeight(i, alpha);
            if (hx > halpha) {
                std::cout << "Variable: " << i << std::endl;
                std::cout << "Label: " << m_labels[i] << " Height: " << hx << std::endl;
                std::cout << "Label: " << alpha << " Height: " << halpha << std::endl;
                return false;
            }
        }
    }
    return true;
}

bool DualGuidedFusionMove::CheckLabelInvariant() {
    size_t clique_index = 0;
    std::vector<Label> labelBuf;
    for (const CliquePtr& cp : m_cliques) {
        const Clique& c = *cp;
        const std::vector<NodeId>& nodes = c.Nodes();
        const size_t k = nodes.size();
        labelBuf.resize(k);
        for (size_t i = 0; i < k; ++i) {
            labelBuf[i] = m_labels[nodes[i]];
        }
        REAL energy = c.Energy(labelBuf);
        REAL sum = 0;
        for (size_t i = 0; i < k; ++i) {
            sum += m_dual[clique_index][i][labelBuf[i]];
        }
        if (abs(sum - energy)) {
            std::cout << "CliqueId: " << clique_index << std::endl;
            std::cout << "Energy: " << energy << std::endl;
            std::cout << "DualSum: " << sum << std::endl;
            return false;
        }
        clique_index++;
    }
    return true;
}

bool DualGuidedFusionMove::CheckDualBoundInvariant() {
    size_t clique_index = 0;
    for (const CliquePtr& cp : m_cliques) {
        const Clique& c = *cp;
        REAL energyBound = c.m_f_max;
        for (size_t i = 0; i < m_dual[clique_index].size(); ++i) {
            for (size_t j = 0; j < m_num_labels; ++j) {
                if (m_dual[clique_index][i][j] > energyBound) {
                    std::cout << "CliqueId: " << clique_index << std::endl;
                    std::cout << "NodeId (w.r.t. Clique): " << i << std::endl;
                    std::cout << "Label: " << j << std::endl;
                    std::cout << "Dual Value: " << m_dual[clique_index][i][j] << std::endl;
                    std::cout << "Energy Bound: " << energyBound << std::endl;
                    return false;
                }
            }
        }
        clique_index++;
    }
    return true;
}

bool DualGuidedFusionMove::CheckActiveInvariant() {
    size_t clique_index = 0;
    for (const CliquePtr& cp : m_cliques) {
        const Clique& c = *cp;
        const std::vector<NodeId>& nodes = c.Nodes();
        const size_t k = nodes.size();
        for (size_t i = 0; i < k; ++i) {
            if (m_dual[clique_index][i][m_labels[nodes[i]]] < 0) {
                std::cout << "CliqueId: " << clique_index << std::endl;
                std::cout << "NodeId (w.r.t. Clique): " << i << std::endl;
                std::cout << "Dual Value: " << m_dual[clique_index][i][m_labels[nodes[i]]] << std::endl;
                return false;
            }
        }
        clique_index++;
    }
    return true;
}

/*
bool DualGuidedFusionMove::CheckZeroSumInvariant() {
    size_t clique_index = 0;
    for (const CliquePtr& cp : m_cliques) {
        const Clique& c = *cp;
        const size_t k = c.Nodes().size();
        for (Label i = 0; i < m_num_labels; ++i) {
            REAL dualSum = 0;
            for (size_t j = 0; j < k; ++j) {
                dualSum += m_dual[clique_index][j][i];
            }
            if (abs(dualSum) > 0) {
                std::cout << "CliqueId: " << clique_index << std::endl;
                std::cout << "Label: " << i << std::endl;
                std::cout << "Dual Sum: " << dualSum << std::endl;
                return false;
            }
        }
        clique_index++;
    }
    return true;
}
*/

#ifndef _SVM_CXX_HPP_
#define _SVM_CXX_HPP_

extern "C" {
#include "svm_light/svm_common.h"
#include "svm_struct/svm_struct_common.h"
}
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <boost/serialization/vector.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

#include "submodular-flow.hpp"
#include "higher-order-energy.hpp"
#include "QPBO.h"
#include "gmm.hpp"

inline REAL doubleToREAL(double d) { return (REAL)(d * 500000.0); }

class PatternData {
    public:
        PatternData(const std::string& name, const cv::Mat& im, const cv::Mat& tri);
        std::string m_name;
        cv::Mat m_image;
        cv::Mat m_tri;
        cv::Mat m_bgdModel;
        GMM m_bgdGMM;
        cv::Mat m_fgdModel;
        GMM m_fgdGMM;
        double m_beta;
        cv::Mat m_downW;
        cv::Mat m_rightW;
};


class LabelData {
    public:
        LabelData() = default;
        LabelData(const std::string& name, const cv::Mat& gt);
        bool operator==(const LabelData& l) const;
        double Loss(const LabelData& l) const;

        std::string m_name;
        cv::Mat m_gt;

    private:
        friend class boost::serialization::access;
        template <class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & m_gt;
        }
};


//typedef SubmodularFlow CRF;
typedef SubmodularFlow CRF;

template <typename PatternData, typename LabelData, typename CRF>
class FeatureGroup {
    public:
        virtual size_t NumFeatures() const = 0;
        virtual std::vector<FVAL> Psi(const PatternData& p, const LabelData& l) const = 0;
        virtual void AddToCRF(CRF& c, const PatternData& p, double* w) const = 0;
        typedef std::vector<std::pair<std::vector<std::pair<size_t, double>>, double>> Constr;
        virtual Constr CollectConstrs(size_t base) const { return Constr(); }
        virtual double MaxViolation(size_t base, double* w) const { return 0.0; }
};

typedef FeatureGroup<PatternData, LabelData, CRF> FG;

class ModelData {
    public:
        typedef QPBO<REAL> QR;
        ModelData();

        long NumFeatures() const;
        void InitializeCRF(CRF& crf, const PatternData& p) const;
        void AddLossToCRF(CRF& crf, const PatternData& p, const LabelData& l) const;
        LabelData* ExtractLabel(const CRF& crf, const PatternData& x) const;
        LabelData* Classify(const PatternData& x, STRUCTMODEL* sm) const;
        LabelData* FindMostViolatedConstraint(const PatternData& x, const LabelData& y, STRUCTMODEL* sm) const;
        std::vector<std::shared_ptr<FG>> m_features;
    private:
        friend class boost::serialization::access;
        template <class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            // FIXME: Write out features???
        }
};



#endif


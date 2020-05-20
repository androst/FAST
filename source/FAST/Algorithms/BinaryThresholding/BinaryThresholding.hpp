#ifndef BINARY_THRESHOLDING_HPP
#define BINARY_THRESHOLDING_HPP

#include "FAST/Algorithms/SegmentationAlgorithm.hpp"

namespace fast {

class FAST_EXPORT  BinaryThresholding : public SegmentationAlgorithm {
    FAST_OBJECT(BinaryThresholding)
    public:
        void setLowerThreshold(float threshold);
        void setUpperThreshold(float threshold);
        void loadAttributes() override;
    private:
        BinaryThresholding();
        void execute();
        void waitToFinish();

        float mLowerThreshold;
        float mUpperThreshold;
        bool mLowerThresholdSet;
        bool mUpperThresholdSet;
};

}

#endif

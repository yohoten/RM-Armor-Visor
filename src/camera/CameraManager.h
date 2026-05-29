#ifndef CAMERA_MANAGER_H
#define CAMERA_MANAGER_H

#include <opencv2/opencv.hpp>

class CameraManager {
public:
    CameraManager(int camera_id);
    ~CameraManager();
    bool initialize();
    bool read(cv::Mat& frame);
    void release();
    
    // Camera control methods
    void setExposure(int value);
    void setGain(int value);
    int getExposure() const;
    int getGain() const;
    void setBrightCondition();
    void setNormalCondition();
    void setDarkCondition();
    
private:
    cv::VideoCapture cap;
    int camera_id;

public:
    // Public camera parameters for UI access
    int exposure;
    int gain;
};

#endif // CAMERA_MANAGER_H
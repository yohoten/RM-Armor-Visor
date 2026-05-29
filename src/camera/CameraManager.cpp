#include "CameraManager.h"
#include <iostream>

CameraManager::CameraManager(int id) : camera_id(id), exposure(30), gain(50) {}

CameraManager::~CameraManager() {
    if (cap.isOpened()) {
        cap.release();
    }
}

bool CameraManager::initialize() {
    cap.open(camera_id);
    if (!cap.isOpened()) {
        std::cerr << "Failed to open camera " << camera_id << std::endl;
        return false;
    }
    
    // Set camera properties
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
    cap.set(cv::CAP_PROP_FPS, 60);
    
    // Set initial camera parameters
    setExposure(exposure);
    setGain(gain);
    
    return true;
}

bool CameraManager::read(cv::Mat& frame) {
    return cap.read(frame);
}

// No changes needed - this method is already correct

void CameraManager::release() {
    if (cap.isOpened()) {
        cap.release();
    }
}

void CameraManager::setExposure(int value) {
    exposure = value;
    // Note: Exposure setting may not work on all cameras
    // Use negative values for manual exposure control
    cap.set(cv::CAP_PROP_EXPOSURE, -exposure);
}

void CameraManager::setGain(int value) {
    gain = value;
    // Note: Gain setting may not work on all cameras
    cap.set(cv::CAP_PROP_GAIN, gain);
}

int CameraManager::getExposure() const {
    return exposure;
}

int CameraManager::getGain() const {
    return gain;
}

void CameraManager::setBrightCondition() {
    exposure = 5;  // Low exposure for bright conditions
    gain = 30;
    setExposure(exposure);
    setGain(gain);
}

void CameraManager::setNormalCondition() {
    exposure = 30; // Medium exposure for normal conditions
    gain = 50;
    setExposure(exposure);
    setGain(gain);
}

void CameraManager::setDarkCondition() {
    exposure = 100; // High exposure for dark conditions
    gain = 80;
    setExposure(exposure);
    setGain(gain);
}



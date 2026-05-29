// src/utils/ConfigManager.cpp — YAML-based config persistence via cv::FileStorage
#include "ConfigManager.h"
#include "../core/ImageProcessor.h"
#include "../camera/CameraManager.h"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <fstream>

bool ConfigManager::saveConfig(const std::string& filename,
                               const ImageProcessor* processor,
                               const CameraManager* camera) {
    if (!processor || !camera) return false;
    try {
        cv::FileStorage fs(filename, cv::FileStorage::WRITE);
        if (!fs.isOpened()) { std::cerr << "Cannot open " << filename << " for writing\n"; return false; }

        fs << "version" << "1.0";

        // ── Camera ──
        fs << "camera" << "{";
        fs << "exposure"  << camera->exposure;
        fs << "gain"      << camera->gain;
        fs << "}";

        // ── Red HSV ──
        fs << "red_hsv" << "{";
        fs << "hue_low_1"        << processor->red1.h_lo;
        fs << "hue_high_1"       << processor->red1.h_hi;
        fs << "saturation_low_1" << processor->red1.s_lo;
        fs << "value_low_1"      << processor->red1.v_lo;
        fs << "hue_low_2"        << processor->red2.h_lo;
        fs << "hue_high_2"       << processor->red2.h_hi;
        fs << "saturation_low_2" << processor->red2.s_lo;
        fs << "value_low_2"      << processor->red2.v_lo;
        fs << "}";

        // ── Blue HSV ──
        fs << "blue_hsv" << "{";
        fs << "hue_low"         << processor->blue_range.h_lo;
        fs << "hue_high"        << processor->blue_range.h_hi;
        fs << "saturation_low"  << processor->blue_range.s_lo;
        fs << "value_low"       << processor->blue_range.v_lo;
        fs << "}";

        // ── Filter ──
        fs << "filter" << "{";
        fs << "min_area"   << processor->filter.min_area;
        fs << "max_area"   << processor->filter.max_area;
        fs << "filter.min_ratio"  << processor->filter.min_ratio;
        fs << "filter.max_ratio"  << processor->filter.max_ratio;
        fs << "filter.min_distance" << processor->filter.min_distance;
        fs << "filter.max_distance" << processor->filter.max_distance;
        fs << "filter.max_angle_diff" << processor->filter.max_angle_diff;
        fs << "filter.max_height_diff_ratio" << processor->filter.max_height_diff_ratio;
        fs << "}";

        fs.release();
        std::cout << "Config saved to " << filename << std::endl;
        return true;
    } catch (const cv::Exception& e) {
        std::cerr << "Save failed: " << e.what() << std::endl;
        return false;
    }
}

bool ConfigManager::loadConfig(const std::string& filename,
                               ImageProcessor* processor,
                               CameraManager* camera) {
    if (!processor || !camera) return false;
    try {
        cv::FileStorage fs(filename, cv::FileStorage::READ);
        if (!fs.isOpened()) { std::cerr << "Cannot open " << filename << " for reading\n"; return false; }

        // ── Camera ──
        cv::FileNode cam = fs["camera"];
        if (!cam.empty()) {
            camera->exposure = (int)cam["exposure"];
            camera->gain     = (int)cam["gain"];
        }

        // ── Red HSV ──
        cv::FileNode red = fs["red_hsv"];
        if (!red.empty()) {
            processor->red1.h_lo        = (int)red["hue_low_1"];
            processor->red1.h_hi       = (int)red["hue_high_1"];
            processor->red1.s_lo = (int)red["saturation_low_1"];
            processor->red1.v_lo      = (int)red["value_low_1"];
            processor->red2.h_lo        = (int)red["hue_low_2"];
            processor->red2.h_hi       = (int)red["hue_high_2"];
            processor->red2.s_lo = (int)red["saturation_low_2"];
            processor->red2.v_lo      = (int)red["value_low_2"];
        }

        // ── Blue HSV ──
        cv::FileNode blue = fs["blue_hsv"];
        if (!blue.empty()) {
            processor->blue_range.h_lo       = (int)blue["hue_low"];
            processor->blue_range.h_hi      = (int)blue["hue_high"];
            processor->blue_range.s_lo = (int)blue["saturation_low"];
            processor->blue_range.v_lo     = (int)blue["value_low"];
        }

        // ── Filter ──
        cv::FileNode flt = fs["filter"];
        if (!flt.empty()) {
            processor->filter.min_area   = (int)flt["min_area"];
            processor->filter.max_area   = (int)flt["max_area"];
            processor->filter.min_ratio  = (float)flt["filter.min_ratio"];
            processor->filter.max_ratio  = (float)flt["filter.max_ratio"];
            processor->filter.min_distance = (int)flt["filter.min_distance"];
            processor->filter.max_distance = (int)flt["filter.max_distance"];
            processor->filter.max_angle_diff = (float)flt["filter.max_angle_diff"];
            processor->filter.max_height_diff_ratio = (float)flt["filter.max_height_diff_ratio"];
        }

        fs.release();
        camera->setExposure(camera->exposure);
        camera->setGain(camera->gain);
        processor->updateColorRanges();
        std::cout << "Config loaded from " << filename << std::endl;
        return true;
    } catch (const cv::Exception& e) {
        std::cerr << "Load failed: " << e.what() << std::endl;
        return false;
    }
}

ConfigManager::Config ConfigManager::configFromComponents(const ImageProcessor*, const CameraManager*) {
    return Config{};
}
void ConfigManager::configToComponents(const Config&, ImageProcessor*, CameraManager*) {}

#include "ImageProcessor.h"
#include <algorithm>
#include <cmath>

ImageProcessor::ImageProcessor() {
    updateColorRanges();
}

void ImageProcessor::updateColorRanges() {
    lower_red1  = cv::Scalar(red1.h_lo, red1.s_lo, red1.v_lo);
    upper_red1  = cv::Scalar(red1.h_hi, red1.s_hi, red1.v_hi);
    lower_red2  = cv::Scalar(red2.h_lo, red2.s_lo, red2.v_lo);
    upper_red2  = cv::Scalar(red2.h_hi, red2.s_hi, red2.v_hi);
    lower_blue  = cv::Scalar(blue_range.h_lo, blue_range.s_lo, blue_range.v_lo);
    upper_blue  = cv::Scalar(blue_range.h_hi, blue_range.s_hi, blue_range.v_hi);
}

cv::Mat ImageProcessor::preprocess(const cv::Mat& src) {
    cv::Mat blurred, hsv_image;
    
    // Adjust Gaussian blur kernel size based on image size
    int kernel_size = std::max(3, std::min(src.cols, src.rows) / 200);
    if (kernel_size % 2 == 0) kernel_size++; // Ensure kernel size is odd
    
    // Gaussian blur for noise reduction
    cv::GaussianBlur(src, blurred, cv::Size(kernel_size, kernel_size), 0);
    
    // Convert to HSV color space
    cv::cvtColor(blurred, hsv_image, cv::COLOR_BGR2HSV);
    
    return hsv_image;
}

// SEU reference: normalize RotatedRect angle to [-45,45], ensure height >= width
void ImageProcessor::adjustRect(cv::RotatedRect& r) {
    if (r.size.width > r.size.height) {
        std::swap(r.size.width, r.size.height);
        r.angle += 90.0f;
    }
    while (r.angle < -45.0f) r.angle += 180.0f;
    while (r.angle >  45.0f) r.angle -= 180.0f;
}

// Light bar filtering — fitEllipse + solidity (SEU reference improved)
bool ImageProcessor::isLightBar(const std::vector<cv::Point>& contour) {
    double area = cv::contourArea(contour);
    if (area < filter.min_area || area > filter.max_area) return false;

    if (contour.size() < 5) return false; // fitEllipse needs >=5 points
    cv::RotatedRect rect = cv::fitEllipse(contour);
    adjustRect(rect);

    // solidity: contour area / ellipse area. Light bars should be solid.
    double ellipse_area = CV_PI * rect.size.width * rect.size.height / 4.0;
    if (ellipse_area < 1.0) return false;
    double solidity = area / ellipse_area;
    if (solidity < 0.4 || solidity > 1.8) return false;

    // aspect ratio: now height >= width always (from adjustRect)
    float hw_ratio = rect.size.height / std::max(1.0f, rect.size.width);
    if (hw_ratio < filter.min_ratio || hw_ratio > filter.max_ratio) return false;

    return true;
}

// Simplified variant for high-FPS mode: minAreaRect + solidity via convex hull
bool ImageProcessor::isLightBarSimplified(const std::vector<cv::Point>& contour) {
    double area = cv::contourArea(contour);
    if (area < filter.min_area / 2 || area > filter.max_area * 2) return false;

    cv::RotatedRect rect = cv::minAreaRect(contour);
    adjustRect(rect);

    std::vector<cv::Point> hull;
    cv::convexHull(contour, hull);
    double hull_area = cv::contourArea(hull);
    if (hull_area < 1.0) return false;
    if (area / hull_area < 0.45) return false;

    float hw_ratio = rect.size.height / std::max(1.0f, rect.size.width);
    if (hw_ratio < filter.min_ratio / 2.0f || hw_ratio > filter.max_ratio * 1.5f) return false;
    return true;
}

// SEU reference: verify the actual pixel colors inside the light bar match expected color
bool ImageProcessor::verifyLightBarColor(const cv::Mat& hsv, const std::vector<cv::Point>& contour,
                                          ArmorColor expected) {
    if (contour.size() < 3) return false;
    cv::Mat mask = cv::Mat::zeros(hsv.size(), CV_8UC1);
    std::vector<std::vector<cv::Point>> c = {contour};
    cv::drawContours(mask, c, -1, cv::Scalar(255), -1);
    // expand slightly to capture pixels from the light bar body
    cv::dilate(mask, mask, cv::Mat(), cv::Point(-1, -1), 1);

    cv::Scalar mean_hsv = cv::mean(hsv, mask);
    // H is [0,180] in OpenCV; for red we check both wrap-around ranges
    if (expected == ArmorColor::Red) {
        // Red: H near 0 or near 180, high S
        bool h_ok = (mean_hsv[0] < 15.0 || mean_hsv[0] > 160.0);
        return h_ok && mean_hsv[1] > 60.0;
    } else {
        // Blue: H in [90,140], reasonable S
        return (mean_hsv[0] > 85.0 && mean_hsv[0] < 145.0) && mean_hsv[1] > 50.0;
    }
}

std::vector<std::vector<cv::Point>> ImageProcessor::findContoursByColor(const cv::Mat& hsv_image, ArmorColor color) {
    cv::Mat mask, mask1, mask2;
    
    if (color == ArmorColor::Red) {
        cv::inRange(hsv_image, lower_red1, upper_red1, mask1);
        cv::inRange(hsv_image, lower_red2, upper_red2, mask2);
        cv::add(mask1, mask2, mask);
    } else {
        cv::inRange(hsv_image, lower_blue, upper_blue, mask);
    }
    
    // Adjust morphological operation kernel size based on image size
    int kernel_size = std::max(3, std::min(hsv_image.cols, hsv_image.rows) / 250);
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(kernel_size, kernel_size));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);
    
    // Find contours
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(mask, contours, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    
    // Filter contours: shape + color verification
    std::vector<std::vector<cv::Point>> filtered_contours;
    filtered_contours.reserve(contours.size());
    for (const auto& contour : contours) {
        if (isLightBar(contour) && verifyLightBarColor(hsv_image, contour, color)) {
            filtered_contours.push_back(contour);
        }
    }
    return filtered_contours;
}

// Simplified contour finding for high FPS
std::vector<std::vector<cv::Point>> ImageProcessor::findContoursByColorSimplified(const cv::Mat& hsv_image, ArmorColor color) {
    cv::Mat mask, mask1, mask2;
    
    if (color == ArmorColor::Red) {
        cv::inRange(hsv_image, lower_red1, upper_red1, mask1);
        cv::inRange(hsv_image, lower_red2, upper_red2, mask2);
        cv::add(mask1, mask2, mask);
    } else {
        cv::inRange(hsv_image, lower_blue, upper_blue, mask);
    }
    
    // Simplified morphological operations with fixed kernel size for speed
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
    
    // Find contours
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(mask, contours, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    
    // Filter contours: simplified shape + color verification
    std::vector<std::vector<cv::Point>> filtered_contours;
    filtered_contours.reserve(contours.size());
    for (const auto& contour : contours) {
        if (isLightBarSimplified(contour) && verifyLightBarColor(hsv_image, contour, color)) {
            filtered_contours.push_back(contour);
        }
    }
    return filtered_contours;
}

void ImageProcessor::matchArmorPairs(const std::vector<std::vector<cv::Point>>& contours,
                                  ArmorColor color,
                                  std::vector<ArmorInfo>& armors) {
    // Convert to RotatedRect using fitEllipse, normalize angle, sort left-to-right
    std::vector<cv::RotatedRect> lights;
    lights.reserve(contours.size());
    for (const auto& c : contours) {
        if (c.size() < 5) continue;
        cv::RotatedRect r = cv::fitEllipse(c);
        adjustRect(r);
        lights.push_back(r);
    }
    if (lights.size() < 2) return;
    std::sort(lights.begin(), lights.end(),
              [](const cv::RotatedRect& a, const cv::RotatedRect& b) { return a.center.x < b.center.x; });

    std::string color_str = (color == ArmorColor::Red) ? "red" : "blue";

    for (size_t i = 0; i < lights.size(); i++) {
        cv::RotatedRect& r1 = lights[i];
        float len1 = r1.size.height; // height >= width due to adjustRect

        for (size_t j = i + 1; j < lights.size(); j++) {
            cv::RotatedRect& r2 = lights[j];
            float len2 = r2.size.height;
            float mean_h = (len1 + len2) * 0.5f;
            float xDiff = std::abs(r1.center.x - r2.center.x);
            float yDiff = std::abs(r1.center.y - r2.center.y);
            float dist = std::sqrt(xDiff * xDiff + yDiff * yDiff);

            // ── SEU constraint set ──
            // (1) Near-parallel
            float angle_diff = std::abs(r1.angle - r2.angle);
            if (angle_diff > 7.0f) continue;

            // (2) Similar height
            float height_diff = std::abs(len1 - len2) / std::max(len1, len2);
            if (height_diff > 0.20f) continue;

            // (3) Roughly same Y
            if (yDiff / mean_h > 2.0f) continue;

            // (4) Not overlapping horizontally
            if (xDiff / mean_h < 0.5f) continue;

            // (5) Armor aspect ratio range
            float dist_ratio = dist / mean_h;
            if (dist_ratio < 1.0f || dist_ratio > 5.0f) continue;

            // (6) xDiff must dominate — armor plates are wide
            if (xDiff < yDiff) continue;

            // ── Build ArmorInfo ──
            ArmorInfo armor;
            armor.center = cv::Point2f((r1.center.x + r2.center.x) * 0.5f,
                                       (r1.center.y + r2.center.y) * 0.5f);
            armor.color = color_str;
            armor.lightBars = {r1.center, r2.center};
            armor.lightRects = {r1, r2};

            // Bounding box from both rects
            std::vector<cv::Point2f> pts;
            cv::boxPoints(r1, pts);
            for (auto& p : pts) armor.bbox |= cv::Rect(p, cv::Size(1, 1));
            cv::boxPoints(r2, pts);
            for (auto& p : pts) armor.bbox |= cv::Rect(p, cv::Size(1, 1));

            // Confidence: multi-factor composite
            float c_angle = 1.0f - angle_diff / 7.0f;
            float c_height = 1.0f - height_diff / 0.20f;
            float c_align  = 1.0f - yDiff / (mean_h * 2.0f);
            float c_ratio  = 1.0f - std::abs(dist_ratio - 2.5f) / 2.5f;
            armor.confidence = (c_angle + c_height + c_align + c_ratio) * 0.25f;

            armors.push_back(armor);

            // NMS: accept only first valid pair per left light
            break;
        }
    }
}

// Simplified variant: minAreaRect + looser constraints, no fitEllipse
void ImageProcessor::matchArmorPairsSimplified(const std::vector<std::vector<cv::Point>>& contours,
                                               ArmorColor color,
                                               std::vector<ArmorInfo>& armors) {
    std::vector<cv::RotatedRect> lights;
    lights.reserve(contours.size());
    for (const auto& c : contours) {
        cv::RotatedRect r = cv::minAreaRect(c);
        adjustRect(r);
        lights.push_back(r);
    }
    if (lights.size() < 2) return;
    std::sort(lights.begin(), lights.end(),
              [](const cv::RotatedRect& a, const cv::RotatedRect& b) { return a.center.x < b.center.x; });

    std::string color_str = (color == ArmorColor::Red) ? "red" : "blue";

    for (size_t i = 0; i < lights.size(); i++) {
        cv::RotatedRect& r1 = lights[i];
        float len1 = r1.size.height;

        for (size_t j = i + 1; j < lights.size(); j++) {
            cv::RotatedRect& r2 = lights[j];
            float len2 = r2.size.height;
            float mean_h = (len1 + len2) * 0.5f;
            float xDiff = std::abs(r1.center.x - r2.center.x);
            float yDiff = std::abs(r1.center.y - r2.center.y);
            float dist = std::sqrt(xDiff * xDiff + yDiff * yDiff);

            // Looser SEU constraints
            float angle_diff = std::abs(r1.angle - r2.angle);
            if (angle_diff > 15.0f) continue;
            float height_diff = std::abs(len1 - len2) / std::max(len1, len2);
            if (height_diff > 0.35f) continue;
            if (yDiff / mean_h > 3.0f) continue;
            if (xDiff / mean_h < 0.3f) continue;
            float dist_ratio = dist / mean_h;
            if (dist_ratio < 0.8f || dist_ratio > 7.0f) continue;
            if (xDiff < yDiff) continue;

            ArmorInfo armor;
            armor.center = cv::Point2f((r1.center.x + r2.center.x) * 0.5f,
                                       (r1.center.y + r2.center.y) * 0.5f);
            armor.color = color_str;
            armor.lightBars = {r1.center, r2.center};
            armor.lightRects = {r1, r2};
            std::vector<cv::Point2f> pts;
            cv::boxPoints(r1, pts);
            for (auto& p : pts) armor.bbox |= cv::Rect(p, cv::Size(1, 1));
            cv::boxPoints(r2, pts);
            for (auto& p : pts) armor.bbox |= cv::Rect(p, cv::Size(1, 1));
            armor.confidence = 1.0f - angle_diff / 15.0f * 0.4f - height_diff / 0.35f * 0.3f
                               - yDiff / (mean_h * 3.0f) * 0.3f;
            armors.push_back(armor);
            break; // NMS
        }
    }
}

// ── Scoring (SEU-inspired): prefer large + centered + proper shape ──
float ImageProcessor::computeArmorScore(const ArmorInfo& armor, const cv::Size& frame) {
    if (armor.confidence <= 0.0f) return -1e6f;
    float area = armor.bbox.area();
    float size_score   = std::exp(area / 1500.0f);           // prefer larger
    float center_score = std::exp(-cv::norm(armor.center -
                              cv::Point2f(frame.width/2.0f, frame.height/2.0f)) / 250.0f);
    float conf_score   = armor.confidence * 3.0f;
    return size_score + center_score + conf_score;
}

// ── Tracking ──
cv::Rect ImageProcessor::getTrackingROI() const {
    if (!is_tracking()) return cv::Rect(0, 0, 1280, 720);
    cv::Rect roi = last_roi;
    // expand: 3x width, 2x height
    roi.x -= roi.width;
    roi.width *= 3;
    roi.y -= roi.height / 2;
    roi.height *= 2;
    // clamp
    roi.x = std::max(0, roi.x);
    roi.y = std::max(0, roi.y);
    roi.width  = std::min(1280 - roi.x, roi.width);
    roi.height = std::min(720  - roi.y, roi.height);
    return roi;
}
void ImageProcessor::updateTracking(const ArmorInfo& best) {
    last_roi = best.bbox;
    track_cnt = 0;
}
void ImageProcessor::resetTracking() {
    track_cnt = MAX_TRACK_LOST;
}

// ── Unified matching with tracking ROI + scoring ──
std::vector<ImageProcessor::ArmorInfo> ImageProcessor::matchArmors(
    const std::vector<std::vector<cv::Point>>& red_contours,
    const std::vector<std::vector<cv::Point>>& blue_contours) {

    std::vector<ArmorInfo> armors;
    armors.reserve(red_contours.size() + blue_contours.size());
    matchArmorPairs(red_contours,  ArmorColor::Red,  armors);
    matchArmorPairs(blue_contours, ArmorColor::Blue, armors);

    if (armors.empty()) {
        track_cnt++;
        return armors;
    }

    // Pick best via scoring
    cv::Size frame(1280, 720);
    auto best = std::max_element(armors.begin(), armors.end(),
        [&](const ArmorInfo& a, const ArmorInfo& b) {
            return computeArmorScore(a, frame) < computeArmorScore(b, frame);
        });

    if (best != armors.end() && computeArmorScore(*best, frame) > 0) {
        updateTracking(*best);
        // Move best to front
        if (best != armors.begin()) std::iter_swap(armors.begin(), best);
    } else {
        track_cnt++;
    }
    return armors;
}

std::vector<ImageProcessor::ArmorInfo> ImageProcessor::matchArmorsSimplified(
    const std::vector<std::vector<cv::Point>>& red_contours,
    const std::vector<std::vector<cv::Point>>& blue_contours) {

    std::vector<ArmorInfo> armors;
    armors.reserve((red_contours.size() + blue_contours.size()) / 2);
    matchArmorPairsSimplified(red_contours,  ArmorColor::Red,  armors);
    matchArmorPairsSimplified(blue_contours, ArmorColor::Blue, armors);

    if (armors.empty()) { track_cnt++; return armors; }

    cv::Size frame(1280, 720);
    auto best = std::max_element(armors.begin(), armors.end(),
        [&](const ArmorInfo& a, const ArmorInfo& b) {
            return computeArmorScore(a, frame) < computeArmorScore(b, frame);
        });
    if (best != armors.end() && computeArmorScore(*best, frame) > 0) {
        updateTracking(*best);
        if (best != armors.begin()) std::iter_swap(armors.begin(), best);
    } else {
        track_cnt++;
    }
    return armors;
}
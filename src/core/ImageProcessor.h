#ifndef IMAGE_PROCESSOR_H
#define IMAGE_PROCESSOR_H

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <mutex>

class ImageProcessor {
public:
    enum class ArmorColor { Red, Blue };

    struct ArmorInfo {
        cv::Point2f center;  cv::Rect bbox;
        std::string color;   float confidence;
        std::vector<cv::Point2f> lightBars;
        std::vector<cv::RotatedRect> lightRects;
        ArmorInfo() : confidence(0.0f) {}
    };

    // ── parameter structs ──
    struct HSVRange {
        int h_lo, h_hi, s_lo, s_hi, v_lo, v_hi;
        HSVRange(int hl=0, int hh=10, int sl=100, int sh=255, int vl=100, int vh=255)
            : h_lo(hl), h_hi(hh), s_lo(sl), s_hi(sh), v_lo(vl), v_hi(vh) {}
    };
    struct FilterParams {
        int min_area=80, max_area=8000;
        float min_ratio=1.5f, max_ratio=8.0f;
        int min_distance=30, max_distance=500;
        float max_angle_diff=20.0f;
        float max_height_diff_ratio=30.0f;
        float max_track_lost=300.0f;
        int brightness_threshold=0;
    };

    // ── public parameters ──
    HSVRange    red1{0,10,100,255,100,255}, red2{170,180,100,255,100,255};
    HSVRange    blue_range{90,130,100,255,100,255};
    FilterParams filter;

    ImageProcessor();
    cv::Mat preprocess(const cv::Mat& src);
    std::vector<std::vector<cv::Point>> findContoursByColor(const cv::Mat& hsv_image, ArmorColor color);
    std::vector<std::vector<cv::Point>> findContoursByColorSimplified(const cv::Mat& hsv_image, ArmorColor color);
    std::vector<ArmorInfo> matchArmors(const std::vector<std::vector<cv::Point>>& red,
                                       const std::vector<std::vector<cv::Point>>& blue);
    std::vector<ArmorInfo> matchArmorsSimplified(const std::vector<std::vector<cv::Point>>& red,
                                                  const std::vector<std::vector<cv::Point>>& blue);
    void updateColorRanges();

    // ── HSV for binary display (thread-safe) ──
    void setHSV(const cv::Mat& h)  { std::lock_guard<std::mutex> lk(hsv_mutex); hsv = h.clone(); }
    cv::Mat getHSV() const         { std::lock_guard<std::mutex> lk(hsv_mutex); return hsv.clone(); }

    // ── tracking ──
    cv::Rect last_roi;
    int track_cnt = 0;
    static constexpr int MAX_TRACK_LOST = 300;
    bool is_tracking() const { return track_cnt > 0 && track_cnt < MAX_TRACK_LOST; }
    cv::Rect getTrackingROI() const;
    void updateTracking(const ArmorInfo& best);
    void resetTracking();

    // ── color range getters (for UI binary display) ──
    const cv::Scalar& getLowerRed1() const { return lower_red1; }
    const cv::Scalar& getUpperRed1() const { return upper_red1; }
    const cv::Scalar& getLowerRed2() const { return lower_red2; }
    const cv::Scalar& getUpperRed2() const { return upper_red2; }
    const cv::Scalar& getLowerBlue() const { return lower_blue; }
    const cv::Scalar& getUpperBlue() const { return upper_blue; }

private:
    cv::Scalar lower_red1, upper_red1, lower_red2, upper_red2, lower_blue, upper_blue;
    cv::Mat hsv;
    mutable std::mutex hsv_mutex;

    void matchArmorPairs(const std::vector<std::vector<cv::Point>>& contours,
                         ArmorColor color, std::vector<ArmorInfo>& armors);
    void matchArmorPairsSimplified(const std::vector<std::vector<cv::Point>>& contours,
                                   ArmorColor color, std::vector<ArmorInfo>& armors);
    bool isLightBar(const std::vector<cv::Point>& contour);
    bool isLightBarSimplified(const std::vector<cv::Point>& contour);
    bool verifyLightBarColor(const cv::Mat& hsv, const std::vector<cv::Point>& contour, ArmorColor expected);
    float computeArmorScore(const ArmorInfo& armor, const cv::Size& frame_size);
    static void adjustRect(cv::RotatedRect& r);
};

#endif

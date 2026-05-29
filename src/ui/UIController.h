#ifndef UI_CONTROLLER_H
#define UI_CONTROLLER_H

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>

#include "core/ImageProcessor.h"
class CameraManager;

class UIController {
public:
    struct ArmorInfo {
        cv::Point2f center;  cv::Rect bbox;
        std::string color;   float confidence;
        std::vector<cv::Point2f> lightBars;
        std::vector<cv::RotatedRect> lightRects;
        ArmorInfo() : confidence(0.0f) {}
        ArmorInfo(const ImageProcessor::ArmorInfo& o) :
            center(o.center), bbox(o.bbox), color(o.color),
            confidence(o.confidence), lightBars(o.lightBars), lightRects(o.lightRects) {}
    };

    UIController();
    ~UIController();

    void createControlPanel(ImageProcessor* processor, CameraManager* camera);
    void syncFromSliders(ImageProcessor* processor, CameraManager* camera);
    void drawUI(cv::Mat& image, const std::vector<ArmorInfo>& armors,
                ImageProcessor* processor, CameraManager* camera);
    void drawBinaryImages(ImageProcessor* processor);

    void toggleUI();
    void toggleBinary();
    void startNewRound();
    void allowRecognition();
    void finishRecognition();
    void updateFPS(double fps);
    void setRecording(bool on)   { m_recording = on; }
    void showToast(const std::string& msg);

private:
    bool show_ui, show_binary;
    int round_status;
    bool is_recognition_allowed;
    double current_fps;

    // ── cached static layers ──
    cv::Size cached_size;
    cv::Mat static_overlay;
    void rebuildStaticCache(cv::Size sz);

    // ── per-frame HUD helpers ──
    void drawHUD(cv::Mat& image, int armor_count);
    void drawArmorOverlay(cv::Mat& image, const ArmorInfo& armor);
    void drawInfoCard(cv::Mat& image, const ArmorInfo& armor,
                      const cv::Scalar& color, const cv::Point& pos, int w);
    void drawCornerBrackets(cv::Mat& image, const cv::Rect& r,
                            const cv::Scalar& c, int len, int t);
    void drawBottomBar(cv::Mat& image);
    void drawToasts(cv::Mat& image);
    void glowText(cv::Mat& image, const std::string& text, cv::Point pos,
                  double scale, const cv::Scalar& color, int thickness);
    cv::Scalar getArmorColor(const std::string& color) const;

    // ── custom control panel ──
    struct Slider {
        std::string name;    int* val;
        int lo, hi;          std::string hint;
    };
    struct Section {
        std::string name;
        std::vector<int> slider_ids;
        bool collapsed = false;
    };

    std::vector<Slider>  m_sliders;
    std::vector<Section> m_sections;
    int  m_panel_w = 380, m_panel_h = 640;
    int  m_active_slider = -1;
    int  m_hover_slider  = -1;
    int  m_scroll_y = 0;
    int  m_mouse_x = 0, m_mouse_y = 0;
    bool m_dirty = false;
    bool m_recording = false;
    struct Toast { std::string text; std::chrono::steady_clock::time_point expiry; };
    std::vector<Toast> m_toasts;

    void drawControlPanel();
    void updatePanelLayout();

    static void onMouse(int event, int x, int y, int flags, void* userdata);
    void handleMouse(int event, int x, int y, int flags);
};

#endif

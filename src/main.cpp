// src/main.cpp
#include "core/ImageProcessor.h"
#include "camera/CameraManager.h"
#include "ui/UIController.h"
#include "utils/ConfigManager.h"
#include <iostream>
#include <chrono>
#include <cmath>
#include <ctime>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <memory>
#include <future>

// Thread synchronization variables
std::mutex frame_mutex;
std::mutex result_mutex;
std::condition_variable frame_condition;
std::queue<cv::Mat> frame_queue;
std::vector<UIController::ArmorInfo> processed_armors;
cv::Mat processed_frame;
bool new_frame_available = false;
bool processing_complete = false;
std::atomic<bool> terminate_threads{false};

// Performance monitoring
std::atomic<double> current_fps{0.0};
const size_t MAX_QUEUE_SIZE = 5; // Increased queue size for better performance

// Frame skipping variables
std::atomic<int> frame_skip_counter{0};
std::atomic<int> frame_skip_factor{1}; // Process every nth frame (1=process all, 2=skip every other, etc.)

// Adaptive processing variables
std::atomic<bool> use_simplified_processing{false};

// ── Video recording state ──
std::atomic<bool> recording{false};
cv::VideoWriter video_writer;
std::mutex video_mutex;

// ── Communication stub ──
struct CommunicationManager {
    float cur_yaw=0, cur_pitch=0, send_yaw=0, send_pitch=0;
    void update() {} // placeholder for serial polling
} comm;

// Demo-mode fallback frame generator (when no camera)
cv::Mat generateDemoFrame() {
    static int tick = 0; tick++;
    cv::Mat frame(720, 1280, CV_8UC3, cv::Scalar(30, 30, 40));
    // moving colored rectangles to simulate armor plates
    int cx = 640 + (int)(180 * std::sin(tick * 0.03));
    int cy = 360 + (int)(80  * std::cos(tick * 0.05));
    cv::Rect r1(cx - 60, cy - 20, 120, 40);
    cv::rectangle(frame, r1, cv::Scalar(0, 0, 220), -1); // red-ish bar
    cv::Rect r2(cx + 80, cy - 22, 120, 44);
    cv::rectangle(frame, r2, cv::Scalar(0, 0, 220), -1); // red-ish bar
    cv::Rect r3(200 - (int)(100 * std::sin(tick * 0.04)), 250, 90, 30);
    cv::rectangle(frame, r3, cv::Scalar(200, 80, 0), -1); // blue-ish bar
    cv::Rect r4(320 - (int)(100 * std::sin(tick * 0.04)), 248, 90, 34);
    cv::rectangle(frame, r4, cv::Scalar(200, 80, 0), -1); // blue-ish bar
    cv::putText(frame, "DEMO MODE - No Camera", cv::Point(460, 680),
                cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(180, 180, 180), 2);
    cv::putText(frame, "Connect camera or press Ctrl+Q to quit", cv::Point(430, 710),
                cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(140, 140, 140), 1);
    return frame;
}

// Processing thread function
void processingThread(ImageProcessor* processor, UIController*) {
    cv::Mat local_frame;
    std::vector<UIController::ArmorInfo> local_armors;

    while (!terminate_threads) {
        std::unique_lock<std::mutex> lock(frame_mutex);
        frame_condition.wait(lock, []{return new_frame_available || terminate_threads.load();});
        if (terminate_threads) break;

        if (!frame_queue.empty()) {
            local_frame = frame_queue.front();
            frame_queue.pop();
            new_frame_available = !frame_queue.empty();
        }
        lock.unlock();

        if (local_frame.empty()) continue;

        // Frame skip
        if (frame_skip_counter.fetch_add(1) % frame_skip_factor.load() != 0) continue;

        auto t0 = std::chrono::high_resolution_clock::now();

        // ── unified processing path ──
        cv::Mat hsv = processor->preprocess(local_frame);
        processor->setHSV(hsv);

        using AC = ImageProcessor::ArmorColor;
        bool simple = use_simplified_processing.load();

        // Tracking ROI: crop to previous target region for speed
        cv::Rect roi = processor->getTrackingROI();
        // clamp to actual image size (camera may not deliver exact 1280x720)
        roi &= cv::Rect(0, 0, hsv.cols, hsv.rows);
        cv::Mat hsv_roi = roi.area() > 0 ? hsv(roi) : hsv;
        cv::Point roi_offset = (roi.area() > 0) ? roi.tl() : cv::Point(0, 0);

        auto red  = simple ? processor->findContoursByColorSimplified(hsv_roi, AC::Red)
                           : processor->findContoursByColor(hsv_roi, AC::Red);
        auto blue = simple ? processor->findContoursByColorSimplified(hsv_roi, AC::Blue)
                           : processor->findContoursByColor(hsv_roi, AC::Blue);

        // Offset contours back to full-frame coords
        for (auto& c : red)  for (auto& p : c) p += roi_offset;
        for (auto& c : blue) for (auto& p : c) p += roi_offset;

        auto armors = simple ? processor->matchArmorsSimplified(red, blue)
                             : processor->matchArmors(red, blue);

        local_armors.clear();
        local_armors.reserve(armors.size());
        for (const auto& a : armors) local_armors.emplace_back(a);

        {
            std::lock_guard<std::mutex> lk(result_mutex);
            processed_armors = std::move(local_armors);
            processed_frame = local_frame;
            processing_complete = true;
        }

        // Smoothed FPS (P1 #6 — exponential moving average)
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / 1000.0;
        if (ms > 0) {
            double instant_fps = 1000.0 / ms;
            current_fps = current_fps * 0.7 + instant_fps * 0.3; // EMA smooth
        }
    }
}

// Function to adapt processing based on FPS
void adaptProcessingBasedOnFPS(double fps) {
    if (fps < 40) {
        // Low FPS - use simplified processing
        use_simplified_processing = true;
        frame_skip_factor = 3; // Skip 2 out of 3 frames
    } else if (fps < 67) {
        // Medium FPS
        use_simplified_processing = false;
        frame_skip_factor = 2; // Skip every other frame
    } else {
        // High FPS
        use_simplified_processing = false;
        frame_skip_factor = 1; // Process all frames
    }
}

// Main function
int main(int argc, char* argv[]) {
    try {
        int camera_id = 0;
        std::string config_path;
        bool show_help = false;
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "-l" && i + 1 < argc) config_path = argv[++i];
            else if (arg == "-c" && i + 1 < argc) camera_id = std::atoi(argv[++i]);
            else if (arg == "--help" || arg == "-h") show_help = true;
            else if (arg[0] != '-') camera_id = std::atoi(argv[i]);
        }

        if (show_help) {
            std::cout << "ArmorDetector v2.0  |  Usage:\n"
                      << "  armor_detector [-c CAM_ID] [-l preset.yaml] [--help]\n"
                      << "Keys: Ctrl+Q=quit S=UI B=binary R=round A=arm F=finish\n"
                      << "      1/2/3=exposure V=record Space=shot P=save\n"
                      << "      Drag sliders in Control Panel for real-time tuning.\n";
            return 0;
        }

        std::cout << "ArmorDetector v2.0 | Cam " << camera_id << " | Ctrl+Q quit | --help for more\n";

        // Initialize components
        CameraManager camera(camera_id);
        ImageProcessor processor;
        UIController ui;

        bool use_demo_mode = false;
        if (!camera.initialize()) {
            std::cout << "[WARN] Camera not available - entering DEMO MODE" << std::endl;
            std::cout << "       The UI and controls are fully functional." << std::endl;
            use_demo_mode = true;
        }

        // Load config if specified
        if (!config_path.empty()) {
            ConfigManager::loadConfig(config_path, &processor, &camera);
        }

        // Create custom control panel
        ui.createControlPanel(&processor, &camera);

        cv::Mat frame;
        bool running = true;

        std::cout << "Starting main detection loop..." << std::endl;
        std::cout << "Press Ctrl+Q to quit the program." << std::endl;

        // Start processing thread
        std::thread process_thread(processingThread, &processor, &ui);

        // Timing variables for FPS calculation with smoothing
        auto last_time = std::chrono::high_resolution_clock::now();
        int frame_count = 0;
        double fps = 0.0;
        
        // Moving average for FPS smoothing
        const int FPS_HISTORY_SIZE = 10;
        std::vector<double> fps_history(FPS_HISTORY_SIZE, 0.0);
        int fps_history_index = 0;

        while (running) {
            // Read frame from camera (with reconnect + demo fallback)
            if (use_demo_mode) {
                // Periodic reconnect attempt
                static auto last_retry = std::chrono::steady_clock::now();
                auto now_ts = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now_ts - last_retry).count() > 2) {
                    last_retry = now_ts;
                    if (camera.initialize()) {
                        use_demo_mode = false;
                        ui.showToast("CAMERA RECONNECTED");
                        continue;
                    }
                }
                frame = generateDemoFrame();
            } else if (!camera.read(frame)) {
                use_demo_mode = true;
                ui.showToast("CAMERA LOST - DEMO MODE");
                frame = generateDemoFrame();
            }

            // Add frame to processing queue
            {
                std::lock_guard<std::mutex> lock(frame_mutex);
                if (frame_queue.size() < MAX_QUEUE_SIZE) { // Dynamic queue size limit
                    // Reuse memory by moving instead of cloning when possible
                    frame_queue.push(frame);
                    new_frame_available = true;
                    frame_condition.notify_one();
                } else {
                    // Drop frame if queue is full to prevent memory issues
                    // std::cout << "Warning: Frame queue full, dropping frame" << std::endl;
                }
            }

            // Update FPS calculation with smoothing
            frame_count++;
            auto current_time = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_time).count();
            if (elapsed >= 1000) { // Update FPS every second
                fps = frame_count / (elapsed / 1000.0);
                
                // Add to FPS history for smoothing
                fps_history[fps_history_index] = fps;
                fps_history_index = (fps_history_index + 1) % FPS_HISTORY_SIZE;
                
                // Calculate average FPS
                double avg_fps = 0.0;
                for (const auto& f : fps_history) {
                    avg_fps += f;
                }
                avg_fps /= FPS_HISTORY_SIZE;
                
                // Adapt processing based on FPS
                adaptProcessingBasedOnFPS(avg_fps);
                
                frame_count = 0;
                last_time = current_time;
                ui.updateFPS(avg_fps);
            }

            // Sync slider values to processor/camera
            ui.syncFromSliders(&processor, &camera);

            // Display processed results if available
            {
                std::lock_guard<std::mutex> lock(result_mutex);
                if (processing_complete) {
                    // Draw UI
                    ui.drawUI(processed_frame, processed_armors, &processor, &camera);

                    // Show main window
                    try {
                        cv::imshow("Armor Detection", processed_frame);
                    } catch (const cv::Exception& e) {
                        std::cerr << "OpenCV error in imshow: " << e.what() << std::endl;
                    }

                    // Video recording (if active)
                    if (recording.load() && !processed_frame.empty()) {
                        std::lock_guard<std::mutex> lk(video_mutex);
                        if (video_writer.isOpened()) video_writer.write(processed_frame);
                    }

                    // Show binary mask windows if enabled
                    ui.drawBinaryImages(&processor);
                    
                    // Output armor information to console (only when count changes)
                    static size_t last_armor_count = 0;
                    if (processed_armors.size() != last_armor_count) {
                        last_armor_count = processed_armors.size();
                        if (!processed_armors.empty()) {
                            std::cout << "Detected " << processed_armors.size() << " armor(s):" << std::endl;
                            for (const auto& armor : processed_armors) {
                                std::cout << "  [" << armor.color << "] pos=("
                                          << static_cast<int>(armor.center.x) << ","
                                          << static_cast<int>(armor.center.y)
                                          << ") conf=" << armor.confidence << std::endl;
                            }
                        }
                    }
                    
                    processing_complete = false;
                }
            }

            // Handle keyboard input
            int key = cv::waitKey(1) & 0xFF;
            if (key != 255) { // Only process valid key presses
                switch (key) {
                    case 17: // Ctrl+Q
                        running = false;
                        break;
                    case 's':
                    case 'S':
                        ui.toggleUI();
                        break;
                    case 'b':
                    case 'B':
                        ui.toggleBinary();
                        break;
                    case 'r':
                    case 'R':
                        ui.startNewRound();
                        break;
                    case 'a':
                    case 'A':
                        ui.allowRecognition();
                        break;
                    case 'f':
                    case 'F':
                        ui.finishRecognition();
                        break;
                    case '1': camera.setBrightCondition(); ui.showToast("EXP: BRIGHT"); break;
                    case '2': camera.setNormalCondition();  ui.showToast("EXP: NORMAL"); break;
                    case '3': camera.setDarkCondition();    ui.showToast("EXP: DARK");   break;
                    case 'v':
                    case 'V': {
                        recording = !recording;
                        ui.setRecording(recording);
                        if (recording) {
                            time_t now = time(0); char fn[64];
                            strftime(fn, sizeof(fn), "video_%Y%m%d_%H%M%S.mp4", localtime(&now));
                            video_writer.open(fn, cv::VideoWriter::fourcc('a','v','c','1'),
                                              30, cv::Size(1280, 720));
                            ui.showToast("REC STARTED");
                        } else {
                            video_writer.release();
                            ui.showToast("REC SAVED");
                        }
                        break;
                    }
                    case ' ': {
                        time_t now = time(0); char fn[64];
                        strftime(fn, sizeof(fn), "shot_%Y%m%d_%H%M%S.png", localtime(&now));
                        cv::imwrite(fn, processed_frame);
                        ui.showToast("SHOT SAVED");
                        break;
                    }
                    case 'p':
                    case 'P': {
                        time_t now = time(0); char fn[64];
                        strftime(fn, sizeof(fn), "preset_%Y%m%d_%H%M%S.yaml", localtime(&now));
                        ConfigManager::saveConfig(fn, &processor, &camera);
                        break;
                    }
                }
            }
        }

        // Terminate threads
        terminate_threads = true;
        frame_condition.notify_all();
        
        if (process_thread.joinable()) {
            process_thread.join();
        }

        // Clean up
        if (video_writer.isOpened()) video_writer.release();
        try { cv::destroyAllWindows(); } catch (const cv::Exception& e) {
            std::cerr << "OpenCV error in destroyAllWindows: " << e.what() << std::endl;
        }

        std::cout << "===========================================" << std::endl;
        std::cout << "Program exited normally. Press ENTER to close..." << std::endl;
        std::cin.get(); // Wait for user input
    } catch (const std::exception& e) {
        std::cerr << "Program error: " << e.what() << std::endl;
        std::cerr << "Press ENTER to exit..." << std::endl;
        std::cin.get(); // Wait for user input
        return -1;
    } catch (...) {
        std::cerr << "Program unknown error!" << std::endl;
        std::cerr << "Press ENTER to exit..." << std::endl;
        std::cin.get(); // Wait for user input
        return -1;
    }

    return 0;
}
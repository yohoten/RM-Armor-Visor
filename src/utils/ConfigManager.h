#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <opencv2/opencv.hpp>

// Forward declarations
class ImageProcessor;
class CameraManager;

class ConfigManager {
public:
    struct Config {
        // Image processor parameters
        struct {
            int hue_low_red1, saturation_low_red1, value_low_red1;
            int hue_high_red1, saturation_high_red1, value_high_red1;
            int hue_low_red2, saturation_low_red2, value_low_red2;
            int hue_high_red2, saturation_high_red2, value_high_red2;
            int hue_low_blue, saturation_low_blue, value_low_blue;
            int hue_high_blue, saturation_high_blue, value_high_blue;
            int min_area;
            int max_area;
            double min_ratio;
            double max_ratio;
            int min_distance;
            int max_distance;
            int max_angle_diff;
            int max_height_diff_ratio;
        } image_processor;
        
        // Camera parameters
        struct {
            int exposure_time;
            int r_gain;
            int g_gain;
            int b_gain;
            int preset_exposure_bright;
            int preset_exposure_normal;
            int preset_exposure_dark;
        } camera;
        
        std::string version;
    };

public:
    static bool saveConfig(const std::string& filename, 
                          const ImageProcessor* processor, 
                          const CameraManager* camera);
    
    static bool loadConfig(const std::string& filename, 
                          ImageProcessor* processor, 
                          CameraManager* camera);
    
private:
    static Config configFromComponents(const ImageProcessor* processor, 
                                      const CameraManager* camera);
    
    static void configToComponents(const Config& config, 
                                  ImageProcessor* processor, 
                                  CameraManager* camera);
};

#endif // CONFIG_MANAGER_H
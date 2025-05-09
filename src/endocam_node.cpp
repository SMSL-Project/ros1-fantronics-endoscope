#include <ros/ros.h>
#include <image_transport/image_transport.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/Image.h>
#include <compressed_image_transport/compressed_publisher.h>

#include <libusb-1.0/libusb.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

#include <cassert>
#include <vector>
#include <functional>
#include <memory>
#include <string>

using byteVector = std::vector<uint8_t>;

class UsbSupercamera
{
    static constexpr uint16_t USB_VENDOR_ID = 0x2ce3;
    static constexpr uint16_t USB_PRODUCT_ID = 0x3828;
    static constexpr int INTERFACE_A_NUMBER = 0;
    static constexpr int INTERFACE_B_NUMBER = 1;
    static constexpr int INTERFACE_B_ALTERNATE_SETTING = 1;
    static constexpr unsigned char ENDPOINT_1 = 1;
    static constexpr unsigned char ENDPOINT_2 = 2;
    static constexpr unsigned int USB_TIMEOUT = 1000; /* ms */

    libusb_context *ctx = nullptr;
    libusb_device_handle *handle = nullptr;
    std::string device_path_;
    uint8_t bus_number_ = 0;
    uint8_t device_address_ = 0;
    std::vector<std::function<void(const byteVector&)>> handlers_;

    int usb_read(unsigned char endpoint, byteVector &buf)
    {
        int transferred;
        buf.resize(0x1000);
        int ret = libusb_bulk_transfer(
            handle,
            LIBUSB_ENDPOINT_IN | endpoint,
            buf.data(),
            buf.size(),
            &transferred,
            USB_TIMEOUT
        );
        if (ret != 0) {
            buf.clear();
            return ret;
        }
        buf.resize(transferred);
        return 0;
    }

    int usb_write(unsigned char endpoint, const byteVector &buf)
    {
        int transferred;
        return libusb_bulk_transfer(
            handle,
            LIBUSB_ENDPOINT_OUT | endpoint,
            const_cast<uint8_t*>(buf.data()),
            buf.size(),
            &transferred,
            USB_TIMEOUT
        );
    }

    int setup(uint8_t bus_num = 0, uint8_t dev_addr = 0)
    {
        if (libusb_init(&ctx) < 0) return 1;
        
        if (bus_num != 0 && dev_addr != 0) {
            // Open specific device by bus/address
            libusb_device **devs = nullptr;
            int cnt = libusb_get_device_list(ctx, &devs);
            if (cnt < 0) return 1;
            for (int i = 0; i < cnt; i++) {
                libusb_device *dev = devs[i];
                if (libusb_get_bus_number(dev) == bus_num &&
                    libusb_get_device_address(dev) == dev_addr) {
                    if (libusb_open(dev, &handle) == 0) {
                        bus_number_   = bus_num;
                        device_address_ = dev_addr;
                        char path[128];
                        snprintf(path, sizeof(path), "bus-%03d-dev-%03d", bus_num, dev_addr);
                        device_path_ = path;
                    }
                    break;
                }
            }
            libusb_free_device_list(devs, 1);
            if (!handle) return 1;
        } else {
            // Fallback to VID/PID
            handle = libusb_open_device_with_vid_pid(ctx, USB_VENDOR_ID, USB_PRODUCT_ID);
            if (!handle) return 1;
            libusb_device *dev = libusb_get_device(handle);
            bus_number_    = libusb_get_bus_number(dev);
            device_address_ = libusb_get_device_address(dev);
            char path[128];
            snprintf(path, sizeof(path), "bus-%03d-dev-%03d", bus_number_, device_address_);
            device_path_ = path;
        }
        
        libusb_reset_device(handle);
        libusb_claim_interface(handle, INTERFACE_A_NUMBER);
        libusb_claim_interface(handle, INTERFACE_B_NUMBER);
        libusb_set_interface_alt_setting(handle, INTERFACE_B_NUMBER, INTERFACE_B_ALTERNATE_SETTING);
        return 0;
    }

public:
    UsbSupercamera(uint8_t bus_num = 0, uint8_t dev_addr = 0)
    {
        if (setup(bus_num, dev_addr) != 0)
            throw std::runtime_error("Failed to init USB camera");
        // sequence to start streaming
        usb_write(ENDPOINT_2, byteVector{0xFF,0x55,0xFF,0x55,0xEE,0x10});
        usb_write(ENDPOINT_1, byteVector{0xBB,0xAA,5,0,0});
    }

    ~UsbSupercamera()
    {
        if (handle) libusb_close(handle);
        if (ctx)    libusb_exit(ctx);
    }

    int read_frame(byteVector &buf)
    {
        return usb_read(ENDPOINT_1, buf);
    }
    
    void update()
    {
        byteVector buf;
        if (read_frame(buf) == 0 && !buf.empty()) {
            // Process the frame if read successfully
            for (auto& handler : handlers_) {
                handler(buf);
            }
        }
    }
    
    void register_frame_handler(std::function<void(const byteVector&)> handler)
    {
        handlers_.push_back(std::move(handler));
    }
    
    std::string get_device_path() const    { return device_path_; }
    uint8_t     get_bus_number() const     { return bus_number_; }
    uint8_t     get_device_address() const { return device_address_; }

    static std::vector<std::pair<uint8_t, uint8_t>> list_devices(libusb_context *ctx = nullptr)
    {
        std::vector<std::pair<uint8_t, uint8_t>> result;
        bool local_ctx = false;
        if (!ctx) {
            local_ctx = true;
            if (libusb_init(&ctx) < 0) return result;
        }
        libusb_device **devs = nullptr;
        int cnt = libusb_get_device_list(ctx, &devs);
        if (cnt < 0) {
            if (local_ctx) libusb_exit(ctx);
            return result;
        }
        for (int i = 0; i < cnt; ++i) {
            libusb_device *dev = devs[i];
            libusb_device_descriptor desc;
            if (libusb_get_device_descriptor(dev, &desc) == 0) {
                if (desc.idVendor == USB_VENDOR_ID && desc.idProduct == USB_PRODUCT_ID) {
                    result.emplace_back(
                        libusb_get_bus_number(dev),
                        libusb_get_device_address(dev)
                    );
                }
            }
        }
        libusb_free_device_list(devs, 1);
        if (local_ctx) libusb_exit(ctx);
        return result;
    }
};

class UPPCamera
{
    struct [[gnu::packed]] upp_usb_frame_t {
        uint16_t magic;
        uint8_t  cid;
        uint16_t length;
    };
    struct [[gnu::packed]] upp_cam_frame_t {
        uint8_t  fid;
        uint8_t  cam_num;
        unsigned char has_g        :1;
        unsigned char button_press :1;
        unsigned char other        :6;
        uint32_t g_sensor;
    };

    byteVector          buffer_;
    upp_cam_frame_t     last_header_{};
    std::function<void(const byteVector&)> pic_cb_;
    bool buffer_valid_ = false;
    uint8_t last_fid_ = 0;
    int consecutive_good_frames_ = 0;
    int consecutive_bad_frames_ = 0;
    static constexpr int MAX_BAD_FRAMES = 3;

public:
    UPPCamera(std::function<void(const byteVector&)> pic_callback)
     : pic_cb_(std::move(pic_callback)) {}

    // Enhanced validation for UPP frames
    bool validate_frame_buffer(const byteVector &buffer) {
        // 1. Check minimum size
        if (buffer.size() < 1024) {
            return false; // Too small to be a valid frame
        }
        
        // 2. Check for valid JPEG markers (should start with FFD8 and end with FFD9)
        if (buffer.size() < 4 || 
            buffer[0] != 0xFF || buffer[1] != 0xD8 || 
            buffer[buffer.size()-2] != 0xFF || buffer[buffer.size()-1] != 0xD9) {
            return false;
        }
        
        // 3. Check for reasonable JPEG structure (basic JPEG validation)
        bool found_soi = false;
        bool found_eoi = false;
        size_t i = 0;
        
        // Find Start Of Image (SOI) marker
        if (i < buffer.size() - 1 && buffer[i] == 0xFF && buffer[i+1] == 0xD8) {
            found_soi = true;
            i += 2;
        }
        
        if (!found_soi) return false;
        
        // Scan through markers
        while (i < buffer.size() - 1) {
            // Find marker (every marker starts with 0xFF)
            if (buffer[i] != 0xFF) {
                i++;
                continue;
            }
            
            // Skip padding
            while (i < buffer.size() && buffer[i] == 0xFF) i++;
            
            if (i >= buffer.size()) break;
            
            // End Of Image (EOI)
            if (buffer[i] == 0xD9) {
                found_eoi = true;
                break;
            }
            
            // For markers with length fields, skip the segment
            if ((buffer[i] >= 0xC0 && buffer[i] <= 0xCF && buffer[i] != 0xC4 && buffer[i] != 0xC8) || 
                (buffer[i] >= 0xDB && buffer[i] <= 0xFE)) {
                if (i + 2 >= buffer.size()) break;
                
                int length = (buffer[i+1] << 8) | buffer[i+2];
                i += length + 2;
            } else {
                i++;
            }
        }
        
        return found_soi && found_eoi;
    }

    void handle_upp_frame(const byteVector &data)
    {
        if (data.size() < sizeof(upp_usb_frame_t)) return;
        auto usb_hdr = reinterpret_cast<const upp_usb_frame_t*>(data.data());
        if (usb_hdr->magic != 0xBBAA || usb_hdr->cid != 7) return;

        size_t cam_off = sizeof(upp_usb_frame_t);
        if (data.size() < cam_off + sizeof(upp_cam_frame_t)) return;
        auto cam_hdr = reinterpret_cast<const upp_cam_frame_t*>(data.data() + cam_off);
        
        // Check for valid camera header
        if (cam_hdr->cam_num >= 2 || cam_hdr->other != 0) {
            buffer_valid_ = false;
            buffer_.clear();
            consecutive_bad_frames_++;
            return;
        }

        // frame boundary
        if (!buffer_.empty() && cam_hdr->fid != last_header_.fid) {
            if (buffer_valid_ && validate_frame_buffer(buffer_)) {
                pic_cb_(buffer_);
                consecutive_good_frames_++;
                consecutive_bad_frames_ = 0;
            } else {
                consecutive_bad_frames_++;
                // If too many bad frames in a row, try to resync
                if (consecutive_bad_frames_ >= MAX_BAD_FRAMES) {
                    buffer_valid_ = false;
                }
            }
            buffer_.clear();
        }
        
        // Starting a new frame
        if (buffer_.empty()) {
            last_header_ = *cam_hdr;
            buffer_valid_ = true;
            
            // Check for logical FID sequence
            if (last_fid_ != 0 && ((last_fid_ + 1) % 256 != cam_hdr->fid)) {
                // Out of sequence - could be frame drop or corruption
                buffer_valid_ = false;
            }
            last_fid_ = cam_hdr->fid;
            
            // Validate field values
            if (cam_hdr->cam_num >= 2 || cam_hdr->other != 0) {
                buffer_valid_ = false;
            }
        }
        
        // Only append payload if buffer is considered valid
        if (buffer_valid_) {
            auto payload_begin = data.begin() + cam_off + sizeof(upp_cam_frame_t);
            buffer_.insert(buffer_.end(), payload_begin, data.end());
        }
    }
    
    void update()
    {
        // No active polling needed - this class processes frames when handle_upp_frame is called
    }
};

class EndoscopeCamera {
private:
    std::unique_ptr<UsbSupercamera> usb_;
    std::unique_ptr<UPPCamera>      upp_;
    image_transport::Publisher      pub_;
    compressed_image_transport::CompressedPublisher compressed_pub_;

    // Compression parameters
    int jpeg_quality_ = 80;  // JPEG quality (0-100)
    bool use_compressed_ = true;  // Whether to use compressed transport

    // Corruption‐filter state
    std::string name_;
    int         expected_width_  = 0;
    int         expected_height_ = 0;
    cv::Mat     last_frame_;
    cv::Mat     last_good_frame_; // Keep a good frame to use as fallback
    bool        active_         = false;
    int         frame_count_    = 0;
    int         corrupted_count_ = 0;
    ros::Time   last_stats_time_;
    int         consecutive_drops_ = 0;

    bool isCorrupted(const cv::Mat &img) {
        // 1) completely empty?
        if (img.empty()) return true;
        
        // 2) very small image?
        if (img.cols < 160 || img.rows < 120) return true;
        
        // First valid frame sets expected dimensions
        if (expected_width_ == 0 && expected_height_ == 0) {
            expected_width_  = img.cols;
            expected_height_ = img.rows;
            ROS_INFO("[%s] Setting expected dimensions: %dx%d", 
                    name_.c_str(), expected_width_, expected_height_);
        }
        
        // 3) wrong size? Allow small variations but catch major issues
        if (abs(img.cols - expected_width_) > expected_width_/10 || 
            abs(img.rows - expected_height_) > expected_height_/10) {
            ROS_WARN_THROTTLE(2.0, "[%s] Frame size mismatch: got %dx%d, expected %dx%d", 
                            name_.c_str(), img.cols, img.rows, expected_width_, expected_height_);
            return true;
        }
        
        // 4) nearly all black?
        cv::Mat gray;
        cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
        double nonZero = cv::countNonZero(gray);
        if (nonZero < 0.05 * gray.total()) {
            ROS_WARN_THROTTLE(2.0, "[%s] Frame too dark: only %.1f%% non-black pixels", 
                            name_.c_str(), (nonZero * 100.0) / gray.total());
            return true;
        }
        
        // 5) Check for severe compression artifacts
        cv::Mat edges;
        cv::Laplacian(gray, edges, CV_8U, 3);
        cv::Scalar mean, stddev;
        cv::meanStdDev(edges, mean, stddev);
        
        if (stddev[0] < 5.0) {  // Low edge variance - may indicate a blurry or corrupted frame
            ROS_WARN_THROTTLE(2.0, "[%s] Frame appears blurry or corrupted (edge stddev: %.1f)", 
                            name_.c_str(), stddev[0]);
            return true;
        }
        
        return false;
    }

    void updateStats() {
        frame_count_++;
        
        // Print stats every 5 seconds
        ros::Time now = ros::Time::now();
        if ((now - last_stats_time_).toSec() >= 5.0) {
            double duration = (now - last_stats_time_).toSec();
            double fps = frame_count_ / duration;
            double corruption_rate = (corrupted_count_ * 100.0) / (frame_count_ > 0 ? frame_count_ : 1);
            
            ROS_INFO("[%s] Stats: %.1f FPS, %.1f%% corrupted (%d/%d)", 
                    name_.c_str(), fps, corruption_rate, corrupted_count_, frame_count_);
            
            frame_count_ = 0;
            corrupted_count_ = 0;
            last_stats_time_ = now;
        }
    }

public:
    EndoscopeCamera(image_transport::ImageTransport &it,
                    const std::string          &name,
                    uint8_t                     bus_num,
                    uint8_t                     dev_addr)
        : name_(name), last_stats_time_(ros::Time::now())
    {
        try {
            // USB layer
            usb_ = std::make_unique<UsbSupercamera>(bus_num, dev_addr);
            
            // Get compression parameters from ROS parameter server
            ros::NodeHandle pnh("~");
            pnh.param("jpeg_quality", jpeg_quality_, jpeg_quality_);
            pnh.param("use_compressed", use_compressed_, use_compressed_);
            
            // Create publishers
            if (use_compressed_) {
                compressed_pub_ = compressed_image_transport::CompressedPublisher(
                    it.advertise("supercamera/" + name + "/image_raw/compressed", 1)
                );
                ROS_INFO("[%s] Using compressed image transport with JPEG quality %d", 
                        name_.c_str(), jpeg_quality_);
            } else {
                pub_ = it.advertise("supercamera/" + name + "/image_raw", 1);
                ROS_INFO("[%s] Using raw image transport", name_.c_str());
            }

            // UPP layer: decode & publish with corruption check
            upp_ = std::make_unique<UPPCamera>(
                [this](const byteVector &buf) {
                    cv::Mat img = cv::imdecode(buf, cv::IMREAD_COLOR);
                    
                    if (isCorrupted(img)) {
                        corrupted_count_++;
                        consecutive_drops_++;
                        ROS_WARN_THROTTLE(1.0, "[%s] dropped corrupted frame (%d consecutive drops)", 
                                        name_.c_str(), consecutive_drops_);
                        return;
                    }
                    
                    consecutive_drops_ = 0;
                    std_msgs::Header hdr;
                    hdr.stamp = ros::Time::now();
                    
                    if (use_compressed_) {
                        // Publish compressed image
                        sensor_msgs::CompressedImage compressed_msg;
                        compressed_msg.header = hdr;
                        compressed_msg.format = "jpeg";
                        std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, jpeg_quality_};
                        cv::imencode(".jpg", img, compressed_msg.data, params);
                        compressed_pub_.publish(compressed_msg);
                    } else {
                        // Publish raw image
                        auto msg = cv_bridge::CvImage(hdr, "bgr8", img).toImageMsg();
                        pub_.publish(msg);
                    }
                    
                    // Keep reference to last good frame
                    last_good_frame_ = img.clone();
                    last_frame_ = img;
                    
                    updateStats();
                }
            );
            
            // Connect USB camera to UPP parser
            usb_->register_frame_handler([this](const byteVector &data) {
                upp_->handle_upp_frame(data);
            });

            active_ = true;
            ROS_INFO("Camera '%s' init: bus=%d addr=%d path=%s",
                    name_.c_str(),
                    usb_->get_bus_number(),
                    usb_->get_device_address(),
                    usb_->get_device_path().c_str());
        }
        catch (const std::exception &e) {
            ROS_ERROR("EndoscopeCamera '%s' init failed: %s", name_.c_str(), e.what());
        }
    }

    bool is_active() const { return active_; }

    void update() {
        if (!active_) return;
        
        // Attempt to recover if we're stuck with corruption
        if (consecutive_drops_ > 10) {
            ROS_WARN("[%s] Too many consecutive dropped frames, attempting recovery...", name_.c_str());
            if (usb_) {
                // Request a reset of the streaming sequence
                try {
                    // Re-initialize streaming
                    usb_->register_frame_handler({}); // Clear handlers temporarily
                    usb_ = std::make_unique<UsbSupercamera>(
                        usb_->get_bus_number(),
                        usb_->get_device_address()
                    );
                    
                    // Reconnect the handler
                    usb_->register_frame_handler([this](const byteVector &data) {
                        upp_->handle_upp_frame(data);
                    });
                    
                    consecutive_drops_ = 0;
                    ROS_INFO("[%s] Camera reset completed", name_.c_str());
                } catch (const std::exception &e) {
                    ROS_ERROR("[%s] Failed to reset camera: %s", name_.c_str(), e.what());
                }
            }
        }
        
        if (usb_) {
            usb_->update(); // This will read frames and pass to UPP via registered handler
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// main()
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char **argv)
{
    ros::init(argc, argv, "supercamera_node");
    ros::NodeHandle nh;
    image_transport::ImageTransport it(nh);

    ros::NodeHandle pnh("~");
    int bus_num = 0, dev_addr = 0;
    std::string camera_name = "cam";
    
    // Fix parameter names to match launch file
    pnh.param("bus", bus_num, bus_num);
    pnh.param("device", dev_addr, dev_addr); // Changed from "addr" to "device"
    pnh.param("camera_name", camera_name, camera_name); // Changed from "name" to "camera_name"

    ROS_INFO("Starting camera '%s' with bus=%d device=%d", 
             camera_name.c_str(), bus_num, dev_addr);

    auto cam = std::make_unique<EndoscopeCamera>(
        it, camera_name, static_cast<uint8_t>(bus_num), static_cast<uint8_t>(dev_addr)
    );
    if (!cam->is_active()) {
        ROS_ERROR("Camera '%s' failed to initialize. Exiting.", camera_name.c_str());
        return 1;
    }

    ROS_INFO("'%s' ready — entering spin loop", camera_name.c_str());
    ros::Rate loop_rate(1000);
    while (ros::ok()) {
        cam->update();
        ros::spinOnce();
        loop_rate.sleep();
    }
    return 0;
}
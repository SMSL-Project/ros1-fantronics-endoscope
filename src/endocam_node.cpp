#include <ros/ros.h>
#include <image_transport/image_transport.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/Image.h>

#include <libusb-1.0/libusb.h>
#include <opencv2/imgcodecs.hpp>

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

public:
    UPPCamera(std::function<void(const byteVector&)> pic_callback)
     : pic_cb_(std::move(pic_callback)) {}

    void handle_upp_frame(const byteVector &data)
    {
        if (data.size() < sizeof(upp_usb_frame_t)) return;
        auto usb_hdr = reinterpret_cast<const upp_usb_frame_t*>(data.data());
        if (usb_hdr->magic != 0xBBAA || usb_hdr->cid != 7) return;

        size_t cam_off = sizeof(upp_usb_frame_t);
        if (data.size() < cam_off + sizeof(upp_cam_frame_t)) return;
        auto cam_hdr = reinterpret_cast<const upp_cam_frame_t*>(data.data() + cam_off);

        // frame boundary
        if (!buffer_.empty() && cam_hdr->fid != last_header_.fid) {
            pic_cb_(buffer_);
            buffer_.clear();
        }
        if (buffer_.empty()) {
            last_header_ = *cam_hdr;
            assert(cam_hdr->cam_num < 2 && cam_hdr->has_g == 0 && cam_hdr->other == 0);
        }
        // append payload
        auto payload_begin = data.begin() + cam_off + sizeof(upp_cam_frame_t);
        buffer_.insert(buffer_.end(), payload_begin, data.end());
    }
};

class EndoscopeCamera
{
    std::unique_ptr<UsbSupercamera> usb;
    std::unique_ptr<UPPCamera>      upp;
    image_transport::Publisher       pub;
    bool                             active = false;

public:
    EndoscopeCamera(image_transport::ImageTransport& it,
                    const std::string& name,
                    uint8_t bus_num,
                    uint8_t dev_addr)
    {
        try {
            usb = std::make_unique<UsbSupercamera>(bus_num, dev_addr);
            pub = it.advertise("supercamera/" + name + "/image_raw", 1);
            upp = std::make_unique<UPPCamera>([this](const byteVector &pic){
                cv::Mat img = cv::imdecode(pic, cv::IMREAD_COLOR);
                if (img.empty()) return;
                std_msgs::Header hdr;
                hdr.stamp = ros::Time::now();
                auto msg = cv_bridge::CvImage(hdr, "bgr8", img).toImageMsg();
                pub.publish(msg);
            });
            active = true;
            ROS_INFO("Camera init: bus=%d dev=%d path=%s",
                     usb->get_bus_number(),
                     usb->get_device_address(),
                     usb->get_device_path().c_str());
        }
        catch (const std::exception &e) {
            ROS_ERROR("EndoscopeCamera init failed: %s", e.what());
        }
    }

    bool is_active() const { return active; }

    void update()
    {
        if (!active) return;
        byteVector buf;
        if (usb->read_frame(buf) == 0) {
            upp->handle_upp_frame(buf);
        }
    }
};

int main(int argc, char** argv)
{
    // We’ll name each node after the camera_name to keep logs/topics distinct.
    ros::init(argc, argv, "supercamera_node");
    ros::NodeHandle nh;
    ros::NodeHandle pnh("~");  // private namespace for camera-specific params
    image_transport::ImageTransport it(nh);

    // Fetch parameters for *this* camera
    std::string camera_name;
    int bus_num, dev_addr;
    pnh.param<std::string>("camera_name", camera_name, "camera");
    pnh.param("bus",    bus_num,  0);
    pnh.param("device", dev_addr, 0);

    // Initialize exactly one EndoscopeCamera
    auto cam = std::make_unique<EndoscopeCamera>(it, camera_name, (uint8_t)bus_num, (uint8_t)dev_addr);
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
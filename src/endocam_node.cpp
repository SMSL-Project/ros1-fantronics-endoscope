// src/supercamera_node.cpp
#include <ros/ros.h>
#include <image_transport/image_transport.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/Image.h>

#include <libusb-1.0/libusb.h>
#include <opencv2/imgcodecs.hpp>

#include <cassert>
#include <vector>
#include <atomic>
#include <thread>
#include <functional>

//------------------------------------------------------------------------------
// Your original UsbSupercamera (unchanged)
//------------------------------------------------------------------------------
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

    libusb_context *ctx;
    libusb_device_handle *handle;

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

    int setup()
    {
        if (libusb_init(&ctx) < 0) return 1;
        handle = libusb_open_device_with_vid_pid(ctx, USB_VENDOR_ID, USB_PRODUCT_ID);
        if (!handle) return 1;
        libusb_reset_device(handle);
        libusb_claim_interface(handle, INTERFACE_A_NUMBER);
        libusb_claim_interface(handle, INTERFACE_B_NUMBER);
        libusb_set_interface_alt_setting(handle, INTERFACE_B_NUMBER, INTERFACE_B_ALTERNATE_SETTING);
        return 0;
    }

public:
    UsbSupercamera()
    {
        if (setup() != 0) throw std::runtime_error("Failed to init USB camera");
        // Magic commands
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
};

//------------------------------------------------------------------------------
// Your original UPPCamera (unchanged)
//------------------------------------------------------------------------------
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
        unsigned char has_g         :1;
        unsigned char button_press  :1;
        unsigned char other         :6;
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

        // New frame boundary?
        if (!buffer_.empty() && cam_hdr->fid != last_header_.fid) {
            pic_cb_(buffer_);
            buffer_.clear();
        }
        if (buffer_.empty()) {
            last_header_ = *cam_hdr;
            assert(cam_hdr->cam_num < 2 && cam_hdr->has_g == 0 && cam_hdr->other == 0);
        }
        // Append payload
        auto payload_begin = data.begin() + cam_off + sizeof(upp_cam_frame_t);
        buffer_.insert(buffer_.end(), payload_begin, data.end());
    }
};

//------------------------------------------------------------------------------
// ROS node
//------------------------------------------------------------------------------
int main(int argc, char** argv)
{
    ros::init(argc, argv, "supercamera_node");
    ros::NodeHandle nh;
    image_transport::ImageTransport it(nh);
    auto pub = it.advertise("supercamera/image_raw", 1);

    UsbSupercamera usb;
    UPPCamera       upp([&](const byteVector &pic){
        // 1) Decode JPEG into cv::Mat
        cv::Mat img = cv::imdecode(pic, cv::IMREAD_COLOR);
        if (img.empty()) return;
        // 2) Wrap in ROS message
        std_msgs::Header hdr;
        hdr.stamp = ros::Time::now();
        auto msg = cv_bridge::CvImage(hdr, "bgr8", img).toImageMsg();
        // 3) Publish
        pub.publish(msg);
    });

    byteVector buf;
    ros::Rate loop_rate(1000);
    while (ros::ok()) {
        if (usb.read_frame(buf) == 0) {
            upp.handle_upp_frame(buf);
        }
        ros::spinOnce();
        loop_rate.sleep();
    }
    return 0;
}

/*
 * UDP Image Streaming Server for Fantronics Endoscope
 * 
 * This application captures images from the endoscope USB device and streams
 * them over UDP. Clients can receive JPEG-encoded frames.
 * 
 * Usage: ./endoscope_udp_server [port] [target_ip] [target_port] [bus] [device] [quality]
 *   port        - UDP port to bind on (default: 8889)
 *   target_ip   - Target IP address to send to (default: 127.0.0.1)
 *   target_port - Target UDP port to send to (default: 8890)
 *   bus         - USB bus number (default: 0 = auto-detect)
 *   device      - USB device address (default: 0 = auto-detect)
 *   quality     - JPEG quality 1-100 (default: 80)
 */

#include "endoscope_capture.h"
#include <opencv2/imgcodecs.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstring>

class UdpImageServer {
private:
    int socket_fd_;
    int port_;
    struct sockaddr_in target_addr_;
    std::atomic<bool> running_;
    std::mutex frame_mutex_;
    std::vector<uint8_t> current_frame_;
    int jpeg_quality_;
    
    static constexpr size_t MAX_UDP_PACKET = 65507; // Max UDP payload
    static constexpr size_t CHUNK_SIZE = 60000;      // Leave room for headers
    
    struct PacketHeader {
        uint32_t frame_id;
        uint32_t total_chunks;
        uint32_t chunk_index;
        uint32_t chunk_size;
    };

    void send_frame() {
        std::vector<uint8_t> frame_data;
        
        // Get current frame
        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            if (current_frame_.empty()) {
                return;
            }
            frame_data = current_frame_;
        }
        
        // Calculate number of chunks needed
        uint32_t total_chunks = (frame_data.size() + CHUNK_SIZE - 1) / CHUNK_SIZE;
        static uint32_t frame_id = 0;
        frame_id++;
        
        // Send each chunk
        for (uint32_t i = 0; i < total_chunks; i++) {
            PacketHeader header;
            header.frame_id = htonl(frame_id);
            header.total_chunks = htonl(total_chunks);
            header.chunk_index = htonl(i);
            
            size_t offset = i * CHUNK_SIZE;
            size_t chunk_size = std::min(CHUNK_SIZE, frame_data.size() - offset);
            header.chunk_size = htonl(chunk_size);
            
            // Prepare packet
            std::vector<uint8_t> packet(sizeof(PacketHeader) + chunk_size);
            memcpy(packet.data(), &header, sizeof(PacketHeader));
            memcpy(packet.data() + sizeof(PacketHeader), frame_data.data() + offset, chunk_size);
            
            // Send packet
            ssize_t sent = sendto(socket_fd_, packet.data(), packet.size(), 0,
                                 (struct sockaddr*)&target_addr_, sizeof(target_addr_));
            if (sent < 0) {
                std::cerr << "Failed to send UDP packet" << std::endl;
            }
            
            // Small delay between chunks to avoid overwhelming receiver
            if (total_chunks > 1 && i < total_chunks - 1) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    }

public:
    UdpImageServer(int port, const std::string& target_ip, int target_port, int jpeg_quality = 80) 
        : port_(port), running_(false), jpeg_quality_(jpeg_quality) {
        socket_fd_ = -1;
        memset(&target_addr_, 0, sizeof(target_addr_));
        target_addr_.sin_family = AF_INET;
        target_addr_.sin_port = htons(target_port);
        inet_pton(AF_INET, target_ip.c_str(), &target_addr_.sin_addr);
    }
    
    ~UdpImageServer() {
        stop();
    }
    
    bool start() {
        // Create socket
        socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd_ < 0) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }
        
        // Bind to port
        struct sockaddr_in bind_addr;
        memset(&bind_addr, 0, sizeof(bind_addr));
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_addr.s_addr = INADDR_ANY;
        bind_addr.sin_port = htons(port_);
        
        if (bind(socket_fd_, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
            std::cerr << "Failed to bind to port " << port_ << std::endl;
            close(socket_fd_);
            return false;
        }
        
        running_ = true;
        
        char target_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &target_addr_.sin_addr, target_str, INET_ADDRSTRLEN);
        std::cout << "UDP server started on port " << port_ << std::endl;
        std::cout << "Streaming to " << target_str << ":" << ntohs(target_addr_.sin_port) << std::endl;
        
        // Send frames in a separate thread
        std::thread([this]() {
            while (running_) {
                send_frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 FPS
            }
        }).detach();
        
        return true;
    }
    
    void stop() {
        running_ = false;
        if (socket_fd_ >= 0) {
            close(socket_fd_);
            socket_fd_ = -1;
        }
    }
    
    void update_frame(const cv::Mat &frame) {
        if (frame.empty()) return;
        
        // Encode frame as JPEG
        std::vector<uint8_t> encoded;
        std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, jpeg_quality_};
        if (!cv::imencode(".jpg", frame, encoded, params)) {
            return;
        }
        
        // Update current frame
        std::lock_guard<std::mutex> lock(frame_mutex_);
        current_frame_ = std::move(encoded);
    }
};

int main(int argc, char** argv) {
    // Parse command line arguments
    int port = 8889;
    std::string target_ip = "127.0.0.1";
    int target_port = 8890;
    int bus_num = 0;
    int dev_addr = 0;
    int jpeg_quality = 80;
    
    if (argc > 1) port = std::atoi(argv[1]);
    if (argc > 2) target_ip = argv[2];
    if (argc > 3) target_port = std::atoi(argv[3]);
    if (argc > 4) bus_num = std::atoi(argv[4]);
    if (argc > 5) dev_addr = std::atoi(argv[5]);
    if (argc > 6) jpeg_quality = std::atoi(argv[6]);
    
    // Validate parameters
    if (port <= 0 || port > 65535) {
        std::cerr << "Invalid port number: " << port << std::endl;
        return 1;
    }
    if (target_port <= 0 || target_port > 65535) {
        std::cerr << "Invalid target port number: " << target_port << std::endl;
        return 1;
    }
    if (jpeg_quality < 1 || jpeg_quality > 100) {
        std::cerr << "Invalid JPEG quality: " << jpeg_quality << " (must be 1-100)" << std::endl;
        return 1;
    }
    
    std::cout << "Starting UDP Image Streaming Server" << std::endl;
    std::cout << "Bind Port: " << port << std::endl;
    std::cout << "Target: " << target_ip << ":" << target_port << std::endl;
    std::cout << "USB Bus: " << bus_num << " (0 = auto)" << std::endl;
    std::cout << "USB Device: " << dev_addr << " (0 = auto)" << std::endl;
    std::cout << "JPEG Quality: " << jpeg_quality << std::endl;
    std::cout << std::endl;
    
    try {
        // Initialize endoscope capture
        EndoscopeCapture capture("endoscope", bus_num, dev_addr);
        if (!capture.is_active()) {
            std::cerr << "Failed to initialize endoscope camera" << std::endl;
            return 1;
        }
        
        // Start UDP server
        UdpImageServer server(port, target_ip, target_port, jpeg_quality);
        if (!server.start()) {
            std::cerr << "Failed to start UDP server" << std::endl;
            return 1;
        }
        
        std::cout << "Server running. Press Ctrl+C to stop." << std::endl;
        std::cout << std::endl;
        
        // Main loop
        int frame_count = 0;
        auto last_stats = std::chrono::steady_clock::now();
        
        while (true) {
            // Update capture
            capture.update();
            
            // Get frame and send to clients
            if (capture.has_frame()) {
                cv::Mat frame = capture.get_last_frame();
                server.update_frame(frame);
                frame_count++;
                
                // Print stats every 5 seconds
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_stats).count();
                if (elapsed >= 5) {
                    double fps = frame_count / (double)elapsed;
                    std::cout << "Streaming at " << fps << " FPS" << std::endl;
                    last_stats = now;
                    frame_count = 0;
                }
            }
            
            // Small sleep to prevent busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

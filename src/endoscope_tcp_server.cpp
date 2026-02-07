/*
 * TCP/IP Image Streaming Server for Fantronics Endoscope
 * 
 * This application captures images from the endoscope USB device and streams
 * them over TCP/IP. Clients can connect and receive JPEG-encoded frames.
 * 
 * Usage: ./endoscope_tcp_server [port] [bus] [device] [quality]
 *   port    - TCP port to listen on (default: 8888)
 *   bus     - USB bus number (default: 0 = auto-detect)
 *   device  - USB device address (default: 0 = auto-detect)
 *   quality - JPEG quality 1-100 (default: 80)
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

class TcpImageServer {
private:
    int server_fd_;
    int port_;
    std::atomic<bool> running_;
    std::mutex frame_mutex_;
    std::vector<uint8_t> current_frame_;
    int jpeg_quality_;
    
    void handle_client(int client_fd) {
        std::cout << "Client connected (fd=" << client_fd << ")" << std::endl;
        
        while (running_) {
            std::vector<uint8_t> frame_data;
            
            // Get current frame
            {
                std::lock_guard<std::mutex> lock(frame_mutex_);
                if (current_frame_.empty()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                frame_data = current_frame_;
            }
            
            // Send frame size (4 bytes, network byte order)
            uint32_t frame_size = htonl(frame_data.size());
            ssize_t sent = send(client_fd, &frame_size, sizeof(frame_size), MSG_NOSIGNAL);
            if (sent <= 0) {
                std::cout << "Client disconnected (fd=" << client_fd << ")" << std::endl;
                break;
            }
            
            // Send frame data
            size_t total_sent = 0;
            while (total_sent < frame_data.size()) {
                sent = send(client_fd, frame_data.data() + total_sent, 
                           frame_data.size() - total_sent, MSG_NOSIGNAL);
                if (sent <= 0) {
                    std::cout << "Client disconnected during transfer (fd=" << client_fd << ")" << std::endl;
                    break;
                }
                total_sent += sent;
            }
            
            if (sent <= 0) break;
            
            // Small delay to control frame rate
            std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 FPS
        }
        
        close(client_fd);
    }

public:
    TcpImageServer(int port, int jpeg_quality = 80) 
        : port_(port), running_(false), jpeg_quality_(jpeg_quality) {
        server_fd_ = -1;
    }
    
    ~TcpImageServer() {
        stop();
    }
    
    bool start() {
        // Create socket
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }
        
        // Set socket options
        int opt = 1;
        if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            std::cerr << "Failed to set socket options" << std::endl;
            close(server_fd_);
            return false;
        }
        
        // Bind to port
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port_);
        
        if (bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) {
            std::cerr << "Failed to bind to port " << port_ << std::endl;
            close(server_fd_);
            return false;
        }
        
        // Listen for connections
        if (listen(server_fd_, 5) < 0) {
            std::cerr << "Failed to listen on socket" << std::endl;
            close(server_fd_);
            return false;
        }
        
        running_ = true;
        std::cout << "TCP server listening on port " << port_ << std::endl;
        
        // Accept connections in a separate thread
        std::thread([this]() {
            while (running_) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                
                int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
                if (client_fd < 0) {
                    if (running_) {
                        std::cerr << "Failed to accept connection" << std::endl;
                    }
                    continue;
                }
                
                // Handle client in a separate thread
                std::thread(&TcpImageServer::handle_client, this, client_fd).detach();
            }
        }).detach();
        
        return true;
    }
    
    void stop() {
        running_ = false;
        if (server_fd_ >= 0) {
            close(server_fd_);
            server_fd_ = -1;
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
    int port = 8888;
    int bus_num = 0;
    int dev_addr = 0;
    int jpeg_quality = 80;
    
    if (argc > 1) port = std::atoi(argv[1]);
    if (argc > 2) bus_num = std::atoi(argv[2]);
    if (argc > 3) dev_addr = std::atoi(argv[3]);
    if (argc > 4) jpeg_quality = std::atoi(argv[4]);
    
    // Validate parameters
    if (port <= 0 || port > 65535) {
        std::cerr << "Invalid port number: " << port << std::endl;
        return 1;
    }
    if (jpeg_quality < 1 || jpeg_quality > 100) {
        std::cerr << "Invalid JPEG quality: " << jpeg_quality << " (must be 1-100)" << std::endl;
        return 1;
    }
    
    std::cout << "Starting TCP Image Streaming Server" << std::endl;
    std::cout << "Port: " << port << std::endl;
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
        
        // Start TCP server
        TcpImageServer server(port, jpeg_quality);
        if (!server.start()) {
            std::cerr << "Failed to start TCP server" << std::endl;
            return 1;
        }
        
        std::cout << "Server running. Press Ctrl+C to stop." << std::endl;
        std::cout << "Clients can connect using: nc <server_ip> " << port << std::endl;
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

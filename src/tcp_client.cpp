/*
 * Simple TCP Client for receiving image stream
 * 
 * Usage: ./tcp_client [server_ip] [port] [output_dir]
 *   server_ip  - Server IP address (default: 127.0.0.1)
 *   port       - Server port (default: 8888)
 *   output_dir - Optional directory to save received images
 */

#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <string>

class TcpImageClient {
private:
    int socket_fd_;
    std::string server_ip_;
    int server_port_;
    std::string output_dir_;
    bool save_images_;
    
    bool receive_full_data(void* buffer, size_t size) {
        size_t total_received = 0;
        uint8_t* buf = static_cast<uint8_t*>(buffer);
        
        while (total_received < size) {
            ssize_t received = recv(socket_fd_, buf + total_received, 
                                   size - total_received, 0);
            if (received <= 0) {
                return false;
            }
            total_received += received;
        }
        return true;
    }

public:
    TcpImageClient(const std::string& server_ip, int server_port, 
                   const std::string& output_dir = "")
        : server_ip_(server_ip), server_port_(server_port), 
          output_dir_(output_dir), socket_fd_(-1) {
        save_images_ = !output_dir_.empty();
    }
    
    ~TcpImageClient() {
        disconnect();
    }
    
    bool connect() {
        // Create socket
        socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd_ < 0) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }
        
        // Connect to server
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port_);
        if (inet_pton(AF_INET, server_ip_.c_str(), &server_addr.sin_addr) <= 0) {
            std::cerr << "Invalid server IP address" << std::endl;
            close(socket_fd_);
            return false;
        }
        
        if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Failed to connect to server" << std::endl;
            close(socket_fd_);
            return false;
        }
        
        std::cout << "Connected to " << server_ip_ << ":" << server_port_ << std::endl;
        return true;
    }
    
    void disconnect() {
        if (socket_fd_ >= 0) {
            close(socket_fd_);
            socket_fd_ = -1;
        }
    }
    
    bool receive_frame(cv::Mat& frame) {
        // Receive frame size
        uint32_t frame_size;
        if (!receive_full_data(&frame_size, sizeof(frame_size))) {
            return false;
        }
        frame_size = ntohl(frame_size);
        
        if (frame_size == 0 || frame_size > 10 * 1024 * 1024) { // Max 10MB
            std::cerr << "Invalid frame size: " << frame_size << std::endl;
            return false;
        }
        
        // Receive frame data
        std::vector<uint8_t> buffer(frame_size);
        if (!receive_full_data(buffer.data(), frame_size)) {
            return false;
        }
        
        // Decode JPEG
        frame = cv::imdecode(buffer, cv::IMREAD_COLOR);
        return !frame.empty();
    }
    
    void run() {
        int frame_count = 0;
        auto start_time = std::chrono::steady_clock::now();
        auto last_stats = start_time;
        
        cv::namedWindow("Endoscope Stream", cv::WINDOW_AUTOSIZE);
        
        while (true) {
            cv::Mat frame;
            if (!receive_frame(frame)) {
                std::cerr << "Failed to receive frame. Disconnecting..." << std::endl;
                break;
            }
            
            frame_count++;
            
            // Display frame
            cv::imshow("Endoscope Stream", frame);
            
            // Save frame if requested
            if (save_images_) {
                std::string filename = output_dir_ + "/frame_" + 
                                      std::to_string(frame_count) + ".jpg";
                cv::imwrite(filename, frame);
            }
            
            // Print stats every 5 seconds
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_stats).count();
            if (elapsed >= 5) {
                double fps = frame_count / (double)elapsed;
                std::cout << "Receiving at " << fps << " FPS" << std::endl;
                last_stats = now;
                frame_count = 0;
            }
            
            // Check for quit
            if (cv::waitKey(1) == 'q') {
                break;
            }
        }
        
        cv::destroyAllWindows();
    }
};

int main(int argc, char** argv) {
    std::string server_ip = "127.0.0.1";
    int server_port = 8888;
    std::string output_dir = "";
    
    if (argc > 1) server_ip = argv[1];
    if (argc > 2) server_port = std::atoi(argv[2]);
    if (argc > 3) output_dir = argv[3];
    
    std::cout << "TCP Image Client" << std::endl;
    std::cout << "Server: " << server_ip << ":" << server_port << std::endl;
    if (!output_dir.empty()) {
        std::cout << "Saving images to: " << output_dir << std::endl;
    }
    std::cout << "Press 'q' to quit" << std::endl;
    std::cout << std::endl;
    
    TcpImageClient client(server_ip, server_port, output_dir);
    
    if (!client.connect()) {
        return 1;
    }
    
    client.run();
    
    return 0;
}

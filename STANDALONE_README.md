# Standalone Endoscope Image Streaming

This directory contains standalone applications for capturing and streaming images from the Fantronics endoscope camera **without ROS dependencies**. The core USB communication and image decoding functionality has been extracted into a reusable library.

## Overview

The standalone applications include:
- **endoscope_tcp_server**: Streams images over TCP/IP
- **endoscope_udp_server**: Streams images over UDP
- **tcp_client**: Example client to receive and display TCP streams

## Prerequisites

Install the required dependencies:

```bash
sudo apt-get update
sudo apt-get install -y \
    libusb-1.0-0-dev \
    pkg-config \
    libopencv-dev
```

Create the USB rule file so that the USB port can be accessed by non-root user:

```bash
echo 'SUBSYSTEMS=="usb", ENV{DEVTYPE}=="usb_device", ATTRS{idVendor}=="2ce3", ATTRS{idProduct}=="3828", MODE="0666"' | sudo tee /etc/udev/rules.d/99-supercamera.rules
```

Then reload and trigger the rules:

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

**[Important] Re-plug the device**: Disconnect and reconnect the USB cable so that udev applies the new permissions.

## Building

### Building Standalone Applications Only

If you only want the standalone applications **without ROS**:

**Option 1: Using the standalone CMakeLists.txt (recommended)**

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ../CMakeLists_standalone.txt
make
```

**Option 2: Using the main CMakeLists.txt**

```bash
mkdir build
cd build
cmake .. -DCATKIN_ENABLE_TESTING=OFF -DCATKIN_SKIP_TESTING=ON
make endoscope_tcp_server endoscope_udp_server tcp_client
```

### Building with ROS

If you have ROS installed and want to build everything:

```bash
# In your catkin workspace
catkin build ros1_fantronics_endoscope
```

The standalone executables will be in `build/` directory or in the catkin devel space.

## Usage

### TCP Streaming Server

The TCP server accepts incoming client connections and streams JPEG-encoded frames.

```bash
./endoscope_tcp_server [port] [bus] [device] [quality]
```

**Parameters:**
- `port`: TCP port to listen on (default: 8888)
- `bus`: USB bus number (default: 0 = auto-detect)
- `device`: USB device address (default: 0 = auto-detect)
- `quality`: JPEG quality 1-100 (default: 80)

**Examples:**

```bash
# Auto-detect camera, listen on port 8888, quality 80
./endoscope_tcp_server

# Custom port 9000, quality 90
./endoscope_tcp_server 9000 0 0 90

# Specific USB device (bus 1, device 9), quality 85
./endoscope_tcp_server 8888 1 9 85
```

**Protocol:**
1. Each frame is preceded by a 4-byte frame size (network byte order)
2. Frame data follows as JPEG-encoded bytes
3. Frames are streamed at approximately 30 FPS

### UDP Streaming Server

The UDP server sends frames to a specified target IP and port.

```bash
./endoscope_udp_server [bind_port] [target_ip] [target_port] [bus] [device] [quality]
```

**Parameters:**
- `bind_port`: UDP port to bind on (default: 8889)
- `target_ip`: Target IP address to send to (default: 127.0.0.1)
- `target_port`: Target UDP port (default: 8890)
- `bus`: USB bus number (default: 0 = auto-detect)
- `device`: USB device address (default: 0 = auto-detect)
- `quality`: JPEG quality 1-100 (default: 80)

**Examples:**

```bash
# Send to localhost:8890
./endoscope_udp_server

# Send to remote machine
./endoscope_udp_server 8889 192.168.1.100 8890

# With custom quality
./endoscope_udp_server 8889 192.168.1.100 8890 0 0 90
```

**Protocol:**
- Large frames are split into chunks (max 60KB per chunk)
- Each chunk has a header containing:
  - Frame ID (4 bytes)
  - Total chunks (4 bytes)
  - Chunk index (4 bytes)
  - Chunk size (4 bytes)
- Receiver must reassemble chunks to reconstruct the full frame

### TCP Client (Example)

A simple example client that connects to the TCP server and displays the stream.

**C++ Client:**

```bash
./tcp_client [server_ip] [port] [output_dir]
```

**Parameters:**
- `server_ip`: Server IP address (default: 127.0.0.1)
- `port`: Server port (default: 8888)
- `output_dir`: Optional directory to save received images

**Examples:**

```bash
# Connect to local server
./tcp_client

# Connect to remote server
./tcp_client 192.168.1.50 8888

# Save frames to directory
./tcp_client 192.168.1.50 8888 /tmp/frames
```

**Python Client:**

A Python client is also available in `examples/tcp_client.py`:

```bash
# Install dependencies
pip3 install opencv-python numpy

# Run the client
python3 examples/tcp_client.py [server_ip] [port]
```

**Controls:**
- Press 'q' or ESC to quit

**More Examples:**

See the `examples/` directory for more client implementations and examples in different languages.

## Identifying USB Device

To find the USB bus and device address for your endoscope:

```bash
lsusb | grep 2ce3:3828
```

This will output something like:
```
Bus 001 Device 009: ID 2ce3:3828 ...
```

Use these numbers (bus=1, device=9) when running the applications.

## Network Testing

### Testing TCP Stream with netcat

You can test the TCP stream without a GUI using netcat:

```bash
# Start the server
./endoscope_tcp_server 8888

# In another terminal, connect with netcat and save to file
nc localhost 8888 > stream.dat
```

The received data will contain frame size headers followed by JPEG data.

### Testing with ffplay

If you create a simple wrapper script, you can view the stream with ffplay:

```bash
# Connect and pipe to ffplay (requires custom demuxer)
nc localhost 8888 | ffplay -f mjpeg -
```

### Creating a Custom Client

To create your own client application:

1. Connect to the TCP server
2. For each frame:
   - Read 4 bytes (frame size) as uint32_t in network byte order
   - Read `frame_size` bytes (JPEG data)
   - Decode the JPEG data using your preferred image library
   - Display or process the frame

See `src/tcp_client.cpp` for a complete example.

## Performance Tuning

### JPEG Quality

- Lower quality (50-70): Smaller files, faster transmission, lower quality
- Medium quality (75-85): Good balance
- High quality (90-100): Larger files, slower transmission, better quality

### Frame Rate

The servers stream at approximately 30 FPS. To adjust:
- Edit the sleep duration in the server code
- Higher FPS = more CPU and bandwidth usage
- Lower FPS = less resource usage but choppier video

### Network Considerations

**TCP:**
- Reliable, ordered delivery
- Handles packet loss automatically
- May have higher latency due to retransmissions
- Better for LAN with stable connections

**UDP:**
- Lower latency
- No guaranteed delivery (frames may be lost)
- Better for real-time applications
- Requires custom reassembly logic for large frames

## Troubleshooting

### "Failed to init USB camera"

1. Check that the device is connected: `lsusb | grep 2ce3:3828`
2. Verify USB permissions: `ls -l /dev/bus/usb/XXX/YYY`
3. Re-plug the device after setting up udev rules
4. Try running with sudo (not recommended for production)

### "Failed to bind to port"

1. Check if port is already in use: `netstat -tuln | grep PORT`
2. Try a different port number
3. Ensure you have permission to bind to the port

### No frames received

1. Check that the server is running and streaming
2. Verify network connectivity (ping server)
3. Check firewall rules
4. Look at server console for error messages

### Poor frame rate

1. Reduce JPEG quality
2. Check network bandwidth
3. Check CPU usage on both server and client
4. Reduce image resolution if needed (requires code modification)

## Library API

The core functionality is provided by `endoscope_capture.h`:

### EndoscopeCapture Class

```cpp
#include "endoscope_capture.h"

// Create capture instance
EndoscopeCapture capture("name", bus_num, dev_addr);

// Check if initialized
if (!capture.is_active()) {
    // Handle error
}

// Main loop
while (true) {
    // Update to read new frames
    capture.update();
    
    // Check if frame is available
    if (capture.has_frame()) {
        cv::Mat frame = capture.get_last_frame();
        // Process frame...
    }
}
```

### UsbSupercamera Class

Low-level USB communication with the endoscope device.

### UPPCamera Class

Decodes the UPP (USB Protocol) frames and extracts JPEG images.

## License

This code is part of the ros1_fantronics_endoscope package. See the main repository for license information.

## Contributing

Contributions are welcome! Please submit issues and pull requests to the main repository.

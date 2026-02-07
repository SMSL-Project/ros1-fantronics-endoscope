# Quick Start Guide - Standalone Image Streaming

This guide will help you quickly get started with streaming endoscope images over TCP/IP without ROS.

## 1. Install Dependencies

```bash
sudo apt-get update
sudo apt-get install -y libusb-1.0-0-dev pkg-config libopencv-dev
```

## 2. Set Up USB Permissions

```bash
echo 'SUBSYSTEMS=="usb", ENV{DEVTYPE}=="usb_device", ATTRS{idVendor}=="2ce3", ATTRS{idProduct}=="3828", MODE="0666"' | sudo tee /etc/udev/rules.d/99-supercamera.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

**Important:** Unplug and replug the USB endoscope after setting up the rules.

## 3. Build the Applications

```bash
./build_standalone.sh
```

This will create three executables in the `build/` directory:
- `endoscope_tcp_server`
- `endoscope_udp_server`
- `tcp_client`

## 4. Start Streaming

### Option A: Using C++ Client

**Terminal 1 - Start the server:**
```bash
cd build
./endoscope_tcp_server
```

**Terminal 2 - Start the client:**
```bash
cd build
./tcp_client
```

### Option B: Using Python Client

**Terminal 1 - Start the server:**
```bash
cd build
./endoscope_tcp_server
```

**Terminal 2 - Start the Python client:**
```bash
# Install Python dependencies
pip3 install opencv-python numpy

# Run the client
python3 ../examples/tcp_client.py
```

## 5. View the Stream

A window will open showing the live endoscope feed. Press 'q' or ESC to quit.

## Customization

### Change the Port

```bash
./endoscope_tcp_server 9000  # Use port 9000 instead of default 8888
./tcp_client 127.0.0.1 9000  # Connect to port 9000
```

### Change JPEG Quality

```bash
# Higher quality (90), default is 80
./endoscope_tcp_server 8888 0 0 90

# Lower quality for faster streaming (60)
./endoscope_tcp_server 8888 0 0 60
```

### Stream Over Network

**Server machine:**
```bash
./endoscope_tcp_server 8888
```

**Client machine (replace SERVER_IP with actual IP):**
```bash
./tcp_client SERVER_IP 8888
# Or with Python:
python3 tcp_client.py SERVER_IP 8888
```

## Troubleshooting

### "Failed to init USB camera"
- Check that the device is connected: `lsusb | grep 2ce3:3828`
- Verify permissions on the USB device
- Try unplugging and replugging the device

### "Failed to bind to port"
- Port might be in use: `netstat -tuln | grep 8888`
- Try a different port number
- Check firewall settings

### No image displayed
- Ensure the server is running and showing "Streaming at X FPS"
- Check network connectivity if streaming over LAN
- Try connecting with `telnet SERVER_IP 8888` to verify connectivity

## Next Steps

For more advanced usage, see:
- [STANDALONE_README.md](STANDALONE_README.md) - Complete documentation
- [examples/README.md](examples/README.md) - Example code in multiple languages
- [README.md](README.md) - Original ROS integration

## Performance Tips

1. **Lower latency:** Use UDP streaming (`endoscope_udp_server`)
2. **Better quality:** Increase JPEG quality (90-100)
3. **Faster streaming:** Decrease JPEG quality (50-70)
4. **Multiple clients:** TCP server supports multiple concurrent connections

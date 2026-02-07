# Example Clients

This directory contains example client applications for receiving the endoscope image stream.

## Python TCP Client

A simple Python client that connects to the TCP server and displays the stream.

### Requirements

```bash
pip3 install opencv-python numpy
```

### Usage

```bash
# Connect to local server
python3 tcp_client.py

# Connect to remote server
python3 tcp_client.py 192.168.1.50 8888
```

### Controls
- Press 'q' or ESC to quit

## C++ TCP Client

A C++ client is also available in `../src/tcp_client.cpp`. Build it using CMake:

```bash
cd ..
mkdir build && cd build
cmake ../CMakeLists_standalone.txt
make tcp_client
./tcp_client
```

## Creating Your Own Client

### Protocol

The TCP protocol is simple:

1. **Connect** to the server (TCP socket)
2. For each frame:
   - **Read 4 bytes**: Frame size as uint32_t in network byte order (big-endian)
   - **Read N bytes**: JPEG-encoded frame data (N = frame size)
   - **Decode**: Use any JPEG decoder to get the image
   - **Display/Process**: Display the image or process it as needed

### Example in Different Languages

#### Python (Full Example)

See `tcp_client.py` for a complete implementation.

#### JavaScript/Node.js

```javascript
const net = require('net');
const sharp = require('sharp'); // npm install sharp

const client = net.createConnection({ port: 8888, host: '127.0.0.1' });

let buffer = Buffer.alloc(0);
let expectedSize = null;

client.on('data', async (data) => {
  buffer = Buffer.concat([buffer, data]);
  
  // Read frame size
  if (expectedSize === null && buffer.length >= 4) {
    expectedSize = buffer.readUInt32BE(0);
    buffer = buffer.slice(4);
  }
  
  // Read frame data
  if (expectedSize !== null && buffer.length >= expectedSize) {
    const frameData = buffer.slice(0, expectedSize);
    buffer = buffer.slice(expectedSize);
    expectedSize = null;
    
    // Process JPEG frame
    const image = sharp(frameData);
    // ... do something with the image
  }
});
```

#### C/C++ (Simplified)

```cpp
#include <sys/socket.h>
#include <arpa/inet.h>
#include <opencv2/opencv.hpp>

int sock = socket(AF_INET, SOCK_STREAM, 0);
struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(8888)};
inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
connect(sock, (struct sockaddr*)&addr, sizeof(addr));

while (true) {
    // Read frame size
    uint32_t frame_size;
    recv(sock, &frame_size, 4, MSG_WAITALL);
    frame_size = ntohl(frame_size);
    
    // Read frame data
    std::vector<uint8_t> data(frame_size);
    recv(sock, data.data(), frame_size, MSG_WAITALL);
    
    // Decode JPEG
    cv::Mat frame = cv::imdecode(data, cv::IMREAD_COLOR);
    cv::imshow("Stream", frame);
    cv::waitKey(1);
}
```

#### Rust

```rust
use std::net::TcpStream;
use std::io::{Read, BufReader};
use image::load_from_memory;

let mut stream = TcpStream::connect("127.0.0.1:8888")?;
let mut reader = BufReader::new(stream);

loop {
    // Read frame size (4 bytes, big-endian)
    let mut size_buf = [0u8; 4];
    reader.read_exact(&mut size_buf)?;
    let frame_size = u32::from_be_bytes(size_buf);
    
    // Read frame data
    let mut frame_data = vec![0u8; frame_size as usize];
    reader.read_exact(&mut frame_data)?;
    
    // Decode JPEG
    let img = load_from_memory(&frame_data)?;
    // ... process image
}
```

## UDP Protocol

For the UDP server, the protocol is different as frames are split into chunks:

### Packet Header (16 bytes)
- **frame_id** (4 bytes): Unique frame identifier
- **total_chunks** (4 bytes): Total number of chunks for this frame
- **chunk_index** (4 bytes): Index of this chunk (0-based)
- **chunk_size** (4 bytes): Size of data in this chunk

### Reassembly

1. Receive UDP packets
2. Parse the header to get frame_id, total_chunks, chunk_index, chunk_size
3. Group chunks by frame_id
4. When all chunks are received, concatenate them in order
5. Decode the complete JPEG frame

Note: UDP packets may arrive out of order or be lost, so your client needs to handle:
- Out-of-order arrival
- Missing chunks
- Frame timeouts

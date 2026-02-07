#!/usr/bin/env python3
"""
Simple Python TCP client for receiving endoscope image stream

Usage: python3 tcp_client.py [server_ip] [port]
  server_ip - Server IP address (default: 127.0.0.1)
  port      - Server port (default: 8888)

Requirements:
  pip3 install opencv-python numpy
"""

import socket
import struct
import sys
import numpy as np
import cv2

class TcpImageClient:
    def __init__(self, server_ip, server_port):
        self.server_ip = server_ip
        self.server_port = server_port
        self.socket = None
    
    def connect(self):
        """Connect to the server"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.connect((self.server_ip, self.server_port))
            print(f"Connected to {self.server_ip}:{self.server_port}")
            return True
        except Exception as e:
            print(f"Failed to connect: {e}")
            return False
    
    def disconnect(self):
        """Disconnect from the server"""
        if self.socket:
            self.socket.close()
            self.socket = None
    
    def receive_full_data(self, size):
        """Receive exactly 'size' bytes from the socket"""
        data = b''
        while len(data) < size:
            chunk = self.socket.recv(size - len(data))
            if not chunk:
                return None
            data += chunk
        return data
    
    def receive_frame(self):
        """Receive one frame from the server"""
        try:
            # Receive frame size (4 bytes, network byte order)
            size_data = self.receive_full_data(4)
            if not size_data:
                return None
            
            frame_size = struct.unpack('!I', size_data)[0]
            
            if frame_size == 0 or frame_size > 10 * 1024 * 1024:  # Max 10MB
                print(f"Invalid frame size: {frame_size}")
                return None
            
            # Receive frame data
            frame_data = self.receive_full_data(frame_size)
            if not frame_data:
                return None
            
            # Decode JPEG
            nparr = np.frombuffer(frame_data, np.uint8)
            frame = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
            
            return frame
        
        except Exception as e:
            print(f"Error receiving frame: {e}")
            return None
    
    def run(self):
        """Main loop to receive and display frames"""
        cv2.namedWindow('Endoscope Stream', cv2.WINDOW_AUTOSIZE)
        
        frame_count = 0
        import time
        start_time = time.time()
        last_stats_time = start_time
        
        try:
            while True:
                frame = self.receive_frame()
                
                if frame is None:
                    print("Failed to receive frame. Disconnecting...")
                    break
                
                frame_count += 1
                
                # Display frame
                cv2.imshow('Endoscope Stream', frame)
                
                # Print stats every 5 seconds
                current_time = time.time()
                elapsed = current_time - last_stats_time
                if elapsed >= 5.0:
                    fps = frame_count / elapsed
                    print(f"Receiving at {fps:.1f} FPS")
                    last_stats_time = current_time
                    frame_count = 0
                
                # Check for quit (press 'q' or ESC)
                key = cv2.waitKey(1) & 0xFF
                if key == ord('q') or key == 27:
                    break
        
        finally:
            cv2.destroyAllWindows()

def main():
    server_ip = '127.0.0.1'
    server_port = 8888
    
    if len(sys.argv) > 1:
        server_ip = sys.argv[1]
    if len(sys.argv) > 2:
        server_port = int(sys.argv[2])
    
    print("TCP Image Client (Python)")
    print(f"Server: {server_ip}:{server_port}")
    print("Press 'q' or ESC to quit")
    print()
    
    client = TcpImageClient(server_ip, server_port)
    
    if not client.connect():
        return 1
    
    try:
        client.run()
    finally:
        client.disconnect()
    
    return 0

if __name__ == '__main__':
    sys.exit(main())

import socket
import time

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('121.40.131.209', 10012))
print("Connected!")

# move：type=0, moveX=127, moveY=0
sock.sendall(bytes([0x00, 0x7F, 0x00]))
print("Sent: move right")

# attack：type=3, bit2=1 → 0x07
sock.sendall(bytes([0x07]))
print("Sent: attack")

time.sleep(999.0)
sock.close()
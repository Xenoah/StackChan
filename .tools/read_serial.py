import sys
import time
try:
    import serial
except Exception as e:
    print('Missing pyserial:', e, file=sys.stderr)
    sys.exit(2)

if len(sys.argv) < 4:
    print('Usage: read_serial.py PORT BAUD DURATION', file=sys.stderr)
    sys.exit(2)

port = sys.argv[1]
baud = int(sys.argv[2])
duration = float(sys.argv[3])

try:
    s = serial.Serial(port, baud, timeout=0.1)
except Exception as e:
    print('ERROR opening serial port:', e, file=sys.stderr)
    sys.exit(1)

end = time.time() + duration
try:
    while time.time() < end:
        data = s.read(1024)
        if data:
            try:
                sys.stdout.buffer.write(data)
                sys.stdout.flush()
            except Exception:
                print(data.decode(errors='replace'), end='')
    s.close()
except KeyboardInterrupt:
    s.close()
    raise

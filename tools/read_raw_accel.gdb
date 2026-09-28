set pagination off
target extended-remote localhost:3333
monitor halt
# read one raw IMU sample into a RAM scratch buffer
set $status = (int)ICM42688_ReadRaw((ICM42688_Handle*)0x200008b0, (ICM42688_RawData*)0x20007200)
echo READ_STATUS=
print $status
echo RAW_SAMPLE=
print *(ICM42688_RawData*)0x20007200
monitor resume
detach
quit

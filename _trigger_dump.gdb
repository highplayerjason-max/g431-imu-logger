set pagination off
target extended-remote localhost:3333
monitor halt
set {unsigned int}0x20000814 = 2
monitor resume
monitor shutdown
quit

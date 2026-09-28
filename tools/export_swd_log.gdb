set pagination off
set confirm off
target extended-remote localhost:3333
monitor halt

printf "SWD export status=%d JEDEC=0x%06x\n", g_swd_export_status, g_swd_export_jedec
if g_swd_export_status != 0
  monitor resume
  quit 1
end

p g_swd_export_header
dump binary memory build/SwdExport/log_header.bin &g_swd_export_header (&g_swd_export_header + 1)

set $flash_address = 0x001000
set $remaining = g_swd_export_header.record_size
set $buffer = (uint8_t *)0x20001000
set $chunk_index = 0
while $remaining > 0
  set $chunk_size = $remaining
  if $chunk_size > 256
    set $chunk_size = 256
  end
  set $read_status = (int)W25Q64_Read($flash_address, $buffer, $chunk_size)
  if $read_status != 0
    printf "Flash read failed at 0x%x, status=%d\n", $flash_address, $read_status
    monitor resume
    quit 1
  end
  if $chunk_index == 0
    dump binary memory build/SwdExport/log_data.bin $buffer ($buffer + $chunk_size)
  else
    append binary memory build/SwdExport/log_data.bin $buffer ($buffer + $chunk_size)
  end
  set $flash_address = $flash_address + $chunk_size
  set $remaining = $remaining - $chunk_size
  set $chunk_index = $chunk_index + 1
  if $chunk_index % 32 == 0 || $remaining == 0
    printf "Read chunk %d, remaining %d bytes\n", $chunk_index, $remaining
  end
end

monitor resume
detach
quit

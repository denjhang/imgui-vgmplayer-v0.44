set pagination off
set logging file gdb_output.txt
set logging on

run

echo \n\n--- CRASH INFO ---\n\n

echo \n--- Frame 0 (Resampler) ---\n
frame 0
p *CAA
p CAA->smpRateDst

echo \n--- Frame 4 (FillBuffer) ---\n
frame 4
p *(PlayerA*)userParam

echo \n--- Backtrace ---\n
bt

echo \n--- End of GDB script ---\n
quit

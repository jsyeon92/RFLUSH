#grep -rn  "write:" 1_*
#grep -rn  "write:" 2_*
#grep -rn  "write:" 3_*
#grep -rn  "write:" 4_*
#grep -rn  "write:" 5_*
#grep -rn  "write:" 0_*


grep -rn "write:" "1_fio_b$1.txt" 
grep -rn "write:" "2_fio_b$1.txt"
grep -rn "write:" "3_fio_b$1.txt"
grep -rn "write:" "4_fsy_b$1.txt"

